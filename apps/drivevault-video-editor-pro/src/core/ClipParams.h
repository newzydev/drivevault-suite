#pragma once

#include <QList>
#include <QMap>
#include <QPointF>
#include <QString>

#include <cstdint>

namespace dv {

// ---- keyframes ---------------------------------------------------------------------------------------------
struct Keyframe {
    int64_t frame = 0;                 // relative to the start of the clip
    double value = 0;
    QString interp = QStringLiteral("linear");   // linear | ease | hold  (applies from this key to the next)
    bool operator==(const Keyframe&) const = default;
};

// Interpolates between two values: t in [0, 1].
double interpolate(double a, double b, double t, const QString& mode);

// One animatable parameter of a clip: a constant value, or keyframes (sorted by frame).
struct ParamTrack {
    double value = 0;
    QList<Keyframe> keys;

    bool animated() const { return !keys.isEmpty(); }
    double at(int64_t clipFrame) const;
    bool operator==(const ParamTrack&) const = default;
};

// ---- parameter registry ---------------------------------------------------------------------------------------
namespace params {

struct Def {
    const char* key;
    double def;
    double min;
    double max;
    const char* group;     // transform | color | grade | effects | audio
};

const QList<Def>& all();
const Def* find(const QString& key);
double defaultOf(const QString& key);
double clamp(const QString& key, double value);

} // namespace params

// ---- titles / transitions / markers ------------------------------------------------------------------------
struct TitleData {
    QString text;
    QString font = QStringLiteral("Noto Sans Thai");
    int size = 72;                         // pixels at 1080p; scales with the canvas
    QString color = QStringLiteral("#FFFFFF");
    QString strokeColor = QStringLiteral("#000000");
    double strokeWidth = 0;                // pixels at 1080p
    bool shadow = false;
    bool bold = true;
    QString align = QStringLiteral("center");   // left | center | right
    double x = 0.5;                        // centre of the text box, fraction of the canvas
    double y = 0.5;
    bool operator==(const TitleData&) const = default;
};

struct TransitionSpec {
    QString type;                          // "" = none
    int64_t frames = 0;
    bool active() const { return !type.isEmpty() && frames > 0; }
    bool operator==(const TransitionSpec&) const = default;
};
// Types: in  -> dissolve | dip | fade | wipe | slide | zoom;   out -> fade | dip

struct MarkerData {
    QString id;
    int64_t frame = 0;
    QString name;
    QString color = QStringLiteral("#F59E0B");
    bool operator==(const MarkerData&) const = default;
};

} // namespace dv
