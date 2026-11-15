#include "ThaiOcr.h"

#include "pdfengine/PdfDocument.h"
#include "pdftools/Overlay.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QPolygonF>
#include <QRegularExpression>
#include <QTransform>
#include <QTemporaryDir>

#include <algorithm>

#ifdef DP_ONNX_AVAILABLE
#define ORT_API_MANUAL_INIT
#include <onnxruntime_cxx_api.h>

#include <QLibrary>
#include <QtMath>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>
#endif

namespace dp {
namespace {

#ifdef DP_ONNX_AVAILABLE
// OpenThai 2.0 OCR Text (iApp Technology, Apache-2.0): det.onnx is PP-OCRv6-tiny, rec.onnx a Thai CTC line recogniser, rec_dict.txt its characters.
constexpr auto detectionModel = "openthai-ocr-detection.onnx";
constexpr auto recognitionModel = "openthai-ocr-recognition.onnx";
constexpr auto alphabetFile = "openthai-ocr-dict.txt";
// Detector post-processing as in the model's det.yml: probability threshold, box score threshold, unclip ratio.
constexpr float detectionThreshold = 0.2f;
constexpr double minBoxScore = 0.4;
constexpr double unclipRatio = 1.4;

QDir runtimeDir()
{
    return QDir(QCoreApplication::applicationDirPath());
}

QDir modelDir()
{
    return QDir(runtimeDir().filePath(QStringLiteral("models")));
}

bool loadRuntime()
{
    static const bool loaded = [] {
        QLibrary library(runtimeDir().filePath(QStringLiteral("onnxruntime.dll")));
        library.setLoadHints(QLibrary::PreventUnloadHint);
        using GetApi = const OrtApiBase* (ORT_API_CALL*)();
        const auto getApi = reinterpret_cast<GetApi>(library.resolve("OrtGetApiBase"));
        if (!getApi)
            return false;
        const OrtApi* api = getApi()->GetApi(ORT_API_VERSION);
        if (!api)
            return false;
        Ort::InitApi(api);
        return true;
    }();
    return loaded;
}

class Model {
public:
    Model(const Ort::Env& env, const Ort::SessionOptions& options, const QString& path)
        : session(env, reinterpret_cast<const wchar_t*>(path.utf16()), options)
    {
        Ort::AllocatorWithDefaultOptions alloc;
        input = session.GetInputNameAllocated(0, alloc).get();
        output = session.GetOutputNameAllocated(0, alloc).get();
        shape = session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
        if (shape.size() != 4 || shape[1] != 3)
            throw std::runtime_error("Unsupported OCR input");
    }

    Ort::Value run(const QImage& source, QSize content, QSize canvas, bool detection)
    {
        const QImage image = source.scaled(content, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                 .convertToFormat(QImage::Format_RGB888);
        const size_t stride = size_t(canvas.width()) * canvas.height();
        std::vector<float> pixels(stride * 3, 0);
        constexpr std::array<float, 3> means{0.485f, 0.456f, 0.406f};
        constexpr std::array<float, 3> deviations{0.229f, 0.224f, 0.225f};
        for (int y = 0; y < image.height(); ++y) {
            const uchar* row = image.constScanLine(y);
            for (int x = 0; x < image.width(); ++x) {
                const size_t offset = size_t(y) * canvas.width() + x;
                for (size_t c = 0; c < 3; ++c) {
                    const float value = row[3 * x + 2 - c] / 255.0f;  // Paddle input is BGR.
                    pixels[c * stride + offset] = detection ? (value - means[c]) / deviations[c] : 2 * value - 1;
                }
            }
        }
        const int64_t dims[]{1, 3, canvas.height(), canvas.width()};
        const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, pixels.data(), pixels.size(), dims, 4);
        const char* inputName = input.c_str();
        const char* outputName = output.c_str();
        auto values = session.Run(Ort::RunOptions{nullptr}, &inputName, &tensor, 1, &outputName, 1);
        return std::move(values.front());
    }

