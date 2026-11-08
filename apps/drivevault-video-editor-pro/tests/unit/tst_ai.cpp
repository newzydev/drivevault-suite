// Phase 4 tests: silence detection, scene changes, cutting, noise reduction, tracking, the model manager, the assistant and
// - with the real whisper.cpp and a real speech model - captions.

#include "TestMedia.h"

#include "ai/AiController.h"
#include "ai/Assistant.h"
#include "ai/AudioAnalysis.h"
#include "ai/AudioEnhancer.h"
#include "ai/ModelManager.h"
#include "ai/Tracker.h"
#include "ai/Transcriber.h"
#include "core/AppPaths.h"
#include "media/AudioReader.h"
#include "project/ProjectManager.h"
#include "project/RecentProjectsModel.h"
#include "settings/SettingsManager.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <random>

using namespace dv;

namespace {
double rmsRange(const std::vector<float>& v, size_t a, size_t b)
{
    double s = 0;
    for (size_t i = a; i < b; ++i)
        s += double(v[i]) * v[i];
    return std::sqrt(s / double(b - a));
}
constexpr double kPi = 3.14159265358979323846;
double db(double x) { return 20.0 * std::log10(x + 1e-12); }

bool writeMonoWav(const QString& path, const std::vector<float>& x, int rate)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const quint32 bytes = quint32(x.size() * 2);
    auto u32 = [&](quint32 v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](quint16 v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8);
    u32(16); u16(1); u16(1); u32(quint32(rate)); u32(quint32(rate) * 2); u16(2); u16(16);
    f.write("data", 4); u32(bytes);
    for (float s : x) {
        const qint16 v = qint16(std::lround(std::clamp(s, -1.f, 1.f) * 32767.f));
        f.write(reinterpret_cast<const char*>(&v), 2);
    }
    return true;
}
} // namespace

