#include "SelectiveRedaction.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QMap>
#include <QSet>

#include <qpdf/QPDF.hh>
#include <qpdf/Buffer.hh>
#include <qpdf/Pipeline.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <set>
#include <stdexcept>

namespace dp::tools {
namespace {

QByteArray number(double n) { return QByteArray::number(n, 'f', 8); }

QByteArray rectangle(const QRectF& r)
{
    return number(r.x()) + ' ' + number(r.y()) + ' ' + number(r.width()) + ' ' + number(r.height()) + " re ";
}

bool meets(const QRectF& a, const QRectF& b)
{
    return a.left() <= b.right() && b.left() <= a.right() && a.top() <= b.bottom() && b.top() <= a.bottom();
}

QByteArray colorCommand(const QColor& c)
{
    return number(c.redF()) + ' ' + number(c.greenF()) + ' ' + number(c.blueF()) + " rg\n";
}

bool standardFont(QPDFObjectHandle f)
{
    static const std::set<std::string> names{
        "/Helvetica", "/Helvetica-Bold", "/Helvetica-Oblique", "/Helvetica-BoldOblique",
        "/Times-Roman", "/Times-Bold", "/Times-Italic", "/Times-BoldItalic",
        "/Courier", "/Courier-Bold", "/Courier-Oblique", "/Courier-BoldOblique"};
    auto keys = f.getKeys();
    keys.erase("/Type");
    const bool encoded = keys.erase("/Encoding") != 0;
    return keys == std::set<std::string>{"/BaseFont", "/Subtype"}
        && f.getKey("/Subtype").isNameAndEquals("/Type1")
        && names.contains(f.getKey("/BaseFont").getName())
        && (!encoded || f.getKey("/Encoding").isNameAndEquals("/WinAnsiEncoding")
                     || f.getKey("/Encoding").isNameAndEquals("/StandardEncoding"));
}

struct CleanImage {
    QSize size;
    int channels = 0;
    QByteArray pixels;
    std::shared_ptr<Buffer> data;
    QByteArray fingerprint;
    QByteArray sanitizedFingerprint;
};

class BoundedBytes final : public Pipeline {
public:
    explicit BoundedBytes(qsizetype limit) : Pipeline("redaction image", nullptr), m_limit(limit) {}
    void write(const unsigned char* data, size_t len) override
    {
        if (len > size_t(m_limit - bytes.size()))
            throw std::runtime_error("image exceeds decoded size");
        bytes.append(reinterpret_cast<const char*>(data), qsizetype(len));
    }
    void finish() override {}
    QByteArray bytes;

private:
    qsizetype m_limit;
};

std::optional<CleanImage> readImage(QPDFObjectHandle stream)
{
    if (!stream.isStream())
        return {};
    auto d = stream.getDict();
    if (!d.getKey("/Subtype").isNameAndEquals("/Image") || d.getKey("/BitsPerComponent").getIntValue() != 8
        || !d.getKey("/SMask").isNull() || !d.getKey("/Mask").isNull() || !d.getKey("/Decode").isNull()
        || !d.getKey("/ImageMask").isNull() || !d.getKey("/Alternates").isNull()
        || !d.getKey("/OC").isNull() || !d.getKey("/SMaskInData").isNull()
        || (!d.getKey("/Interpolate").isNull() && !d.getKey("/Interpolate").isBool())
        || (d.getKey("/Interpolate").isBool() && d.getKey("/Interpolate").getBoolValue()))
        return {};
    const auto cs = d.getKey("/ColorSpace");
    const int channels = cs.isNameAndEquals("/DeviceRGB") ? 3 : cs.isNameAndEquals("/DeviceGray") ? 1 : 0;
    const auto w = d.getKey("/Width").getIntValue(), h = d.getKey("/Height").getIntValue();
    if (!channels || w <= 0 || h <= 0 || w > 10000 || h > 10000 || w * h > 16'000'000)
        return {};
    const auto filter = d.getKey("/Filter");
    const bool jpeg = filter.isNameAndEquals("/DCTDecode");
    if (!(jpeg || filter.isNull() || filter.isNameAndEquals("/FlateDecode")))
        return {};
    const auto bytes = qsizetype(w * h * channels);
    BoundedBytes data(jpeg ? 64 * 1024 * 1024 : bytes);
    if (!stream.pipeStreamData(&data, nullptr, 0, jpeg ? qpdf_dl_none : qpdf_dl_generalized, true))
        return {};
    CleanImage out{QSize(int(w), int(h)), channels, {}, {}, {}, {}};
    if (!jpeg) {
        if (data.bytes.size() != bytes)
            return {};
        out.pixels = std::move(data.bytes);
    } else {
        QBuffer buffer(&data.bytes);
        QImageReader reader(&buffer, "JPEG");
        if (reader.size() != out.size)
            return {};
        const auto image = reader.read();
        if (image.size() != out.size)
            return {};
        const auto rgb = image.convertToFormat(channels == 3 ? QImage::Format_RGB888 : QImage::Format_Grayscale8);
        if (rgb.isNull())
            return {};
        const qsizetype stride = w * channels;
        out.pixels.resize(bytes);
        for (int y = 0; y < h; ++y)
            std::memcpy(out.pixels.data() + y * stride, rgb.constScanLine(y), size_t(stride));
    }
    return out;
}

class Content final : public QPDFObjectHandle::ParserCallbacks {
public:
    Content(const pdf::RedactionGeometry& geometry, const QRectF& page, QPDFObjectHandle resources)
        : m_geometry(geometry), m_page(page), m_fonts(resources.getKey("/Font")), m_xObjects(resources.getKey("/XObject"))
    {}

