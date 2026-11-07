#include "Effects.h"

#include <QFile>
#include <QTextStream>

#include <algorithm>
#include <cmath>

namespace dv {

namespace {
inline int clamp8(float v) { return v <= 0.f ? 0 : v >= 255.f ? 255 : int(v + 0.5f); }

double evalCurve(const QList<QPointF>& pts, double x)
{
    if (pts.size() < 2)
        return x;
    if (x <= pts.first().x())
        return pts.first().y();
    if (x >= pts.last().x())
        return pts.last().y();
    for (qsizetype i = 0; i + 1 < pts.size(); ++i) {
        const QPointF& a = pts.at(i);
        const QPointF& b = pts.at(i + 1);
        if (x >= a.x() && x <= b.x()) {
            const double t = b.x() > a.x() ? (x - a.x()) / (b.x() - a.x()) : 0.0;
            // smooth (cosine) interpolation between the control points
            const double s = (1.0 - std::cos(t * M_PI)) * 0.5;
            return a.y() + (b.y() - a.y()) * (0.5 * t + 0.5 * s);
        }
    }
    return x;
}

// One channel of the tone pipeline: exposure / white balance, lift-gamma-gain, contrast / brightness, curves.
std::array<uint8_t, 256> buildChannelLut(const EffectSettings& e, int c)
{
    const double wb = c == 0 ? 1.0 + 0.35 * e.temperature + 0.10 * e.tint
                      : c == 1 ? 1.0 - 0.20 * e.tint
                               : 1.0 - 0.35 * e.temperature + 0.10 * e.tint;
    const double exposureGain = std::pow(2.0, e.exposure);
    std::array<uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i) {
        double v = i / 255.0 * exposureGain * wb;
        v = e.gain[size_t(c)] * v + e.lift[size_t(c)] * (1.0 - v);
        v = std::clamp(v, 0.0, 1.0);
        v = std::pow(v, 1.0 / e.gamma[size_t(c)]);
        v = (v - 0.5) * (1.0 + e.contrast) + 0.5 + e.brightness;
        v = std::clamp(v, 0.0, 1.0);
        v = evalCurve(e.curves[0], v);
        v = evalCurve(e.curves[size_t(c) + 1], std::clamp(v, 0.0, 1.0));
        lut[size_t(i)] = uint8_t(std::clamp(v, 0.0, 1.0) * 255.0 + 0.5);
    }
    return lut;
}

// Running-sum box blur over the four channels; horizontal then vertical, edge pixels repeat.
void boxBlur(QImage& img, int radius)
{
    if (radius < 1)
        return;
    const int w = img.width();
    const int h = img.height();
    std::vector<uint32_t> line(size_t(std::max(w, h)) * 4);
    const int window = 2 * radius + 1;

    auto pass = [&](bool horizontal) {
        const int n = horizontal ? w : h;
        const int lines = horizontal ? h : w;
        for (int l = 0; l < lines; ++l) {
            auto at = [&](int i) -> uint8_t* {
                return horizontal ? img.scanLine(l) + size_t(i) * 4 : img.scanLine(i) + size_t(l) * 4;
            };
            for (int i = 0; i < n; ++i)
                for (int c = 0; c < 4; ++c)
                    line[size_t(i) * 4 + size_t(c)] = at(i)[c];
            uint32_t sum[4] = {0, 0, 0, 0};
            for (int k = -radius; k <= radius; ++k) {
                const int idx = std::clamp(k, 0, n - 1);
                for (int c = 0; c < 4; ++c)
                    sum[c] += line[size_t(idx) * 4 + size_t(c)];
            }
            for (int i = 0; i < n; ++i) {
                uint8_t* px = at(i);
                for (int c = 0; c < 4; ++c)
                    px[c] = uint8_t(sum[c] / uint32_t(window));
                const int out = std::clamp(i - radius, 0, n - 1);
                const int in = std::clamp(i + radius + 1, 0, n - 1);
                for (int c = 0; c < 4; ++c)
                    sum[c] += line[size_t(in) * 4 + size_t(c)] - line[size_t(out) * 4 + size_t(c)];
            }
        }
    };
    pass(true);
    pass(false);
}

void gaussianLike(QImage& img, double radius)
{
    const int r = std::max(1, int(std::lround(radius * 0.58)));
    for (int i = 0; i < 3; ++i)      // three box passes approximate a Gaussian
        boxBlur(img, r);
}
} // namespace

