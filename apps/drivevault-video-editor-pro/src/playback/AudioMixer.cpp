#include "AudioMixer.h"

#include "core/Logger.h"

#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dv {

namespace {
inline float dbToLinear(double db) { return float(std::pow(10.0, db / 20.0)); }

enum class Shape { LowShelf, Peak, HighShelf };

inline int64_t sampleToFrameClamped(int64_t samplesIntoClip, const RenderSnapshot& snap, int rate)
{
    return std::max<int64_t>(0, timecode::sampleToFrame(samplesIntoClip, snap.fps, rate));
}

// RBJ audio EQ cookbook.

struct Coeffs { double b0, b1, b2, a1, a2; };

Coeffs rbj(Shape shape, double freq, double gainDb, double rate)
{
    const double A = std::pow(10.0, gainDb / 40.0);
    const double w0 = 2.0 * M_PI * std::min(freq, rate * 0.45) / rate;
    const double cs = std::cos(w0);
    const double sn = std::sin(w0);
    double b0, b1, b2, a0, a1, a2;
    if (shape == Shape::Peak) {
        const double alpha = sn / (2.0 * 0.8);
        b0 = 1 + alpha * A; b1 = -2 * cs; b2 = 1 - alpha * A;
        a0 = 1 + alpha / A; a1 = -2 * cs; a2 = 1 - alpha / A;
    } else {
        const double alpha = sn / 2.0 * std::sqrt(2.0);          // shelf slope S = 1
        const double s = 2 * std::sqrt(A) * alpha;
        if (shape == Shape::LowShelf) {
            b0 = A * ((A + 1) - (A - 1) * cs + s);
            b1 = 2 * A * ((A - 1) - (A + 1) * cs);
            b2 = A * ((A + 1) - (A - 1) * cs - s);
            a0 = (A + 1) + (A - 1) * cs + s;
            a1 = -2 * ((A - 1) + (A + 1) * cs);
            a2 = (A + 1) + (A - 1) * cs - s;
        } else {
            b0 = A * ((A + 1) + (A - 1) * cs + s);
            b1 = -2 * A * ((A - 1) + (A + 1) * cs);
            b2 = A * ((A + 1) + (A - 1) * cs - s);
            a0 = (A + 1) - (A - 1) * cs + s;
            a1 = 2 * ((A - 1) - (A + 1) * cs);
            a2 = (A + 1) - (A - 1) * cs - s;
        }
    }
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}
} // namespace

float AudioMixer::softLimit(float x)
{
    constexpr float knee = 0.9f;
    const float a = std::abs(x);
    if (a <= knee)
        return x;
    const float y = knee + (1.f - knee) * std::tanh((a - knee) / (1.f - knee));
    return x < 0 ? -y : y;
}

void AudioMixer::setSnapshot(SnapshotPtr snapshot)
{
    m_snapshot = std::move(snapshot);
    // Decoders and filter states of clips / media that left the timeline are released.
    QSet<QString> alive;
    QSet<QString> clips;
    if (m_snapshot)
        for (const SnapshotClip& c : m_snapshot->clips) {
            alive.insert(QString::number(c.trackIndex) + QLatin1Char('|') + c.mediaPath);
            clips.insert(c.clip.id);
        }
    for (auto it = m_readers.begin(); it != m_readers.end();)
        it = alive.contains(it.key()) ? ++it : m_readers.erase(it);
    for (auto it = m_dsp.begin(); it != m_dsp.end();)
        it = clips.contains(it.key()) ? ++it : m_dsp.erase(it);
}

AudioReader* AudioMixer::readerFor(const SnapshotClip& clip)
{
    const QString key = QString::number(clip.trackIndex) + QLatin1Char('|') + clip.mediaPath;
    if (m_failed.contains(key))
        return nullptr;
    auto it = m_readers.find(key);
    if (it == m_readers.end()) {
        auto reader = std::make_shared<AudioReader>(m_snapshot->sampleRate);
        QString error;
        if (!reader->open(clip.mediaPath, &error)) {
            qCWarning(lcProject) << "Cannot decode audio of" << clip.mediaPath << ":" << error;
            m_failed.insert(key);
            return nullptr;
        }
        it = m_readers.insert(key, std::move(reader));
    }
    return it->get();
}

void AudioMixer::applyEq(Dsp& dsp, const ClipData& clip, int64_t cf, float* buf, int frames, int rate)
{
    const double gains[3] = {clip.param("eqLow", cf), clip.param("eqMid", cf), clip.param("eqHigh", cf)};
    static const Shape shapes[3] = {Shape::LowShelf, Shape::Peak, Shape::HighShelf};
    static const double freqs[3] = {120.0, 1000.0, 8000.0};
    for (int b = 0; b < 3; ++b) {
        Biquad& q = dsp.eq[size_t(b)];
        if (std::abs(gains[b]) < 0.05) {
            q.identity = true;
            continue;
        }
        if (dsp.eqGain[size_t(b)] != gains[b]) {          // coefficients only change when the gain does
            const Coeffs c = rbj(shapes[b], freqs[b], gains[b], rate);
            q.b0 = c.b0; q.b1 = c.b1; q.b2 = c.b2; q.a1 = c.a1; q.a2 = c.a2;
            dsp.eqGain[size_t(b)] = gains[b];
        }
        q.identity = false;
        for (int ch = 0; ch < 2; ++ch) {
            double x1 = q.x1[size_t(ch)], x2 = q.x2[size_t(ch)], y1 = q.y1[size_t(ch)], y2 = q.y2[size_t(ch)];
            for (int i = 0; i < frames; ++i) {
                const double x = buf[size_t(i) * 2 + size_t(ch)];
                const double y = q.b0 * x + q.b1 * x1 + q.b2 * x2 - q.a1 * y1 - q.a2 * y2;
                x2 = x1; x1 = x; y2 = y1; y1 = y;
                buf[size_t(i) * 2 + size_t(ch)] = float(y);
            }
            q.x1[size_t(ch)] = x1; q.x2[size_t(ch)] = x2; q.y1[size_t(ch)] = y1; q.y2[size_t(ch)] = y2;
        }
    }
}