    void contentSize(size_t size) override
    {
        if (size > 16 * 1024 * 1024)
            reject();
    }

    void handleObject(QPDFObjectHandle token) override
    {
        if (!token.isOperator()) {
            if (m_args.size() >= 8 || !(token.isNumber() || token.isString() || token.isName()
                                       || (m_inText && token.isArray())))
                reject();
            else
                m_args.append(token);
            return;
        }
        if (!operation(QByteArray::fromStdString(token.getOperatorValue())))
            reject();
        m_args.clear();
    }

    void handleEOF() override
    {
        if (!m_args.isEmpty() || m_inText || !m_stack.isEmpty() || !m_path.isEmpty() || m_imageRect
            || m_nextText != m_geometry.text.size())
            valid = false;
    }

    QByteArray commands;
    QByteArray vectors;
    bool valid = true;
    QMap<QByteArray, QByteArray> usedFonts;
    QList<QString> removedText;
    QList<CleanImage> images;
    qsizetype imageBytes = 0;

private:
    struct State {
        QByteArray fill = "0 g\n";
        QByteArray stroke = "0 G\n";
        QByteArray pen = "1 w\n";
        double width = 1;
        QByteArray font;
        QByteArray fontSize;
        int mode = 0;
    };
    const pdf::RedactionGeometry& m_geometry;
    QRectF m_page;
    QPDFObjectHandle m_fonts;
    QPDFObjectHandle m_xObjects;
    QList<QPDFObjectHandle> m_args;
    QList<State> m_stack;
    State m_state;
    bool m_inText = false;
    qsizetype m_nextText = 0;
    QByteArray m_path;
    QList<QPointF> m_points;
    bool m_rectangle = false;
    std::optional<QRectF> m_imageRect;
    bool m_imageDrawn = false;

    void reject()
    {
        valid = false;
        terminateParsing();
    }

    bool numbers(int n)
    {
        return m_args.size() == n && std::all_of(m_args.begin(), m_args.end(), [](auto a) {
            return a.isNumber() && std::isfinite(a.getNumericValue()) && std::abs(a.getNumericValue()) <= 1e6;
        });
    }

    double arg(int i) { return m_args[i].getNumericValue(); }

    QByteArray command(const QByteArray& op)
    {
        QByteArray out;
        for (auto arg : m_args)
            out += number(arg.getNumericValue()) + ' ';
        return out + op + '\n';
    }

    void vectorCommand(const QByteArray& data)
    {
        commands += data;
        vectors += data;
    }

