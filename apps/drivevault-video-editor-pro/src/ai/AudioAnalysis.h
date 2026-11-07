#pragma once

#include "ai/AiTypes.h"
#include "core/RenderSnapshot.h"

#include <QList>

#include <vector>

namespace dv::ai {

struct SilenceParams {
    double thresholdDb = -35.0;     // windows quieter than this are silent
    double minSilenceSec = 0.5;     // shorter pauses are kept
    double paddingSec = 0.1;        // room kept on both sides of a pause
};

struct Activity {
    std::vector<float> rmsDb;       // level of every window of the mix
    double windowSec = 0.01;
};

// Level of the whole timeline mix (the same AudioMixer as preview and export), one value per 10 ms.
Activity measureActivity(SnapshotPtr snapshot, const ProgressFn& progress, const std::atomic_bool& cancel);

// Pure: silent runs (seconds) of a level curve. A run needs `minSilenceSec`; `paddingSec` is trimmed from each side.
struct Run {
    double start = 0;
    double end = 0;
};
QList<Run> findSilentRuns(const std::vector<float>& rmsDb, double windowSec, const SilenceParams& params);

// Silent passages of the timeline as frame ranges (end exclusive, inside the sequence).
QList<FrameRange> detectSilence(SnapshotPtr snapshot, const SilenceParams& params, const ProgressFn& progress,
                                const std::atomic_bool& cancel);

// Scene changes: frames of the composited timeline whose picture differs strongly from the previous one.
// Cuts that already exist at clip edges are not reported.
QList<int64_t> detectSceneChanges(SnapshotPtr snapshot, double sensitivity, const ProgressFn& progress,
                                  const std::atomic_bool& cancel);

} // namespace dv::ai
