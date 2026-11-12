#include "PdfDocument.h"

#include "PdfiumRuntime.h"

#include <fpdf_annot.h>
#include <fpdf_doc.h>
#include <fpdf_edit.h>
#include <fpdf_formfill.h>
#include <fpdf_text.h>
#include <fpdf_save.h>
#include <fpdf_signature.h>
#include <fpdfview.h>

#include <QFile>
#include <QHash>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>

namespace dp::pdf {

namespace {

// FPDF_FILEACCESS reads the file on demand, so a big PDF is not loaded into memory and the path may be any Unicode path.
struct FileSource {
    QFile file;
    FPDF_FILEACCESS access{};

    static int getBlock(void* param, unsigned long position, unsigned char* buffer, unsigned long size)
    {
        auto* self = static_cast<FileSource*>(param);
        if (!self->file.seek(qint64(position)))
            return 0;
        return self->file.read(reinterpret_cast<char*>(buffer), qint64(size)) == qint64(size) ? 1 : 0;
    }
};

constexpr int kMaxSide = 16384;                 // pixels
constexpr qint64 kMaxPixels = 64'000'000;       // about 245 MB of RGB32: a page image is never bigger than that (a poster at 800 % is shrunk)
constexpr double kFine = 16.0;                  // PDFium converts coordinates to integers: ask for 1/16 point resolution
constexpr int kMaxOutlineItems = 5000;
constexpr int kMaxOutlineDepth = 24;

int normRotation(int rotation) { return ((rotation % 4) + 4) % 4; }
QSizeF rotated(QSizeF size, int rotation) { return (rotation % 2) ? QSizeF(size.height(), size.width()) : size; }

// Releases a PDFium page / text page when it goes out of scope. All users hold the PDFium mutex.
struct PageHandle {
    FPDF_PAGE page = nullptr;
    FPDF_TEXTPAGE text = nullptr;
    PageHandle(FPDF_DOCUMENT doc, int index, bool withText)
    {
        page = FPDF_LoadPage(doc, index);
        if (page && withText)
            text = FPDFText_LoadPage(page);
    }
    ~PageHandle()
    {
        if (text)
            FPDFText_ClosePage(text);
        if (page)
            FPDF_ClosePage(page);
    }
    PageHandle(const PageHandle&) = delete;
    PageHandle& operator=(const PageHandle&) = delete;
};

// page space (points, origin bottom left) <-> the page as shown (points, origin top left) with the view rotation
QPointF toShown(FPDF_PAGE page, QSizeF shown, int rotation, double px, double py)
{
    int dx = 0, dy = 0;
    FPDF_PageToDevice(page, 0, 0, int(std::lround(shown.width() * kFine)), int(std::lround(shown.height() * kFine)), rotation, px, py, &dx, &dy);
    return QPointF(dx / kFine, dy / kFine);
}
QPointF toPage(FPDF_PAGE page, QSizeF shown, int rotation, QPointF pt)
{
    double px = 0, py = 0;
    FPDF_DeviceToPage(page, 0, 0, int(std::lround(shown.width() * kFine)), int(std::lround(shown.height() * kFine)), rotation,
                      int(std::lround(pt.x() * kFine)), int(std::lround(pt.y() * kFine)), &px, &py);
    return QPointF(px, py);
}
QRectF shownRect(FPDF_PAGE page, QSizeF shown, int rotation, double left, double bottom, double right, double top)
{
    const QPointF a = toShown(page, shown, rotation, left, top);
    const QPointF b = toShown(page, shown, rotation, right, bottom);
    return QRectF(a, b).normalized();
}

QString utf16Bytes(const std::vector<char>& bytes, bool nulTerminated = true)
{
    int chars = int(bytes.size() / 2);
    if (nulTerminated && chars > 0)
        --chars;
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(bytes.data()), chars);
}

QList<QRectF> textRectsOf(FPDF_PAGE page, FPDF_TEXTPAGE text, QSizeF shown, int rotation, int start, int count)
{
    QList<QRectF> out;
    if (!text || count == 0)
        return out;
    const int n = FPDFText_CountRects(text, start, count);
    for (int i = 0; i < n; ++i) {
        double l = 0, t = 0, r = 0, b = 0;
        if (FPDFText_GetRect(text, i, &l, &t, &r, &b))
            out.append(shownRect(page, shown, rotation, l, b, r, t));
    }
    return out;
}

bool describeTextObject(FPDF_PAGE page, QSizeF size, FPDF_PAGEOBJECT object, EditableTextRun& run)
{
    if (!object || FPDFTextObj_GetTextRenderMode(object) != FPDF_TEXTRENDERMODE_FILL)
        return false;
    FS_MATRIX matrix{};
    float left, bottom, right, top, fontSize;
    unsigned int r, g, b, a;
    if (!FPDFPageObj_GetMatrix(object, &matrix) || std::abs(matrix.b) > 0.001 || std::abs(matrix.c) > 0.001
        || matrix.a <= 0 || matrix.d <= 0 || std::abs(matrix.a - matrix.d) > 0.001
        || !FPDFPageObj_GetBounds(object, &left, &bottom, &right, &top)
        || !FPDFTextObj_GetFontSize(object, &fontSize) || !FPDFPageObj_GetFillColor(object, &r, &g, &b, &a) || a != 255)
        return false;
    const double shownFontSize = fontSize * matrix.d;
    if (shownFontSize < 5 || shownFontSize > 144)
        return false;
    run = {{}, shownRect(page, size, 0, left, bottom, right, top),
           toShown(page, size, 0, matrix.e, matrix.f), QColor(int(r), int(g), int(b)), shownFontSize};
    return true;
}

QList<FPDF_PAGEOBJECT> findEditableRun(const PageHandle& page, QSizeF size, QPointF point, EditableTextRun& run)
{
    if (!page.text || FPDFPage_GetRotation(page.page) != 0)
        return {};
    const QPointF pos = toPage(page.page, size, 0, point);
    const int index = FPDFText_GetCharIndexAtPos(page.text, pos.x(), pos.y(), 2, 2);
    if (index < 0)
        return {};
    const auto selected = FPDFText_GetTextObject(page.text, index);
    const int count = FPDFPage_CountObjects(page.page);
    if (count <= 0 || count > 100000)
        return {};
    std::unordered_set<FPDF_PAGEOBJECT> topLevel;
    for (int i = 0; i < count; ++i)
        topLevel.insert(FPDFPage_GetObject(page.page, i));
    EditableTextRun origin;
    if (!topLevel.contains(selected) || !describeTextObject(page.page, size, selected, origin))
        return {};

    run = origin;
    QList<FPDF_PAGEOBJECT> objects{selected};
    int first = index, last = index;
    const int charCount = FPDFText_CountChars(page.text);
    const auto font = FPDFTextObj_GetFont(selected);
    // Some writers position every Thai glyph as a separate text object. Collect neighbours on the
    // same baseline with the same style, stopping at a line break or a substantial visual gap.
    for (const int direction : {-1, 1}) {
        auto previous = selected;
        auto edge = origin;
        bool generatedSpace = false;
        for (int i = index + direction; i >= 0 && i < charCount; i += direction) {
            const auto ch = FPDFText_GetUnicode(page.text, i);
            if (ch == '\r' || ch == '\n' || ch == 0)
                break;
            if (ch == ' ' && FPDFText_IsGenerated(page.text, i) == 1) {
                generatedSpace = true;
                continue;
            }
            if (std::abs(i - index) > 512 || last - first >= 511)
                return {};
            const auto object = FPDFText_GetTextObject(page.text, i);
            if (object != previous) {
                EditableTextRun adjacent;
                if (!topLevel.contains(object) || !describeTextObject(page.page, size, object, adjacent)
                    || FPDFTextObj_GetFont(object) != font || adjacent.color != origin.color
                    || std::abs(adjacent.fontSize - origin.fontSize) > 0.1
                    || std::abs(adjacent.baseline.y() - origin.baseline.y()) > origin.fontSize * 0.27)
                    break;
                const double gap = direction < 0 ? edge.bounds.left() - adjacent.bounds.right()
                                                 : adjacent.bounds.left() - edge.bounds.right();
                if (gap > origin.fontSize * (generatedSpace ? 0.52 : 0.35) || objects.contains(object))
                    break;
                objects.append(object);
                run.bounds |= adjacent.bounds;
                if (direction < 0)
                    run.baseline = adjacent.baseline;
                edge = adjacent;
                previous = object;
            }
            generatedSpace = false;
            if (direction < 0)
                first = i;
            else
                last = i;
        }
    }
    std::vector<unsigned short> text(size_t(last - first) + 2);
    const int length = FPDFText_GetText(page.text, first, last - first + 1, text.data());
    if (length <= 1)
        return {};
    run.text = QString::fromUtf16(reinterpret_cast<const char16_t*>(text.data()), length - 1);
    if (run.text.trimmed().isEmpty() || run.text.contains(QChar::ReplacementCharacter))
        return {};
    return objects;
}

struct PdfFileWriter : FPDF_FILEWRITE {
    explicit PdfFileWriter(const QString& path) : FPDF_FILEWRITE{1, writeBlock}, file(path) {}
    static int writeBlock(FPDF_FILEWRITE* writer, const void* data, unsigned long size)
    {
        return static_cast<PdfFileWriter*>(writer)->file.write(static_cast<const char*>(data), size) == size;
    }
    QFile file;
};

QString destTitle(FPDF_BOOKMARK bookmark)
{
    const unsigned long bytes = FPDFBookmark_GetTitle(bookmark, nullptr, 0);
    if (bytes < 2)
        return {};
    std::vector<char> buffer(bytes);
    FPDFBookmark_GetTitle(bookmark, buffer.data(), bytes);
    return utf16Bytes(buffer);
}

void collectOutline(FPDF_DOCUMENT doc, FPDF_BOOKMARK parent, int level, QList<OutlineItem>& out)
{
    if (level >= kMaxOutlineDepth)
        return;
    for (FPDF_BOOKMARK bm = FPDFBookmark_GetFirstChild(doc, parent); bm && out.size() < kMaxOutlineItems;
         bm = FPDFBookmark_GetNextSibling(doc, bm)) {
        OutlineItem item;
        item.title = destTitle(bm);
        item.level = level;
        FPDF_DEST dest = FPDFBookmark_GetDest(doc, bm);
        if (!dest) {
            if (FPDF_ACTION action = FPDFBookmark_GetAction(bm))
                if (FPDFAction_GetType(action) == PDFACTION_GOTO)
                    dest = FPDFAction_GetDest(doc, action);
        }
        item.page = dest ? FPDFDest_GetDestPageIndex(doc, dest) : -1;
        out.append(item);
        collectOutline(doc, bm, level + 1, out);
    }
}

QString annotationType(int subtype)
{
    switch (subtype) {
    case FPDF_ANNOT_TEXT: return QStringLiteral("Text");
    case FPDF_ANNOT_FREETEXT: return QStringLiteral("FreeText");
    case FPDF_ANNOT_LINE: return QStringLiteral("Line");
    case FPDF_ANNOT_SQUARE: return QStringLiteral("Square");
    case FPDF_ANNOT_CIRCLE: return QStringLiteral("Circle");
    case FPDF_ANNOT_POLYGON: return QStringLiteral("Polygon");
    case FPDF_ANNOT_POLYLINE: return QStringLiteral("PolyLine");
    case FPDF_ANNOT_HIGHLIGHT: return QStringLiteral("Highlight");
    case FPDF_ANNOT_UNDERLINE: return QStringLiteral("Underline");
    case FPDF_ANNOT_SQUIGGLY: return QStringLiteral("Squiggly");
    case FPDF_ANNOT_STRIKEOUT: return QStringLiteral("StrikeOut");
    case FPDF_ANNOT_STAMP: return QStringLiteral("Stamp");
    case FPDF_ANNOT_CARET: return QStringLiteral("Caret");
    case FPDF_ANNOT_INK: return QStringLiteral("Ink");
    case FPDF_ANNOT_FILEATTACHMENT: return QStringLiteral("FileAttachment");
    case FPDF_ANNOT_REDACT: return QStringLiteral("Redact");
    default: return QStringLiteral("Other");
    }
}

} // namespace