    bool text(const QByteArray& op)
    {
        if (op == "ET" && m_args.isEmpty()) {
            m_inText = false;
            return true;
        }
        if (op == "Tf" && m_args.size() == 2 && m_args[0].isName() && m_args[1].isNumber()) {
            const auto name = m_args[0].getName();
            if (name.size() > 127 || !standardFont(m_fonts.getKey(name)) || arg(1) <= 0 || arg(1) > 2000)
                return false;
            m_state.font = QByteArray::fromStdString(name);
            m_state.fontSize = number(arg(1));
            return true;
        }
        if (op == "Tr" && numbers(1) && (arg(0) == 0 || arg(0) == 3)) {
            m_state.mode = int(arg(0));
            return true;
        }
        if (op == "Tm")
            return numbers(6) && arg(0) == 1 && arg(1) == 0 && arg(2) == 0 && arg(3) == 1;
        if (op == "Td" || op == "TD")
            return numbers(2);
        if (op == "TL")
            return numbers(1);
        if (op == "T*")
            return m_args.isEmpty();
        if (m_args.size() != 1 || m_state.font.isEmpty())
            return false;
        QByteArray value;
        QSet<qsizetype> breaks;
        if (op == "Tj" && m_args.first().isString()) {
            value = QByteArray::fromStdString(m_args.first().getStringValue());
        } else if (op == "TJ" && m_args.first().isArray()) {
            for (const auto& item : m_args.first().getArrayAsVector()) {
                if (item.isString())
                    value += QByteArray::fromStdString(item.getStringValue());
                else if (item.isNumber())
                    breaks.insert(value.size());
                else
                    return false;
            }
        } else {
            return false;
        }
        if (value.isEmpty())
            return true;
        if (m_nextText >= m_geometry.text.size())
            return false;
        const auto& glyphs = m_geometry.text.at(m_nextText++);
        if (glyphs.size() != value.size())
            return m_state.mode == 3 && glyphs.isEmpty();
        QByteArray kept;
        QByteArray removed;
        QPointF start;
        auto flushKept = [&] {
            if (kept.isEmpty())
                return;
            auto font = usedFonts.constFind(m_state.font);
            if (font == usedFonts.cend())
                font = usedFonts.insert(m_state.font, "/Font" + QByteArray::number(usedFonts.size()));
            commands += m_state.fill + "BT " + *font + ' ' + m_state.fontSize + " Tf 1 0 0 1 "
                + number(start.x()) + ' ' + number(start.y()) + " Tm <" + kept.toHex() + "> Tj ET\n";
            kept.clear();
        };
        auto flushRemoved = [&] {
            if (removed.trimmed().size() >= 2)
                removedText.append(QString::fromLatin1(removed.trimmed()));
            removed.clear();
        };
        for (qsizetype i = 0; i < glyphs.size(); ++i) {
            if (breaks.contains(i))
                flushKept();
            const auto& glyph = glyphs.at(i);
            const unsigned char c = value.at(i);
            if (c < 32 || c > 126 || glyph.character != c
                || !std::isfinite(glyph.origin.x()) || !std::isfinite(glyph.origin.y()))
                return false;
            const bool covered = c != ' ' && std::any_of(m_geometry.masks.begin(), m_geometry.masks.end(),
                [&](const auto& mask) { return meets(mask, glyph.bounds.adjusted(-0.25, -0.25, 0.25, 0.25)); });
            if (covered || (c != ' ' && !m_page.contains(glyph.bounds))) {
                flushKept();
                removed.append(char(c));
            } else if (m_state.mode == 0) {
                flushRemoved();
                if (kept.isEmpty())
                    start = glyph.origin;
                kept.append(char(c));
            }
        }
        flushKept();
        flushRemoved();
        return true;
    }

