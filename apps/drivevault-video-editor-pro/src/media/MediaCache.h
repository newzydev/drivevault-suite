#pragma once

#include "core/MediaTypes.h"

#include <QImage>
#include <QString>

#include <atomic>
#include <memory>
#include <vector>

namespace dv::mediacache {

// Peak envelope of an audio track: one byte (0-255) per 1/kPeaksPerSecond second.
constexpr int kPeaksPerSecond = 100;
using Peaks = std::vector<uint8_t>;

// Stable key for a file's cache entries (path + size + modification time).
QString keyFor(const QString& path, const MediaInfo& info);

QString thumbnailPath(const QString& cacheDir, const QString& key);
QString peaksPath(const QString& cacheDir, const QString& key);

// Both return false when cancelled or on failure; `error` explains failures.
bool buildThumbnail(const QString& path, const MediaInfo& info, const QString& outFile, const std::atomic_bool& cancel,
                    QString* error = nullptr);
bool buildPeaks(const QString& path, Peaks* out, const std::atomic_bool& cancel, QString* error = nullptr);

bool savePeaks(const QString& file, const Peaks& peaks);
bool loadPeaks(const QString& file, Peaks* out);

// Deletes the oldest files below `dir` until the total size is <= limitBytes. Returns bytes removed.
qint64 trim(const QString& dir, qint64 limitBytes);

} // namespace dv::mediacache
