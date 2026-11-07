#include "AudioEnhancer.h"

#include "core/FileUtils.h"
#include "media/AudioReader.h"
#include "media/MediaProbe.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <complex>

namespace dv::ai {

namespace {

constexpr int kN = 1024;
constexpr int kHop = kN / 2;
constexpr int kBins = kN / 2 + 1;
constexpr double kPi = 3.14159265358979323846;

using Cx = std::complex<float>;

void fft(std::vector<Cx>& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2 * kPi / double(len) * (inverse ? 1 : -1);
        const Cx wl(float(std::cos(ang)), float(std::sin(ang)));
        for (size_t i = 0; i < n; i += len) {
            Cx w(1, 0);
            for (size_t k = 0; k < len / 2; ++k) {
                const Cx u = a[i + k];
                const Cx v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse)
        for (Cx& c : a)
            c /= float(n);
}

const std::vector<float>& window()
{
    static const std::vector<float> w = [] {
        std::vector<float> v(kN);
        for (int i = 0; i < kN; ++i)
            v[size_t(i)] = float(std::sin(kPi * i / kN));   // sqrt of the periodic Hann: analysis and synthesis window
        return v;
    }();
    return w;
}

// Spectrum of the frame that starts at `pos` (zero outside the signal).
void spectrum(const std::vector<float>& x, int64_t pos, std::vector<Cx>& out)
{
    out.assign(kN, Cx(0, 0));
    const auto& w = window();
    for (int i = 0; i < kN; ++i) {
        const int64_t k = pos + i;
        if (k >= 0 && k < int64_t(x.size()))
            out[size_t(i)] = Cx(x[size_t(k)] * w[size_t(i)], 0);
    }
    fft(out, false);
}

struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    float process(float in)
    {
        const double out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = in; y2 = y1; y1 = out;
        return float(out);
    }
};
Biquad highPassFilter(int rate, double hz)
{
    const double w0 = 2 * kPi * hz / rate, c = std::cos(w0), alpha = std::sin(w0) / (2 * 0.7071);
    const double a0 = 1 + alpha;
    Biquad b;
    b.b0 = (1 + c) / 2 / a0;
    b.b1 = -(1 + c) / a0;
    b.b2 = (1 + c) / 2 / a0;
    b.a1 = -2 * c / a0;
    b.a2 = (1 - alpha) / a0;
    return b;
}

bool writeWav(const QString& path, const std::vector<float>& l, const std::vector<float>& r, int rate, QString* error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("Cannot create %1").arg(path);
        return false;
    }
    const quint32 frames = quint32(l.size());
    const quint32 dataBytes = frames * 4;
    auto u32 = [&](quint32 v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](quint16 v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); u32(36 + dataBytes); f.write("WAVEfmt ", 8);
    u32(16); u16(1); u16(2); u32(quint32(rate)); u32(quint32(rate) * 4); u16(4); u16(16);
    f.write("data", 4); u32(dataBytes);
    QByteArray block;
    block.reserve(1 << 16);
    for (quint32 i = 0; i < frames; ++i) {
        for (float s : {l[i], r[i]}) {
            const qint16 v = qint16(std::lround(std::clamp(s, -1.f, 1.f) * 32767.f));
            block.append(reinterpret_cast<const char*>(&v), 2);
        }
        if (block.size() >= (1 << 16)) {
            f.write(block);
            block.clear();
        }
    }
    f.write(block);
    if (!f.commit()) {
        *error = QStringLiteral("Cannot write %1").arg(path);
        return false;
    }
    return true;
}

} // namespace