    bool paint(const QByteArray& op)
    {
        const bool fill = op == "f" || op == "F" || op == "f*";
        const bool stroke = op == "S" || op == "s";
        if (!(fill || stroke || op == "n") || !m_args.isEmpty() || m_points.isEmpty())
            return false;
        QRectF bounds(m_points.first(), QSizeF(0, 0));
        for (const auto& point : m_points) {
            bounds.setCoords(qMin(bounds.left(), point.x()), qMin(bounds.top(), point.y()),
                             qMax(bounds.right(), point.x()), qMax(bounds.bottom(), point.y()));
        }
        if (stroke)
            bounds.adjust(-5 * m_state.width, -5 * m_state.width, 5 * m_state.width, 5 * m_state.width);
        QList<QRectF> pieces{bounds.intersected(m_page)};
        const bool intersects = std::any_of(m_geometry.masks.begin(), m_geometry.masks.end(),
                                            [&](const auto& mask) { return meets(mask, bounds); });
        const bool needsCrop = !m_page.contains(bounds);
        if (op != "n" && (intersects || needsCrop)) {
            if (fill && m_rectangle && !pieces.first().isEmpty()) {
                for (const auto& mask : m_geometry.masks) {
                    QList<QRectF> next;
                    for (const auto& r : pieces) {
                        const auto hit = r.intersected(mask);
                        if (hit.isEmpty()) {
                            next.append(r);
                            continue;
                        }
                        for (const auto& part : {
                            QRectF(r.left(), r.top(), r.width(), hit.top() - r.top()),
                            QRectF(r.left(), hit.bottom(), r.width(), r.bottom() - hit.bottom()),
                            QRectF(r.left(), hit.top(), hit.left() - r.left(), hit.height()),
                            QRectF(hit.right(), hit.top(), r.right() - hit.right(), hit.height())}) {
                            if (!part.isEmpty())
                                next.append(part);
                        }
                    }
                    if (next.size() > 4000)
                        return false;
                    pieces = std::move(next);
                }
                for (const auto& r : pieces)
                    vectorCommand(m_state.fill + rectangle(r) + "f\n");
            } else if (meets(m_page, bounds) && std::none_of(m_geometry.masks.begin(), m_geometry.masks.end(),
                                    [&](const auto& mask) { return mask.contains(bounds); })) {
                return false;
            }
        } else if (op != "n") {
            vectorCommand(m_state.fill + m_state.stroke + m_state.pen + m_path + op + '\n');
        }
        m_path.clear();
        m_points.clear();
        m_rectangle = false;
        return true;
    }

    bool imageOperation(const QByteArray& op)
    {
        if (m_imageDrawn) {
            if (op != "Q" || !m_args.isEmpty())
                return false;
            m_state = m_stack.takeLast();
            m_imageRect.reset();
            m_imageDrawn = false;
            return true;
        }
        if (op != "Do" || m_args.size() != 1 || !m_args.first().isName() || !m_page.contains(*m_imageRect))
            return false;
        auto image = readImage(m_xObjects.getKey(m_args.first().getName()));
        if (!image)
            return false;
        const QRectF where = *m_imageRect;
        const int width = image->size.width(), height = image->size.height(), channels = image->channels;
        image->fingerprint = QByteArray::number(width) + '/' + QByteArray::number(height) + '/'
            + QByteArray::number(channels) + '/' + QCryptographicHash::hash(image->pixels, QCryptographicHash::Sha256);
        for (const auto& mask : m_geometry.masks) {
            const QRectF area = where.intersected(mask);
            if (area.isEmpty())
                continue;
            const double sx = width / where.width(), sy = height / where.height();
            const int l = qBound(0, int(std::floor((area.left() - where.left()) * sx)), width);
            const int r = qBound(0, int(std::ceil((area.right() - where.left()) * sx)), width);
            const int t = qBound(0, int(std::floor((where.bottom() - area.bottom()) * sy)), height);
            const int b = qBound(0, int(std::ceil((where.bottom() - area.top()) * sy)), height);
            for (int y = t; y < b; ++y)
                std::memset(image->pixels.data() + (qsizetype(y) * width + l) * channels, 0, size_t((r - l) * channels));
        }
        const auto name = QByteArray::number(images.size());
        imageBytes += image->pixels.size();
        if (imageBytes > 64 * 1024 * 1024)
            return false;
        image->sanitizedFingerprint = QCryptographicHash::hash(image->pixels, QCryptographicHash::Sha256);
        image->data = std::make_shared<Buffer>(image->pixels.toStdString());
        image->pixels.clear();
        images.append(std::move(*image));
        vectorCommand("q " + number(where.width()) + " 0 0 " + number(where.height()) + ' ' + number(where.x()) + ' '
                      + number(where.y()) + " cm /Img" + name + " Do Q\n");
        m_imageDrawn = true;
        return true;
    }

