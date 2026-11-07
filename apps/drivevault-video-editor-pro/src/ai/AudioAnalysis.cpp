#include "AudioAnalysis.h"

#include "playback/AudioMixer.h"
#include "playback/Compositor.h"

#include <QImage>

#include <algorithm>
#include <cmath>

namespace dv::ai {

Activity measureActivity(SnapshotPtr snapshot, const ProgressFn& progress, const std::atomic_bool& cancel)
{
    Activity a;
    if (!snapshot || snapshot->durationFrames <= 0)
        return a;
    const int rate = snapshot->sampleRate;
    const int window = std::max(1, rate / 100);
    const int64_t total = timecode::frameToSample(snapshot->durationFrames, snapshot->fps, rate);
    const int64_t windows = total / window;
    a.rmsDb.reserve(size_t(windows));

    AudioMixer mixer;
    mixer.setSnapshot(snapshot);
    constexpr int kBlockWindows = 50;   // 0.5 s per mixer call
    std::vector<float> buf(size_t(window) * kBlockWindows * 2);
    for (int64_t w = 0; w < windows; w += kBlockWindows) {
        if (cancel.load())
            return {};
        const int n = int(std::min<int64_t>(kBlockWindows, windows - w));
        mixer.render(w * window, n * window, buf.data());
        for (int i = 0; i < n; ++i) {
            double sum = 0;
            const float* p = buf.data() + size_t(i) * window * 2;
            for (int s = 0; s < window * 2; ++s)
                sum += double(p[s]) * double(p[s]);
            const double rms = std::sqrt(sum / (window * 2));
            a.rmsDb.push_back(float(20.0 * std::log10(rms + 1e-9)));
        }
        if (progress)
            progress(double(w + n) / double(windows));
    }
    return a;
}

QList<Run> findSilentRuns(const std::vector<float>& rmsDb, double windowSec, const SilenceParams& p)
{
    QList<Run> runs;
    const size_t n = rmsDb.size();
    size_t i = 0;
    while (i < n) {
        if (rmsDb[i] >= p.thresholdDb) {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < n && rmsDb[j] < p.thresholdDb)
            ++j;
        const double t0 = double(i) * windowSec;
        const double t1 = double(j) * windowSec;
        if (t1 - t0 >= p.minSilenceSec) {
            // keep the padding next to the sound; the sequence edges need none
            const double a = i == 0 ? t0 : t0 + p.paddingSec;
            const double b = j == n ? t1 : t1 - p.paddingSec;
            if (b - a > 0.02)
                runs.append({a, b});
        }
        i = j;
    }
    return runs;
}

QList<FrameRange> detectSilence(SnapshotPtr snapshot, const SilenceParams& params, const ProgressFn& progress,
                                const std::atomic_bool& cancel)
{
    QList<FrameRange> out;
    const Activity act = measureActivity(snapshot, progress, cancel);
    if (act.rmsDb.empty() || cancel.load())
        return out;
    const double fps = snapshot->fps.toDouble();
    for (const Run& r : findSilentRuns(act.rmsDb, act.windowSec, params)) {
        const int64_t a = int64_t(std::ceil(r.start * fps));
        const int64_t b = std::min<int64_t>(int64_t(std::floor(r.end * fps)), snapshot->durationFrames);
        if (b > a)
            out.append({a, b});
    }
    return out;
}

namespace {
std::vector<uint8_t> lumaOf(const QImage& image)
{
    std::vector<uint8_t> g(size_t(image.width()) * image.height());
    for (int y = 0; y < image.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            g[size_t(y) * image.width() + x] = uint8_t((qRed(line[x]) * 77 + qGreen(line[x]) * 150 + qBlue(line[x]) * 29) >> 8);
    }
    return g;
}
} // namespace

QList<int64_t> detectSceneChanges(SnapshotPtr snapshot, double sensitivity, const ProgressFn& progress,
                                  const std::atomic_bool& cancel)
{
    QList<int64_t> out;
    if (!snapshot || snapshot->durationFrames < 2)
        return out;
    // Existing cuts: starts and ends of clips on the picture tracks.
    QList<int64_t> edges;
    for (const SnapshotClip& c : snapshot->clips)
        if (c.trackIndex >= 0 && c.trackIndex < snapshot->tracks.size() && snapshot->tracks.at(c.trackIndex).kind != QLatin1String("audio")) {
            edges.append(c.clip.startFrame);
            edges.append(c.clip.startFrame + c.clip.durationFrames);
        }
    auto atEdge = [&](int64_t f) {
        return std::any_of(edges.begin(), edges.end(), [&](int64_t e) { return std::llabs(e - f) <= 1; });
    };

    // sensitivity 0..1: higher finds more changes. Mean luma difference threshold in 0..255.
    const double threshold = 36.0 - 28.0 * std::clamp(sensitivity, 0.0, 1.0);
    const QSize size(64, 36);
    Compositor compositor;
    std::vector<uint8_t> prev;
    const int64_t total = snapshot->durationFrames;
    for (int64_t f = 0; f < total; ++f) {
        if (cancel.load())
            return {};
        const QImage image = compositor.render(*snapshot, f, size).convertToFormat(QImage::Format_ARGB32);
        std::vector<uint8_t> cur = lumaOf(image);
        if (!prev.empty() && prev.size() == cur.size()) {
            double diff = 0;
            for (size_t i = 0; i < cur.size(); ++i)
                diff += std::abs(int(cur[i]) - int(prev[i]));
            diff /= double(cur.size());
            if (diff >= threshold && !atEdge(f))
                out.append(f);
        }
        prev = std::move(cur);
        if (progress && f % 8 == 0)
            progress(double(f) / double(total));
    }
    return out;
}

} // namespace dv::ai