struct PdfDocument::Impl {
    FileSource source;
    FPDF_DOCUMENT doc = nullptr;
    FPDF_FORMFILLINFO formInfo{};               // the form fill environment draws the fields of a fill-in form (FPDF_RenderPageBitmap does not)
    FPDF_FORMHANDLE form = nullptr;
};

std::shared_ptr<PdfDocument> PdfDocument::open(const QString& path, const QString& password, OpenError* error)
{
    auto fail = [&](OpenError e) {
        if (error)
            *error = e;
        return std::shared_ptr<PdfDocument>();
    };
    ensurePdfiumInitialized();

    std::shared_ptr<PdfDocument> self(new PdfDocument());
    self->m_impl = std::make_unique<Impl>();
    self->m_path = path;
    FileSource& src = self->m_impl->source;
    src.file.setFileName(path);
    if (!src.file.open(QIODevice::ReadOnly))
        return fail(OpenError::File);
    if (src.file.size() <= 0)
        return fail(OpenError::Format);
    if (src.file.size() > 0xFFFFFFFFLL)
        return fail(OpenError::TooLarge);          // FPDF_FILEACCESS uses a 32 bit length on Windows
    src.access.m_FileLen = static_cast<unsigned long>(src.file.size());
    src.access.m_GetBlock = &FileSource::getBlock;
    src.access.m_Param = &src;

    QMutexLocker lock(&pdfiumMutex());
    const QByteArray pw = password.toUtf8();
    self->m_impl->doc = FPDF_LoadCustomDocument(&src.access, pw.isEmpty() ? nullptr : pw.constData());
    if (!self->m_impl->doc) {
        switch (FPDF_GetLastError()) {
        case FPDF_ERR_PASSWORD: return fail(OpenError::Password);
        case FPDF_ERR_FORMAT: return fail(OpenError::Format);
        case FPDF_ERR_FILE: return fail(OpenError::File);
        default: return fail(OpenError::Unknown);
        }
    }
    const int count = FPDF_GetPageCount(self->m_impl->doc);
    if (count <= 0) {
        FPDF_CloseDocument(self->m_impl->doc);
        self->m_impl->doc = nullptr;
        return fail(OpenError::Format);
    }
    self->m_impl->formInfo.version = 1;
    self->m_impl->form = FPDFDOC_InitFormFillEnvironment(self->m_impl->doc, &self->m_impl->formInfo);
    if (self->m_impl->form)
        FPDF_SetFormFieldHighlightAlpha(self->m_impl->form, 0);          // no tinted fields
    self->m_pageSizes.reserve(count);
    for (int i = 0; i < count; ++i) {
        FS_SIZEF size{0, 0};
        if (!FPDF_GetPageSizeByIndexF(self->m_impl->doc, i, &size) || size.width <= 0 || size.height <= 0)
            size = {595.f, 842.f};                // a broken page is shown as A4 instead of failing the whole file
        self->m_pageSizes.append(QSizeF(size.width, size.height));
    }
    if (error)
        *error = OpenError::None;
    return self;
}

