#pragma once

#include "core/MediaTypes.h"
#include "core/Timecode.h"
#include "core/TimelineTypes.h"

#include <QList>
#include <memory>

namespace dv {

// Immutable copy of everything the preview and the exporter need to render the timeline. It is
// built on the UI thread and handed to worker threads, so they never touch live models.
struct SnapshotClip {
    ClipData clip;
    int trackIndex = 0;       // index into RenderSnapshot::tracks (0 = top track)
    QString mediaPath;
    QString proxyPath;        // low-resolution stand-in for the preview (empty = none); the exporter never uses it
    MediaInfo media;
    bool online = true;       // false when the media file is missing
};

struct RenderSnapshot {
    int width = 1920;
    int height = 1080;
    timecode::FrameRate fps{30, 1};
    int sampleRate = 48000;
    QList<TrackInfo> tracks;      // top to bottom, as shown in the timeline
    QList<SnapshotClip> clips;
    int64_t durationFrames = 0;   // end of the last clip
    uint64_t revision = 0;        // unique per built snapshot: lets the preview cache know when its frames are stale

    bool isEmpty() const { return clips.isEmpty(); }
};
using SnapshotPtr = std::shared_ptr<const RenderSnapshot>;

} // namespace dv