    bool operation(const QByteArray& op)
    {
        if (m_imageRect)
            return imageOperation(op);
        if ((op == "g" || op == "G" || op == "rg" || op == "RG" || op == "k" || op == "K")
            && numbers(op.size() == 2 ? 3 : (op.toLower() == "g" ? 1 : 4))) {
            if (std::any_of(m_args.begin(), m_args.end(), [](auto a) {
                    return a.getNumericValue() < 0 || a.getNumericValue() > 1;
                }))
                return false;
            (op == op.toLower() ? m_state.fill : m_state.stroke) = command(op);
            return true;
        }
        if (m_inText)
            return text(op);
        if (op == "BT" && m_args.isEmpty() && m_path.isEmpty()) {
            m_inText = true;
            return true;
        }
        if (op == "q" && m_args.isEmpty() && m_path.isEmpty() && m_stack.size() < 64) {
            m_stack.append(m_state);
            return true;
        }
        if (op == "Q" && m_args.isEmpty() && m_path.isEmpty() && !m_stack.isEmpty()) {
            m_state = m_stack.takeLast();
            return true;
        }
        if (op == "cm" && !m_stack.isEmpty() && m_path.isEmpty() && numbers(6)
            && arg(0) > 0 && arg(1) == 0 && arg(2) == 0 && arg(3) > 0) {
            m_imageRect = QRectF(arg(4), arg(5), arg(0), arg(3));
            return true;
        }
        if (op == "w" && numbers(1) && arg(0) > 0) {
            m_state.width = arg(0);
            m_state.pen = command(op);
            return true;
        }
        if (op == "re" && numbers(4) && arg(2) > 0 && arg(3) > 0) {
            m_rectangle = m_path.isEmpty();
            m_points.append(QPointF(arg(0), arg(1)));
            m_points.append(QPointF(arg(0) + arg(2), arg(1) + arg(3)));
        } else if (((op == "m" || op == "l") && numbers(2)) || (op == "c" && numbers(6))
                   || (op == "h" && m_args.isEmpty() && !m_path.isEmpty())) {
            if (m_path.isEmpty() && op != "m")
                return false;
            m_rectangle = false;
            for (int i = 0; i < m_args.size(); i += 2)
                m_points.append(QPointF(arg(i), arg(i + 1)));
        } else {
            return paint(op);
        }
        m_path += command(op);
        return m_path.size() <= 4 * 1024 * 1024;
    }
};

void addPage(QPDF& pdf, QPDFPageDocumentHelper& document, QSizeF size, QPDFObjectHandle fonts,
             const QByteArray& commands, const QList<CleanImage>& images)
{
    auto resources = QPDFObjectHandle::newDictionary();
    resources.replaceKey("/Font", fonts);
    auto objects = QPDFObjectHandle::newDictionary();
    for (qsizetype i = 0; i < images.size(); ++i) {
        const auto& image = images.at(i);
        auto object = pdf.newStream(image.data);
        auto d = object.getDict();
        d.replaceKey("/Type", QPDFObjectHandle::newName("/XObject"));
        d.replaceKey("/Subtype", QPDFObjectHandle::newName("/Image"));
        d.replaceKey("/Width", QPDFObjectHandle::newInteger(image.size.width()));
        d.replaceKey("/Height", QPDFObjectHandle::newInteger(image.size.height()));
        d.replaceKey("/BitsPerComponent", QPDFObjectHandle::newInteger(8));
        d.replaceKey("/ColorSpace", QPDFObjectHandle::newName(image.channels == 3 ? "/DeviceRGB" : "/DeviceGray"));
        objects.replaceKey("/Img" + std::to_string(i), object);
    }
    if (!images.isEmpty())
        resources.replaceKey("/XObject", objects);
    auto page = QPDFObjectHandle::parse("<< /Type /Page /MediaBox [0 0 "
        + number(size.width()).toStdString() + " " + number(size.height()).toStdString() + "] >>");
    page.replaceKey("/Resources", resources);
    page.replaceKey("/Contents", pdf.newStream(commands.toStdString()));
    document.addPage(QPDFPageObjectHelper(pdf.makeIndirectObject(page)), false);
}

bool visibleText(const pdf::RedactionGeometry& geometry, const QImage& full, const QImage& vectors, QSizeF page)
{
    QList<QRect> glyphs;
    qint64 area = 0;
    const double sx = full.width() / page.width(), sy = full.height() / page.height();
    for (const auto& run : geometry.text) {
        for (const auto& glyph : run) {
            if (glyph.character == U' ')
                continue;
            const auto& box = glyph.shownBounds;
            const QRect rect = QRectF(box.left() * sx, box.top() * sy,
                                      box.width() * sx, box.height() * sy).toAlignedRect()
                .adjusted(-1, -1, 1, 1).intersected(full.rect());
            area += qint64(rect.width()) * rect.height();
            if (rect.isEmpty() || area > 8 * qint64(full.width()) * full.height())
                return false;
            glyphs.append(rect);
        }
    }
    if (glyphs.isEmpty())
        return true;
    QImage coverage(full.size(), QImage::Format_Grayscale8);
    if (coverage.isNull())
        return false;
    coverage.fill(0);
    for (const auto& rect : glyphs) {
        for (int y = rect.top(); y <= rect.bottom(); ++y) {
            auto* row = coverage.scanLine(y);
            for (int x = rect.left(); x <= rect.right(); ++x)
                row[x] = qMin(2, row[x] + 1);
        }
    }
    // A glyph whose removal cannot change an exclusive pixel may be invisible, covered or duplicated.
    for (const auto& rect : glyphs) {
        bool visible = false;
        for (int y = rect.top(); y <= rect.bottom() && !visible; ++y) {
            const auto* count = coverage.constScanLine(y);
            const auto* a = reinterpret_cast<const QRgb*>(full.constScanLine(y));
            const auto* b = reinterpret_cast<const QRgb*>(vectors.constScanLine(y));
            for (int x = rect.left(); x <= rect.right(); ++x) {
                if (count[x] == 1 && a[x] != b[x]) {
                    visible = true;
                    break;
                }
            }
        }
        if (!visible)
            return false;
    }
    return true;
}

} // namespace

QString writeSelectiveRedaction(const pdf::PdfDocument& source, const QString& staged,
                                const QList<QList<QRectF>>& marks, const QColor& color, const Progress& progress)
{
    try {
        QPDF input;
        input.setSuppressWarnings(true);
        const auto filename = source.path().toUtf8();
        input.processFile(filename.constData());
        if (input.isEncrypted())
            return QStringLiteral("redactProtected");
        const auto pages = QPDFPageDocumentHelper(input).getAllPages();
        if (pages.size() != size_t(source.pageCount()))
            return QStringLiteral("redactUnsupported");
        QPDF output;
        output.emptyPDF();
        QPDFPageDocumentHelper destination(output);
        QPDF vectorOnly;
        vectorOnly.emptyPDF();
        QPDFPageDocumentHelper vectorDestination(vectorOnly);
        QSet<QString> removed;
        QHash<QByteArray, QByteArray> imageVersions;
        qsizetype imageBytes = 0;
        for (int i = 0; i < source.pageCount(); ++i) {
            if (progress && !progress(i, source.pageCount() * 2))
                return QStringLiteral("cancelled");
            auto page = pages.at(size_t(i));
            auto box = page.getMediaBox();
            const auto size = source.pageSizePt(i);
            if (!box.isArray() || box.getArrayNItems() != 4 || box.getArrayItem(0).getNumericValue() != 0
                || box.getArrayItem(1).getNumericValue() != 0
                || std::abs(box.getArrayItem(2).getNumericValue() - size.width()) > 0.001
                || std::abs(box.getArrayItem(3).getNumericValue() - size.height()) > 0.001
                || !page.getAttribute("/CropBox", false).isNull()
                || !page.getObjectHandle().getKey("/UserUnit").isNull())
                return QStringLiteral("redactUnsupported");
            const auto geometry = source.redactionGeometry(i, marks.at(i));
            if (!geometry.valid || std::any_of(geometry.masks.begin(), geometry.masks.end(), [](const auto& r) { return r.isEmpty(); }))
                return QStringLiteral("redactUnsupported");
            auto resources = page.getAttribute("/Resources", false);
            if (!resources.isDictionary())
                resources = QPDFObjectHandle::newDictionary();
            auto fonts = resources.getKey("/Font");
            Content content(geometry, QRectF(QPointF(), size), resources);
            page.parsePageContents(&content);
            if (!content.valid)
                return QStringLiteral("redactUnsupported");
            imageBytes += content.imageBytes;
            if (imageBytes > 128 * 1024 * 1024)
                return QStringLiteral("redactUnsupported");
            for (const auto& image : content.images) {
                const auto existing = imageVersions.constFind(image.fingerprint);
                if (existing != imageVersions.cend() && *existing != image.sanitizedFingerprint)
                    return QStringLiteral("redactDuplicateImage");
                imageVersions.insert(image.fingerprint, image.sanitizedFingerprint);
            }
            for (const auto& value : content.removedText)
                removed.insert(value);
            for (const auto& mask : geometry.masks) {
                const auto fill = colorCommand(color) + rectangle(mask) + "f\n";
                content.commands += fill;
                content.vectors += fill;
            }
            auto pageFonts = QPDFObjectHandle::newDictionary();
            for (auto it = content.usedFonts.cbegin(); it != content.usedFonts.cend(); ++it) {
                auto font = fonts.getKey(it.key().toStdString());
                auto cleanFont = QPDFObjectHandle::newDictionary();
                cleanFont.replaceKey("/Type", QPDFObjectHandle::newName("/Font"));
                cleanFont.replaceKey("/Subtype", QPDFObjectHandle::newName("/Type1"));
                cleanFont.replaceKey("/BaseFont", QPDFObjectHandle::newName(font.getKey("/BaseFont").getName()));
                if (!font.getKey("/Encoding").isNull())
                    cleanFont.replaceKey("/Encoding", QPDFObjectHandle::newName(font.getKey("/Encoding").getName()));
                pageFonts.replaceKey(it.value().toStdString(), cleanFont);
            }
            addPage(output, destination, size, pageFonts, content.commands, content.images);
            addPage(vectorOnly, vectorDestination, size, QPDFObjectHandle::newDictionary(), content.vectors, content.images);
        }
        if (!input.getWarnings().empty())
            return QStringLiteral("redactUnsupported");
        const auto outputName = staged.toUtf8();
        const QString vectorPath = QFileInfo(staged).absolutePath() + QStringLiteral("/comparison.pdf");
        const auto vectorName = vectorPath.toUtf8();
        {
            QPDFWriter writer(output, outputName.constData());
            writer.write();
            QPDFWriter vectorWriter(vectorOnly, vectorName.constData());
            vectorWriter.write();
        }
        const auto result = pdf::PdfDocument::open(staged, {}, nullptr);
        const auto reference = pdf::PdfDocument::open(vectorPath, {}, nullptr);
        if (!result || !reference || result->pageCount() != source.pageCount() || reference->pageCount() != source.pageCount())
            return QStringLiteral("redactVerify");
        for (int i = 0; i < source.pageCount(); ++i) {
            if (progress && !progress(source.pageCount() + i, source.pageCount() * 2))
                return QStringLiteral("cancelled");
            const QString extracted = result->pageText(i);
            for (const auto& text : removed)
                if (extracted.contains(text, Qt::CaseInsensitive))
                    return QStringLiteral("redactDuplicate");
            const auto geometry = result->redactionGeometry(i, marks.at(i));
            if (!geometry.valid || result->pageSizePt(i) != source.pageSizePt(i))
                return QStringLiteral("redactVerify");
            for (const auto& object : geometry.text) {
                for (const auto& glyph : object) {
                    if (glyph.character != U' ' && std::any_of(geometry.masks.begin(), geometry.masks.end(),
                            [&](const auto& mask) { return meets(mask, glyph.bounds); }))
                        return QStringLiteral("redactVerify");
                }
            }
            const auto points = source.pageSizePt(i);
            const double scale = std::min(2.0, std::sqrt(12'000'000.0 / (points.width() * points.height())));
            const auto before = source.render(i, scale, nullptr, 0, false, false);
            const auto after = result->render(i, scale);
            const auto vectors = reference->render(i, scale);
            if (before.isNull() || before.size() != after.size() || before.size() != vectors.size())
                return QStringLiteral("redactVerify");
            if (!visibleText(geometry, after, vectors, points))
                return QStringLiteral("redactHidden");
            QList<QRect> regions;
            const double sx = before.width() / points.width();
            const double sy = before.height() / points.height();
            for (const auto& mark : marks.at(i)) {
                const QRect rect = QRectF(mark.x() * sx, mark.y() * sy, mark.width() * sx, mark.height() * sy).toAlignedRect();
                if (!after.rect().contains(rect.center()) || after.pixelColor(rect.center()) != color)
                    return QStringLiteral("redactVerify");
                regions.append(rect.adjusted(-1, -1, 1, 1));
            }
            for (int y = 0; y < before.height(); ++y) {
                const auto* a = reinterpret_cast<const QRgb*>(before.constScanLine(y));
                const auto* b = reinterpret_cast<const QRgb*>(after.constScanLine(y));
                for (int x = 0; x < before.width(); ++x) {
                    if (a[x] != b[x] && std::none_of(regions.begin(), regions.end(),
                                                    [&](const auto& region) { return region.contains(x, y); }))
                        return QStringLiteral("redactAppearance");
                }
            }
        }
        if (progress && !progress(source.pageCount() * 2, source.pageCount() * 2))
            return QStringLiteral("cancelled");
        return {};
    } catch (const std::exception&) {
        return QStringLiteral("redactUnsupported");
    }
}

} // namespace dp::tools
