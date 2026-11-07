#pragma once

#include <QString>
#include <cstdint>

namespace dv::timecode {

// Frame-accurate rational frame rate (e.g. 30000/1001 for 29.97).
struct FrameRate {
    int64_t num = 30;
    int64_t den = 1;

    bool isValid() const { return num > 0 && den > 0; }
    double toDouble() const { return isValid() ? double(num) / double(den) : 0.0; }
    bool operator==(const FrameRate&) const = default;
};

// Nominal frames per second used for the timecode counter (ceil(num/den)).
int nominalFps(const FrameRate& rate);

// Formats a frame index as HH:MM:SS:FF (non-drop-frame) using integer arithmetic only.
QString format(int64_t frame, const FrameRate& rate);

// Converts a frame index to microseconds (rounded) without floating point drift.
int64_t frameToMicroseconds(int64_t frame, const FrameRate& rate);

// Sample index of a frame boundary at `sampleRate` (rounded to nearest) and the frame that contains a sample (floor).
int64_t frameToSample(int64_t frame, const FrameRate& rate, int sampleRate);
int64_t sampleToFrame(int64_t sample, const FrameRate& rate, int sampleRate);

// Parses text such as "30", "29.97", "23.976", "30000/1001" into a rational frame rate.
// Returns {0, 0} when the text is not a valid frame rate.
FrameRate parse(const QString& text);

// Human friendly label such as "29.97" or "30".
QString label(const FrameRate& rate);

} // namespace dv::timecode
