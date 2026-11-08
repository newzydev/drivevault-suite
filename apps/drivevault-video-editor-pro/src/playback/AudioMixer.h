#pragma once

#include "core/RenderSnapshot.h"
#include "media/AudioReader.h"

#include <QHash>
#include <QSet>

#include <array>
#include <memory>

namespace dv {

// Mixes every audible audio clip of the timeline into interleaved float stereo. Per clip: 3-band EQ, noise gate,
// volume (keyframable), fade in / out. Per track: 3-band EQ, gain, pan, mute, solo. The master bus has a soft limiter.
// Shared by live playback and the exporter. One instance = one thread (it owns the decoders and filter states).
class AudioMixer {
public:
    struct Peaks {
        float left = 0;
        float right = 0;
    };

    void setSnapshot(SnapshotPtr snapshot);
    SnapshotPtr snapshot() const { return m_snapshot; }

    // Renders `frames` stereo frames starting at timeline sample `startSample` (project sample rate).
    // Muted tracks, offline media and gaps produce silence. Optionally reports the peak level of the block.
    void render(int64_t startSample, int frames, float* out, Peaks* peaks = nullptr);

    // Gain curve of the master bus: linear below the knee, smoothly saturating to +-1 above it.
    static float softLimit(float x);

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<double, 2> x1{}, x2{}, y1{}, y2{};   // per channel
        bool identity = true;
    };
    struct Dsp {                     // filter / gate state of one clip; reset when playback is not contiguous
        int64_t expectedStart = -1;
        std::array<Biquad, 3> eq;
        std::array<double, 3> eqGain{{1e9, 1e9, 1e9}};
        float gateEnv = 0;
        float gateGain = 0;           // a gate starts closed and opens (4 ms attack) once the signal passes the threshold
    };

    AudioReader* readerFor(const SnapshotClip& clip);
    void applyEq(Dsp& dsp, const ClipData& clip, int64_t clipFrame, float* buf, int frames, int rate);
    static void applyEqGains(Dsp& dsp, const double* gains, float* buf, int frames, int rate);   // gains: low, mid, high (dB)

    SnapshotPtr m_snapshot;
    QHash<QString, Dsp> m_trackDsp;                           // key: track id; only the EQ part is used
    std::vector<std::vector<float>> m_trackBufs;              // per track index; only tracks with an active EQ are filled
    QHash<QString, std::shared_ptr<AudioReader>> m_readers;   // key: track index + path
    QHash<QString, Dsp> m_dsp;                                // key: clip id
    QSet<QString> m_failed;
    std::vector<float> m_temp;
};

} // namespace dv