PdfDocument::~PdfDocument()
{
    if (m_impl && m_impl->doc) {
        QMutexLocker lock(&pdfiumMutex());
        if (m_impl->form) {
            FPDFDOC_ExitFormFillEnvironment(m_impl->form);
            m_impl->form = nullptr;
        }
        FPDF_CloseDocument(m_impl->doc);
        m_impl->doc = nullptr;
    }
}

QSizeF PdfDocument::pageSizePt(int page, int rotation) const
{
    const QSizeF base = (page >= 0 && page < m_pageSizes.size()) ? m_pageSizes.at(page) : QSizeF(595, 842);
    return rotated(base, normRotation(rotation));
}

EditableTextRun PdfDocument::editableTextAt(int page, QPointF point) const
{
    if (page < 0 || page >= pageCount() || !std::isfinite(point.x()) || !std::isfinite(point.y()))
        return {};
    QMutexLocker lock(&pdfiumMutex());
    if (FPDF_GetSecurityHandlerRevision(m_impl->doc) >= 0 || FPDF_GetSignatureCount(m_impl->doc) != 0)
        return {};
    PageHandle handle(m_impl->doc, page, true);
    EditableTextRun run;
    return findEditableRun(handle, m_pageSizes.at(page), point, run).isEmpty() ? EditableTextRun{} : run;
}

