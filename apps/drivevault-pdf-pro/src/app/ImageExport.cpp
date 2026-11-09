#include "ImageExport.h"
#include "pdfengine/PdfDocument.h"
#include "pdftools/SplitPlan.h"

#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QImageWriter>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <array>
#include <cmath>
#include <limits>
#include <optional>

namespace dp {
namespace {

quint32 crc32(const QByteArray& bytes)
{
    static constexpr auto table = [] {
        std::array<quint32, 256> values{};
        for (quint32 i = 0; i < values.size(); ++i) {
            quint32 crc = i;
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
            values[i] = crc;
        }
        return values;
    }();
    quint32 crc = 0xffffffffu;
    for (unsigned char byte : bytes)
        crc = table[(crc ^ byte) & 0xff] ^ (crc >> 8);
    return ~crc;
}

// PNG/JPEG are already compressed. Write stored ZIP entries and retain only their central directory metadata.
class ImageZip {
public:
    explicit ImageZip(QIODevice& file) : m_file(file), m_stream(&file)
    {
        m_stream.setByteOrder(QDataStream::LittleEndian);
    }

    bool add(QByteArray name, const QByteArray& image)
    {
        if (m_file.pos() < 0 || name.size() > 0xffff || image.size() > std::numeric_limits<int>::max())
            return false;
        const Entry entry{std::move(name), crc32(image), quint32(image.size()), quint64(m_file.pos())};
        m_stream << quint32(0x04034b50) << quint16(entry.offset >= max32 ? 45 : 20);
        fileFields(entry);
        m_stream << quint16(0);
        if (!raw(entry.name) || !raw(image))
            return false;
        m_entries.append(entry);
        return m_stream.status() == QDataStream::Ok;
    }

    bool finish()
    {
        const quint64 start = m_file.pos();
        for (const Entry& entry : m_entries) {
            const bool wide = entry.offset >= max32;
            m_stream << quint32(0x02014b50) << quint16(wide ? 45 : 20) << quint16(wide ? 45 : 20);
            fileFields(entry);
            m_stream << quint16(wide ? 12 : 0) << quint16(0) << quint16(0) << quint16(0)
                     << quint32(0) << quint32(wide ? max32 : entry.offset);
            if (!raw(entry.name))
                return false;
            if (wide)
                m_stream << quint16(1) << quint16(8) << entry.offset;
        }
        const quint64 end = m_file.pos();
        const quint64 size = end - start;
        const quint64 count = m_entries.size();
        if (start >= max32 || size >= max32 || count >= 0xffff) {
            m_stream << quint32(0x06064b50) << quint64(44) << quint16(45) << quint16(45)
                     << quint32(0) << quint32(0) << count << count << size << start;
            m_stream << quint32(0x07064b50) << quint32(0) << end << quint32(1);
        }
        m_stream << quint32(0x06054b50) << quint16(0) << quint16(0)
                 << quint16(qMin(count, quint64(0xffff))) << quint16(qMin(count, quint64(0xffff)))
                 << quint32(qMin(size, max32)) << quint32(qMin(start, max32)) << quint16(0);
        return m_stream.status() == QDataStream::Ok;
    }

private:
    struct Entry { QByteArray name; quint32 crc; quint32 size; quint64 offset; };
    static constexpr quint64 max32 = 0xffffffffu;

    void fileFields(const Entry& entry)
    {
        // UTF-8 names, stored data, and a valid DOS date (1980-01-01).
        m_stream << quint16(0x800) << quint16(0) << quint16(0) << quint16(0x21)
                 << entry.crc << entry.size << entry.size << quint16(entry.name.size());
    }

    bool raw(const QByteArray& data)
    {
        return m_stream.writeRawData(data.constData(), int(data.size())) == data.size();
    }