// ---- Lut3D ------------------------------------------------------------------------------------------------

std::shared_ptr<const Lut3D> Lut3D::load(const QString& path, QString* error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = f.errorString();
        return nullptr;
    }
    auto lut = std::make_shared<Lut3D>();
    QTextStream in(&f);
    int size = 0;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1String("TITLE")))
            continue;
        if (line.startsWith(QLatin1String("LUT_3D_SIZE"))) {
            size = line.section(QLatin1Char(' '), 1).toInt();
            if (size < 2 || size > 128) {
                if (error)
                    *error = QStringLiteral("Unsupported LUT size");
                return nullptr;
            }
            lut->m_data.reserve(size_t(size) * size * size * 3);
            continue;
        }
        if (line.startsWith(QLatin1String("LUT_1D_SIZE"))) {
            if (error)
                *error = QStringLiteral("1D LUTs are not supported");
            return nullptr;
        }
        if (line.startsWith(QLatin1String("DOMAIN_")))
            continue;
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() != 3)
            continue;
        bool ok = true;
        for (const QString& p : parts) {
            lut->m_data.push_back(p.toFloat(&ok));
            if (!ok) {
                if (error)
                    *error = QStringLiteral("Invalid number in LUT");
                return nullptr;
            }
        }
    }
    if (size == 0 || lut->m_data.size() != size_t(size) * size * size * 3) {
        if (error)
            *error = QStringLiteral("The .cube file is incomplete");
        return nullptr;
    }
    lut->m_size = size;
    return lut;
}

void Lut3D::apply(float& r, float& g, float& b) const
{
    const float n = float(m_size - 1);
    const float fr = std::clamp(r, 0.f, 1.f) * n;
    const float fg = std::clamp(g, 0.f, 1.f) * n;
    const float fb = std::clamp(b, 0.f, 1.f) * n;
    const int r0 = std::min(int(fr), m_size - 2), g0 = std::min(int(fg), m_size - 2), b0 = std::min(int(fb), m_size - 2);
    const float dr = fr - float(r0), dg = fg - float(g0), db = fb - float(b0);
    auto at = [&](int ri, int gi, int bi) { return &m_data[(size_t(bi) * m_size * m_size + size_t(gi) * m_size + size_t(ri)) * 3]; };
    float out[3];
    for (int c = 0; c < 3; ++c) {
        const float c000 = at(r0, g0, b0)[c], c100 = at(r0 + 1, g0, b0)[c], c010 = at(r0, g0 + 1, b0)[c], c110 = at(r0 + 1, g0 + 1, b0)[c];
        const float c001 = at(r0, g0, b0 + 1)[c], c101 = at(r0 + 1, g0, b0 + 1)[c], c011 = at(r0, g0 + 1, b0 + 1)[c], c111 = at(r0 + 1, g0 + 1, b0 + 1)[c];
        const float c00 = c000 + (c100 - c000) * dr, c10 = c010 + (c110 - c010) * dr;
        const float c01 = c001 + (c101 - c001) * dr, c11 = c011 + (c111 - c011) * dr;
        const float c0 = c00 + (c10 - c00) * dg, c1 = c01 + (c11 - c01) * dg;
        out[c] = c0 + (c1 - c0) * db;
    }
    r = out[0];
    g = out[1];
    b = out[2];
}

// ---- EffectSettings -----------------------------------------------------------------------------------------

