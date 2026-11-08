#pragma once

#include "core/ClipParams.h"

#include <QList>
#include <QMap>
#include <QPointF>
#include <QString>
#include <cstdint>

namespace dv {

// One clip on a track. All positions are frame indices at the project frame rate.
// A clip is either a piece of media or a generated title / subtitle (type == "title").
struct ClipData {
    QString id;
    QString mediaId;           // empty for titles
    QString trackId;           // "V1", "A1", "S1" ...
    int64_t startFrame = 0;    // position on the timeline
    int64_t durationFrames = 0;
    int64_t sourceInFrame = 0; // offset into the media, in project frames
    QString linkId;            // clips sharing a linkId (video + its audio, or a user group) are edited together

    QString type = QStringLiteral("media");     // media | title
    QMap<QString, ParamTrack> params;           // only non-default parameters are stored
    TitleData title;
    QString lutPath;                            // .cube 3D LUT (colour grading)
    QMap<QString, QList<QPointF>> curves;       // "master" | "r" | "g" | "b" -> points (x, y in 0..1)
    TransitionSpec transIn;
    TransitionSpec transOut;
    int64_t fadeInFrames = 0;                   // audio fades
    int64_t fadeOutFrames = 0;

    int64_t endFrame() const { return startFrame + durationFrames; }
    bool isTitle() const { return type == QLatin1String("title"); }

    // Value of a parameter at a clip-relative frame (default when it was never set).
    double param(const QString& key, int64_t clipFrame) const
    {
        const auto it = params.constFind(key);
        return it == params.constEnd() ? params::defaultOf(key) : it->at(clipFrame);
    }

    bool operator==(const ClipData&) const = default;
};

struct TrackInfo {
    QString id;   // "V1", "A2", "S1" ...
    QString name;
    QString kind; // "video" | "audio" | "subtitle"
    bool muted = false;
    bool locked = false;
    bool visible = true;
    double gainDb = 0;   // audio tracks: channel fader
    double pan = 0;      // audio tracks: -1 (left) .. +1 (right)
    double eqLow = 0;    // audio tracks: 3-band EQ of the whole track (dB, -18 .. +18), applied after the clips are summed
    double eqMid = 0;
    double eqHigh = 0;
    bool solo = false;
    int lane = 1;        // height of the lane on the timeline: 0 compact, 1 normal, 2 tall

    bool operator==(const TrackInfo&) const = default;
};

} // namespace dv