bool PdfDocument::writeWithoutTextRun(int page, QPointF point, const EditableTextRun& expected, const QString& destination)
{
    if (page < 0 || page >= pageCount())
        return false;
    QMutexLocker lock(&pdfiumMutex());
    if (FPDF_GetSecurityHandlerRevision(m_impl->doc) >= 0 || FPDF_GetSignatureCount(m_impl->doc) != 0)
        return false;
    PageHandle handle(m_impl->doc, page, true);
    EditableTextRun actual;
    const auto objects = findEditableRun(handle, m_pageSizes.at(page), point, actual);
    if (objects.isEmpty() || actual.text != expected.text || actual.bounds != expected.bounds || actual.baseline != expected.baseline)
        return false;
    FPDFText_ClosePage(handle.text);
    handle.text = nullptr;
    for (const auto object : objects) {
        if (!FPDFPage_RemoveObject(handle.page, object))
            return false;
        FPDFPageObj_Destroy(object);
    }
    if (!FPDFPage_GenerateContent(handle.page))
        return false;
    PdfFileWriter output(destination);
    return output.file.open(QIODevice::WriteOnly | QIODevice::NewOnly)
        && FPDF_SaveAsCopy(m_impl->doc, &output, FPDF_NO_INCREMENTAL) && output.file.flush();
}

