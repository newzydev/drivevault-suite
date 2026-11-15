#include "OpenThaiAsr.h"

#include "core/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <complex>
#include <memory>
#include <numbers>
#include <string>
#include <thread>

#ifdef DV_ONNX_AVAILABLE
#include <onnxruntime_cxx_api.h>
#endif

namespace dv::ai {

namespace {

constexpr int kRate = 16000;
constexpr int kWinLen = 400;      // 25 ms
constexpr int kHop = 160;         // 10 ms
constexpr int kFft = 512;
constexpr int kBins = kFft / 2 + 1;
constexpr int kMels = 128;
constexpr float kPreemph = 0.97f;
constexpr double kLogGuard = 1.0 / 16777216.0;   // 2^-24, added before the log
constexpr double kFrameSeconds = 0.08;           // one encoder frame = 8 feature frames

// Slaney mel scale (the one of librosa / NeMo): linear below 1 kHz, logarithmic above.
double hzToMel(double hz)
{
    constexpr double fSp = 200.0 / 3.0, minLogHz = 1000.0, minLogMel = minLogHz / fSp, logStep = 0.06875177742094912;   // ln(6.4) / 27
    return hz < minLogHz ? hz / fSp : minLogMel + std::log(hz / minLogHz) / logStep;
}
double melToHz(double mel)
{
    constexpr double fSp = 200.0 / 3.0, minLogHz = 1000.0, minLogMel = minLogHz / fSp, logStep = 0.06875177742094912;
    return mel < minLogMel ? mel * fSp : minLogHz * std::exp(logStep * (mel - minLogMel));
}

const std::vector<float>& melFilters()    // [kMels][kBins], Slaney area normalised
{
    static const std::vector<float> filters = [] {
        std::vector<float> w(size_t(kMels) * kBins, 0.f);
        std::vector<double> points(size_t(kMels) + 2);
        const double lo = hzToMel(0), hi = hzToMel(kRate / 2.0);
        for (int i = 0; i < kMels + 2; ++i)
            points[size_t(i)] = melToHz(lo + (hi - lo) * i / (kMels + 1));
        for (int m = 0; m < kMels; ++m) {
            const double f0 = points[size_t(m)], f1 = points[size_t(m) + 1], f2 = points[size_t(m) + 2];
            const double enorm = 2.0 / (f2 - f0);
            for (int k = 0; k < kBins; ++k) {
                const double f = double(k) * kRate / kFft;
                const double lower = (f - f0) / (f1 - f0), upper = (f2 - f) / (f2 - f1);
                w[size_t(m) * kBins + size_t(k)] = float(std::max(0.0, std::min(lower, upper)) * enorm);
            }
        }
        return w;
    }();
    return filters;
}

const std::vector<float>& hannWindow()    // symmetric Hann of 400 samples, centred in the 512 point frame
{
    static const std::vector<float> w = [] {
        std::vector<float> v(size_t(kFft), 0.f);
        const int offset = (kFft - kWinLen) / 2;
        for (int i = 0; i < kWinLen; ++i)
            v[size_t(offset + i)] = float(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / (kWinLen - 1)));
        return v;
    }();
    return w;
}

void fft512(std::complex<float>* a)
{
    // iterative radix-2, in place
    for (int i = 1, j = 0; i < kFft; ++i) {
        int bit = kFft >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    static const std::vector<std::complex<float>> twiddle = [] {
        std::vector<std::complex<float>> t(size_t(kFft / 2));
        for (int i = 0; i < kFft / 2; ++i)
            t[size_t(i)] = std::polar(1.0f, float(-2.0 * std::numbers::pi * i / kFft));
        return t;
    }();
    for (int len = 2; len <= kFft; len <<= 1) {
        const int step = kFft / len;
        for (int i = 0; i < kFft; i += len) {
            for (int k = 0; k < len / 2; ++k) {
                const std::complex<float> u = a[i + k], v = a[i + k + len / 2] * twiddle[size_t(k * step)];
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
            }
        }
    }
}

bool fileHas(const QString& dir, const QString& name) { return QFileInfo::exists(QDir(dir).filePath(name)); }

} // namespace

// ---- features -------------------------------------------------------------------------------------------------------

std::vector<float> openThaiLogMel(const std::vector<float>& pcm, int* framesOut)
{
    const size_t n = pcm.size();
    const int frames = int(n / kHop) + 1;
    if (framesOut)
        *framesOut = frames;
    // pre-emphasis, then zero padding of n_fft / 2 on both sides (centred frames)
    std::vector<float> x(n + size_t(kFft), 0.f);
    const size_t pad = kFft / 2;
    for (size_t i = 0; i < n; ++i)
        x[pad + i] = i == 0 ? pcm[0] : pcm[i] - kPreemph * pcm[i - 1];

    const std::vector<float>& filters = melFilters();
    const std::vector<float>& window = hannWindow();
    std::vector<float> out(size_t(kMels) * size_t(frames));
    std::vector<std::complex<float>> buf(static_cast<size_t>(kFft));      // (not size_t(kFft): that would be a function declaration)
    std::vector<float> power(static_cast<size_t>(kBins));
    for (int f = 0; f < frames; ++f) {
        const size_t start = size_t(f) * kHop;
        for (int i = 0; i < kFft; ++i)
            buf[size_t(i)] = {(start + size_t(i) < x.size() ? x[start + size_t(i)] : 0.f) * window[size_t(i)], 0.f};
        fft512(buf.data());
        for (int k = 0; k < kBins; ++k)
            power[size_t(k)] = std::norm(buf[size_t(k)]);
        for (int m = 0; m < kMels; ++m) {
            const float* w = &filters[size_t(m) * kBins];
            double acc = 0;
            for (int k = 0; k < kBins; ++k)
                acc += double(w[k]) * power[size_t(k)];
            out[size_t(m) * size_t(frames) + size_t(f)] = float(std::log(acc + kLogGuard));
        }
    }
    return out;
}

// ---- cutting at pauses ------------------------------------------------------------------------------------------------

std::vector<std::pair<size_t, size_t>> openThaiSplit(const std::vector<float>& pcm, double maxSeconds)
{
    std::vector<std::pair<size_t, size_t>> pieces;
    const size_t win = kRate / 50;                    // 20 ms windows
    const size_t count = pcm.size() / win;
    if (count == 0)
        return pieces;
    std::vector<float> level(count);
    float peak = -200.f;
    for (size_t w = 0; w < count; ++w) {
        double s = 0;
        for (size_t i = 0; i < win; ++i)
            s += double(pcm[w * win + i]) * pcm[w * win + i];
        level[w] = float(10.0 * std::log10(s / double(win) + 1e-12));
        peak = std::max(peak, level[w]);
    }
    if (peak < -70.f)
        return pieces;                                // digital silence
    const float threshold = std::max(-55.f, peak - 40.f);
    const size_t minPause = 15;                       // 0.3 s
    const size_t minPiece = 6 * 50;                   // do not cut a piece shorter than 6 s at a pause
    const size_t maxPiece = size_t(maxSeconds * 50);

    size_t start = 0;                                 // window index where the current piece begins
    auto addPiece = [&](size_t from, size_t to) {
        // trim leading / trailing silence, keep a little context
        while (from < to && level[from] < threshold)
            ++from;
        while (to > from && level[to - 1] < threshold)
            --to;
        if (to - from < 10)                           // under 0.2 s of sound
            return;
        const size_t a = from > 5 ? from - 5 : 0, b = std::min(count, to + 5);
        pieces.emplace_back(a * win, b * win);
    };
    size_t w = 0;
    while (w < count) {
        if (w - start >= maxPiece) {                  // forced cut at the quietest window of the last 5 s
            size_t best = w, from = w > 250 ? w - 250 : start;
            for (size_t i = std::max(from, start + 1); i < w; ++i)
                if (level[i] < level[best] || best == w)
                    best = i;
            addPiece(start, best);
            start = best;
            w = best + 1;
            continue;
        }
        if (level[w] < threshold) {
            size_t run = w;
            while (run < count && level[run] < threshold)
                ++run;
            if (run - w >= minPause && w - start >= minPiece) {
                addPiece(start, (w + run) / 2);
                start = (w + run) / 2;
            }
            w = run;
            continue;
        }
        ++w;
    }
    addPiece(start, count);
    return pieces;
}

// ---- the model folder ---------------------------------------------------------------------------------------------------

bool openThaiModelComplete(const QString& dir)
{
    if (dir.isEmpty() || !fileHas(dir, QStringLiteral("tokens.txt")) || !fileHas(dir, QStringLiteral("decoder_joint.onnx")))
        return false;
    return (fileHas(dir, QStringLiteral("encoder.onnx")) && fileHas(dir, QStringLiteral("encoder.onnx.data")))
           || (fileHas(dir, QStringLiteral("encoder.int8.onnx")) && fileHas(dir, QStringLiteral("encoder.int8.onnx.data")));
}

QString openThaiModelDir()
{
    QStringList dirs;
    const QString env = qEnvironmentVariable("DV_OPENTHAI_DIR");
    if (!env.isEmpty())
        dirs << env;
    const QString app = QCoreApplication::applicationDirPath();
    dirs << app + QStringLiteral("/ai/openthai") << paths::dataDir() + QStringLiteral("/models/openthai-asr");
    QDir d(app);
    for (int i = 0; i < 6; ++i) {
        dirs << d.filePath(QStringLiteral(".tools/openthai-asr"));
        if (!d.cdUp())
            break;
    }
    for (const QString& dir : std::as_const(dirs)) {
        if (openThaiModelComplete(dir))
            return QDir::cleanPath(dir);
    }
    return {};
}

#ifndef DV_ONNX_AVAILABLE

bool openThaiCompiledIn() { return false; }

OpenThaiResult openThaiTranscribe(const std::vector<float>&, const QString&, const QString&, const ProgressFn&, const std::atomic_bool&)
{
    OpenThaiResult r;
    r.error = QStringLiteral("This build has no ONNX Runtime, so the OpenThai speech model cannot run.");
    return r;
}

#else

bool openThaiCompiledIn() { return true; }

namespace {

struct Engine {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "dv-openthai"};
    std::unique_ptr<Ort::Session> encoder, decoder;
    std::vector<std::string> encIn, encOut, decIn, decOut;
    std::vector<ONNXTensorElementDataType> encInType, decInType;
    std::vector<std::vector<int64_t>> decInShape;
    std::vector<std::string> tokens;
    int blank = 0;
};

void describe(Ort::Session& s, std::vector<std::string>& in, std::vector<ONNXTensorElementDataType>& inType, std::vector<std::vector<int64_t>>* inShape,
              std::vector<std::string>& out)
{
    Ort::AllocatorWithDefaultOptions alloc;
    for (size_t i = 0; i < s.GetInputCount(); ++i) {
        in.emplace_back(s.GetInputNameAllocated(i, alloc).get());
        const auto info = s.GetInputTypeInfo(i).GetTensorTypeAndShapeInfo();
        inType.push_back(info.GetElementType());
        if (inShape)
            inShape->push_back(info.GetShape());
    }
    for (size_t i = 0; i < s.GetOutputCount(); ++i)
        out.emplace_back(s.GetOutputNameAllocated(i, alloc).get());
}

bool contains(const std::string& s, const char* part) { return s.find(part) != std::string::npos; }

// A one-element integer tensor in the type the model asks for (int32 or int64).
struct IntArg {
    int32_t v32 = 0;
    int64_t v64 = 0;
    ONNXTensorElementDataType type = ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64;
    Ort::Value make(const Ort::MemoryInfo& mem, const std::vector<int64_t>& shape)
    {
        const size_t count = [&] { size_t c = 1; for (int64_t d : shape) c *= size_t(d); return c; }();
        if (type == ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32)
            return Ort::Value::CreateTensor<int32_t>(mem, &v32, count, shape.data(), shape.size());
        return Ort::Value::CreateTensor<int64_t>(mem, &v64, count, shape.data(), shape.size());
    }
};
IntArg intArg(int64_t value, ONNXTensorElementDataType type)
{
    IntArg a;
    a.v32 = int32_t(value);
    a.v64 = value;
    a.type = type;
    return a;
}

bool loadEngine(Engine& e, const QString& dir, QString* error)
{
    try {
        // tokens: id = line number - 1; the blank is the id after the last piece
        QFile tf(QDir(dir).filePath(QStringLiteral("tokens.txt")));
        if (!tf.open(QIODevice::ReadOnly)) {
            *error = QStringLiteral("tokens.txt cannot be read.");
            return false;
        }
        const QList<QByteArray> lines = tf.readAll().split('\n');
        for (QByteArray line : lines) {
            if (line.endsWith('\r'))
                line.chop(1);
            e.tokens.emplace_back(line.constData(), size_t(line.size()));
        }
        while (!e.tokens.empty() && e.tokens.back().empty())
            e.tokens.pop_back();
        e.blank = int(e.tokens.size());

        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(std::clamp(int(std::thread::hardware_concurrency()) / 2, 2, 8));
        options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        const bool fp32 = fileHas(dir, QStringLiteral("encoder.onnx")) && fileHas(dir, QStringLiteral("encoder.onnx.data"));
        const QString encoderFile = QDir(dir).filePath(fp32 ? QStringLiteral("encoder.onnx") : QStringLiteral("encoder.int8.onnx"));
        const QString decoderFile = QDir(dir).filePath(QStringLiteral("decoder_joint.onnx"));
        e.encoder = std::make_unique<Ort::Session>(e.env, QDir::toNativeSeparators(encoderFile).toStdWString().c_str(), options);
        e.decoder = std::make_unique<Ort::Session>(e.env, QDir::toNativeSeparators(decoderFile).toStdWString().c_str(), options);
        describe(*e.encoder, e.encIn, e.encInType, nullptr, e.encOut);
        describe(*e.decoder, e.decIn, e.decInType, &e.decInShape, e.decOut);
        return true;
    } catch (const Ort::Exception& ex) {
        *error = QStringLiteral("The speech model could not be loaded: %1").arg(QString::fromUtf8(ex.what()));
        return false;
    }
}

struct Emitted {
    int token;
    double timeSec;
};

// One piece of audio -> tokens with times (relative to the piece start).
bool decodePiece(Engine& e, const std::vector<float>& pcm, int promptIndex, float blankBias, std::vector<Emitted>* out, QString* error)
{
    try {
        int frames = 0;
        std::vector<float> mel = openThaiLogMel(pcm, &frames);
        Ort::MemoryInfo mem = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        // ---- encoder -------------------------------------------------------------------------------------------
        std::vector<int64_t> audioShape{1, kMels, frames}, one{1};
        std::vector<IntArg> ints;
        ints.reserve(4);
        std::vector<Ort::Value> inputs;
        std::vector<const char*> inNames;
        for (size_t i = 0; i < e.encIn.size(); ++i) {
            const std::string& name = e.encIn[i];
            inNames.push_back(name.c_str());
            if (contains(name, "audio") || contains(name, "signal"))
                inputs.push_back(Ort::Value::CreateTensor<float>(mem, mel.data(), mel.size(), audioShape.data(), audioShape.size()));
            else if (contains(name, "prompt")) {
                ints.push_back(intArg(promptIndex, e.encInType[i]));
                inputs.push_back(ints.back().make(mem, one));
            } else {                                                   // "length"
                ints.push_back(intArg(frames, e.encInType[i]));
                inputs.push_back(ints.back().make(mem, one));
            }
        }
        std::vector<const char*> outNames{e.encOut.front().c_str()};
        std::vector<Ort::Value> encoded = e.encoder->Run(Ort::RunOptions{nullptr}, inNames.data(), inputs.data(), inputs.size(), outNames.data(), 1);
        const std::vector<int64_t> shape = encoded[0].GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() != 3) {
            *error = QStringLiteral("Unexpected encoder output.");
            return false;
        }
        const float* enc = encoded[0].GetTensorData<float>();
        const bool frameMajor = shape[2] == 1024;                      // [1, T, 1024]; otherwise [1, 1024, T]
        const int64_t steps = frameMajor ? shape[1] : shape[2];
        const int64_t width = 1024;

        // ---- decoder (greedy RNN-T) ------------------------------------------------------------------------------
        std::vector<size_t> stateIn, stateOut;
        for (size_t i = 0; i < e.decIn.size(); ++i)
            if (contains(e.decIn[i], "state"))
                stateIn.push_back(i);
        for (size_t i = 0; i < e.decOut.size(); ++i)
            if (contains(e.decOut[i], "state"))
                stateOut.push_back(i);
        if (stateIn.size() != stateOut.size() || stateIn.empty()) {
            *error = QStringLiteral("Unexpected decoder layout.");
            return false;
        }
        std::vector<std::vector<float>> states(stateIn.size());
        std::vector<std::vector<int64_t>> stateShapes(stateIn.size());
        for (size_t s = 0; s < stateIn.size(); ++s) {
            stateShapes[s] = e.decInShape[stateIn[s]];
            size_t count = 1;
            for (int64_t& d : stateShapes[s]) {
                d = std::max<int64_t>(1, d);
                count *= size_t(d);
            }
            states[s].assign(count, 0.f);
        }
        std::vector<const char*> dIn, dOut;
        for (const std::string& n : e.decIn)
            dIn.push_back(n.c_str());
        for (const std::string& n : e.decOut)
            dOut.push_back(n.c_str());

        std::vector<float> column(static_cast<size_t>(width));
        std::vector<int64_t> encShape{1, width, 1}, targetShape{1, 1};
        int last = e.blank;
        for (int64_t t = 0; t < steps; ++t) {
            for (int64_t c = 0; c < width; ++c)
                column[size_t(c)] = frameMajor ? enc[t * width + c] : enc[c * steps + t];
            for (int symbols = 0; symbols < 10; ++symbols) {
                std::vector<IntArg> dints;
                dints.reserve(4);
                std::vector<Ort::Value> din;
                for (size_t i = 0; i < e.decIn.size(); ++i) {
                    const std::string& name = e.decIn[i];
                    const auto st = std::find(stateIn.begin(), stateIn.end(), i);
                    if (st != stateIn.end()) {
                        const size_t s = size_t(st - stateIn.begin());
                        din.push_back(Ort::Value::CreateTensor<float>(mem, states[s].data(), states[s].size(), stateShapes[s].data(), stateShapes[s].size()));
                    } else if (contains(name, "encoder")) {
                        din.push_back(Ort::Value::CreateTensor<float>(mem, column.data(), column.size(), encShape.data(), encShape.size()));
                    } else if (contains(name, "length")) {
                        dints.push_back(intArg(1, e.decInType[i]));
                        din.push_back(dints.back().make(mem, one));
                    } else {                                           // "targets"
                        dints.push_back(intArg(last, e.decInType[i]));
                        din.push_back(dints.back().make(mem, targetShape));
                    }
                }
                std::vector<Ort::Value> result = e.decoder->Run(Ort::RunOptions{nullptr}, dIn.data(), din.data(), din.size(), dOut.data(), dOut.size());
                const size_t vocab = result[0].GetTensorTypeAndShapeInfo().GetElementCount();
                float* logits = result[0].GetTensorMutableData<float>();
                if (vocab != size_t(e.blank) + 1) {
                    *error = QStringLiteral("The speech model and tokens.txt do not match.");
                    return false;
                }
                logits[e.blank] += blankBias;
                const int k = int(std::max_element(logits, logits + vocab) - logits);
                if (k == e.blank)
                    break;
                out->push_back({k, double(t) * kFrameSeconds});
                last = k;
                for (size_t s = 0; s < stateIn.size(); ++s) {
                    const float* next = result[stateOut[s]].GetTensorData<float>();
                    std::copy(next, next + states[s].size(), states[s].begin());
                }
            }
        }
        return true;
    } catch (const Ort::Exception& ex) {
        *error = QStringLiteral("The speech model failed: %1").arg(QString::fromUtf8(ex.what()));
        return false;
    }
}

