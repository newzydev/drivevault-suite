#pragma once

#include <QString>
#include <cstdint>

namespace dv {

// Everything the application needs to know about an imported media file.
struct MediaInfo {
    QString kind;            // "video" | "audio" | "image"
    int64_t durationUs = 0;  // 0 for stills
    bool hasVideo = false;
    bool hasAudio = false;
    int width = 0;           // display size (rotation applied)
    int height = 0;
    int64_t fpsNum = 0;      // average frame rate, 0 for audio / stills
    int64_t fpsDen = 1;
    bool variableFrameRate = false;
    int audioRate = 0;
    int audioChannels = 0;
    QString videoCodec;
    QString audioCodec;
    int64_t fileSize = 0;
    int64_t modifiedMs = 0;

    bool isStill() const { return kind == QLatin1String("image"); }
};

// A media item as stored in the project (.dvproj).
struct MediaRef {
    QString id;
    QString path;          // absolute path as last seen
    QString relativePath;  // relative to the project folder (when on the same drive)
    QString name;
    MediaInfo info;
};

} // namespace dv