QImage PdfDocument::render(int page, double pxPerPt, const std::atomic_bool* cancel, int rotation, bool transparent,
                           bool includeAnnotations) const
{
    if (page < 0 || page >= m_pageSizes.size() || pxPerPt <= 0.0)
        return {};
    rotation = normRotation(rotation);
    const QSizeF pt = rotated(m_pageSizes.at(page), rotation);
    double scale = pxPerPt;
    scale = std::min({scale, kMaxSide / pt.width(), kMaxSide / pt.height()});
    int w = std::max(1, int(std::lround(pt.width() * scale)));
    int h = std::max(1, int(std::lround(pt.height() * scale)));
    while (qint64(w) * h > kMaxPixels) {
        scale *= 0.9;
        w = std::max(1, int(std::lround(pt.width() * scale)));
        h = std::max(1, int(std::lround(pt.height() * scale)));
    }
    if (cancel && cancel->load())
        return {};

    QImage image(w, h, transparent ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    if (image.isNull())
        return {};
    image.fill(transparent ? Qt::transparent : Qt::white);

    QMutexLocker lock(&pdfiumMutex());
    if (cancel && cancel->load())
        return {};
    FPDF_PAGE fpdfPage = FPDF_LoadPage(m_impl->doc, page);
    if (fpdfPage && m_impl->form)
        FORM_OnAfterLoadPage(fpdfPage, m_impl->form);
    if (!fpdfPage)
        return {};
    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(w, h, transparent ? FPDFBitmap_BGRA : FPDFBitmap_BGRx,
                                              image.bits(), int(image.bytesPerLine()));
    if (bitmap) {
        FPDF_RenderPageBitmap(bitmap, fpdfPage, 0, 0, w, h, rotation, includeAnnotations ? FPDF_ANNOT : 0);
        if (m_impl->form && includeAnnotations)
            FPDF_FFLDraw(m_impl->form, bitmap, fpdfPage, 0, 0, w, h, rotation, FPDF_ANNOT);
        FPDFBitmap_Destroy(bitmap);
    }
    if (m_impl->form)
        FORM_OnBeforeClosePage(fpdfPage, m_impl->form);
    FPDF_ClosePage(fpdfPage);
    return bitmap ? image : QImage();
}

// ---- text ------------------------------------------------------------------------------------------------------------------

QString PdfDocument::pageText(int page) const
{
    if (page < 0 || page >= m_pageSizes.size())
        return {};
    QMutexLocker lock(&pdfiumMutex());
    PageHandle h(m_impl->doc, page, true);
    QString result;
    if (h.text) {
        const int count = FPDFText_CountChars(h.text);
        if (count > 0) {
            std::vector<unsigned short> buffer(size_t(count) + 1);
            const int written = FPDFText_GetText(h.text, 0, count, buffer.data());
            if (written > 1)
                result = QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer.data()), written - 1);
        }
    }
    result.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return result;
}

int PdfDocument::charCount(int page) const
{
    if (page < 0 || page >= m_pageSizes.size())
        return 0;
    QMutexLocker lock(&pdfiumMutex());
    PageHandle h(m_impl->doc, page, true);
    return h.text ? FPDFText_CountChars(h.text) : 0;
}

QString PdfDocument::textRange(int page, int start, int count) const
{
    if (page < 0 || page >= m_pageSizes.size() || start < 0 || count <= 0)
        return {};
    QMutexLocker lock(&pdfiumMutex());
    PageHandle h(m_impl->doc, page, true);
    if (!h.text)
        return {};
    const int total = FPDFText_CountChars(h.text);
    count = std::min(count, total - start);
    if (count <= 0)
        return {};
    std::vector<unsigned short> buffer(size_t(count) + 1);
    const int written = FPDFText_GetText(h.text, start, count, buffer.data());
    QString result = written > 1 ? QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer.data()), written - 1) : QString();
    result.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return result;
}

int PdfDocument::charIndexAt(int page, QPointF pt, double tolerance, int rotation) const
{
    if (page < 0 || page >= m_pageSizes.size())
        return -1;
    rotation = normRotation(rotation);
    QMutexLocker lock(&pdfiumMutex());
    PageHandle h(m_impl->doc, page, true);
    if (!h.text)
        return -1;
    const QPointF pp = toPage(h.page, rotated(m_pageSizes.at(page), rotation), rotation, pt);
    return FPDFText_GetCharIndexAtPos(h.text, pp.x(), pp.y(), tolerance, tolerance);
}