bool EffectSettings::needsColor() const
{
    const bool toneChange = brightness != 0 || contrast != 0 || exposure != 0 || temperature != 0 || tint != 0;
    bool grade = false;
    for (int i = 0; i < 3; ++i)
        grade = grade || lift[size_t(i)] != 0 || gamma[size_t(i)] != 1 || gain[size_t(i)] != 1;
    bool curved = false;
    for (const auto& c : curves)
        curved = curved || c.size() >= 2;
    return toneChange || grade || curved || saturation != 1 || lut != nullptr;
}

bool EffectSettings::isIdentity() const
{
    return !needsColor() && blur <= 0 && sharpen <= 0 && vignette <= 0
           && crop[0] <= 0 && crop[1] <= 0 && crop[2] <= 0 && crop[3] <= 0;
}

EffectSettings EffectSettings::fromClip(const ClipData& c, int64_t f, QHash<QString, std::shared_ptr<const Lut3D>>& luts)
{
    EffectSettings e;
    e.brightness = c.param("brightness", f);
    e.contrast = c.param("contrast", f);
    e.saturation = c.param("saturation", f);
    e.exposure = c.param("exposure", f);
    e.temperature = c.param("temperature", f);
    e.tint = c.param("tint", f);
    static const char* const rgb[3] = {"R", "G", "B"};
    for (int i = 0; i < 3; ++i) {
        e.lift[size_t(i)] = c.param(QStringLiteral("lift") + QLatin1String(rgb[i]), f);
        e.gamma[size_t(i)] = c.param(QStringLiteral("gamma") + QLatin1String(rgb[i]), f);
        e.gain[size_t(i)] = c.param(QStringLiteral("gain") + QLatin1String(rgb[i]), f);
    }
    e.blur = c.param("blur", f);
    e.sharpen = c.param("sharpen", f);
    e.vignette = c.param("vignette", f);
    e.crop = {c.param("cropL", f), c.param("cropR", f), c.param("cropT", f), c.param("cropB", f)};
    static const char* const names[4] = {"master", "r", "g", "b"};
    for (int i = 0; i < 4; ++i)
        e.curves[size_t(i)] = c.curves.value(QLatin1String(names[i]));
    if (!c.lutPath.isEmpty()) {
        auto it = luts.find(c.lutPath);
        if (it == luts.end())
            it = luts.insert(c.lutPath, Lut3D::load(c.lutPath));   // a failed load is cached as null
        e.lut = it.value();
        e.lutIntensity = c.param("lutIntensity", f);
    }
    return e;
}

// ---- applying -----------------------------------------------------------------------------------------------

