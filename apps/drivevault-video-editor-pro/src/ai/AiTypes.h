#pragma once

#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>

namespace dv::ai {

// Frames of the timeline, end exclusive.
struct FrameRange {
    int64_t start = 0;
    int64_t end = 0;
    bool operator==(const FrameRange&) const = default;
};

struct Segment {            // one recognised phrase
    double startSec = 0;
    double endSec = 0;
    QString text;
};

// Progress 0..1. Called from the worker thread.
using ProgressFn = std::function<void(double)>;

} // namespace dv::ai
