#include "MediaCache.h"

#include "AudioReader.h"
#include "VideoReader.h"
#include "core/FileUtils.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dv::mediacache {

QString keyFor(const QString& path, const MediaInfo& info)
{
    const QByteArray raw = QDir::cleanPath(path).toLower().toUtf8() + '|' + QByteArray::number(qlonglong(info.fileSize)) + '|'
                           + QByteArray::number(qlonglong(info.modifiedMs));
    return QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Sha1).toHex().left(20));
}

QString thumbnailPath(const QString& cacheDir, const QString& key)
{
    return cacheDir + QStringLiteral("/thumbs/") + key + QStringLiteral(".png");
}

QString peaksPath(const QString& cacheDir, const QString& key)
{
    return cacheDir + QStringLiteral("/peaks/") + key + QStringLiteral(".peaks");
}

bool buildThumbnail(const QString& path, const MediaInfo& info, const QString& outFile, const std::atomic_bool& cancel,
                    QString* error)
{
    VideoReader reader;
    if (!reader.open(path, error))
        return false;
    if (cancel.load())
        return false;

    // Fit inside 256x144 keeping the aspect ratio.
    const QSize src = reader.displaySize();
    QSize target = src.scaled(256, 144, Qt::KeepAspectRatio);
    target = QSize(std::max(2, target.width() & ~1), std::max(2, target.height() & ~1));
    const int64_t at = info.isStill() ? 0 : std::min<int64_t>(info.durationUs / 10, 2000000);
    const QImage frame = reader.frameAt(at, target, error);
    if (frame.isNull() || cancel.load())
        return false;

    QDir().mkpath(QFileInfo(outFile).absolutePath());
    // Written next to the target and renamed: readers never see a half-written picture. Virus scanners and indexers can lock a
    // new file for a moment on Windows, so a few short retries make this reliable under load.
    const QImage rgb = frame.convertToFormat(QImage::Format_RGB32);
    const QString temp = outFile + QStringLiteral(".") + QString::number(quintptr(QThread::currentThreadId())) + QStringLiteral(".tmp");
    for (int attempt = 0; attempt < 6; ++attempt) {
        if (cancel.load())
            return false;
        if (attempt > 0)
            QThread::msleep(40 * attempt);
        QDir().mkpath(QFileInfo(outFile).absolutePath());
        if (!rgb.save(temp, "PNG"))
            continue;
        QFile::remove(outFile);
        if (QFile::rename(temp, outFile))
            return true;
        if (QFileInfo::exists(outFile)) {      // another thread produced the same thumbnail first
            QFile::remove(temp);
            return true;
        }
    }
    QFile::remove(temp);
    if (error)
        *error = QStringLiteral("Cannot write thumbnail");
    return false;
}

bool buildPeaks(const QString& path, Peaks* out, const std::atomic_bool& cancel, QString* error)
{
    constexpr int kRate = 16000;
    constexpr int kBucket = kRate / kPeaksPerSecond;
    AudioReader reader(kRate);
    if (!reader.open(path, error))
        return false;

    Peaks peaks;
    std::vector<float> chunk(size_t(kBucket) * 200 * 2);
    int64_t pos = 0;
    for (;;) {
        if (cancel.load())
            return false;
        if (!reader.read(pos, kBucket * 200, chunk.data()))
            break;
        for (int b = 0; b < 200; ++b) {
            float peak = 0.f;
            for (int i = 0; i < kBucket * 2; ++i)
                peak = std::max(peak, std::abs(chunk[size_t(b) * kBucket * 2 + size_t(i)]));
            peaks.push_back(uint8_t(std::min(1.f, peak) * 255.f));
        }
        pos += kBucket * 200;
        if (pos > int64_t(kRate) * 60 * 60 * 12)   // 12 hours safety bound
            break;
        if (reader.atEnd(pos))
            break;
    }
    // Trim trailing zero buckets produced by the final partial chunk.
    while (!peaks.empty() && peaks.back() == 0)
        peaks.pop_back();
    *out = std::move(peaks);
    return true;
}

bool savePeaks(const QString& file, const Peaks& peaks)
{
    QByteArray data("DVPK", 4);
    const qint32 pps = kPeaksPerSecond;
    const qint32 count = qint32(peaks.size());
    data.append(reinterpret_cast<const char*>(&pps), 4);
    data.append(reinterpret_cast<const char*>(&count), 4);
    data.append(reinterpret_cast<const char*>(peaks.data()), qsizetype(peaks.size()));
    return fileutils::writeAtomic(file, data);
}

bool loadPeaks(const QString& file, Peaks* out)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = f.readAll();
    if (data.size() < 12 || data.left(4) != "DVPK")
        return false;
    qint32 pps = 0;
    qint32 count = 0;
    std::memcpy(&pps, data.constData() + 4, 4);
    std::memcpy(&count, data.constData() + 8, 4);
    if (pps != kPeaksPerSecond || count < 0 || data.size() != 12 + count)
        return false;
    out->assign(reinterpret_cast<const uint8_t*>(data.constData()) + 12, reinterpret_cast<const uint8_t*>(data.constData()) + 12 + count);
    return true;
}

qint64 trim(const QString& dir, qint64 limitBytes)
{
    struct Entry { QString path; qint64 size; QDateTime time; };
    QList<Entry> files;
    qint64 total = 0;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        files.append({it.filePath(), it.fileInfo().size(), it.fileInfo().lastModified()});
        total += it.fileInfo().size();
    }
    if (total <= limitBytes)
        return 0;
    std::sort(files.begin(), files.end(), [](const Entry& a, const Entry& b) { return a.time < b.time; });
    qint64 removed = 0;
    for (const Entry& e : files) {
        if (total - removed <= limitBytes)
            break;
        if (QFile::remove(e.path))
            removed += e.size;
    }
    return removed;
}

} // namespace dv::mediacache
