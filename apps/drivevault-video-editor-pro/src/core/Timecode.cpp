#include "Timecode.h"

#include <QStringList>
#include <numeric>

namespace dv::timecode {

int nominalFps(const FrameRate& rate)
{
    if (!rate.isValid())
        return 0;
    return int((rate.num + rate.den - 1) / rate.den);
}

QString format(int64_t frame, const FrameRate& rate)
{
    const int fps = nominalFps(rate);
    if (fps <= 0)
        return QStringLiteral("00:00:00:00");

    const bool negative = frame < 0;
    int64_t f = negative ? -frame : frame;
    const int64_t ff = f % fps;
    f /= fps;
    const int64_t ss = f % 60;
    f /= 60;
    const int64_t mm = f % 60;
    const int64_t hh = f / 60;

    return QStringLiteral("%1%2:%3:%4:%5")
        .arg(negative ? QStringLiteral("-") : QString())
        .arg(hh, 2, 10, QLatin1Char('0'))
        .arg(mm, 2, 10, QLatin1Char('0'))
        .arg(ss, 2, 10, QLatin1Char('0'))
        .arg(ff, 2, 10, QLatin1Char('0'));
}

int64_t frameToMicroseconds(int64_t frame, const FrameRate& rate)
{
    if (!rate.isValid())
        return 0;
    // frame * den / num seconds -> microseconds, rounded half away from zero.
    // Splitting frame into quotient and remainder keeps every intermediate product inside int64
    // (the remainder is < num, so remainder * den * 1e6 stays far below 2^63 for real frame rates).
    const bool negative = frame < 0;
    const int64_t f = negative ? -frame : frame;
    const int64_t perFrame = rate.den * 1000000;
    const int64_t whole = (f / rate.num) * perFrame;
    const int64_t rest = ((f % rate.num) * perFrame + rate.num / 2) / rate.num;
    const int64_t result = whole + rest;
    return negative ? -result : result;
}

int64_t frameToSample(int64_t frame, const FrameRate& rate, int sampleRate)
{
    if (!rate.isValid() || sampleRate <= 0)
        return 0;
    return (frame * rate.den * sampleRate + rate.num / 2) / rate.num;
}

int64_t sampleToFrame(int64_t sample, const FrameRate& rate, int sampleRate)
{
    if (!rate.isValid() || sampleRate <= 0)
        return 0;
    return sample >= 0 ? (sample * rate.num) / (rate.den * sampleRate) : -((-sample * rate.num + rate.den * sampleRate - 1) / (rate.den * sampleRate));
}

FrameRate parse(const QString& text)
{
    const QString t = text.trimmed();
    if (t.contains(QLatin1Char('/'))) {
        const QStringList parts = t.split(QLatin1Char('/'));
        bool okN = false;
        bool okD = false;
        const qlonglong n = parts.value(0).toLongLong(&okN);
        const qlonglong d = parts.value(1).toLongLong(&okD);
        if (parts.size() == 2 && okN && okD && n > 0 && d > 0) {
            const int64_t g = std::gcd<int64_t, int64_t>(n, d);
            return {n / g, d / g};
        }
        return {0, 0};
    }

    bool ok = false;
    const double v = t.toDouble(&ok);
    if (!ok || v <= 0.0 || v > 1000.0)
        return {0, 0};

    // The NTSC family are exact rationals over 1001.
    struct Known {
        double value;
        int64_t num;
    };
    static const Known kNtsc[] = {{23.976, 24000}, {29.97, 30000}, {47.952, 48000}, {59.94, 60000}, {119.88, 120000}};
    for (const Known& k : kNtsc) {
        if (qAbs(v - k.value) < 0.0051)
            return {k.num, 1001};
    }
    if (qAbs(v - double(qRound64(v))) < 1e-9)
        return {qRound64(v), 1};
    return {qRound64(v * 1000.0), 1000};
}

QString label(const FrameRate& rate)
{
    if (!rate.isValid())
        return QStringLiteral("--");
    if (rate.den == 1)
        return QString::number(rate.num);
    if (rate.den == 1001) {
        if (rate.num == 24000)
            return QStringLiteral("23.976");
        return QString::number(double(rate.num) / 1001.0, 'f', 2);
    }
    return QString::number(rate.toDouble(), 'f', 3);
}

} // namespace dv::timecode