TextPosition PdfDocument::textPositionAt(int page, QPointF pt, double tolerance, int rotation) const
{
    if (page < 0 || page >= m_pageSizes.size())
        return {};
    rotation = normRotation(rotation);
    QMutexLocker lock(&pdfiumMutex());
    PageHandle h(m_impl->doc, page, true);
    if (!h.text)
        return {};
    const QSizeF shown = rotated(m_pageSizes.at(page), rotation);
    const QPointF point = toPage(h.page, shown, rotation, pt);
    int index = FPDFText_GetCharIndexAtPos(h.text, point.x(), point.y(), tolerance, tolerance);
    if (index < 0)
        return {};

    const auto isMark = [&h](int character) {
        const auto category = QChar::category(FPDFText_GetUnicode(h.text, character));
        return category == QChar::Mark_NonSpacing || category == QChar::Mark_SpacingCombining
            || category == QChar::Mark_Enclosing;
    };
    while (index > 0 && isMark(index))
        --index;
    int end = index + 1;
    const int count = FPDFText_CountChars(h.text);
    while (end < count && isMark(end))
        ++end;

    double left, right, bottom, top;
    if (!FPDFText_GetCharBox(h.text, index, &left, &right, &bottom, &top))
        return {};
    const QRectF glyph = shownRect(h.page, shown, rotation, left, bottom, right, top);
    FS_RECTF loose{};
    const QRectF line = FPDFText_GetLooseCharBox(h.text, index, &loose)
        ? shownRect(h.page, shown, rotation, loose.left, loose.bottom, loose.right, loose.top).united(glyph)
        : glyph;
    FS_MATRIX matrix{};
    const QPointF baseline = FPDFText_GetMatrix(h.text, index, &matrix)
        ? QPointF(matrix.a, matrix.b) : QPointF(1, 0);
    const QPointF advance = toShown(h.page, shown, rotation, left + 16 * baseline.x(), bottom + 16 * baseline.y())
        - toShown(h.page, shown, rotation, left, bottom);
    const auto direction = QChar::direction(FPDFText_GetUnicode(h.text, index));
    const double readingDirection = direction == QChar::DirR || direction == QChar::DirAL ? -1.0 : 1.0;
    const bool vertical = std::abs(advance.y()) > std::abs(advance.x());
    const double axis = readingDirection * (vertical ? advance.y() : advance.x());
    const double offset = vertical ? pt.y() - glyph.center().y() : pt.x() - glyph.center().x();
    const bool trailing = offset * axis > 0;
    const bool farEdge = trailing == (axis >= 0);
    const QRectF caret = vertical
        ? QRectF(line.left(), farEdge ? glyph.bottom() : glyph.top(), line.width(), 0)
        : QRectF(farEdge ? glyph.right() : glyph.left(), line.top(), 0, line.height());
    return {trailing ? end : index, caret};
}

QList<QRectF> PdfDocument::textRects(int page, int start, int count, int rotation) const
{
    if (page < 0 || page >= m_pageSizes.size() || start < 0)
        return {};
    rotation = normRotation(rotation);
    QMutexLocker lock(&pdfiumMutex());
    PageHandle h(m_impl->doc, page, true);
    return textRectsOf(h.page, h.text, rotated(m_pageSizes.at(page), rotation), rotation, start, count);
}

QList<SearchHit> PdfDocument::searchPage(int page, const QString& text, bool matchCase, bool wholeWord, int rotation) const
{
    QList<SearchHit> hits;
    if (page < 0 || page >= m_pageSizes.size() || text.isEmpty())
        return hits;
    rotation = normRotation(rotation);
    QMutexLocker lock(&pdfiumMutex());
    PageHandle h(m_impl->doc, page, true);
    if (!h.text)
        return hits;
    std::vector<unsigned short> needle(text.utf16(), text.utf16() + text.size());
    needle.push_back(0);
    const unsigned long flags = (matchCase ? FPDF_MATCHCASE : 0) | (wholeWord ? FPDF_MATCHWHOLEWORD : 0);
    FPDF_SCHHANDLE search = FPDFText_FindStart(h.text, needle.data(), flags, 0);
    if (!search)
        return hits;
    const QSizeF shown = rotated(m_pageSizes.at(page), rotation);
    while (FPDFText_FindNext(search)) {
        SearchHit hit;
        hit.page = page;
        hit.rects = textRectsOf(h.page, h.text, shown, rotation, FPDFText_GetSchResultIndex(search), FPDFText_GetSchCount(search));
        hits.append(hit);
    }
    FPDFText_FindClose(search);
    return hits;
}

