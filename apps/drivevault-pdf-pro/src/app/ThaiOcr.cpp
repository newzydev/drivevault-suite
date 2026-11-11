#include "ThaiOcr.h"

#include "pdfengine/PdfDocument.h"
#include "pdftools/Overlay.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

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
constexpr auto detectionModel = "paddle-text-detection.onnx";
constexpr auto recognitionModel = "paddle-thai-recognition.onnx";
constexpr auto alphabetFile = "paddle-thai-dict.txt";

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

QList<QRect> findTextBoxes(Ort::Value& result, QSize size, const std::atomic_bool& cancel)
{
    const auto shape = result.GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 4 || shape[0] != 1 || shape[1] != 1 || shape[2] <= 0 || shape[3] <= 0
        || shape[2] > 2048 || shape[3] > 2048)
        throw std::runtime_error("Unsupported OCR detection map");
    const int h = int(shape[2]), w = int(shape[3]);
    const float* map = result.GetTensorData<float>();
    std::vector<bool> visited(size_t(w) * h, false);
    std::vector<int> component;
    QList<QRect> boxes;
    for (int i = 0; i < w * h && !cancel.load(std::memory_order_relaxed); ++i) {
        if (visited[i] || map[i] < 0.30f)
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
                    || visited[n] || map[n] < 0.30f)
                    continue;
                component.push_back(n);
                visited[n] = true;
            }
        }
        if (bounds.height() < 3 || bounds.width() < 5 || score / component.size() < 0.52)
            continue;
        const int pad = std::max(2, qRound(std::min(bounds.height(), bounds.width()) * 0.65));
        bounds.adjust(-pad, -pad, pad, pad);
        const double sx = double(size.width()) / w, sy = double(size.height()) / h;
        const QRect pixels(qFloor(bounds.left() * sx), qFloor(bounds.top() * sy),
                           qCeil(bounds.width() * sx), qCeil(bounds.height() * sy));
        boxes << pixels.intersected(QRect(QPoint(), size));
    }
    std::sort(boxes.begin(), boxes.end(), [](const QRect& a, const QRect& b) {
        return a.top() == b.top() ? a.left() < b.left() : a.top() < b.top();
    });
    for (qsizetype start = 0; start < boxes.size();) {
        const QRect first = boxes[start];
        qsizetype end = start + 1;
        while (end < boxes.size() && boxes[end].top() <= first.top() + first.height() / 3)
            ++end;
        std::sort(boxes.begin() + start, boxes.begin() + end, [](const QRect& a, const QRect& b) {
            return a.left() < b.left();
        });
        start = end;
    }
    return boxes;
}

QString recognize(Model& model, const QImage& crop, const QStringList& alphabet)
{
    const int h = model.shape[2] > 0 ? int(model.shape[2]) : 48;
    const int w = std::clamp(qRound(double(crop.width()) * h / crop.height()), 8, 2048);
    const int canvasWidth = model.shape[3] > 0 ? int(model.shape[3]) : std::max(320, w);
    auto result = model.run(crop, {std::min(w, canvasWidth), h}, {canvasWidth, h}, false);
    const auto shape = result.GetTensorTypeAndShapeInfo().GetShape();
    if (shape.size() != 3 || shape[0] != 1 || shape[2] != alphabet.size())
        throw std::runtime_error("OCR alphabet does not match the model");
    const float* values = result.GetTensorData<float>();
    QString text;
    int previous = 0;
    for (int t = 0; t < shape[1]; ++t) {
        const float* row = values + t * shape[2];
        const int token = int(std::max_element(row, row + shape[2]) - row);
        if (token != 0 && token != previous)
            text += alphabet[token];
        previous = token;
    }
    return text.trimmed();
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
    for (const QRect& box : findTextBoxes(detections, image.size(), cancel)) {
        if (cancel.load(std::memory_order_relaxed))
            break;
        const QString text = recognize(reader, image.copy(box), alphabet);
        if (text.isEmpty())
            continue;
        const double sx = size.width() / image.width(), sy = size.height() / image.height();
        const QPointF baseline(box.left() * sx, (box.top() + box.height() * 0.78) * sy);
        runs.append({text, baseline, baseline, 0, box.height() * sy * 0.72, box.width() * sx, true});
    }
    return runs;
}
#endif

} // namespace

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
                           const std::atomic_bool& cancel, const tools::Progress& progress)
{
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
            if (!doc.pageText(page).trimmed().isEmpty())
                continue;
            const QSizeF size = doc.pageSizePt(page);
            const QImage image = doc.render(page, 3, &cancel, 0, false, false);
            if (image.isNull())
                return cancel.load() ? QStringLiteral("cancelled") : QStringLiteral("ocrFailed");
            auto runs = readPage(detector, reader, alphabet, image, size, cancel);
            if (!runs.isEmpty())
                edits.append({page, size, {}, std::move(runs)});
        }
    } catch (const std::exception&) {
        return QStringLiteral("ocrFailed");
    }
    if (cancel.load())
        return QStringLiteral("cancelled");
    if (edits.isEmpty())
        return QStringLiteral("ocrNoText");

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
    return QFile::rename(copy, destination) ? QString() : QStringLiteral("write");
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