QString pieceText(const std::string& piece)
{
    QString s = QString::fromUtf8(piece.data(), int(piece.size()));
    s.replace(QChar(0x2581), QLatin1Char(' '));
    return s;
}

} // namespace

OpenThaiResult openThaiTranscribe(const std::vector<float>& pcm, const QString& modelDir, const QString& language, const ProgressFn& progress,
                                  const std::atomic_bool& cancel)
{
    OpenThaiResult result;
    if (!openThaiModelComplete(modelDir)) {
        result.error = QStringLiteral("The OpenThai speech model was not found.");
        return result;
    }
    const auto pieces = openThaiSplit(pcm);
    if (pieces.empty())
        return result;                                                  // nothing audible: no captions, no error

    Engine engine;
    if (!loadEngine(engine, modelDir, &result.error))
        return result;
    if (progress)
        progress(0.05);

    const int prompt = language == QLatin1String("mix") ? 63 : 32;      // 32 = Thai, 63 = Thai mixed with English
    struct Word { QString text; double time; };
    size_t doneSamples = 0, totalSamples = 0;
    for (const auto& p : pieces)
        totalSamples += p.second - p.first;

    for (const auto& piece : pieces) {
        if (cancel.load()) {
            result.cancelled = true;
            return result;
        }
        const std::vector<float> chunk(pcm.begin() + qptrdiff(piece.first), pcm.begin() + qptrdiff(piece.second));
        const double chunkSeconds = double(chunk.size()) / kRate;
        const double startSec = double(piece.first) / kRate;
        std::vector<Emitted> tokens;
        if (!decodePiece(engine, chunk, prompt, chunkSeconds < 2.0 ? -2.5f : -1.0f, &tokens, &result.error))
            return result;

        // tokens -> phrases: a new phrase after a pause, or at a word start once the line is long enough
        QString text;
        double first = 0, lastTime = 0;
        bool open = false;
        auto flush = [&] {
            // the model writes sara am as nikhahit + sara aa; use the single standard character U+0E33
            const QString t = text.replace(QStringLiteral("ํา"), QStringLiteral("ำ")).simplified();
            if (open && !t.isEmpty())
                result.segments.append({startSec + first, startSec + lastTime + 0.16, t});
            text.clear();
            open = false;
        };
        for (const Emitted& tok : tokens) {
            const QString word = pieceText(engine.tokens[size_t(tok.token)]);
            if (word.isEmpty() || word.startsWith(QLatin1Char('<')))   // <unk> and other special pieces
                continue;
            const bool wordStart = word.startsWith(QLatin1Char(' '));
            if (open && (tok.timeSec - lastTime > 0.6 || (wordStart && text.size() >= 34) || text.size() >= 70))
                flush();
            if (!open) {
                first = tok.timeSec;
                open = true;
            }
            text += word;
            lastTime = tok.timeSec;
        }
        flush();
        doneSamples += piece.second - piece.first;
        if (progress)
            progress(0.05 + 0.95 * double(doneSamples) / double(std::max<size_t>(1, totalSamples)));
    }
    // a phrase never runs into the next one
    for (qsizetype i = 0; i + 1 < result.segments.size(); ++i)
        result.segments[i].endSec = std::min(result.segments[i].endSec, result.segments[i + 1].startSec);
    return result;
}

#endif // DV_ONNX_AVAILABLE

} // namespace dv::ai