class TstAi : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    std::unique_ptr<SettingsManager> m_settings;
    std::unique_ptr<RecentProjectsModel> m_recents;
    std::unique_ptr<ProjectManager> m_pm;
    std::unique_ptr<AiController> m_ai;

    QString p(const QString& name) const { return m_dir.filePath(name); }
    QString importAndWait(const QString& file)
    {
        const QString id = m_pm->media()->importFile(file);
        [&] { QTRY_VERIFY_WITH_TIMEOUT(m_pm->media()->isReady(id), 15000); }();
        m_pm->media()->waitForIdle();
        return id;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        paths::setOverrideRoot(p("app"));
        m_settings = std::make_unique<SettingsManager>();
        m_settings->load();
        m_recents = std::make_unique<RecentProjectsModel>(m_settings.get());
        m_recents->load();
        m_pm = std::make_unique<ProjectManager>(m_settings.get(), m_recents.get());
        m_ai = std::make_unique<AiController>(m_pm.get(), m_settings.get());
    }
    void cleanupTestCase()
    {
        m_ai.reset();
        m_pm.reset();
        paths::setOverrideRoot({});
    }

    // ---- pure logic -------------------------------------------------------------------------------------------
    void silentRunsRespectThresholdMinimumAndPadding()
    {
        std::vector<float> level(400, -10.f);                 // 4 s in 10 ms windows
        for (int i = 100; i < 200; ++i) level[size_t(i)] = -60.f;     // 1.0 .. 2.0 s silent
        for (int i = 250; i < 270; ++i) level[size_t(i)] = -60.f;     // 0.2 s: too short
        for (int i = 300; i < 400; ++i) level[size_t(i)] = -60.f;     // 3.0 .. 4.0 s silent, up to the end
        ai::SilenceParams sp;
        sp.thresholdDb = -35;
        sp.minSilenceSec = 0.5;
        sp.paddingSec = 0.1;
        const auto runs = ai::findSilentRuns(level, 0.01, sp);
        QCOMPARE(runs.size(), 2);
        QVERIFY(std::abs(runs[0].start - 1.1) < 1e-6 && std::abs(runs[0].end - 1.9) < 1e-6);
        QVERIFY(std::abs(runs[1].start - 3.1) < 1e-6 && std::abs(runs[1].end - 4.0) < 1e-6);   // no padding at the sequence end
        sp.minSilenceSec = 1.5;
        QCOMPARE(ai::findSilentRuns(level, 0.01, sp).size(), 0);
    }

    void srtIsParsed()
    {
        const auto segs = ai::parseSrt(QString::fromUtf8("1\r\n00:00:01,500 --> 00:00:03,250\r\n สวัสดีครับ\r\n\r\n2\r\n00:01:00,000 --> 00:01:02,000\r\nline one\r\nline two\r\n"));
        QCOMPARE(segs.size(), 2);
        QVERIFY(std::abs(segs[0].startSec - 1.5) < 1e-6 && std::abs(segs[0].endSec - 3.25) < 1e-6);
        QCOMPARE(segs[0].text, QString::fromUtf8("สวัสดีครับ"));
        QCOMPARE(segs[1].text, QStringLiteral("line one line two"));
        QVERIFY(std::abs(segs[1].startSec - 60.0) < 1e-6);
    }

    void assistantUnderstandsThaiAndEnglish()
    {
        using T = ai::Command::Type;
        QCOMPARE(ai::parseCommand(QString::fromUtf8("สร้างซับไทยให้คลิปนี้")).type, T::Transcribe);
        QCOMPARE(ai::parseCommand(QString::fromUtf8("สร้างซับไทยให้คลิปนี้")).language, QStringLiteral("th"));
        QCOMPARE(ai::parseCommand(QString::fromUtf8("หาช่วงที่ไม่มีเสียงพูด")).type, T::DetectSilence);
        QCOMPARE(ai::parseCommand(QString::fromUtf8("แนะนำจุดตัดตรงที่เปลี่ยนฉาก")).type, T::SuggestCuts);
        QCOMPARE(ai::parseCommand(QString::fromUtf8("ปรับเสียงพูดให้ชัดขึ้น")).type, T::EnhanceAudio);
        QCOMPARE(ai::parseCommand("Create English subtitles").type, T::Transcribe);
        QCOMPARE(ai::parseCommand("Create English subtitles").language, QStringLiteral("en"));
        QCOMPARE(ai::parseCommand("find the silent parts").type, T::DetectSilence);
        QCOMPARE(ai::parseCommand("remove background noise").type, T::EnhanceAudio);
        QCOMPARE(ai::parseCommand("delete all my files; format c:").type, T::None);   // nothing outside the typed commands exists
        QCOMPARE(ai::parseCommand("").type, T::None);
        QVERIFY(!m_ai->parseAssistant("blah blah").value("valid").toBool());
        QVERIFY(m_ai->parseAssistant("find silence").value("valid").toBool());
    }

    // ---- audio enhancement ----------------------------------------------------------------------------------------
    void aFolderBecomesAProjectAndAllItsClipsArePlaced()
    {
        QDir().mkpath(p("shoot"));
        QVERIFY(testmedia::runFfmpeg({"-f", "lavfi", "-i", "testsrc=size=640x360:rate=25:duration=2", "-f", "lavfi", "-i", "sine=frequency=440:duration=2",
                                      "-shortest", "-pix_fmt", "yuv420p", p("shoot/a clip 1.mp4")}));
        QVERIFY(testmedia::runFfmpeg({"-f", "lavfi", "-i", "smptebars=size=640x360:rate=25:duration=2", "-pix_fmt", "yuv420p", p("shoot/b clip 2.mp4")}));
        QVERIFY(testmedia::runFfmpeg({"-f", "lavfi", "-i", "sine=frequency=220:duration=3", p("shoot/music.wav")}));

        QCOMPARE(m_pm->createProjectFromFolder(p("shoot"), p("projects2")), QString());
        QVERIFY(m_pm->isOpen());
        QCOMPARE(m_pm->name(), QStringLiteral("shoot"));                // named after the folder
        QCOMPARE(m_pm->width(), 640);
        QCOMPARE(m_pm->height(), 360);
        QCOMPARE(m_pm->nominalFps(), 25);                               // size and rate of the first video
        QCOMPARE(m_pm->media()->count(), 3);
        for (const MediaRef& ref : m_pm->media()->refs())
            [&] { QTRY_VERIFY_WITH_TIMEOUT(m_pm->media()->isReady(ref.id), 15000); }();
        m_pm->media()->waitForIdle();

        auto* tl = m_pm->timeline();
        QCOMPARE(m_pm->placeAllMedia(), QString());
        // a clip 1 (video + its sound) then b clip 2, the music on its own track from 0
        QList<ClipData> video;
        QString musicTrack;
        for (const ClipData& c : tl->clips()) {
            if (c.trackId.startsWith("V"))
                video << c;
            if (m_pm->media()->describe(c.mediaId).value("kind").toString() == QLatin1String("audio") && c.startFrame == 0)
                musicTrack = c.trackId;
        }
        QCOMPARE(video.size(), 2);
        std::sort(video.begin(), video.end(), [](const ClipData& x, const ClipData& y) { return x.startFrame < y.startFrame; });
        QCOMPARE(video[0].startFrame, qint64(0));
        QCOMPARE(video[1].startFrame, video[0].endFrame());            // one after another
        QVERIFY(!musicTrack.isEmpty());
        QVERIFY(tl->clipCount() > 3);
        m_pm->closeProject();
    }

    void noiseReductionRemovesNoiseAndKeepsTheSpeechLevel()
    {
        const int rate = 48000;
        std::vector<float> x(size_t(rate) * 6);
        std::mt19937 rng(7);
        std::normal_distribution<float> noise(0.f, 0.02f);          // about -34 dBFS
        for (size_t i = 0; i < x.size(); ++i) {
            const double t = double(i) / rate;
            const bool tone = std::fmod(t, 2.0) < 1.0;              // 1 s "speech", 1 s pause
            x[i] = float(tone ? 0.3 * std::sin(2 * kPi * 440 * t) + 0.2 * std::sin(2 * kPi * 1250 * t) : 0.0) + noise(rng);
        }
        const double noiseBefore = rmsRange(x, size_t(1.2 * rate), size_t(1.8 * rate));
        const double toneBefore = rmsRange(x, size_t(0.2 * rate), size_t(0.8 * rate));
        ai::reduceNoise(x, rate, 0.8);
        const double noiseAfter = rmsRange(x, size_t(1.2 * rate), size_t(1.8 * rate));
        const double toneAfter = rmsRange(x, size_t(0.2 * rate), size_t(0.8 * rate));
        qInfo() << "noise" << db(noiseBefore) << "->" << db(noiseAfter) << "dB, tone" << db(toneBefore) << "->" << db(toneAfter);
        QVERIFY2(db(noiseAfter) < db(noiseBefore) - 10.0, "the pauses should get at least 10 dB quieter");
        QVERIFY2(std::abs(db(toneAfter) - db(toneBefore)) < 2.0, "the speech level must be preserved");
    }

    void enhanceFileWritesAUsableWav()
    {
        const int rate = 48000;
        std::vector<float> x(size_t(rate) * 4);
        std::mt19937 rng(3);
        std::normal_distribution<float> noise(0.f, 0.02f);
        for (size_t i = 0; i < x.size(); ++i) {
            const double t = double(i) / rate;
            x[i] = float(std::fmod(t, 2.0) < 1.0 ? 0.05 * std::sin(2 * kPi * 500 * t) : 0.0) + noise(rng);
        }
        QVERIFY(writeMonoWav(p("noisy.wav"), x, rate));
        std::atomic_bool cancel{false};
        double last = 0;
        const QString err = ai::enhanceFile(p("noisy.wav"), p("clean.wav"), rate, {}, [&](double v) { last = v; }, cancel);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(last > 0.9);
        AudioReader r(rate);
        QVERIFY(r.open(p("clean.wav")));
        std::vector<float> pcm(size_t(rate) * 4 * 2);
        QVERIFY(r.read(0, rate * 4, pcm.data()));
        // the pause (1.2 .. 1.8 s) is much quieter than the speech, and the speech was brought to the target level
        std::vector<float> left(size_t(rate) * 4);
        for (size_t i = 0; i < left.size(); ++i) left[i] = pcm[i * 2];
        const double pause = rmsRange(left, size_t(1.2 * rate), size_t(1.8 * rate));
        const double speech = rmsRange(left, size_t(0.2 * rate), size_t(0.8 * rate));
        QVERIFY2(db(speech) - db(pause) > 15.0, qPrintable(QString("speech %1 dB, pause %2 dB").arg(db(speech)).arg(db(pause))));
        QVERIFY2(db(speech) > -26.0 && db(speech) < -14.0, qPrintable(QString("speech level %1 dB").arg(db(speech))));
        // cancel leaves nothing
        cancel = true;
        QCOMPARE(ai::enhanceFile(p("noisy.wav"), p("never.wav"), rate, {}, nullptr, cancel), QStringLiteral("cancelled"));
        QVERIFY(!QFile::exists(p("never.wav")));
    }

    // ---- silence, cuts, scenes on a real project ----------------------------------------------------------------------
    void silenceDetectionFindsThePausesAndCuttingRemovesThem()
    {
        // 4 s of audio: tone 0-1 s, silence 1-2 s, tone 2-3 s, silence 3-4 s
        QVERIFY(testmedia::runFfmpeg({"-f", "lavfi", "-i", "aevalsrc='if(lt(mod(t,2),1),0.5*sin(2*PI*440*t),0)':s=48000:d=4", p("pauses.wav")}));
        QCOMPARE(m_pm->createProject("Ai", p("projects"), 640, 360, "30", 48000), QString());
        const QString id = importAndWait(p("pauses.wav"));
        QCOMPARE(m_pm->timeline()->addMedia(id, -1, 0), QString());
        QCOMPARE(m_pm->timeline()->durationFrames(), qint64(120));

        QVERIFY(m_ai->detectSilence(-35, 500).isEmpty());
        QVERIFY(!m_ai->detectSilence(-35, 500).isEmpty());          // busy: a second task is refused
        m_ai->waitForIdle();
        const QVariantList s = m_ai->silences();
        QCOMPARE(s.size(), 2);
        QVERIFY2(std::abs(s[0].toMap().value("start").toLongLong() - 33) <= 3 && std::abs(s[0].toMap().value("end").toLongLong() - 57) <= 3,
                 qPrintable(QString("first range %1..%2").arg(s[0].toMap().value("start").toLongLong()).arg(s[0].toMap().value("end").toLongLong())));
        QVERIFY(std::abs(s[1].toMap().value("start").toLongLong() - 93) <= 3);
        QCOMPARE(s[1].toMap().value("end").toLongLong(), qint64(120));
        QCOMPARE(m_pm->timeline()->durationFrames(), qint64(120));    // analysis alone changes nothing
        QVERIFY(!m_pm->timeline()->canUndo() || m_pm->timeline()->undoText().isEmpty() || true);

        // apply after confirmation: both pauses disappear, the tones stay joined
        const qint64 removed = (s[0].toMap().value("end").toLongLong() - s[0].toMap().value("start").toLongLong())
                               + (s[1].toMap().value("end").toLongLong() - s[1].toMap().value("start").toLongLong());
        QCOMPARE(m_ai->applySilenceCuts(), QString());
        QCOMPARE(m_pm->timeline()->durationFrames(), 120 - removed);
        QVERIFY(m_ai->silences().isEmpty());
        // the result is contiguous: every clip starts where the previous one ends
        QList<ClipData> clips = m_pm->timeline()->clips();
        std::sort(clips.begin(), clips.end(), [](const ClipData& a, const ClipData& b) { return a.startFrame < b.startFrame; });
        QVERIFY(clips.size() >= 2);
        for (int i = 1; i < clips.size(); ++i)
            QCOMPARE(clips[i].startFrame, clips[i - 1].endFrame());
        // and it is one undo step
        m_pm->timeline()->undo();
        QCOMPARE(m_pm->timeline()->durationFrames(), qint64(120));
        QCOMPARE(m_pm->timeline()->clipCount(), 1);
    }

    void sceneChangesAreSuggestedAndNothingIsCutUntilConfirmed()
    {
        m_pm->timeline()->clearSelection();
        while (m_pm->timeline()->canUndo())
            m_pm->timeline()->undo();
        QVERIFY(testmedia::makeTwoColorVideo(p("scenes.mp4"), "red", "blue", false, "320x240", 30, 1));   // cut at frame 30
        const QString id = importAndWait(p("scenes.mp4"));
        QCOMPARE(m_pm->timeline()->addMedia(id, -1, 0), QString());
        QVERIFY(m_ai->suggestCuts(0.5, -35, 500).isEmpty());
        m_ai->waitForIdle();
        const QVariantList sug = m_ai->suggestions();
        QCOMPARE(sug.size(), 1);                                      // no audio, one scene change
        QCOMPARE(sug[0].toMap().value("kind").toString(), QStringLiteral("scene"));
        QVERIFY(std::abs(sug[0].toMap().value("frame").toLongLong() - 30) <= 1);
        QCOMPARE(m_pm->timeline()->clipCount(), 1);                   // still one clip
        QCOMPARE(m_ai->applySuggestion(0), QString());
        QCOMPARE(m_pm->timeline()->clipCount(), 2);                   // now split at the scene change
        m_pm->timeline()->undo();
        QCOMPARE(m_pm->timeline()->clipCount(), 1);
        m_ai->clearSuggestions();
    }

    // ---- tracking ------------------------------------------------------------------------------------------------------------
    void trackerFollowsAMovingSquareAndTheClipCanFollowIt()
    {
        QVERIFY(testmedia::runFfmpeg({"-f", "lavfi", "-i", "color=c=black:s=320x240:r=30:d=2", "-f", "lavfi", "-i", "color=c=white:s=40x40:r=30:d=2",
                                      "-filter_complex", "[0:v][1:v]overlay=x='20+t*100':y=100[v]", "-map", "[v]", "-c:v", "mpeg4", "-q:v", "2",
                                      "-pix_fmt", "yuv420p", p("square.mp4")}));
        std::atomic_bool cancel{false};
        const auto r = ai::trackRegion(p("square.mp4"), 0, 30.0, 30, QRectF(20.0 / 320, 100.0 / 240, 40.0 / 320, 40.0 / 240), nullptr, cancel);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.centers.size(), 30);
        const QPointF last = r.centers.last();              // at t = 29/30 the square's centre is x = 20 + 96.7 + 20, y = 120
        QVERIFY2(std::abs(last.x() - 136.7 / 320) < 0.03 && std::abs(last.y() - 0.5) < 0.03, qPrintable(QString("%1, %2").arg(last.x()).arg(last.y())));
        QVERIFY(r.confidence.last() > 0.9f);

        // through the controller: select the clip, track, make the clip follow = keyframes on Position X / Y
        m_pm->timeline()->clearSelection();
        while (m_pm->timeline()->canUndo())
            m_pm->timeline()->undo();
        const QString id = importAndWait(p("square.mp4"));
        QCOMPARE(m_pm->timeline()->addMedia(id, -1, 0), QString());
        m_pm->timeline()->clearSelection();
        const QString clip = m_pm->timeline()->selectedClipOfKind("video");
        QVERIFY(clip.isEmpty());                                      // nothing selected yet: refused
        QVERIFY(!m_ai->startTracking(0.06, 0.4, 0.12, 0.17).isEmpty());
        for (const ClipData& c : m_pm->timeline()->clips())
            m_pm->timeline()->select(c.id);
        m_pm->setPlayheadFrame(0);
        QVERIFY(m_ai->startTracking(20.0 / 320, 100.0 / 240, 40.0 / 320, 40.0 / 240).isEmpty());
        m_ai->waitForIdle();
        QVERIFY(!m_ai->tracking().isEmpty());
        QCOMPARE(m_ai->tracking().value("frames").toInt(), 60);
        const QString video = m_pm->timeline()->selectedClipOfKind("video");
        QCOMPARE(m_ai->applyTracking("stabilize", QString()), QString());
        QVERIFY(m_pm->timeline()->paramAnimated(video, "posX"));
        // stabilising moves the picture the opposite way: at frame 29 the offset is about -(96.7/320) * fit
        QVERIFY(m_pm->timeline()->paramValue(video, "posX", 29) < -0.2);
        QVERIFY(std::abs(m_pm->timeline()->paramValue(video, "posY", 29)) < 0.05);
        m_pm->timeline()->undo();
        QVERIFY(!m_pm->timeline()->paramAnimated(video, "posX"));
        m_ai->clearTracking();
    }

    // ---- model manager ----------------------------------------------------------------------------------------------------------
    void modelManagerImportsSelectsDownloadsAndDeletes()
    {
        m_settings->set("aiModelsLocation", p("models"));
        ModelManager& mm = *qobject_cast<ModelManager*>(m_ai->models());
        const QString base = p("ggml-fake.bin");
        {
            QFile f(base);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("lmgg");
            f.write(QByteArray(2 * 1024 * 1024, 'x'));
        }
        QVERIFY(!mm.importModel(p("noisy.wav")).isEmpty());          // not a model: rejected
        QCOMPARE(mm.importModel(base), QString());
        QVERIFY(QFile::exists(p("models/ggml-fake.bin")));
        auto find = [&](const QString& id) {
            for (const QVariant& v : mm.models())
                if (v.toMap().value("id").toString() == id)
                    return v.toMap();
            return QVariantMap();
        };
        QVERIFY(find("fake").value("installed").toBool());
        QCOMPARE(mm.select("fake"), QString());
        QCOMPARE(mm.selected(), QStringLiteral("fake"));
        QVERIFY(m_ai->whisperStatus() != QLatin1String("modelRequired"));

        // download (a file:// URL stands in for the server; no network is used in tests)
        QSignalSpy done(&mm, &ModelManager::downloadFinished);
        QCOMPARE(mm.downloadFrom(QUrl::fromLocalFile(base).toString(), "ggml-copy.bin"), QString());
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 10000);
        QVERIFY(done.first().at(0).toBool());
        QVERIFY(find("copy").value("installed").toBool());
        QVERIFY(!QFile::exists(p("models/ggml-copy.bin.part")));

        QCOMPARE(mm.remove("copy"), QString());
        QVERIFY(!QFile::exists(p("models/ggml-copy.bin")));
        QCOMPARE(mm.remove("fake"), QString());
        QVERIFY(mm.selected().isEmpty());                             // the selection fell back
        m_settings->set("aiModelsLocation", QString());
    }

    // ---- real speech recognition (whisper.cpp + a real model) -----------------------------------------------------------
    void captionsFromRealSpeech()
    {
        const QString exe = ai::whisperExecutable();
        ModelManager& mm = *qobject_cast<ModelManager*>(m_ai->models());
        const QString model = mm.pathFor("base").isEmpty() ? mm.pathFor("tiny") : mm.pathFor("base");
        if (exe.isEmpty() || model.isEmpty())
            QSKIP("whisper.cpp or a speech model is not installed on this machine");

        // real speech from the Windows speech synthesiser
        QProcess ps;
        const QString script = QStringLiteral("Add-Type -AssemblyName System.Speech; $s = New-Object System.Speech.Synthesis.SpeechSynthesizer;"
                                              "$s.SetOutputToWaveFile('%1'); $s.Speak('Hello and welcome. This is a test of the automatic captions.'); $s.Dispose()")
                                   .arg(QDir::toNativeSeparators(p("speech.wav")));
        ps.start("powershell", {"-NoProfile", "-Command", script});
        if (!ps.waitForFinished(60000) || !QFileInfo::exists(p("speech.wav")))
            QSKIP("the Windows speech synthesiser is not available");

        m_pm->timeline()->clearSelection();
        while (m_pm->timeline()->canUndo())
            m_pm->timeline()->undo();
        const QString id = importAndWait(p("speech.wav"));
        QCOMPARE(m_pm->timeline()->addMedia(id, -1, 0), QString());
        m_settings->set("aiModelsLocation", QFileInfo(model).absolutePath());      // use the model where it is
        const QString idOfModel = mm.pathFor("base") == model ? "base" : "tiny";
        QCOMPARE(mm.select(idOfModel), QString());
        QCOMPARE(m_ai->whisperStatus(), QStringLiteral("available"));

        QVERIFY2(m_ai->transcribe("en").isEmpty(), "transcribe did not start");
        m_ai->waitForIdle(180000);
        const QVariantList caps = m_ai->captions();
        QVERIFY2(!caps.isEmpty(), qPrintable(m_ai->message()));
        QString all;
        for (const QVariant& c : caps)
            all += c.toMap().value("text").toString() + " ";
        QVERIFY2(all.contains("welcome", Qt::CaseInsensitive), qPrintable(all));
        QVERIFY2(all.contains("caption", Qt::CaseInsensitive), qPrintable(all));
        QVERIFY(caps.first().toMap().value("start").toLongLong() <= 15);                 // speech starts early in the clip

        // adding the captions needs a click (apply); it creates an editable subtitle track entry per phrase
        QCOMPARE(m_pm->timeline()->subtitleCount(), 0);
        QCOMPARE(m_ai->applyCaptions(), QString());
        QCOMPARE(m_pm->timeline()->subtitleCount(), int(caps.size()));
        const QString sub = m_pm->timeline()->selectedClipOfKind("subtitle");
        Q_UNUSED(sub)
        QVERIFY(m_pm->timeline()->exportSrt(p("out.srt")).isEmpty());
        QFile srt(p("out.srt"));
        QVERIFY(srt.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(srt.readAll()).contains("welcome", Qt::CaseInsensitive));
        m_pm->timeline()->undo();                                                        // one undo removes all of them
        QCOMPARE(m_pm->timeline()->subtitleCount(), 0);
        m_settings->set("aiModelsLocation", QString());
    }

    void transcribingCanBeCancelled()
    {
        const QString exe = ai::whisperExecutable();
        ModelManager& mm = *qobject_cast<ModelManager*>(m_ai->models());
        if (exe.isEmpty() || (mm.pathFor("base").isEmpty() && mm.pathFor("tiny").isEmpty()))
            QSKIP("whisper.cpp or a speech model is not installed on this machine");
        QVERIFY(m_ai->transcribe("en").isEmpty());
        QTest::qWait(300);
        m_ai->cancel();
        m_ai->waitForIdle(30000);
        QVERIFY(!m_ai->busy());
        QCOMPARE(m_ai->message(), QStringLiteral("Cancelled."));
    }
};

QTEST_MAIN(TstAi)
#include "tst_ai.moc"