// ---- structure -------------------------------------------------------------------------------------------------------------

QList<SearchHit> PdfDocument::matchPage(int page, const QRegularExpression& pattern, int limit) const
{
    QList<SearchHit> hits;
    if (page < 0 || page >= m_pageSizes.size() || !pattern.isValid() || limit <= 0)
        return hits;
    QString text;
    QList<int> indices;
    {
        QMutexLocker lock(&pdfiumMutex());
        PageHandle h(m_impl->doc, page, true);
        if (!h.text)
            return hits;
        const int count = FPDFText_CountChars(h.text);
        for (int i = 0; i < count; ++i) {
            const char32_t ch = FPDFText_GetUnicode(h.text, i);
            if (!ch || ch > 0x10ffff || (ch >= 0xd800 && ch <= 0xdfff))
                continue;
            if (ch > 0xffff) {
                text.append(QChar::highSurrogate(ch));
                indices.append(i);
                text.append(QChar::lowSurrogate(ch));
            } else {
                text.append(QChar(ushort(ch)));
            }
            indices.append(i);
        }
    }
    QList<QPair<int, int>> ranges;
    auto matches = pattern.globalMatch(text);
    while (ranges.size() < limit && matches.hasNext()) {
        const auto match = matches.next();
        if (match.capturedLength() == 0)
            continue;
        const int start = indices.at(match.capturedStart());
        const int end = indices.at(match.capturedEnd() - 1);
        ranges.append({start, end - start + 1});
    }
    if (ranges.isEmpty())
        return hits;
    QMutexLocker lock(&pdfiumMutex());
    PageHandle h(m_impl->doc, page, true);
    if (!h.text)
        return hits;
    for (const auto& [start, length] : ranges) {
        auto rects = textRectsOf(h.page, h.text, m_pageSizes.at(page), 0, start, length);
        if (!rects.isEmpty())
            hits.append({page, std::move(rects)});
    }
    return hits;
}

bool PdfDocument::isEncrypted() const
{
    QMutexLocker lock(&pdfiumMutex());
    return FPDF_GetSecurityHandlerRevision(m_impl->doc) >= 0;
}

RedactionGeometry PdfDocument::redactionGeometry(int page, const QList<QRectF>& shownMasks) const
{
    RedactionGeometry out;
    if (page < 0 || page >= m_pageSizes.size())
        return out;
    QMutexLocker lock(&pdfiumMutex());
    PageHandle h(m_impl->doc, page, true);
    if (!h.text || FPDFPage_GetRotation(h.page) != 0)
        return out;
    for (const auto& rect : shownMasks) {
        const auto size = m_pageSizes.at(page);
        out.masks.append(QRectF(toPage(h.page, size, 0, rect.topLeft()),
                                toPage(h.page, size, 0, rect.bottomRight())).normalized());
    }
    QHash<FPDF_PAGEOBJECT, qsizetype> indices;
    for (int i = 0; i < FPDFPage_CountObjects(h.page); ++i) {
        auto object = FPDFPage_GetObject(h.page, i);
        if (FPDFPageObj_GetType(object) == FPDF_PAGEOBJ_TEXT) {
            indices.insert(object, out.text.size());
            out.text.append(QList<RedactionGlyph>());
        }
    }
    for (int i = 0; i < FPDFText_CountChars(h.text); ++i) {
        if (FPDFText_IsGenerated(h.text, i) == 1)
            continue;
        const auto it = indices.constFind(FPDFText_GetTextObject(h.text, i));
        if (it == indices.cend())
            return {};
        double l, r, b, t, x, y;
        if (!FPDFText_GetCharBox(h.text, i, &l, &r, &b, &t) || !FPDFText_GetCharOrigin(h.text, i, &x, &y))
            return {};
        out.text[*it].append({FPDFText_GetUnicode(h.text, i), QPointF(x, y),
                              QRectF(QPointF(l, b), QPointF(r, t)).normalized(),
                              shownRect(h.page, m_pageSizes.at(page), 0, l, b, r, t)});
    }
    out.valid = true;
    return out;
}