    std::vector<int64_t> shape;

private:
    Ort::Session session;
    std::string input, output;
};

QStringList readAlphabet()
{
    QFile file(modelDir().filePath(QLatin1String(alphabetFile)));
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("Missing OCR alphabet");
    QStringList chars{QString()};  // CTC blank
    while (!file.atEnd()) {
        QByteArray line = file.readLine();
        while (line.endsWith('\n') || line.endsWith('\r'))
            line.chop(1);
        if (!line.isEmpty())
            chars << QString::fromUtf8(line);
    }
    chars << QStringLiteral(" ");
    return chars;
}

// One text line found by the detector: the axis-aligned bounds (reading order, where the text layer goes) and the box that follows the line's tilt.
struct Detected {
    QRect bounds;
    QPolygonF quad;       // top-left, top-right, bottom-right, bottom-left, in image pixels
};

QList<Detected> findTextBoxes(Ort::Value& result, QSize size, const std::atomic_bool& cancel)
{
    const auto shape = result.GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 4 || shape[0] != 1 || shape[1] != 1 || shape[2] <= 0 || shape[3] <= 0
        || shape[2] > 2048 || shape[3] > 2048)
        throw std::runtime_error("Unsupported OCR detection map");
    const int h = int(shape[2]), w = int(shape[3]);
    const float* map = result.GetTensorData<float>();
    std::vector<bool> visited(size_t(w) * h, false);
    std::vector<int> component;
    QList<Detected> boxes;
    const double sx = double(size.width()) / w, sy = double(size.height()) / h;
    for (int i = 0; i < w * h && !cancel.load(std::memory_order_relaxed); ++i) {
        if (visited[i] || map[i] < detectionThreshold)
            continue;
        component.clear();
        component.push_back(i);
        visited[i] = true;
        QRect bounds(i % w, i / w, 1, 1);
        double score = 0;
        for (size_t next = 0; next < component.size(); ++next) {
            const int p = component[next], x = p % w, y = p / w;
            bounds |= QRect(x, y, 1, 1);
            score += map[p];
            for (const int offset : {-1, 1, -w, w}) {
                const int n = p + offset;
                if (n < 0 || n >= w * h || (offset == -1 && x == 0) || (offset == 1 && x == w - 1)
                    || visited[n] || map[n] < detectionThreshold)
                    continue;
                component.push_back(n);
                visited[n] = true;
            }
        }
        if (bounds.height() < 3 || bounds.width() < 5 || score / component.size() < minBoxScore)
            continue;
        // The line's direction from the second moments of its pixels (a slightly tilted scan gives a tilted line; beyond 30 degrees, or below 0.3,
        // the box stays upright), then the extents along and across that direction.
        double meanX = 0, meanY = 0;
        for (const int p : component) {
            meanX += p % w;
            meanY += p / w;
        }
        meanX /= double(component.size());
        meanY /= double(component.size());
        double sxx = 0, syy = 0, sxy = 0;
        for (const int p : component) {
            const double dx = p % w - meanX, dy = p / w - meanY;
            sxx += dx * dx;
            syy += dy * dy;
            sxy += dx * dy;
        }
        double angle = 0.5 * std::atan2(2.0 * sxy, sxx - syy);
        if (std::abs(angle) > 0.5236 || std::abs(angle) < 0.0052)
            angle = 0;
        const double c = std::cos(angle), s = std::sin(angle);
        double uMin = 1e18, uMax = -1e18, vMin = 1e18, vMax = -1e18;
        for (const int p : component) {
            const double dx = p % w - meanX, dy = p / w - meanY;
            const double u = dx * c + dy * s, v = -dx * s + dy * c;
            uMin = std::min(uMin, u);
            uMax = std::max(uMax, u);
            vMin = std::min(vMin, v);
            vMax = std::max(vMax, v);
        }
        const double boxW = uMax - uMin + 1, boxH = vMax - vMin + 1;
        // the detector draws a shrunken text region; the box grows by area * ratio / perimeter (DB "unclip") on every side
        const double pad = std::max(2.0, boxW * boxH * unclipRatio / (2.0 * (boxW + boxH)));
        const std::array<QPointF, 4> corners{QPointF(uMin - pad, vMin - pad), QPointF(uMax + pad, vMin - pad), QPointF(uMax + pad, vMax + pad),
                                              QPointF(uMin - pad, vMax + pad)};
        QPolygonF quad;
        for (const QPointF& corner : corners)
            quad << QPointF((meanX + corner.x() * c - corner.y() * s) * sx, (meanY + corner.x() * s + corner.y() * c) * sy);
        const QRect pixels = quad.boundingRect().toAlignedRect().intersected(QRect(QPoint(), size));
        if (pixels.width() < 2 || pixels.height() < 2)
            continue;
        boxes.append({pixels, quad});
    }
    std::sort(boxes.begin(), boxes.end(), [](const Detected& a, const Detected& b) {
        return a.bounds.top() == b.bounds.top() ? a.bounds.left() < b.bounds.left() : a.bounds.top() < b.bounds.top();
    });
    for (qsizetype start = 0; start < boxes.size();) {
        const QRect first = boxes[start].bounds;
        qsizetype end = start + 1;
        while (end < boxes.size() && boxes[end].bounds.top() <= first.top() + first.height() / 3)
            ++end;
        std::sort(boxes.begin() + start, boxes.begin() + end, [](const Detected& a, const Detected& b) {
            return a.bounds.left() < b.bounds.left();
        });
        start = end;
    }
    return boxes;
}

// The line cut out along its own direction and straightened (a plain copy when the box is upright).
QImage cutLine(const QImage& image, const Detected& line)
{
    const QPolygonF& q = line.quad;
    const auto length = [](const QPointF& a, const QPointF& b) { return std::hypot(a.x() - b.x(), a.y() - b.y()); };
    const int w = std::max(1, int(std::ceil(std::max(length(q[0], q[1]), length(q[3], q[2])))));
    const int h = std::max(1, int(std::ceil(std::max(length(q[0], q[3]), length(q[1], q[2])))));
    const bool upright = std::abs(q[0].y() - q[1].y()) < 0.75 && std::abs(q[0].x() - q[3].x()) < 0.75;
    if (upright)
        return image.copy(line.bounds);
    QTransform toLine;
    if (!QTransform::quadToQuad(q, QPolygonF(QRectF(0, 0, w, h)), toLine))
        return image.copy(line.bounds);
    QImage out(w, h, QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setTransform(toLine);
    painter.drawImage(0, 0, image);
    return out;
}

// Dark text on white from any line crop (light on dark, coloured, gradients): each column's background is the median of the crop's top and bottom
// margin rows, smoothed along the line, and text is whatever differs from it, light or dark. The recogniser was trained on grey lines only.
QImage flattenCrop(const QImage& source, int width, int height)
{
    const QImage gray = source.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8);
    const int k = std::max(2, height / 6);
    std::vector<uchar> background(static_cast<size_t>(width));  // (size_t(width) would declare a function)
    std::vector<int> margin;
    for (int x = 0; x < width; ++x) {
        margin.clear();
        for (int y = 0; y < k; ++y)
            margin.push_back(gray.constScanLine(y)[x]);
        for (int y = height - k; y < height; ++y)
            margin.push_back(gray.constScanLine(y)[x]);
        std::sort(margin.begin(), margin.end());
        const size_t n = margin.size();
        background[size_t(x)] = uchar(n % 2 ? margin[n / 2] : (margin[n / 2 - 1] + margin[n / 2]) / 2);
    }
    const int window = std::max(3, std::min(height | 1, 255));
    std::vector<uchar> smooth(static_cast<size_t>(width));
    std::vector<uchar> neighbours(static_cast<size_t>(window));
    for (int x = 0; x < width; ++x) {
        for (int i = 0; i < window; ++i)
            neighbours[size_t(i)] = background[size_t(std::clamp(x + i - window / 2, 0, width - 1))];
        std::nth_element(neighbours.begin(), neighbours.begin() + window / 2, neighbours.end());
        smooth[size_t(x)] = neighbours[size_t(window / 2)];
    }
    std::vector<int> histogram(256, 0);
    std::vector<int> difference(static_cast<size_t>(width) * static_cast<size_t>(height));
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const int d = std::abs(int(gray.constScanLine(y)[x]) - int(smooth[size_t(x)]));
            difference[size_t(y) * width + x] = d;
            ++histogram[size_t(d)];
        }
    const double target = 0.98 * double(width) * height;
    int cumulative = 0, high = 0;
    for (; high < 256; ++high) {
        cumulative += histogram[size_t(high)];
        if (double(cumulative) >= target)
            break;
    }
    const double scale = 255.0 / std::max(double(high), 24.0);
    QImage out(width, height, QImage::Format_RGB888);
    for (int y = 0; y < height; ++y) {
        uchar* row = out.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const int value = int(255.0 - std::clamp(difference[size_t(y) * width + x] * scale, 0.0, 255.0));
            row[3 * x] = row[3 * x + 1] = row[3 * x + 2] = uchar(value);
        }
    }
    return out;
}