    QIODevice& m_file;
    QDataStream m_stream;
    QList<Entry> m_entries;
};

QString encodePage(const pdf::PdfDocument& doc, int page, const ImageExport& options,
                   const std::atomic_bool& cancel, QIODevice& output)
{
    const double scale = options.dpi / 72.0;
    const QSizeF points = doc.pageSizePt(page, options.rotation);
    if (points.isEmpty())
        return QStringLiteral("range");
    QImage image = doc.render(page, scale, &cancel, options.rotation, options.transparent && !options.jpeg);
    if (cancel.load())
        return QStringLiteral("cancelled");
    if (image.isNull())
        return QStringLiteral("render");
    // The viewer may reduce oversized pages. An export must meet the selected DPI.
    if (image.width() != qMax(1.0, std::round(points.width() * scale))
        || image.height() != qMax(1.0, std::round(points.height() * scale)))
        return QStringLiteral("imageSize");
    const int dotsPerMeter = qRound(options.dpi / 0.0254);
    image.setDotsPerMeterX(dotsPerMeter);
    image.setDotsPerMeterY(dotsPerMeter);
    QImageWriter writer(&output, options.jpeg ? "jpeg" : "png");
    if (options.jpeg)
        writer.setQuality(options.quality);
    return writer.write(image) ? QString() : QStringLiteral("write");
}

} // namespace

QString exportImages(const pdf::PdfDocument& doc, const QString& destination, const ImageExport& options,
                     const std::atomic_bool& cancel, const std::function<void(int, int)>& progress)
{
    const int total = int(options.pages.size());
    if (total == 0 || (options.target == ImageExport::Target::Image && total != 1))
        return QStringLiteral("range");
    if (options.dpi < 72 || options.dpi > 600 || options.quality < 1 || options.quality > 100)
        return QStringLiteral("imageOptions");
    if (QFileInfo::exists(destination))
        return QStringLiteral("exists");
    if (cancel.load())
        return QStringLiteral("cancelled");

    const QFileInfo target(destination);
    const QString staging = target.absolutePath() + QStringLiteral("/.dv-images-XXXXXX");
    if (options.target == ImageExport::Target::Image) {
        QTemporaryFile file(staging);
        if (!file.open())
            return QStringLiteral("write");
        const QString error = encodePage(doc, options.pages.first(), options, cancel, file);
        if (!error.isEmpty())
            return error;
        if (!file.flush())
            return QStringLiteral("write");
        file.close();
        if (cancel.load())
            return QStringLiteral("cancelled");
        if (!file.rename(target.absoluteFilePath()))
            return QFileInfo::exists(destination) ? QStringLiteral("exists") : QStringLiteral("write");
        file.setAutoRemove(false);
        progress(1, 1);
        return {};
    }

    const bool zipped = options.target == ImageExport::Target::Zip;
    QTemporaryFile archive(staging);
    std::optional<QTemporaryDir> folder;
    if (zipped) {
        if (!archive.open())
            return QStringLiteral("write");
    } else {
        folder.emplace(staging);
        if (!folder->isValid())
            return QStringLiteral("write");
    }
    ImageZip zip(archive);
    const QString stem = tools::safeFileName(QFileInfo(doc.path()).completeBaseName());
    const QString extension = options.jpeg ? QStringLiteral("jpg") : QStringLiteral("png");
    for (int i = 0; i < total; ++i) {
        if (cancel.load())
            return QStringLiteral("cancelled");
        const int page = options.pages.at(i);
        const QString name = QStringLiteral("%1-%2.%3").arg(stem).arg(page + 1, 4, 10, QLatin1Char('0')).arg(extension);
        if (zipped) {
            QBuffer buffer;
            buffer.open(QIODevice::WriteOnly);
            const QString error = encodePage(doc, page, options, cancel, buffer);
            if (!error.isEmpty())
                return error;
            if (cancel.load())
                return QStringLiteral("cancelled");
            if (!zip.add(name.toUtf8(), buffer.data()))
                return QStringLiteral("write");
        } else {
            QFile file(folder->filePath(name));
            if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
                return QStringLiteral("write");
            const QString error = encodePage(doc, page, options, cancel, file);
            if (!error.isEmpty())
                return error;
            if (!file.flush())
                return QStringLiteral("write");
        }
        progress(i + 1, total);
    }
    if (zipped) {
        if (!zip.finish() || !archive.flush())
            return QStringLiteral("write");
        archive.close();
    }
    if (cancel.load())
        return QStringLiteral("cancelled");
    const QString finalPath = target.absoluteFilePath();
    const bool renamed = zipped ? archive.rename(finalPath) : QDir().rename(folder->path(), finalPath);
    if (!renamed)
        return QFileInfo::exists(destination) ? QStringLiteral("exists") : QStringLiteral("write");
    if (zipped)
        archive.setAutoRemove(false);
    else
        folder->setAutoRemove(false);
    return {};
}

} // namespace dp
