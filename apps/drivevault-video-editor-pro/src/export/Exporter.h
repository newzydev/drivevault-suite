#pragma once

#include "core/RenderSnapshot.h"

#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

namespace dv {

struct EncoderInfo {
    QString id;       // FFmpeg encoder name, e.g. "h264_nvenc"
    QString label;
    bool hardware = false;
};

struct ExportSettings {
    QString outputPath;
    QString container = QStringLiteral("mp4");   // mp4 | mov | mkv
    int width = 1920;
    int height = 1080;
    int quality = 75;                            // 0..100 -> video bitrate
    int videoBitrateKbps = 0;                    // 0 = derive from quality
    int audioBitrateKbps = 192;
    QString encoder = QStringLiteral("auto");    // "auto" or an EncoderInfo::id
    int64_t startFrame = 0;                      // export range on the timeline (end is exclusive, -1 = to the end)
    int64_t endFrame = -1;
    bool audioOnly = false;                      // write an AAC .m4a without a video stream
};

// Result of checking the exported file against the sequence it came from.
struct ExportValidation {
    bool ok = false;
    QStringList problems;
    int width = 0;
    int height = 0;
    double fps = 0;
    qint64 durationUs = 0;
    qint64 expectedDurationUs = 0;
    bool hasAudio = false;
    int audioRate = 0;
    QString videoCodec;
    QString audioCodec;
    qint64 fileSize = 0;
};

struct ExportResult {
    bool ok = false;
    bool cancelled = false;
    QString error;
    QString encoder;
    QString outputPath;
    qint64 frames = 0;
    double seconds = 0;       // wall clock
    double throughputFps = 0; // frames per second of render + encode
    ExportValidation validation;
};

class Exporter {
public:
    using Progress = std::function<void(qint64 framesDone, qint64 totalFrames)>;

    // Renders the whole timeline (video composite + audio mix) to `settings.outputPath`. Synchronous;
    // call from a worker thread. The file is written under a temporary name and only moved into place
    // when the export succeeded and validated; cancelling or failing leaves no partial output.
    static ExportResult run(SnapshotPtr snapshot, const ExportSettings& settings, const Progress& progress,
                            const std::atomic_bool& cancel);

    // H.264 encoders that actually open on this machine (probed once, cached). Hardware first.
    static QList<EncoderInfo> availableEncoders();

    // Preview proxy: re-encodes the picture of `src` (no audio) as a small H.264 file (at most `maxWidth` wide, short GOP so
    // that seeking is fast). Same frame rate and timing as the source. Returns "" or an error; cancel leaves no file.
    static QString buildProxy(const QString& src, const QString& dst, int maxWidth, const Progress& progress, const std::atomic_bool& cancel);

    // Renders one frame of the sequence to a PNG (atomic). Returns an error message or an empty string.
    static QString exportFrame(const RenderSnapshot& snapshot, int64_t frame, QSize size, const QString& path);

    // Checks a finished file against the snapshot.
    static ExportValidation validate(const QString& path, const RenderSnapshot& snapshot, const ExportSettings& settings);

    // True when the snapshot has at least one audible audio clip.
    static bool hasAudibleAudio(const RenderSnapshot& snapshot);
    // Missing media used on the timeline (names); empty when everything is available.
    static QStringList offlineMedia(const RenderSnapshot& snapshot);
    static int defaultBitrateKbps(int width, int height, double fps, int quality);

private:
    static ExportResult runAudioOnly(SnapshotPtr snapshot, const ExportSettings& settings, const Progress& progress,
                                     const std::atomic_bool& cancel);
};

} // namespace dv
