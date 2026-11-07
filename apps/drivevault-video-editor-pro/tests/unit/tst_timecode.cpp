#include "core/Timecode.h"

#include <QtTest>

using namespace dv::timecode;

class TstTimecode : public QObject {
    Q_OBJECT
private slots:
    void parseIntegerRates()
    {
        QCOMPARE(parse("30"), (FrameRate{30, 1}));
        QCOMPARE(parse("24"), (FrameRate{24, 1}));
        QCOMPARE(parse(" 60 "), (FrameRate{60, 1}));
    }
    void parseNtscRatesAreExactRationals()
    {
        QCOMPARE(parse("29.97"), (FrameRate{30000, 1001}));
        QCOMPARE(parse("23.976"), (FrameRate{24000, 1001}));
        QCOMPARE(parse("59.94"), (FrameRate{60000, 1001}));
    }
    void parseFractionAndReduces()
    {
        QCOMPARE(parse("30000/1001"), (FrameRate{30000, 1001}));
        QCOMPARE(parse("60/2"), (FrameRate{30, 1}));
    }
    void parseRejectsGarbage()
    {
        for (const char* text : {"", "abc", "0", "-5", "1001", "30/0", "1/2/3", "30/-1"})
            QVERIFY2(!parse(QString::fromLatin1(text)).isValid(), text);
    }
    void nominalFps()
    {
        QCOMPARE(dv::timecode::nominalFps({30000, 1001}), 30);
        QCOMPARE(dv::timecode::nominalFps({24, 1}), 24);
        QCOMPARE(dv::timecode::nominalFps({0, 0}), 0);
    }
    void formatCountsFramesSecondsMinutesHours()
    {
        const FrameRate r{30, 1};
        QCOMPARE(format(0, r), QStringLiteral("00:00:00:00"));
        QCOMPARE(format(29, r), QStringLiteral("00:00:00:29"));
        QCOMPARE(format(30, r), QStringLiteral("00:00:01:00"));
        QCOMPARE(format(30 * 60, r), QStringLiteral("00:01:00:00"));
        QCOMPARE(format(30 * 3600 + 30 * 61 + 7, r), QStringLiteral("01:01:01:07"));
        QCOMPARE(format(-1, r), QStringLiteral("-00:00:00:01"));
    }
    void formatInvalidRate()
    {
        QCOMPARE(format(100, {0, 0}), QStringLiteral("00:00:00:00"));
    }
    void microsecondsDoNotDrift()
    {
        const FrameRate ntsc{30000, 1001};
        // 107892 frames (the classic "one hour" of 29.97 drop-frame) = 107892 * 1001 / 30000 s = 3599.9964 s.
        QCOMPARE(frameToMicroseconds(107892, ntsc), qint64(3599996400LL));
        QCOMPARE(frameToMicroseconds(30000, ntsc), qint64(1001000000LL)); // exactly 1001 s
        QCOMPARE(frameToMicroseconds(-30000, ntsc), qint64(-1001000000LL));
        QCOMPARE(frameToMicroseconds(0, ntsc), qint64(0));
        // Monotonic and consistent: 10 million frames never wraps or loses precision.
        QVERIFY(frameToMicroseconds(10'000'000, ntsc) > frameToMicroseconds(9'999'999, ntsc));
    }
    void labels()
    {
        QCOMPARE(label({30, 1}), QStringLiteral("30"));
        QCOMPARE(label({30000, 1001}), QStringLiteral("29.97"));
        QCOMPARE(label({24000, 1001}), QStringLiteral("23.976"));
        QCOMPARE(label({60000, 1001}), QStringLiteral("59.94"));
    }
};

QTEST_APPLESS_MAIN(TstTimecode)
#include "tst_timecode.moc"