void reduceNoise(std::vector<float>& x, int /*sampleRate*/, double reduction)
{
    if (x.size() < size_t(kN) * 4)
        return;
    reduction = std::clamp(reduction, 0.0, 1.0);
    const int64_t total = int64_t(x.size());
    const int64_t first = -kHop;                       // frames overlap the edges, so every sample is covered twice
    const int64_t frames = (total + kHop) / kHop + 1;

    // pass 1: frame energies -> the quietest tenth is "noise"
    std::vector<Cx> spec;
    std::vector<double> energy(static_cast<size_t>(frames));
    for (int64_t f = 0; f < frames; ++f) {
        spectrum(x, first + f * kHop, spec);
        double e = 0;
        for (int b = 0; b < kBins; ++b)
            e += std::norm(spec[size_t(b)]);
        energy[size_t(f)] = e;
    }
    std::vector<double> sorted = energy;
    std::sort(sorted.begin(), sorted.end());
    const double limit = sorted[sorted.size() / 10];

    // pass 2: average noise magnitude per bin
    std::vector<double> noise(kBins, 0.0);
    int count = 0;
    for (int64_t f = 0; f < frames; ++f) {
        if (energy[size_t(f)] > limit)
            continue;
        spectrum(x, first + f * kHop, spec);
        for (int b = 0; b < kBins; ++b)
            noise[size_t(b)] += std::abs(spec[size_t(b)]);
        ++count;
    }
    if (count == 0)
        return;
    for (double& n : noise)
        n /= count;

    // pass 3: subtract with a smoothed gain, overlap-add
    const double alpha = 1.0 + 2.0 * reduction;
    const double floorGain = 0.03 + 0.35 * (1.0 - reduction);
    std::vector<float> out(x.size(), 0.f);
    std::vector<float> gainPrev(kBins, 1.f);
    const auto& w = window();
    for (int64_t f = 0; f < frames; ++f) {
        spectrum(x, first + f * kHop, spec);
        for (int b = 0; b < kBins; ++b) {
            const double mag = std::abs(spec[size_t(b)]) + 1e-12;
            double g = std::max(floorGain, 1.0 - alpha * noise[size_t(b)] / mag);
            g = std::max(g, double(gainPrev[size_t(b)]) * 0.7);   // slow release: less "musical noise"
            gainPrev[size_t(b)] = float(g);
            spec[size_t(b)] *= float(g);
            if (b > 0 && b < kN / 2)
                spec[size_t(kN - b)] = std::conj(spec[size_t(b)]);
        }
        fft(spec, true);
        for (int i = 0; i < kN; ++i) {
            const int64_t k = first + f * kHop + i;
            if (k >= 0 && k < total)
                out[size_t(k)] += spec[size_t(i)].real() * w[size_t(i)];
        }
    }
    x = std::move(out);
}

QString enhanceFile(const QString& inPath, const QString& outPath, int rate, const EnhanceParams& params,
                    const ProgressFn& progress, const std::atomic_bool& cancel)
{
    MediaInfo info;
    QString error;
    if (!MediaProbe::probe(inPath, &info, &error))
        return error;
    if (!info.hasAudio)
        return QStringLiteral("The media has no audio.");
    AudioReader reader(rate);
    if (!reader.open(inPath, &error))
        return error;
    const int64_t total = int64_t(std::llround(double(info.durationUs) * rate / 1e6));
    if (total < rate / 10)
        return QStringLiteral("The audio is too short.");
    std::vector<float> ch[2] = {std::vector<float>(size_t(total)), std::vector<float>(size_t(total))};
    constexpr int kBlock = 16384;
    std::vector<float> buf(size_t(kBlock) * 2);
    for (int64_t pos = 0; pos < total; pos += kBlock) {
        if (cancel.load())
            return QStringLiteral("cancelled");
        const int n = int(std::min<int64_t>(kBlock, total - pos));
        if (!reader.read(pos, n, buf.data()))
            return QStringLiteral("Cannot decode the audio.");
        for (int i = 0; i < n; ++i) {
            ch[0][size_t(pos + i)] = buf[size_t(i) * 2];
            ch[1][size_t(pos + i)] = buf[size_t(i) * 2 + 1];
        }
        if (progress)
            progress(0.25 * double(pos + n) / double(total));
    }

    for (int c = 0; c < 2; ++c) {
        if (cancel.load())
            return QStringLiteral("cancelled");
        if (params.highPass) {
            Biquad hp = highPassFilter(rate, 80.0);
            for (float& s : ch[c])
                s = hp.process(s);
        }
        if (params.reduction > 0.001)
            reduceNoise(ch[c], rate, params.reduction);
        if (progress)
            progress(0.25 + 0.6 * (c + 1) / 2.0);
    }

    if (params.normalize) {
        // level of the active part (windows above -50 dBFS), so pauses do not pull the gain up
        const int win = rate / 20;
        double sum = 0;
        int64_t n = 0;
        for (int64_t i = 0; i + win <= total; i += win) {
            double e = 0;
            for (int k = 0; k < win; ++k)
                e += double(ch[0][size_t(i + k)]) * ch[0][size_t(i + k)] + double(ch[1][size_t(i + k)]) * ch[1][size_t(i + k)];
            e /= win * 2;
            if (10.0 * std::log10(e + 1e-12) > -50.0) {
                sum += e * win;
                n += win;
            }
        }
        if (n > 0) {
            const double rmsDb = 10.0 * std::log10(sum / double(n) + 1e-12);
            const double gain = std::pow(10.0, std::clamp(params.targetRmsDb - rmsDb, -12.0, 18.0) / 20.0);
            for (auto& v : ch)
                for (float& s : v) {
                    float y = float(s * gain);
                    if (std::abs(y) > 0.9f)                         // gentle limiter above the knee
                        y = std::copysign(0.9f + 0.1f * std::tanh((std::abs(y) - 0.9f) / 0.1f), y);
                    s = y;
                }
        }
    }

    if (!QDir().mkpath(QFileInfo(outPath).absolutePath()))
        return QStringLiteral("Cannot create the output folder.");
    if (!writeWav(outPath, ch[0], ch[1], rate, &error))
        return error;
    if (progress)
        progress(1.0);
    return {};
}

} // namespace dv::ai