struct Reading {
    QString text;
    double confidence = 0;       // mean probability of the characters that were read
};

Reading recognize(Model& model, const QImage& crop, const QStringList& alphabet)
{
    const int h = model.shape[2] > 0 ? int(model.shape[2]) : 48;
    const int w = std::clamp(qRound(double(crop.width()) * h / crop.height()), 16, 3200);
    const int canvasWidth = model.shape[3] > 0 ? int(model.shape[3]) : w;
    const QImage prepared = flattenCrop(crop, std::min(w, canvasWidth), h);
    auto result = model.run(prepared, {std::min(w, canvasWidth), h}, {canvasWidth, h}, false);
    const auto shape = result.GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 3 || shape[0] != 1 || shape[2] != alphabet.size())
        throw std::runtime_error("OCR alphabet does not match the model");
    const float* values = result.GetTensorData<float>();
    Reading reading;
    int previous = 0, emitted = 0;
    double probability = 0;
    for (int t = 0; t < shape[1]; ++t) {
        const float* row = values + t * shape[2];
        const int token = int(std::max_element(row, row + shape[2]) - row);
        if (token != 0 && token != previous) {
            reading.text += alphabet[token];
            probability += row[token];
            ++emitted;
        }
        previous = token;
    }
    reading.confidence = emitted ? probability / emitted : 0.0;
    reading.text = reading.text.trimmed();
    return reading;
}

