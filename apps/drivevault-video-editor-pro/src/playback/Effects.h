#pragma once

#include "core/TimelineTypes.h"

#include <QHash>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QString>

#include <array>
#include <memory>
#include <vector>

namespace dv {

// A 3D colour look-up table in the Adobe / Resolve ".cube" format.
class Lut3D {
public:
    static std::shared_ptr<const Lut3D> load(const QString& path, QString* error = nullptr);
    int size() const { return m_size; }
    // r, g, b in [0, 1]; trilinear interpolation.
    void apply(float& r, float& g, float& b) const;

private:
    int m_size = 0;
    std::vector<float> m_data;   // size^3 * 3, red varies fastest
};

// Everything the image effects need for one clip at one frame (keyframes already evaluated).
struct EffectSettings {
    double brightness = 0, contrast = 0, saturation = 1, exposure = 0, temperature = 0, tint = 0;
    std::array<double, 3> lift{0, 0, 0};
    std::array<double, 3> gamma{1, 1, 1};
    std::array<double, 3> gain{1, 1, 1};
    double blur = 0, sharpen = 0, vignette = 0;
    std::array<double, 4> crop{0, 0, 0, 0};          // left, right, top, bottom as fractions
    std::array<QList<QPointF>, 4> curves;            // master, r, g, b
    std::shared_ptr<const Lut3D> lut;
    double lutIntensity = 1;

    bool needsColor() const;
    bool isIdentity() const;
    // Evaluates the clip's parameters at `clipFrame`. `luts` caches loaded .cube files for the calling thread.
    static EffectSettings fromClip(const ClipData& clip, int64_t clipFrame, QHash<QString, std::shared_ptr<const Lut3D>>& luts);
};

// Applies the effects in place. `image` may be RGB32 or ARGB32 (premultiplied); the result is premultiplied ARGB.
// `scale` converts pixel based parameters (blur radius) from a 1080p canvas to the actual canvas height / 1080.
void applyEffects(QImage& image, const EffectSettings& settings, double scale);

} // namespace dv
