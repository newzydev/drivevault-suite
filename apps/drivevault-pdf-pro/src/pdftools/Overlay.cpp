#define _USE_MATH_DEFINES
#include "Overlay.h"

#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFMatrix.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFPageObjectHelper.hh>
#include <qpdf/QPDFSystemError.hh>
#include <qpdf/QPDFWriter.hh>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontInfo>
#include <QMarginsF>
#include <QMutex>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QRawFont>
#include <QSet>
#include <QTemporaryDir>
#include <QGlyphRun>
#include <QTextLayout>
#include <QTextLine>
#include <QTextOption>
#include <QtEndian>

#include <cstdio>

#include <algorithm>
#include <cmath>
#include <map>

namespace dp::tools {

namespace {

struct CancelledError {};

QMutex g_imageMutex;
QHash<QString, QImage> g_images;

QImage loadImage(const QString& path)
{
    const QFileInfo fi(path);
    const QString key = path + QLatin1Char('|') + QString::number(fi.lastModified().toMSecsSinceEpoch()) + QLatin1Char('|') + QString::number(fi.size());
    QMutexLocker lock(&g_imageMutex);
    if (auto it = g_images.constFind(key); it != g_images.constEnd())
        return it.value();
    QImage img(path);
    if (g_images.size() > 24)
        g_images.clear();
    g_images.insert(key, img);
    return img;
}

QFont fontOf(const EditItem& it, int pixelSize)
{
    QFont f(it.font);
    f.setPixelSize(std::max(1, pixelSize));
    f.setBold(it.bold);
    f.setItalic(it.italic);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

// Text is laid out at 16 times the size and drawn back at 1/16, so the line breaks are the same on the screen and in the PDF
// (fonts are sized in whole pixels).
//
// The letters are drawn as outlines (the text is shaped by Qt: Thai vowels and tone marks are stacked correctly) so they look the same in every viewer;
// the text itself is added to the PDF as an invisible text layer (see saveWithEdits) so that it can be searched and copied.
void drawText(QPainter& p, const QRectF& rect, const QString& text, const EditItem& it, const QColor& color, const QPointF& center, TextRuns* runs)
{
    constexpr double k = 16.0;
    if (text.isEmpty())
        return;
    QTextLayout layout(text, fontOf(it, int(std::lround(it.fontSize * k))));
    QTextOption opt;
    opt.setAlignment(it.align == 1 ? Qt::AlignHCenter : it.align == 2 ? Qt::AlignRight : Qt::AlignLeft);
    opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(opt);
    double y = 0;
    layout.beginLayout();
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;
        line.setLineWidth(std::max(1.0, rect.width() * k));
        line.setPosition(QPointF(0, y));
        y += line.height();
    }
    layout.endLayout();
    p.save();
    p.translate(rect.topLeft());
    p.scale(1.0 / k, 1.0 / k);
    for (const QGlyphRun& run : layout.glyphRuns()) {
        const QRawFont raw = run.rawFont();
        const QList<quint32> glyphs = run.glyphIndexes();
        const QList<QPointF> pos = run.positions();
        for (int i = 0; i < glyphs.size(); ++i) {
            QPainterPath path = raw.pathForGlyph(glyphs[i]);
            path.translate(pos[i]);
            p.fillPath(path, color);
        }
    }
    p.restore();
    if (runs)
        for (int i = 0; i < layout.lineCount(); ++i) {
            const QTextLine line = layout.lineAt(i);
            QString part = text.mid(line.textStart(), line.textLength());
            while (part.endsWith(QLatin1Char('\n')) || part.endsWith(QLatin1Char('\r')))
                part.chop(1);
            if (part.trimmed().isEmpty())
                continue;
            TextRun r;
            r.text = part;
            r.origin = rect.topLeft() + QPointF(line.x() / k, (line.y() + line.ascent()) / k);
            r.center = center;
            r.size = it.fontSize;
            r.width = line.naturalTextWidth() / k;
            runs->append(r);
        }
}

// ---- the invisible text layer ----------------------------------------------------------------------------------------------
// The outlines of the letters carry no text, so each drawn line is added again as invisible text (render mode 3) in a simple font whose /ToUnicode
// table maps its one-byte codes to the real characters. Search and copy then give back the exact Unicode text, in the order it was typed.
std::string hexOf(const QString& s)
{
    std::string out;
    static const char* digits = "0123456789ABCDEF";
    for (const QChar c : s) {
        const ushort u = c.unicode();
        for (int shift = 12; shift >= 0; shift -= 4)
            out += digits[(u >> shift) & 0xF];
    }
    return out;
}

void addTextLayer(QPDF& pdf, QPDFObjectHandle form, const TextRuns& runs, const QSizeF& size)
{
    if (runs.isEmpty())
        return;
    struct FontBuild {
        std::vector<char32_t> chars;                 // code (index + 1) -> character
    };
    std::vector<FontBuild> fonts(1);
    std::map<char32_t, std::pair<int, int>> codeOf;  // character -> (font, code)
    auto encode = [&](char32_t ch) {
        if (auto it = codeOf.find(ch); it != codeOf.end())
            return it->second;
        if (fonts.back().chars.size() >= 255)
            fonts.emplace_back();
        fonts.back().chars.push_back(ch);
        const std::pair<int, int> v{int(fonts.size()) - 1, int(fonts.back().chars.size())};
        codeOf.emplace(ch, v);
        return v;
    };
    constexpr double advance = 0.5;                  // em per character (a rough width: the real letters are drawn as outlines)
    std::string content = "\n";
    for (const TextRun& run : runs) {
        const QList<uint> ucs = run.text.toUcs4();
        if (ucs.isEmpty())
            continue;
        const double a = run.angle * M_PI / 180.0;
        const QPointF d = run.origin - run.center;
        const QPointF rp(run.center.x() + d.x() * std::cos(a) - d.y() * std::sin(a), run.center.y() + d.x() * std::sin(a) + d.y() * std::cos(a));
        const double x = rp.x();
        const double y = size.height() - rp.y();     // PDF: origin at the bottom
        char buf[160];
        std::snprintf(buf, sizeof buf, "BT 3 Tr %.4f %.4f %.4f %.4f %.3f %.3f Tm\n", std::cos(a), -std::sin(a), std::sin(a), std::cos(a), x, y);
        content += buf;
        if (run.fitWidth) {
            const double scale = run.width / (advance * run.size * ucs.size());
            std::snprintf(buf, sizeof buf, "%.3f Tz\n", std::clamp(scale, 0.1, 10.0) * 100);
            content += buf;
        }
        int currentFont = -1;
        std::string hex;
        auto flush = [&] {
            if (!hex.empty()) {
                content += "<" + hex + "> Tj\n";
                hex.clear();
            }
        };
        for (const uint ch : ucs) {
            const auto [font, code] = encode(char32_t(ch));
            if (font != currentFont) {
                flush();
                std::snprintf(buf, sizeof buf, "/DvTxt%d %.3f Tf\n", font + 1, run.size);
                content += buf;
                currentFont = font;
            }
            static const char* digits = "0123456789ABCDEF";
            hex += digits[(code >> 4) & 0xF];
            hex += digits[code & 0xF];
        }
        flush();
        content += "ET\n";
    }

    QPDFObjectHandle resources = form.getDict().getKey("/Resources");
    if (!resources.isDictionary()) {
        resources = QPDFObjectHandle::newDictionary();
        form.getDict().replaceKey("/Resources", resources);
    }
    QPDFObjectHandle fontDict = resources.getKey("/Font");
    if (!fontDict.isDictionary()) {
        fontDict = QPDFObjectHandle::newDictionary();
        resources.replaceKey("/Font", fontDict);
    }
    for (size_t f = 0; f < fonts.size(); ++f) {
        std::string cmap = "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
                           "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n1 begincodespacerange\n<01> <FF>\nendcodespacerange\n";
        const size_t n = fonts[f].chars.size();
        for (size_t start = 0; start < n; start += 100) {
            const size_t end = std::min(n, start + 100);
            cmap += std::to_string(end - start) + " beginbfchar\n";
            for (size_t i = start; i < end; ++i) {
                char line[64];
                const QString ch = QString::fromUcs4(&fonts[f].chars[i], 1);
                std::snprintf(line, sizeof line, "<%02X> <", unsigned(i + 1));
                cmap += line + hexOf(ch) + ">\n";
            }
            cmap += "endbfchar\n";
        }
        cmap += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
        QPDFObjectHandle toUnicode = QPDFObjectHandle::newStream(&pdf, cmap);
        QPDFObjectHandle widths = QPDFObjectHandle::newArray();
        QPDFObjectHandle differences = QPDFObjectHandle::newArray();
        differences.appendItem(QPDFObjectHandle::newInteger(1));
        for (size_t i = 0; i < n; ++i) {
            widths.appendItem(QPDFObjectHandle::newInteger(int(advance * 1000)));
            // Every invisible code needs a real glyph bounding box for search highlighting.
            differences.appendItem(QPDFObjectHandle::newName("/H"));
        }
        QPDFObjectHandle encoding = QPDFObjectHandle::newDictionary();
        encoding.replaceKey("/Type", QPDFObjectHandle::newName("/Encoding"));
        encoding.replaceKey("/BaseEncoding", QPDFObjectHandle::newName("/WinAnsiEncoding"));
        encoding.replaceKey("/Differences", differences);
        QPDFObjectHandle font = QPDFObjectHandle::newDictionary();
        font.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
        font.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
        font.replaceKey("/BaseFont", QPDFObjectHandle::newName("/Helvetica"));
        font.replaceKey("/FirstChar", QPDFObjectHandle::newInteger(1));
        font.replaceKey("/LastChar", QPDFObjectHandle::newInteger(int(n)));
        font.replaceKey("/Widths", widths);
        font.replaceKey("/Encoding", encoding);
        font.replaceKey("/ToUnicode", toUnicode);
        fontDict.replaceKey("/DvTxt" + std::to_string(f + 1), pdf.makeIndirectObject(font));
    }
    std::shared_ptr<Buffer> old = form.getStreamData(qpdf_dl_generalized);
    // Qt's page stream can leave its device-resolution transform active. The
    // searchable text coordinates are already in PDF points.
    std::string data = "q\n";
    data.append(reinterpret_cast<const char*>(old->getBuffer()), old->getSize());
    data += "\nQ\n" + content;
    form.replaceStreamData(data, QPDFObjectHandle::newNull(), QPDFObjectHandle::newNull());
}

QString replaceTokens(QString text, int page, int pageCount)
{
    text.replace(QStringLiteral("{n}"), QString::number(page + 1));
    text.replace(QStringLiteral("{total}"), QString::number(pageCount));
    return text;
}

} // namespace

QRectF EditItem::bounds() const
{
    switch (kind) {
    case Kind::Line:
    case Kind::Arrow:
        return QRectF(a, b).normalized();
    case Kind::Pen:
    case Kind::Polygon: {
        if (points.isEmpty())
            return {};
        double x0 = points[0].x(), x1 = x0, y0 = points[0].y(), y1 = y0;
        for (const QPointF& p : points) {
            x0 = std::min(x0, p.x());
            x1 = std::max(x1, p.x());
            y0 = std::min(y0, p.y());
            y1 = std::max(y1, p.y());
        }
        return QRectF(QPointF(x0, y0), QPointF(x1, y1));
    }
    default:
        return rect.normalized();
    }
}

QList<EditItem> marksForPage(const QList<MarkDef>& marks, int page, int pageCount, const QSizeF& size)
{
    QList<EditItem> out;
    int nextId = -1;
    for (const MarkDef& m : marks) {
        const int to = m.toPage < 0 ? pageCount - 1 : m.toPage;
        if (page < m.fromPage || page > to)
            continue;
        EditItem it;
        it.id = nextId--;
        it.kind = Kind::Text;
        it.page = page;
        it.text = replaceTokens(m.text, page, pageCount);
        it.font = m.font;
        it.fontSize = m.fontSize;
        it.bold = m.bold;
        it.italic = m.italic;
        it.color = m.color;
        it.opacity = m.opacity;
        it.align = m.align;
        const double margin = 28;
        const double h = m.fontSize * 1.6;
        switch (m.type) {
        case MarkDef::Watermark: {
            // one line across the middle, turned 45 degrees; the box is as wide as the page diagonal allows
            const double w = std::min(size.width(), size.height()) * 1.2;
            it.align = 1;
            it.rect = QRectF((size.width() - w) / 2, (size.height() - h) / 2, w, h);
            it.angle = -45;
            break;
        }
        case MarkDef::PageNumber:
        case MarkDef::Footer:
            it.rect = QRectF(margin, size.height() - margin - h, size.width() - 2 * margin, h);
            break;
        case MarkDef::Header:
            it.rect = QRectF(margin, margin, size.width() - 2 * margin, h);
            break;
        }
        out.append(it);
    }
    return out;
}

void paintItem(QPainter& p, const EditItem& it, TextRuns* runs)
{
    const int firstRun = runs ? int(runs->size()) : 0;
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setOpacity(p.opacity() * std::clamp(it.opacity, 0.0, 1.0));
    const QRectF box = it.bounds();
    if (it.angle != 0.0) {
        p.translate(box.center());
        p.rotate(it.angle);
        p.translate(-box.center());
    }
    const QPen outline(it.color, std::max(0.1, it.width), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    switch (it.kind) {
    case Kind::Text:
        if (it.fill.isValid() && it.fill.alpha() > 0)
            p.fillRect(it.rect, it.fill);
        drawText(p, it.rect.adjusted(2, 1, -2, -1), it.text, it, it.color, box.center(), runs);
        break;
    case Kind::Image: {
        const QImage img = loadImage(it.imagePath);
        if (!img.isNull())
            p.drawImage(it.rect, img);
        break;
    }
    case Kind::Rect:
        p.setPen(it.width > 0 ? outline : Qt::NoPen);
        p.setBrush(it.fill.isValid() ? QBrush(it.fill) : Qt::NoBrush);
        p.drawRect(it.rect);
        break;
    case Kind::Ellipse:
        p.setPen(it.width > 0 ? outline : Qt::NoPen);
        p.setBrush(it.fill.isValid() ? QBrush(it.fill) : Qt::NoBrush);
        p.drawEllipse(it.rect);
        break;
    case Kind::Line:
    case Kind::Arrow: {
        p.setPen(outline);
        p.drawLine(it.a, it.b);
        if (it.kind == Kind::Arrow) {
            const QPointF d = it.b - it.a;
            const double len = std::hypot(d.x(), d.y());
            if (len > 0.5) {
                const double head = std::max(8.0, it.width * 4.0);
                const double ang = std::atan2(d.y(), d.x());
                for (const double s : {+0.45, -0.45})
                    p.drawLine(it.b, it.b - QPointF(std::cos(ang + s), std::sin(ang + s)) * head);
            }
        }
        break;
    }
    case Kind::Pen: {
        p.setPen(outline);
        p.setBrush(Qt::NoBrush);
        if (it.points.size() == 1) {
            p.drawPoint(it.points[0]);
        } else if (it.points.size() > 1) {
            QPainterPath path(it.points[0]);
            for (int i = 1; i < it.points.size(); ++i)
                path.lineTo(it.points[i]);
            p.drawPath(path);
        }
        break;
    }
    case Kind::Polygon: {
        p.setPen(it.width > 0 ? outline : Qt::NoPen);
        p.setBrush(it.fill.isValid() ? QBrush(it.fill) : Qt::NoBrush);
        if (it.points.size() >= 2)
            p.drawPolygon(QPolygonF(it.points));
        break;
    }
    case Kind::Note: {
        // a sticky note: a yellow card with a darker edge and the text on it
        p.setPen(QPen(it.color.darker(160), 1));
        p.setBrush(it.fill.isValid() ? QBrush(it.fill) : QBrush(QColor(255, 240, 140)));
        p.drawRect(it.rect);
        drawText(p, it.rect.adjusted(5, 4, -5, -4), it.text, it, it.color, box.center(), runs);
        break;
    }
    case Kind::Highlight:
        p.fillRect(it.rect, it.color);
        break;
    case Kind::Underline:
        p.setPen(outline);
        p.drawLine(it.rect.bottomLeft(), it.rect.bottomRight());
        break;
    case Kind::Strike:
        p.setPen(outline);
        p.drawLine(QPointF(it.rect.left(), it.rect.center().y()), QPointF(it.rect.right(), it.rect.center().y()));
        break;
    case Kind::Stamp: {
        p.setPen(QPen(it.color, std::max(1.0, it.width), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(it.fill.isValid() ? QBrush(it.fill) : Qt::NoBrush);
        p.drawRoundedRect(it.rect, 6, 6);
        EditItem t = it;
        t.bold = true;
        t.align = 1;
        // vertically centred: the text box is as high as one line
        const double lineH = it.fontSize * 1.5;
        drawText(p, QRectF(it.rect.x() + 4, it.rect.center().y() - lineH / 2, it.rect.width() - 8, lineH), it.text, t, it.color, box.center(), runs);
        break;
    }
    }
    p.restore();
    if (runs)
        for (int i = firstRun; i < runs->size(); ++i)
            (*runs)[i].angle = it.angle;
}

QImage renderOverlay(const QList<EditItem>& items, const QSizeF& baseSize, double ppp, int turns, int hiddenId)
{
    turns = ((turns % 4) + 4) % 4;
    const QSizeF shown = (turns % 2) ? QSizeF(baseSize.height(), baseSize.width()) : baseSize;
    const QSize px(std::max(1, int(std::lround(shown.width() * ppp))), std::max(1, int(std::lround(shown.height() * ppp))));
    QImage img(px, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    switch (turns) {
    case 1: p.translate(px.width(), 0); p.rotate(90); break;
    case 2: p.translate(px.width(), px.height()); p.rotate(180); break;
    case 3: p.translate(0, px.height()); p.rotate(270); break;
    default: break;
    }
    p.scale(ppp, ppp);
    for (const EditItem& it : items)
        if (it.id != hiddenId)
            paintItem(p, it);
    p.end();
    return img;
}

QStringList restrictedFonts(const QList<EditItem>& items, const QList<MarkDef>& marks)
{
    QSet<QString> families;
    for (const EditItem& it : items)
        if (it.kind == Kind::Text || it.kind == Kind::Stamp || it.kind == Kind::Note)
            families.insert(it.font);
    for (const MarkDef& m : marks)
        families.insert(m.font);
    QStringList bad;
    for (const QString& family : families) {
        const QRawFont raw = QRawFont::fromFont(QFont(family));
        if (!raw.isValid())
            continue;
        const QByteArray os2 = raw.fontTable("OS/2");
        if (os2.size() < 10)
            continue;
        const quint16 fsType = qFromBigEndian<quint16>(reinterpret_cast<const uchar*>(os2.constData()) + 8);
        if (fsType & 0x0002)                                  // "restricted licence embedding": the font must not be put into documents
            bad << family;
    }
    bad.sort();
    return bad;
}

EditResult saveWithEdits(const QString& inFile, const QString& password, const QString& outFile, const QList<PageEdits>& pages, bool overwrite,
                         const Progress& progress)
{
    EditResult result;
    QList<PageEdits> work;
    for (const PageEdits& pe : pages)
        if (!pe.items.isEmpty() || !pe.textLayer.isEmpty())
            work.append(pe);
    if (work.isEmpty()) {
        result.error = QStringLiteral("noEdits");
        return result;
    }
    if (!overwrite && QFileInfo::exists(outFile)) {
        result.error = QStringLiteral("exists");
        return result;
    }
    for (const PageEdits& pe : work)
        for (const EditItem& it : pe.items)
            if (it.kind == Kind::Image && (it.imagePath.isEmpty() || loadImage(it.imagePath).isNull())) {
                result.error = QStringLiteral("imageMissing");
                result.detail = it.imagePath;
                return result;
            }

    QTemporaryDir dir;
    if (!dir.isValid()) {
        result.error = QStringLiteral("write");
        return result;
    }
    const QString overlayFile = dir.filePath(QStringLiteral("overlay.pdf"));
    const QString tmp = outFile + QStringLiteral(".part.tmp");
    const int total = int(work.size());
    std::vector<TextRuns> runsPerPage(size_t(work.size()));
    try {
        // 1. one overlay document with a page per edited page; Qt shapes the text and embeds the fonts
        {
            QPdfWriter writer(overlayFile);
            writer.setResolution(1200);
            writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);
            QPainter painter;
            for (int i = 0; i < work.size(); ++i) {
                if (progress && !progress(i, total * 2))
                    throw CancelledError();
                const QPageSize size(work[i].size, QPageSize::Point, QString(), QPageSize::ExactMatch);
                writer.setPageSize(size);
                if (i == 0) {
                    if (!painter.begin(&writer)) {
                        result.error = QStringLiteral("write");
                        return result;
                    }
                } else {
                    writer.newPage();
                }
                painter.resetTransform();
                painter.scale(1200.0 / 72.0, 1200.0 / 72.0);
                painter.setOpacity(1.0);
                runsPerPage[size_t(i)] = work[i].textLayer;
                for (const EditItem& it : work[i].items)
                    paintItem(painter, it, &runsPerPage[size_t(i)]);
            }
            painter.end();
        }

        // 2. burn each overlay page into its page
        QPDF src;
        src.setSuppressWarnings(true);
        src.processFile(inFile.toUtf8().constData(), password.isEmpty() ? nullptr : password.toUtf8().constData());
        if (src.isEncrypted())
            result.warnings << QStringLiteral("encrypted");
        QPDF ov;
        ov.setSuppressWarnings(true);
        ov.processFile(overlayFile.toUtf8().constData());
        std::vector<QPDFPageObjectHelper> ovPages = QPDFPageDocumentHelper(ov).getAllPages();
        std::vector<QPDFPageObjectHelper> srcPages = QPDFPageDocumentHelper(src).getAllPages();
        if (ovPages.size() != size_t(work.size())) {
            result.error = QStringLiteral("unknown");
            result.detail = QStringLiteral("overlay page count");
            return result;
        }
        for (int i = 0; i < work.size(); ++i) {
            if (progress && !progress(total + i, total * 2))
                throw CancelledError();
            const int index = work[i].page;
            if (index < 0 || index >= int(srcPages.size())) {
                result.error = QStringLiteral("range");
                return result;
            }
            QPDFObjectHandle ovForm = ovPages[size_t(i)].getFormXObjectForPage();
            addTextLayer(ov, ovForm, runsPerPage[size_t(i)], work[i].size);
            QPDFPageObjectHelper target = srcPages[size_t(index)];
            QPDFObjectHandle fo = src.copyForeignObject(ovForm);
            QPDFObjectHandle resources = target.getAttribute("/Resources", true);
            if (!resources.isDictionary()) {
                resources = QPDFObjectHandle::newDictionary();
                target.getObjectHandle().replaceKey("/Resources", resources);
            }
            QPDFObjectHandle xobjects = resources.getKey("/XObject");
            if (!xobjects.isDictionary()) {
                xobjects = QPDFObjectHandle::newDictionary();
                resources.replaceKey("/XObject", xobjects);
            }
            std::string name = "/DvEdit" + std::to_string(i + 1);
            while (xobjects.hasKey(name))
                name += "x";
            QPDFObjectHandle::Rectangle box = target.getCropBox(true, true).getArrayAsRectangle();
            const std::string place = target.placeFormXObject(fo, name, box, true, true, true);
            xobjects.replaceKey(name, fo);
            target.addPageContents(QPDFObjectHandle::newStream(&src, "q\n"), true);
            target.addPageContents(QPDFObjectHandle::newStream(&src, "Q\n" + place), false);
        }
        result.pages = total;

        QFile::remove(tmp);
        const QByteArray outputName = tmp.toUtf8();        // Keep the filename alive for QPDFWriter.
        QPDFWriter writer(src, outputName.constData());
        writer.setObjectStreamMode(qpdf_o_generate);
        writer.setStreamDataMode(qpdf_s_compress);
        writer.write();
        if (progress && !progress(total * 2, total * 2))
            throw CancelledError();
    } catch (const CancelledError&) {
        QFile::remove(tmp);
        result.cancelled = true;
        result.error = QStringLiteral("cancelled");
        return result;
    } catch (const QPDFExc& e) {
        QFile::remove(tmp);
        result.error = e.getErrorCode() == qpdf_e_password ? QStringLiteral("password") : QStringLiteral("format");
        result.detail = QString::fromUtf8(e.what());
        return result;
    } catch (const QPDFSystemError& e) {
        QFile::remove(tmp);
        result.error = QFileInfo::exists(inFile) ? QStringLiteral("write") : QStringLiteral("file");
        result.detail = QString::fromUtf8(e.what());
        return result;
    } catch (const std::exception& e) {
        QFile::remove(tmp);
        result.error = QStringLiteral("unknown");
        result.detail = QString::fromUtf8(e.what());
        return result;
    }
    if (result.error.isEmpty() == false)
        return result;
    if (overwrite)
        QFile::remove(outFile);
    if (!QFile::rename(tmp, outFile)) {
        QFile::remove(tmp);
        result.error = QStringLiteral("write");
        result.detail = QStringLiteral("cannot create ") + outFile;
        return result;
    }
    result.ok = true;
    result.bytes = QFileInfo(outFile).size();
    return result;
}

} // namespace dp::tools