tools::TextRuns readPage(Model& detector, Model& reader, const QStringList& alphabet,
                         const QImage& image, const QSizeF& size, const std::atomic_bool& cancel)
{
    const double scale = std::min(1.0, 1280.0 / std::max(image.width(), image.height()));
    const auto dimension = [scale](int pixels) { return std::max(32, qRound(pixels * scale / 32) * 32); };
    const QSize input(detector.shape[3] > 0 ? int(detector.shape[3]) : dimension(image.width()),
                      detector.shape[2] > 0 ? int(detector.shape[2]) : dimension(image.height()));
    auto detections = detector.run(image, input, input, true);
    tools::TextRuns runs;
    for (const Detected& line : findTextBoxes(detections, image.size(), cancel)) {
        if (cancel.load(std::memory_order_relaxed))
            break;
        const QRect box = line.bounds;
        const Reading reading = recognize(reader, cutLine(image, line), alphabet);
        const QString text = fixThaiScripts(canonicalThai(reading.text));
        if (text.isEmpty() || !keepOcrLine(text, reading.confidence))
            continue;
        const double sx = size.width() / image.width(), sy = size.height() / image.height();
        const QPointF baseline(box.left() * sx, (box.top() + box.height() * 0.78) * sy);
        runs.append({text, baseline, baseline, 0, box.height() * sy * 0.72, box.width() * sx, true});
    }
    return runs;
}
#endif

} // namespace

// ---- Thai text rules (the same as the runtime of OpenThai 2.0 OCR Text, thai_norm.py / line_engine.py) --------------------------------

namespace {
bool isVowelMark(QChar c) { const ushort u = c.unicode(); return u == 0x0E31 || (u >= 0x0E34 && u <= 0x0E3A) || u == 0x0E47; }
bool isToneMark(QChar c) { const ushort u = c.unicode(); return (u >= 0x0E48 && u <= 0x0E4C) || u == 0x0E4E; }
bool isNikhahit(QChar c) { return c.unicode() == 0x0E4D; }
bool isThaiMark(QChar c) { return isVowelMark(c) || isToneMark(c) || isNikhahit(c); }
bool isThaiChar(QChar c) { return c.unicode() >= 0x0E00 && c.unicode() <= 0x0E7F; }
bool isThaiConsonant(QChar c) { return c.unicode() >= 0x0E01 && c.unicode() <= 0x0E2E; }
bool isLeadingVowel(QChar c) { return c.unicode() >= 0x0E40 && c.unicode() <= 0x0E44; }
} // namespace

