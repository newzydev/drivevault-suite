#include "Tracker.h"

#include "media/VideoReader.h"

#include <QImage>

#include <algorithm>
#include <cmath>
#include <vector>

namespace dv::ai {

namespace {
std::vector<uint8_t> grey(const QImage& src)
{
    const QImage image = src.convertToFormat(QImage::Format_Grayscale8);
    std::vector<uint8_t> g(size_t(image.width()) * image.height());
    for (int y = 0; y < image.height(); ++y)
        std::copy(image.constScanLine(y), image.constScanLine(y) + image.width(), g.begin() + size_t(y) * image.width());
    return g;
}
} // namespace

TrackResult trackRegion(const QString& mediaPath, int64_t startUs, double fps, int frames, const QRectF& region,
                        const ProgressFn& progress, const std::atomic_bool& cancel)
{
    TrackResult result;
    VideoReader reader;
    if (!reader.open(mediaPath, &result.error))
        return result;
    if (frames < 1 || fps <= 0 || region.width() <= 0 || region.height() <= 0) {
        result.error = QStringLiteral("Invalid tracking request.");
        return result;
    }
    const QSize disp = reader.displaySize();
    const int W = 240;
    const int H = std::max(16, int(std::lround(double(W) * disp.height() / std::max(1, disp.width()))));
    const QSize size(W, H);

    auto frameGrey = [&](int i, std::vector<uint8_t>& out) {
        const QImage img = reader.frameAt(startUs + int64_t(std::llround(double(i) * 1e6 / fps)), size);
        if (img.isNull() || img.size() != size)
            return false;
        out = grey(img);
        return true;
    };

    std::vector<uint8_t> frame;
    if (!frameGrey(0, frame)) {
        result.error = QStringLiteral("Cannot decode the video.");
        return result;
    }
    const int tw = std::clamp(int(std::lround(region.width() * W)), 6, W - 2);
    const int th = std::clamp(int(std::lround(region.height() * H)), 6, H - 2);
    int tx = std::clamp(int(std::lround(region.center().x() * W)) - tw / 2, 0, W - tw);
    int ty = std::clamp(int(std::lround(region.center().y() * H)) - th / 2, 0, H - th);
    std::vector<uint8_t> tmpl(size_t(tw) * th);
    for (int y = 0; y < th; ++y)
        for (int x = 0; x < tw; ++x)
            tmpl[size_t(y) * tw + x] = frame[size_t(ty + y) * W + tx + x];

    result.centers.append({(tx + tw / 2.0) / W, (ty + th / 2.0) / H});
    result.confidence.append(1.f);
    const int radius = std::max(8, int(W * 0.12));

    for (int i = 1; i < frames; ++i) {
        if (cancel.load()) {
            result.centers.clear();
            result.confidence.clear();
            result.error = QStringLiteral("cancelled");
            return result;
        }
        if (!frameGrey(i, frame)) {          // past the end of the media: stay where we were
            result.centers.append(result.centers.last());
            result.confidence.append(0.f);
            continue;
        }
        long best = -1;
        int bx = tx, by = ty;
        for (int dy = -radius; dy <= radius; ++dy) {
            const int y0 = ty + dy;
            if (y0 < 0 || y0 + th > H)
                continue;
            for (int dx = -radius; dx <= radius; ++dx) {
                const int x0 = tx + dx;
                if (x0 < 0 || x0 + tw > W)
                    continue;
                long sad = 0;
                for (int y = 0; y < th && (best < 0 || sad < best); ++y) {
                    const uint8_t* a = &frame[size_t(y0 + y) * W + x0];
                    const uint8_t* b = &tmpl[size_t(y) * tw];
                    for (int x = 0; x < tw; ++x)
                        sad += std::abs(int(a[x]) - int(b[x]));
                }
                if (best < 0 || sad < best) {
                    best = sad;
                    bx = x0;
                    by = y0;
                }
            }
        }
        tx = bx;
        ty = by;
        result.centers.append({(tx + tw / 2.0) / W, (ty + th / 2.0) / H});
        result.confidence.append(best < 0 ? 0.f : float(1.0 - double(best) / (255.0 * tw * th)));
        if (progress)
            progress(double(i + 1) / frames);
    }
    return result;
}

} // namespace dv::ai