QList<OutlineItem> PdfDocument::outline() const
{
    QList<OutlineItem> out;
    QMutexLocker lock(&pdfiumMutex());
    collectOutline(m_impl->doc, nullptr, 0, out);
    return out;
}

QList<AnnotationInfo> PdfDocument::annotations(int rotation) const
{
    QList<AnnotationInfo> out;
    rotation = normRotation(rotation);
    for (int pageIndex = 0; pageIndex < m_pageSizes.size(); ++pageIndex) {
        QMutexLocker lock(&pdfiumMutex());      // released between pages: a big file must not block rendering for the whole scan
        PageHandle h(m_impl->doc, pageIndex, false);
        if (!h.page)
            continue;
        const int n = FPDFPage_GetAnnotCount(h.page);
        for (int i = 0; i < n; ++i) {
            FPDF_ANNOTATION annot = FPDFPage_GetAnnot(h.page, i);
            if (!annot)
                continue;
            const int subtype = FPDFAnnot_GetSubtype(annot);
            if (subtype != FPDF_ANNOT_LINK && subtype != FPDF_ANNOT_WIDGET && subtype != FPDF_ANNOT_POPUP && subtype != FPDF_ANNOT_UNKNOWN) {
                AnnotationInfo info;
                info.page = pageIndex;
                info.type = annotationType(subtype);
                const unsigned long bytes = FPDFAnnot_GetStringValue(annot, "Contents", nullptr, 0);
                if (bytes >= 2) {
                    std::vector<char> buffer(bytes);
                    FPDFAnnot_GetStringValue(annot, "Contents", reinterpret_cast<FPDF_WCHAR*>(buffer.data()), bytes);
                    info.contents = utf16Bytes(buffer);
                }
                FS_RECTF rect{0, 0, 0, 0};
                if (FPDFAnnot_GetRect(annot, &rect))
                    info.rect = shownRect(h.page, rotated(m_pageSizes.at(pageIndex), rotation), rotation, rect.left, rect.bottom, rect.right, rect.top);
                out.append(info);
            }
            FPDFPage_CloseAnnot(annot);
        }
    }
    return out;
}

LinkTarget PdfDocument::linkAt(int page, QPointF pt, int rotation) const
{
    LinkTarget target;
    if (page < 0 || page >= m_pageSizes.size())
        return target;
    rotation = normRotation(rotation);
    QMutexLocker lock(&pdfiumMutex());
    PageHandle h(m_impl->doc, page, false);
    if (!h.page)
        return target;
    const QPointF pp = toPage(h.page, rotated(m_pageSizes.at(page), rotation), rotation, pt);
    FPDF_LINK link = FPDFLink_GetLinkAtPoint(h.page, pp.x(), pp.y());
    if (!link)
        return target;
    FPDF_DEST dest = FPDFLink_GetDest(m_impl->doc, link);
    FPDF_ACTION action = dest ? nullptr : FPDFLink_GetAction(link);
    if (!dest && action) {
        const unsigned long type = FPDFAction_GetType(action);
        if (type == PDFACTION_GOTO) {
            dest = FPDFAction_GetDest(m_impl->doc, action);
        } else if (type == PDFACTION_URI) {
            const unsigned long bytes = FPDFAction_GetURIPath(m_impl->doc, action, nullptr, 0);
            if (bytes > 1) {
                std::vector<char> buffer(bytes);
                FPDFAction_GetURIPath(m_impl->doc, action, buffer.data(), bytes);
                target.kind = LinkTarget::Uri;
                target.uri = QString::fromLatin1(buffer.data(), int(bytes) - 1);
                return target;
            }
        }
    }
    if (dest) {
        const int index = FPDFDest_GetDestPageIndex(m_impl->doc, dest);
        if (index >= 0) {
            target.kind = LinkTarget::Page;
            target.page = index;
        }
    }
    return target;
}

} // namespace dp::pdf

namespace dp::pdf {

QString PdfDocument::pageLabel(int page) const
{
    if (page < 0 || page >= m_pageSizes.size())
        return {};
    QMutexLocker lock(&pdfiumMutex());
    const unsigned long bytes = FPDF_GetPageLabel(m_impl->doc, page, nullptr, 0);      // UTF-16 with the terminator
    if (bytes <= 2)
        return {};
    QByteArray buffer(int(bytes), '\0');
    FPDF_GetPageLabel(m_impl->doc, page, buffer.data(), bytes);
    return QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer.constData()), int(bytes / 2) - 1);
}

} // namespace dp::pdf
