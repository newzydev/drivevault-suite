#include "PdfDocument.h"

#include "PdfiumRuntime.h"

#include <fpdf_text.h>
#include <fpdfview.h>

#include <QFile>
#include <QMutexLocker>

#include <algorithm>
#include <cmath>

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
constexpr qint64 kMaxPixels = 150'000'000;      // about 570 MB of RGB32: refuse to render more than that in one image

} // namespace

struct PdfDocument::Impl {
    FileSource source;
    FPDF_DOCUMENT doc = nullptr;
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
        FPDF_CloseDocument(m_impl->doc);
        m_impl->doc = nullptr;
    }
}

QSizeF PdfDocument::pageSizePt(int page) const
{
    return (page >= 0 && page < m_pageSizes.size()) ? m_pageSizes.at(page) : QSizeF(595, 842);
}

QImage PdfDocument::render(int page, double pxPerPt, const std::atomic_bool* cancel) const
{
    if (page < 0 || page >= m_pageSizes.size() || pxPerPt <= 0.0)
        return {};
    const QSizeF pt = m_pageSizes.at(page);
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

    QImage image(w, h, QImage::Format_RGB32);
    if (image.isNull())
        return {};
    image.fill(Qt::white);

    QMutexLocker lock(&pdfiumMutex());
    if (cancel && cancel->load())
        return {};
    FPDF_PAGE fpdfPage = FPDF_LoadPage(m_impl->doc, page);
    if (!fpdfPage)
        return {};
    // BGRx: the byte order of QImage::Format_RGB32 on little endian machines
    FPDF_BITMAP bitmap = FPDFBitmap_CreateEx(w, h, FPDFBitmap_BGRx, image.bits(), int(image.bytesPerLine()));
    if (bitmap) {
        FPDF_RenderPageBitmap(bitmap, fpdfPage, 0, 0, w, h, 0, FPDF_ANNOT);
        FPDFBitmap_Destroy(bitmap);
    }
    FPDF_ClosePage(fpdfPage);
    return bitmap ? image : QImage();
}

QString PdfDocument::pageText(int page) const
{
    if (page < 0 || page >= m_pageSizes.size())
        return {};
    QMutexLocker lock(&pdfiumMutex());
    FPDF_PAGE fpdfPage = FPDF_LoadPage(m_impl->doc, page);
    if (!fpdfPage)
        return {};
    QString result;
    if (FPDF_TEXTPAGE textPage = FPDFText_LoadPage(fpdfPage)) {
        const int count = FPDFText_CountChars(textPage);
        if (count > 0) {
            std::vector<unsigned short> buffer(size_t(count) + 1);
            const int written = FPDFText_GetText(textPage, 0, count, buffer.data());
            if (written > 1)
                result = QString::fromUtf16(reinterpret_cast<const char16_t*>(buffer.data()), written - 1);
        }
        FPDFText_ClosePage(textPage);
    }
    FPDF_ClosePage(fpdfPage);
    result.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    return result;
}

} // namespace dp::pdf