void AudioMixer::render(int64_t startSample, int frames, float* out, Peaks* peaks)
{
    std::memset(out, 0, size_t(frames) * 2 * sizeof(float));
    if (peaks)
        *peaks = {};
    if (!m_snapshot || m_snapshot->clips.isEmpty() || frames <= 0)
        return;
    const RenderSnapshot& snap = *m_snapshot;
    const int64_t endSample = startSample + frames;
    const int rate = snap.sampleRate;
    m_temp.resize(size_t(frames) * 2);

    const bool anySolo = std::any_of(snap.tracks.begin(), snap.tracks.end(),
                                     [](const TrackInfo& t) { return t.kind == QLatin1String("audio") && t.solo; });

    for (const SnapshotClip& sc : snap.clips) {
        if (sc.trackIndex < 0 || sc.trackIndex >= snap.tracks.size())
            continue;
        const TrackInfo& track = snap.tracks.at(sc.trackIndex);
        if (track.kind != QLatin1String("audio") || track.muted || (anySolo && !track.solo) || !sc.online || !sc.media.hasAudio)
            continue;

        const int64_t clipStart = timecode::frameToSample(sc.clip.startFrame, snap.fps, rate);
        const int64_t clipEnd = timecode::frameToSample(sc.clip.endFrame(), snap.fps, rate);
        const int64_t from = std::max(startSample, clipStart);
        const int64_t to = std::min(endSample, clipEnd);
        if (to <= from)
            continue;

        AudioReader* reader = readerFor(sc);
        if (!reader)
            continue;
        const int64_t sourceIn = timecode::frameToSample(sc.clip.sourceInFrame, snap.fps, rate);
        const int count = int(to - from);
        reader->read(sourceIn + (from - clipStart), count, m_temp.data());

        // ---- per clip processing -------------------------------------------------------------------------
        Dsp& dsp = m_dsp[sc.clip.id];
        if (dsp.expectedStart != from) {                        // seek / first block: restart the filters
            dsp = Dsp();
        }
        dsp.expectedStart = to;

        const int64_t cf0 = sampleToFrameClamped(from - clipStart, snap, rate);
        const int64_t cf1 = sampleToFrameClamped(to - clipStart, snap, rate);
        applyEq(dsp, sc.clip, cf0, m_temp.data(), count, rate);

        const double gateDb = sc.clip.param("gate", cf0);
        if (gateDb > -79.5) {
            const float thr = dbToLinear(gateDb);
            const float release = std::exp(-1.f / (0.08f * float(rate)));
            const float attack = std::exp(-1.f / (0.004f * float(rate)));
            for (int i = 0; i < count; ++i) {
                const float level = std::max(std::abs(m_temp[size_t(i) * 2]), std::abs(m_temp[size_t(i) * 2 + 1]));
                dsp.gateEnv = std::max(level, dsp.gateEnv * release);
                const float target = dsp.gateEnv >= thr ? 1.f : 0.f;
                const float coef = target > dsp.gateGain ? attack : release;
                dsp.gateGain = target + (dsp.gateGain - target) * coef;
                m_temp[size_t(i) * 2] *= dsp.gateGain;
                m_temp[size_t(i) * 2 + 1] *= dsp.gateGain;
            }
        }

        const float g0 = dbToLinear(sc.clip.param("volume", cf0));
        const float g1 = dbToLinear(sc.clip.param("volume", cf1));
        const int64_t fadeIn = timecode::frameToSample(sc.clip.fadeInFrames, snap.fps, rate);
        const int64_t fadeOut = timecode::frameToSample(sc.clip.fadeOutFrames, snap.fps, rate);
        const float trackGain = dbToLinear(track.gainDb);
        const double theta = (track.pan + 1.0) * M_PI / 4.0;     // constant power pan, unity at the centre
        const float panL = float(std::cos(theta) * std::sqrt(2.0));
        const float panR = float(std::sin(theta) * std::sqrt(2.0));

        float* dst = out + size_t(from - startSample) * 2;
        for (int i = 0; i < count; ++i) {
            const int64_t pos = (from - clipStart) + i;
            float g = g0 + (g1 - g0) * float(i) / float(std::max(1, count));
            if (fadeIn > 0 && pos < fadeIn)
                g *= float(pos) / float(fadeIn);
            const int64_t remaining = (clipEnd - clipStart) - pos;
            if (fadeOut > 0 && remaining < fadeOut)
                g *= float(remaining) / float(fadeOut);
            g *= trackGain;
            dst[size_t(i) * 2] += m_temp[size_t(i) * 2] * g * panL;
            dst[size_t(i) * 2 + 1] += m_temp[size_t(i) * 2 + 1] * g * panR;
        }
    }

    float pl = 0, pr = 0;
    const float masterGain = dbToLinear(snap.masterGainDb);
    for (int i = 0; i < frames; ++i) {
        out[size_t(i) * 2] = softLimit(out[size_t(i) * 2] * masterGain);
        out[size_t(i) * 2 + 1] = softLimit(out[size_t(i) * 2 + 1] * masterGain);
        pl = std::max(pl, std::abs(out[size_t(i) * 2]));
        pr = std::max(pr, std::abs(out[size_t(i) * 2 + 1]));
    }
    if (peaks)
        *peaks = {pl, pr};
}

} // namespace dv
