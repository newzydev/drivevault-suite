#pragma once

#include "core/TimelineTypes.h"
#include "core/MediaTypes.h"
#include "core/Timecode.h"

#include <QDateTime>
#include <QJsonArray>
#include <QList>
#include <QString>

namespace dv {

// In-memory representation of a .dvproj document (schema version 2).
struct ProjectData {
    QString name;
    QDateTime createdAt;
    QDateTime modifiedAt;
    int width = 1920;
    int height = 1080;
    timecode::FrameRate frameRate{30, 1};
    int sampleRate = 48000;
    QList<TrackInfo> tracks;
    QList<MediaRef> media;
    QList<ClipData> clips;
    QList<MarkerData> markers;
    int64_t inPoint = -1;     // export range marks (frames), -1 = not set
    int64_t outPoint = -1;
};

} // namespace dv