void applyEffects(QImage& image, const EffectSettings& e, double scale)
{
    if (image.isNull() || e.isIdentity())
        return;
    if (image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int w = image.width();
    const int h = image.height();

    // ---- colour: tone LUTs, saturation, 3D LUT, vignette (one pass) ------------------------------------------------
    if (e.needsColor() || e.vignette > 0) {
        const auto lr = buildChannelLut(e, 0);
        const auto lg = buildChannelLut(e, 1);
        const auto lb = buildChannelLut(e, 2);
        const bool sat = e.saturation != 1;
        const bool lut3 = e.lut && e.lutIntensity > 0;
        const bool color = e.needsColor();
        const float satf = float(e.saturation);
        const float mix = float(e.lutIntensity);

        std::vector<float> dx2;
        if (e.vignette > 0) {
            dx2.resize(size_t(w));
            for (int x = 0; x < w; ++x) {
                const double nx = (x + 0.5) / w * 2.0 - 1.0;
                dx2[size_t(x)] = float(nx * nx);
            }
        }
        for (int y = 0; y < h; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(image.scanLine(y));
            const double ny = (y + 0.5) / h * 2.0 - 1.0;
            for (int x = 0; x < w; ++x) {
                uint32_t px = row[x];
                const int a = int(px >> 24);
                if (a == 0)
                    continue;
                float r = float((px >> 16) & 0xFF), g = float((px >> 8) & 0xFF), b = float(px & 0xFF);
                if (a < 255) {                                   // un-premultiply
                    const float k = 255.f / float(a);
                    r = std::min(255.f, r * k);
                    g = std::min(255.f, g * k);
                    b = std::min(255.f, b * k);
                }
                if (color) {
                    r = lr[size_t(clamp8(r))];
                    g = lg[size_t(clamp8(g))];
                    b = lb[size_t(clamp8(b))];
                    if (sat) {
                        const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                        r = luma + (r - luma) * satf;
                        g = luma + (g - luma) * satf;
                        b = luma + (b - luma) * satf;
                    }
                    if (lut3) {
                        float cr = std::clamp(r / 255.f, 0.f, 1.f), cg = std::clamp(g / 255.f, 0.f, 1.f), cb = std::clamp(b / 255.f, 0.f, 1.f);
                        const float or_ = cr, og = cg, ob = cb;
                        e.lut->apply(cr, cg, cb);
                        r = (or_ + (cr - or_) * mix) * 255.f;
                        g = (og + (cg - og) * mix) * 255.f;
                        b = (ob + (cb - ob) * mix) * 255.f;
                    }
                }
                if (e.vignette > 0) {
                    const float d = std::sqrt((dx2[size_t(x)] + float(ny * ny)) * 0.5f);   // 0 at the centre, 1 at the corners
                    const float t = std::clamp((d - 0.35f) / 0.65f, 0.f, 1.f);
                    const float f = 1.f - float(e.vignette) * t * t * (3.f - 2.f * t);
                    r *= f;
                    g *= f;
                    b *= f;
                }
                const int ir = clamp8(r), ig = clamp8(g), ib = clamp8(b);
                if (a < 255)                                      // re-premultiply
                    row[x] = uint32_t(a) << 24 | uint32_t(ir * a / 255) << 16 | uint32_t(ig * a / 255) << 8 | uint32_t(ib * a / 255);
                else
                    row[x] = 0xFF000000u | uint32_t(ir) << 16 | uint32_t(ig) << 8 | uint32_t(ib);
            }
        }
    }

    // ---- blur / sharpen -----------------------------------------------------------------------------------------------
    if (e.blur > 0) {
        gaussianLike(image, e.blur * scale);
    }
    if (e.sharpen > 0) {
        QImage soft = image.copy();
        gaussianLike(soft, 2.0 * std::max(0.5, scale));
        const float amount = float(e.sharpen);
        for (int y = 0; y < h; ++y) {
            uint8_t* a = image.scanLine(y);
            const uint8_t* b = soft.constScanLine(y);
            for (int x = 0; x < w * 4; x += 4) {
                for (int c = 0; c < 3; ++c)
                    a[x + c] = uint8_t(clamp8(float(a[x + c]) + amount * (float(a[x + c]) - float(b[x + c]))));
                // premultiplied: colour may not exceed alpha
                for (int c = 0; c < 3; ++c)
                    a[x + c] = std::min(a[x + c], a[x + 3]);
            }
        }
    }

    // ---- crop: pixels outside the kept rectangle become transparent -----------------------------------------------------
    if (e.crop[0] > 0 || e.crop[1] > 0 || e.crop[2] > 0 || e.crop[3] > 0) {
        const int x0 = int(std::lround(e.crop[0] * w)), x1 = w - int(std::lround(e.crop[1] * w));
        const int y0 = int(std::lround(e.crop[2] * h)), y1 = h - int(std::lround(e.crop[3] * h));
        for (int y = 0; y < h; ++y) {
            auto* row = reinterpret_cast<uint32_t*>(image.scanLine(y));
            for (int x = 0; x < w; ++x)
                if (y < y0 || y >= y1 || x < x0 || x >= x1)
                    row[x] = 0;
        }
    }
}

} // namespace dv