QString canonicalThai(const QString& input)
{
    // legacy glyph variants (shifted marks, tail-less tho than / yo ying) -> standard code points; zero-width characters dropped; NBSP -> space
    static const ushort legacy[] = {0x0E10, 0x0E34, 0x0E35, 0x0E36, 0x0E37, 0x0E48, 0x0E49, 0x0E4A, 0x0E4B, 0x0E4C, 0x0E48, 0x0E49, 0x0E4A, 0x0E4B, 0x0E4C,
                                    0x0E0D, 0x0E31, 0x0E4D, 0x0E47, 0x0E48, 0x0E49, 0x0E4A, 0x0E4B, 0x0E4C, 0x0E38, 0x0E39, 0x0E3A};   // U+F700 .. U+F71A
    QString text;
    text.reserve(input.size());
    for (const QChar c : input) {
        const ushort u = c.unicode();
        if (u >= 0xF700 && u <= 0xF71A)
            text += QChar(legacy[u - 0xF700]);
        else if (u == 0x200B || u == 0x200C || u == 0x200D || u == 0x2060 || u == 0x00AD || u == 0xFEFF)
            continue;
        else if (u == 0x00A0)
            text += QLatin1Char(' ');
        else
            text += c;
    }
    text = text.normalized(QString::NormalizationForm_C);

    // marks after one base in one order: vowel, tone, nikhahit; a doubled mark is one
    QString ordered;
    ordered.reserve(text.size());
    for (qsizetype i = 0; i < text.size();) {
        if (!isThaiMark(text.at(i))) {
            ordered += text.at(i++);
            continue;
        }
        qsizetype end = i;
        while (end < text.size() && isThaiMark(text.at(end)))
            ++end;
        QString run = text.mid(i, end - i);
        if (run.size() >= 2) {
            std::stable_sort(run.begin(), run.end(), [](QChar a, QChar b) {
                const auto rank = [](QChar c) { return isVowelMark(c) ? 0 : isToneMark(c) ? 1 : 2; };
                return rank(a) < rank(b);
            });
            QString unique;
            for (const QChar c : std::as_const(run))
                if (unique.isEmpty() || unique.back() != c)
                    unique += c;
            run = unique;
        }
        ordered += run;
        i = end;
    }
    text = ordered;

    // sara am: nikhahit (+ tone) + sara aa -> tone + sara am, in either typed order
    static const QRegularExpression saraAm(QStringLiteral("([\\x{0E48}-\\x{0E4B}]?)\\x{0E4D}([\\x{0E48}-\\x{0E4B}]?)\\x{0E32}"));
    text.replace(saraAm, QStringLiteral("\\1\\2") + QString(QChar(0x0E33)));
    text.replace(QStringLiteral("เเ"), QStringLiteral("แ"));
    text.replace(QStringLiteral("ำำ"), QStringLiteral("ำ"));
    static const QRegularExpression spaces(QStringLiteral("[ \\t\\f\\v]+"));
    text.replace(spaces, QStringLiteral(" "));
    return text.trimmed();
}

QString fixThaiScripts(const QString& input)
{
    static const QRegularExpression loneW(QStringLiteral("(?<![A-Za-z])[Ww](?=\\.?[\\x{0E01}-\\x{0E2E}\\x{0E40}-\\x{0E44}])"));
    static const QRegularExpression zeroInThaiDigits(QStringLiteral("(?<=[\\x{0E50}-\\x{0E59}])[Oo0]|[Oo0](?=[\\x{0E50}-\\x{0E59}])"));
    QString text = input;
    text.replace(loneW, QString(QChar(0x0E1E)));
    text.replace(zeroInThaiDigits, QString(QChar(0x0E50)));
    return text;
}

double invalidThaiShare(const QString& text)
{
    int thai = 0, bad = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (!isThaiChar(ch))
            continue;
        ++thai;
        const QChar prev = i > 0 ? text.at(i - 1) : QLatin1Char(' ');
        if (isThaiMark(ch) && !(isThaiConsonant(prev) || (isThaiMark(prev) && i >= 2 && isThaiConsonant(text.at(i - 2)))))
            ++bad;
        else if (isLeadingVowel(ch) && !(i + 1 < text.size() && isThaiConsonant(text.at(i + 1))))
            ++bad;
    }
    return thai ? double(bad) / thai : 0.0;
}

bool keepOcrLine(const QString& text, double confidence)
{
    // The model card uses 0.8 for page-to-text scoring. A searchable copy needs the text more than it fears a stamp read as letters (a dropped line is
    // missing from the search for good), so the line is kept from 0.5 and the spelling check still removes the ill-spelled junk.
    constexpr double dropBelow = 0.5, junkBelow = 0.9, junkInvalid = 0.05;
    constexpr int minChars = 2;
    QString compact = text;
    compact.remove(QLatin1Char(' '));
    if (compact.isEmpty() || confidence < dropBelow || compact.size() < minChars)
        return false;
    return !(confidence < junkBelow && invalidThaiShare(text) > junkInvalid);
}

