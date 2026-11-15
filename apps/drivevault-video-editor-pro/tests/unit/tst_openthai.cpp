// OpenThai speech model: the feature extraction and the pause splitting are checked on generated audio; the recogniser itself runs on a real Thai
// recording when DV_TEST_THAI_WAV (16 kHz mono PCM16 wav) and the model folder are present (skipped otherwise: the 2.6 GB model is not in the repository).

#include "ai/AiController.h"
#include "ai/OpenThaiAsr.h"
#include "core/AppPaths.h"
#include "project/ProjectManager.h"
#include "project/RecentProjectsModel.h"
#include "settings/SettingsManager.h"

#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QFile>
#include <QtTest>

#include <cmath>

using namespace dv;
using namespace dv::ai;

namespace {
std::vector<float> readWav16kMono(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray all = f.readAll();
    const int dataAt = all.indexOf("data");
    if (dataAt < 0)
        return {};
    const char* p = all.constData() + dataAt + 8;
    const size_t n = size_t(all.size() - dataAt - 8) / 2;
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i)
        out[i] = float(*reinterpret_cast<const qint16*>(p + i * 2)) / 32768.f;
    return out;
}
std::vector<float> tone(double seconds, double freq, double amp)
{
    std::vector<float> x(size_t(seconds * 16000));
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = float(amp * std::sin(2.0 * 3.14159265358979 * freq * double(i) / 16000.0));
    return x;
}
} // namespace

class TstOpenThai : public QObject {
    Q_OBJECT
private slots:
    void featuresHaveTheRightShapeAndRespondToFrequency()
    {
        int frames = 0;
        const std::vector<float> low = openThaiLogMel(tone(1.0, 300, 0.5), &frames);
        QCOMPARE(frames, 101);                                    // 1 + samples / hop (centred frames)
        QCOMPARE(low.size(), size_t(128) * 101);
        int framesHigh = 0;
        const std::vector<float> high = openThaiLogMel(tone(1.0, 4000, 0.5), &framesHigh);
        auto argmaxBand = [&](const std::vector<float>& m) {
            int best = 0;
            for (int b = 1; b < 128; ++b)
                if (m[size_t(b) * 101 + 50] > m[size_t(best) * 101 + 50])
                    best = b;
            return best;
        };
        QVERIFY2(argmaxBand(low) < argmaxBand(high), "a higher tone must land in a higher mel band");
        QVERIFY(argmaxBand(low) < 40 && argmaxBand(high) > 80);
        // digital silence is the log of the guard value 2^-24 in every band
        const std::vector<float> silent = openThaiLogMel(std::vector<float>(16000, 0.f), &frames);
        QVERIFY(std::abs(double(silent[500]) - std::log(1.0 / 16777216.0)) < 1e-3);
    }
    void splitCutsAtPausesAndSkipsSilence()
    {
        QVERIFY(openThaiSplit(std::vector<float>(16000 * 5, 0.f)).empty());           // silence: nothing to recognise
        std::vector<float> x = tone(8.0, 440, 0.3);                                   // 8 s of sound
        x.insert(x.end(), 16000 * 1, 0.f);                                            // 1 s pause
        const std::vector<float> b = tone(8.0, 330, 0.3);                             // 8 s of sound
        x.insert(x.end(), b.begin(), b.end());
        const auto pieces = openThaiSplit(x);
        QCOMPARE(int(pieces.size()), 2);
        QVERIFY(pieces[0].second <= pieces[1].first + 16000);
        QVERIFY(pieces[0].second > size_t(16000 * 8) && pieces[0].second < size_t(16000 * 9.2));
        // no piece is longer than the limit, even without any pause
        const auto forced = openThaiSplit(tone(60.0, 440, 0.3), 20.0);
        QVERIFY(forced.size() >= 3);
        for (const auto& p : forced)
            QVERIFY((p.second - p.first) <= size_t(16000 * 21));
    }
    void modelFolderChecks()
    {
        QVERIFY(!openThaiModelComplete(QString()));
        QVERIFY(!openThaiModelComplete(QDir::tempPath()));
    }
    void recognisesRealThaiSpeech()
    {
        const QString wav = qEnvironmentVariable("DV_TEST_THAI_WAV");
        const QString dir = openThaiModelDir();
        if (!openThaiCompiledIn() || wav.isEmpty() || dir.isEmpty())
            QSKIP("needs an ONNX Runtime build, DV_TEST_THAI_WAV and the OpenThai model folder");
        const std::vector<float> pcm = readWav16kMono(wav);
        QVERIFY(pcm.size() > 16000);
        std::atomic_bool cancel{false};
        QElapsedTimer timer;
        timer.start();
        const OpenThaiResult r = openThaiTranscribe(pcm, dir, QStringLiteral("th"), {}, cancel);
        QVERIFY2(r.ok(), qPrintable(r.error));
        fprintf(stderr, "OPENTHAI %.1f s of audio in %.1f s\n", double(pcm.size()) / 16000.0, double(timer.elapsed()) / 1000.0);
        for (const auto& s : r.segments)
            fprintf(stderr, "OPENTHAI [%.2f - %.2f] %s\n", s.startSec, s.endSec, s.text.toUtf8().constData());        // UTF-8 bytes: read the redirected output as UTF-8
        QVERIFY(!r.segments.isEmpty());
        int thai = 0;
        for (const auto& s : r.segments)
            for (QChar c : s.text)
                thai += (c.unicode() >= 0x0E00 && c.unicode() <= 0x0E7F);
        QVERIFY2(thai > 20, "expected Thai text");
        // times are increasing and inside the audio
        double previous = -1;
        for (const auto& s : r.segments) {
            QVERIFY(s.startSec >= previous && s.endSec > s.startSec && s.endSec <= double(pcm.size()) / 16000.0 + 0.5);
            previous = s.startSec;
        }
        // cancelling stops the work
        std::atomic_bool stop{true};
        QVERIFY(openThaiTranscribe(pcm, dir, QStringLiteral("th"), {}, stop).cancelled);
    }

