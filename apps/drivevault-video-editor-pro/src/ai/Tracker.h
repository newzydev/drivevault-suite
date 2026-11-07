#pragma once

#include "ai/AiTypes.h"

#include <QList>
#include <QPointF>
#include <QRectF>

namespace dv::ai {

struct TrackResult {
    QList<QPointF> centers;      // centre of the region per step, 0..1 of the source picture; [0] is the start region
    QList<float> confidence;     // 1 = identical to the template, 0 = nothing alike
    QString error;
};

// Follows a region through a video by template matching (sum of absolute differences on a reduced grey picture,
// searched around the last position). `region` is in 0..1 of the picture at `startUs`; one position per
// frame of `fps` for `frames` frames. Classic computer vision: no model is needed.
TrackResult trackRegion(const QString& mediaPath, int64_t startUs, double fps, int frames, const QRectF& region,
                        const ProgressFn& progress, const std::atomic_bool& cancel);

} // namespace dv::ai