bool thaiOcrAvailable()
{
#ifdef DP_ONNX_AVAILABLE
    const QDir models = modelDir();
    return runtimeDir().exists(QStringLiteral("onnxruntime.dll"))
        && models.exists(QLatin1String(detectionModel)) && models.exists(QLatin1String(recognitionModel))
        && models.exists(QLatin1String(alphabetFile));
#else
    return false;
#endif
}

QString saveSearchableCopy(const pdf::PdfDocument& doc, const QList<int>& pages, const QString& destination,
                           const std::atomic_bool& cancel, const tools::Progress& progress, ThaiOcrReport* report)
{
    if (report)
        *report = {};
#ifdef DP_ONNX_AVAILABLE
    if (!thaiOcrAvailable() || !loadRuntime())
        return QStringLiteral("ocrModelRequired");
    if (doc.isEncrypted())
        return QStringLiteral("ocrEncrypted");
    if (QFileInfo::exists(destination))
        return QStringLiteral("exists");
    if (cancel.load())
        return QStringLiteral("cancelled");
    const int total = int(pages.size()) + 1;
    QList<tools::PageEdits> edits;
    ThaiOcrReport summary;
    try {
        Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "DriveVault Thai OCR");
        env.DisableTelemetryEvents();
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(2);
        Model detector(env, options, modelDir().filePath(QLatin1String(detectionModel)));
        Model reader(env, options, modelDir().filePath(QLatin1String(recognitionModel)));
        const QStringList alphabet = readAlphabet();
        int done = 0;
        for (const int page : pages) {
            if (cancel.load() || (progress && !progress(done++, total)))
                return QStringLiteral("cancelled");
            if (page < 0 || page >= doc.pageCount())
                return QStringLiteral("range");
            if (!doc.pageText(page).trimmed().isEmpty()) {
                ++summary.skippedTextPages;
                continue;
            }
            const QSizeF size = doc.pageSizePt(page);
            const QImage image = doc.render(page, 3, &cancel, 0, false, false);
            if (image.isNull())
                return cancel.load() ? QStringLiteral("cancelled") : QStringLiteral("ocrFailed");
            auto runs = readPage(detector, reader, alphabet, image, size, cancel);
            if (!runs.isEmpty())
                edits.append({page, size, {}, std::move(runs)});
            else
                ++summary.unrecognizedPages;
        }
    } catch (const std::exception&) {
        return QStringLiteral("ocrFailed");
    }
    if (cancel.load())
        return QStringLiteral("cancelled");
    if (edits.isEmpty()) {
        if (report)
            *report = summary;
        return QStringLiteral("ocrNoText");
    }

    QTemporaryDir staging(QFileInfo(destination).dir().filePath(QStringLiteral(".drivevault-ocr-XXXXXX")));
    if (!staging.isValid())
        return QStringLiteral("write");
    const QString copy = staging.filePath(QStringLiteral("searchable.pdf"));
    const auto saved = tools::saveWithEdits(doc.path(), {}, copy, edits, false, [&](int, int) {
        return !cancel.load() && (!progress || progress(total - 1, total));
    });
    if (!saved.ok)
        return saved.error;
    pdf::OpenError error;
    auto verified = pdf::PdfDocument::open(copy, {}, &error);
    if (!verified || verified->pageCount() != doc.pageCount())
        return QStringLiteral("ocrFailed");
    for (const auto& edit : edits) {
        if (cancel.load())
            return QStringLiteral("cancelled");
        if (verified->pageText(edit.page).trimmed().isEmpty())
            return QStringLiteral("ocrFailed");
    }
    if (cancel.load())
        return QStringLiteral("cancelled");
    verified.reset();
    if (!QFile::rename(copy, destination))
        return QStringLiteral("write");
    summary.recognizedPages = int(edits.size());
    if (report)
        *report = summary;
    return {};
#else
    Q_UNUSED(doc);
    Q_UNUSED(pages);
    Q_UNUSED(destination);
    Q_UNUSED(cancel);
    Q_UNUSED(progress);
    return QStringLiteral("ocrModelRequired");
#endif
}

} // namespace dp
