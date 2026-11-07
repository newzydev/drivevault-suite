#include "ClipParams.h"

#include <algorithm>

namespace dv {

double interpolate(double a, double b, double t, const QString& mode)
{
    t = std::clamp(t, 0.0, 1.0);
    if (mode == QLatin1String("hold"))
        return a;
    if (mode == QLatin1String("ease"))
        t = t * t * (3.0 - 2.0 * t);   // smoothstep: ease in and out
    return a + (b - a) * t;
}

double ParamTrack::at(int64_t clipFrame) const
{
    if (keys.isEmpty())
        return value;
    if (clipFrame <= keys.first().frame)
        return keys.first().value;
    if (clipFrame >= keys.last().frame)
        return keys.last().value;
    for (qsizetype i = 0; i + 1 < keys.size(); ++i) {
        const Keyframe& a = keys.at(i);
        const Keyframe& b = keys.at(i + 1);
        if (clipFrame >= a.frame && clipFrame < b.frame)
            return interpolate(a.value, b.value, double(clipFrame - a.frame) / double(b.frame - a.frame), a.interp);
    }
    return keys.last().value;
}

namespace params {

const QList<Def>& all()
{
    static const QList<Def> kAll = {
        // transform (position as a fraction of the canvas, scale 1 = fit, rotation in degrees)
        {"opacity", 1.0, 0.0, 1.0, "transform"},
        {"posX", 0.0, -2.0, 2.0, "transform"},
        {"posY", 0.0, -2.0, 2.0, "transform"},
        {"scale", 1.0, 0.05, 8.0, "transform"},
        {"rotation", 0.0, -360.0, 360.0, "transform"},
        // basic colour / look
        {"brightness", 0.0, -1.0, 1.0, "color"},
        {"contrast", 0.0, -1.0, 1.0, "color"},
        {"saturation", 1.0, 0.0, 2.0, "color"},
        {"exposure", 0.0, -3.0, 3.0, "color"},
        {"temperature", 0.0, -1.0, 1.0, "color"},
        {"tint", 0.0, -1.0, 1.0, "color"},
        {"lutIntensity", 1.0, 0.0, 1.0, "color"},
        // lift / gamma / gain per channel
        {"liftR", 0.0, -0.5, 0.5, "grade"}, {"liftG", 0.0, -0.5, 0.5, "grade"}, {"liftB", 0.0, -0.5, 0.5, "grade"},
        {"gammaR", 1.0, 0.2, 3.0, "grade"}, {"gammaG", 1.0, 0.2, 3.0, "grade"}, {"gammaB", 1.0, 0.2, 3.0, "grade"},
        {"gainR", 1.0, 0.0, 2.0, "grade"}, {"gainG", 1.0, 0.0, 2.0, "grade"}, {"gainB", 1.0, 0.0, 2.0, "grade"},
        // image effects
        {"blur", 0.0, 0.0, 50.0, "effects"},
        {"sharpen", 0.0, 0.0, 3.0, "effects"},
        {"vignette", 0.0, 0.0, 1.0, "effects"},
        {"cropL", 0.0, 0.0, 0.9, "effects"}, {"cropR", 0.0, 0.0, 0.9, "effects"},
        {"cropT", 0.0, 0.0, 0.9, "effects"}, {"cropB", 0.0, 0.0, 0.9, "effects"},
        // audio (dB)
        {"volume", 0.0, -60.0, 12.0, "audio"},
        {"eqLow", 0.0, -18.0, 18.0, "audio"}, {"eqMid", 0.0, -18.0, 18.0, "audio"}, {"eqHigh", 0.0, -18.0, 18.0, "audio"},
        {"gate", -80.0, -80.0, -20.0, "audio"},
    };
    return kAll;
}

const Def* find(const QString& key)
{
    for (const Def& d : all())
        if (key == QLatin1String(d.key))
            return &d;
    return nullptr;
}

double defaultOf(const QString& key)
{
    const Def* d = find(key);
    return d ? d->def : 0.0;
}

double clamp(const QString& key, double value)
{
    const Def* d = find(key);
    return d ? std::clamp(value, d->min, d->max) : value;
}

} // namespace params
} // namespace dv
