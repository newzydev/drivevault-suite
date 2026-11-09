#include "Redaction.h"
#include "pdfengine/PdfDocument.h"
#include "pdftools/RedactionAudit.h"
#include "pdftools/SelectiveRedaction.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QPainter>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dp {
namespace {

// This writer has no access to the source PDF. It accepts only sanitized pixels, page sizes and generated commands.
class RasterPdf {
public:
    explicit RasterPdf(QFile& file) : m_file(file) {}

    bool start(int pages)
    {
        QByteArray kids;
        for (int i = 0; i < pages; ++i)
            kids += QByteArray::number(3 + i * 3) + " 0 R ";
        return write("%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
            && object("<< /Type /Catalog /Pages 2 0 R >>")
            && object("<< /Type /Pages /Kids [" + kids + "] /Count " + QByteArray::number(pages) + " >>");
    }

    bool page(const QImage& image, QSizeF points, QByteArray& hash)
    {
        const auto width = QByteArray::number(points.width(), 'f', 6);
        const auto height = QByteArray::number(points.height(), 'f', 6);
        const auto number = int(m_offsets.size()) + 1;
        const auto commands = "q " + width + " 0 0 " + height + " 0 0 cm /Image Do Q\n";
        if (!object("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + width + " " + height
                    + "] /Resources << /XObject << /Image " + QByteArray::number(number + 2)
                    + " 0 R >> >> /Contents " + QByteArray::number(number + 1) + " 0 R >>")
            || !stream({}, commands))
            return false;

        const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
        if (rgb.isNull())
            return false;
        const qsizetype rowBytes = qsizetype(rgb.width()) * 3;
        QByteArray pixels(rowBytes * rgb.height(), Qt::Uninitialized);
        for (int y = 0; y < rgb.height(); ++y)
            std::memcpy(pixels.data() + y * rowBytes, rgb.constScanLine(y), size_t(rowBytes));
        hash = QCryptographicHash::hash(pixels, QCryptographicHash::Sha256);
        const auto compressed = qCompress(pixels);
        if (compressed.size() <= 4)
            return false;
        return stream("/Type /XObject /Subtype /Image /BitsPerComponent 8 /ColorSpace /DeviceRGB /Filter /FlateDecode /Width "
                      + QByteArray::number(rgb.width()) + " /Height " + QByteArray::number(rgb.height()),
                      QByteArrayView(compressed).sliced(4));
    }

    bool finish()
    {
        const auto xref = m_file.pos();
        const auto count = QByteArray::number(m_offsets.size() + 1);
        if (xref >= maxBytes || !write("xref\n0 " + count + "\n0000000000 65535 f \n"))
            return false;
        for (const auto offset : m_offsets) {
            if (!write(QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n"))
                return false;
        }
        return write("trailer\n<< /Size " + count + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n")
            && m_file.flush();
    }

private:
    static constexpr qint64 maxBytes = 0xffffffff;
    QFile& m_file;
    QList<qint64> m_offsets;

    bool write(QByteArrayView data)
    {
        return m_file.pos() + data.size() < maxBytes && m_file.write(data.data(), data.size()) == data.size();
    }

    bool begin()
    {
        m_offsets.append(m_file.pos());
        return write(QByteArray::number(m_offsets.size()) + " 0 obj\n");
    }

    bool object(const QByteArray& data) { return begin() && write(data) && write("\nendobj\n"); }

    bool stream(const QByteArray& dict, QByteArrayView data)
    {
        return begin() && write("<< " + dict + " /Length " + QByteArray::number(data.size()) + " >>\nstream\n")
            && write(data) && write("\nendstream\nendobj\n");
    }
};

QRect pixelRect(const QRectF& rect, const QSize& size, const QSizeF& points)
{
    const double sx = size.width() / points.width();
    const double sy = size.height() / points.height();
    return QRect(QPoint(int(std::floor(rect.left() * sx)) - 1, int(std::floor(rect.top() * sy)) - 1),
                 QPoint(int(std::ceil(rect.right() * sx)), int(std::ceil(rect.bottom() * sy))))
        .intersected(QRect(QPoint(), size));
}

} // namespace

QString redactPdf(const pdf::PdfDocument& source, const QString& path, const QList<RedactionMark>& marks,
                  const QColor& color, const QString& label, const tools::Progress& progress, bool selective)
{
    if (source.isEncrypted())
        return QStringLiteral("redactProtected");
    if (marks.isEmpty() || !color.isValid() || color.alpha() != 255 || label.size() > 80)
        return QStringLiteral("redactMarks");
    if (QFileInfo::exists(path))
        return QStringLiteral("exists");
    const int count = source.pageCount();
    QList<QList<QRectF>> byPage(count);
    for (const auto& mark : marks) {
        const auto size = source.pageSizePt(mark.page);
        const QRectF rect = mark.rect.normalized();
        if (mark.page < 0 || mark.page >= count || !rect.isValid()
            || !QRectF(QPointF(), size).contains(rect))
            return QStringLiteral("redactMarks");
        byPage[mark.page].append(rect);
    }
    const auto advance = [&](int done) { return !progress || progress(done, 3 * count); };
    const QTemporaryDir staging(QFileInfo(path).absolutePath() + QStringLiteral("/.dv-redact-XXXXXX"));
    if (!staging.isValid())
        return QStringLiteral("write");
    const QString staged = staging.filePath(QStringLiteral("verified.pdf"));
    const auto publish = [&] {
        if (!QFile::rename(staged, QFileInfo(path).absoluteFilePath()))
            return QFileInfo::exists(path) ? QStringLiteral("exists") : QStringLiteral("write");
        return QString();
    };
    if (selective) {
        if (!label.isEmpty())
            return QStringLiteral("redactSelectiveLabel");
        const QString error = tools::writeSelectiveRedaction(source, staged, byPage, color, progress);
        return error.isEmpty() ? publish() : error;
    }
    QFile file(staged);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return QStringLiteral("write");
    RasterPdf writer(file);
    if (!writer.start(count))
        return QStringLiteral("write");
    QList<QByteArray> hashes;
    QList<double> scales;
    for (int page = 0; page < count; ++page) {
        if (!advance(page))
            return QStringLiteral("cancelled");
        const auto size = source.pageSizePt(page);
        if (size.isEmpty() || !std::isfinite(size.width() * size.height()))
            return QStringLiteral("render");
        const double scale = std::min({200.0 / 72, 16000.0 / size.width(), 16000.0 / size.height(),
                                       std::sqrt(20'000'000.0 / (size.width() * size.height()))});
        QImage image = source.render(page, scale);
        if (image.isNull())
            return QStringLiteral("render");
        QPainter painter(&image);
        for (const auto& rect : byPage.at(page))
            painter.fillRect(pixelRect(rect, image.size(), size), color);
        if (!label.isEmpty()) {
            for (const auto& rect : byPage.at(page)) {
                const QRect box = pixelRect(rect, image.size(), size).adjusted(3, 3, -3, -3);
                if (box.height() < 10 || box.width() < 10)
                    continue;
                QFont font = painter.font();
                font.setPixelSize(qMin(36, box.height() - 2));
                painter.setFont(font);
                painter.setPen(qGray(color.rgb()) < 128 ? Qt::white : Qt::black);
                painter.save();
                painter.setClipRect(box);
                painter.drawText(box, Qt::AlignCenter | Qt::TextSingleLine,
                                 painter.fontMetrics().elidedText(label, Qt::ElideRight, box.width()));
                painter.restore();
            }
        }
        painter.end();
        QByteArray hash;
        if (!writer.page(image, size, hash))
            return QStringLiteral("write");
        hashes.append(hash);
        scales.append(scale);
    }
    if (!writer.finish())
        return QStringLiteral("write");
    file.close();
    bool cancelled = false;
    if (!tools::auditRedaction(file.fileName(), hashes, [&](int page) {
            cancelled = !advance(count + page);
            return !cancelled;
        }))
        return cancelled ? QStringLiteral("cancelled") : QStringLiteral("redactVerify");
    auto result = pdf::PdfDocument::open(file.fileName(), {}, nullptr);
    if (!result || result->pageCount() != count)
        return QStringLiteral("redactVerify");
    for (int page = 0; page < count; ++page) {
        if (!advance(2 * count + page))
            return QStringLiteral("cancelled");
        if (result->charCount(page) != 0 || !result->pageText(page).isEmpty())
            return QStringLiteral("redactVerify");
        if (byPage.at(page).isEmpty())
            continue;
        const auto image = result->render(page, scales.at(page));
        if (image.isNull())
            return QStringLiteral("redactVerify");
        for (const auto& rect : byPage.at(page)) {
            const QRect box = pixelRect(rect, image.size(), result->pageSizePt(page));
            if (box.isEmpty())
                return QStringLiteral("redactVerify");
            const QRect inner = box.adjusted(2, 2, -2, -2);
            if (image.pixelColor(box.center()) != color && (inner.isEmpty() || image.pixelColor(inner.topLeft()) != color))
                return QStringLiteral("redactVerify");
        }
    }
    if (!advance(3 * count))
        return QStringLiteral("cancelled");
    // Close PDFium's file handle before the atomic rename on Windows.
    result.reset();
    return publish();
}

} // namespace dp