    // ---- through the controller: which engine, and captions from a real timeline --------------------------------------
    void engineChoiceFollowsLanguageAndSettings()
    {
        QTemporaryDir tmp;
        paths::setOverrideRoot(tmp.filePath("app"));
        SettingsManager settings;
        settings.load();
        RecentProjectsModel recents(&settings);
        ProjectManager pm(&settings, &recents);
        AiController ai(&pm, &settings);
        QString problem;
        const bool openThai = ai.openThaiStatus() == QLatin1String("available");
        // automatic: Thai -> OpenThai only when it is installed; everything else -> whisper.cpp
        QCOMPARE(ai.speechEngineFor("th", &problem), openThai ? QString("openthai") : QString("whisper"));
        QCOMPARE(ai.speechEngineFor("en", &problem), QString("whisper"));
        QCOMPARE(ai.speechEngineFor("auto", &problem), QString("whisper"));
        QVERIFY(settings.set("aiSpeechEngine", "whisper"));
        QCOMPARE(ai.speechEngineFor("th", &problem), QString("whisper"));
        QVERIFY(settings.set("aiSpeechEngine", "openthai"));
        QCOMPARE(ai.speechEngineFor("en", &problem), QString());                          // OpenThai cannot do English
        QVERIFY(problem.contains("Thai"));
        QCOMPARE(ai.speechEngineFor("th", &problem), openThai ? QString("openthai") : QString());
        if (!openThai)
            QVERIFY(!problem.isEmpty());
        paths::setOverrideRoot({});
    }
    void captionsFromARealTimelineWithOpenThai()
    {
        const QString wav = qEnvironmentVariable("DV_TEST_THAI_WAV");
        if (!openThaiCompiledIn() || wav.isEmpty() || openThaiModelDir().isEmpty())
            QSKIP("needs an ONNX Runtime build, DV_TEST_THAI_WAV and the OpenThai model folder");
        QTemporaryDir tmp;
        paths::setOverrideRoot(tmp.filePath("app"));
        SettingsManager settings;
        settings.load();
        RecentProjectsModel recents(&settings);
        ProjectManager pm(&settings, &recents);
        AiController ai(&pm, &settings);
        QVERIFY(settings.set("aiEnabled", true));
        QCOMPARE(pm.createProject("Thai", tmp.filePath("projects"), 640, 360, "30", 48000), QString());
        const QString media = pm.media()->importFile(wav);
        QVERIFY(!media.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(pm.media()->isReady(media), 15000);
        pm.media()->waitForIdle();
        QCOMPARE(pm.timeline()->addMedia(media, -1, 0), QString());
        QCOMPARE(ai.captionStatus("th"), QString("available"));
        QCOMPARE(ai.transcribe("th"), QString());
        ai.waitForIdle(180000);
        QVERIFY2(!ai.captions().isEmpty(), qPrintable(ai.message()));
        QVERIFY(ai.captions().first().toMap().value("text").toString().size() > 3);
        QCOMPARE(ai.applyCaptions(), QString());
        QVERIFY(pm.timeline()->subtitleCount() > 3);
        pm.timeline()->undo();                                                           // one undo step removes them all
        QCOMPARE(pm.timeline()->subtitleCount(), 0);
        paths::setOverrideRoot({});
    }
};

QTEST_MAIN(TstOpenThai)
#include "tst_openthai.moc"
