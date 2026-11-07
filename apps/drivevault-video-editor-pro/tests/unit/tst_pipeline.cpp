// End-to-end tests of the editing pipeline on real media: import -> edit -> save -> reopen -> preview
// -> export -> read the exported file back and compare it with what the timeline should contain.

#include "TestMedia.h"

#include "core/AppPaths.h"
#include "export/ExportController.h"
#include "export/Exporter.h"
#include "media/AudioReader.h"
#include "media/MediaProbe.h"
#include "media/VideoReader.h"
#include "playback/AudioMixer.h"
#include "playback/Compositor.h"
#include "playback/PlaybackEngine.h"
#include "project/ProjectFile.h"
#include "project/ProjectManager.h"
#include "project/RecentProjectsModel.h"
#include "settings/SettingsManager.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace dv;

namespace {
bool near(QRgb c, int r, int g, int b, int tol = 60)
{
    return std::abs(qRed(c) - r) <= tol && std::abs(qGreen(c) - g) <= tol && std::abs(qBlue(c) - b) <= tol;
}
QRgb centre(const QImage& i) { return i.isNull() ? qRgb(1, 2, 3) : i.pixel(i.width() / 2, i.height() / 2); }
double rms(const std::vector<float>& v)
{
    double s = 0;
    for (float x : v) s += double(x) * x;
    return v.empty() ? 0 : std::sqrt(s / double(v.size()));
}
}

class TstPipeline : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    std::unique_ptr<SettingsManager> m_settings;
    std::unique_ptr<RecentProjectsModel> m_recents;
    std::unique_ptr<ProjectManager> m_pm;

    QString p(const QString& name) const { return m_dir.filePath(name); }

    QString importAndWait(const QString& file)
    {
        const QString id = m_pm->media()->importFile(file);
        [&] { QTRY_VERIFY_WITH_TIMEOUT(m_pm->media()->isReady(id), 15000); }();
        m_pm->media()->waitForIdle();
        return id;
    }
    QList<ClipData> onTrack(const QString& track) const
    {
        QList<ClipData> v;
        for (const ClipData& c : m_pm->timeline()->clips())
            if (c.trackId == track)
                v << c;
        std::sort(v.begin(), v.end(), [](const ClipData& a, const ClipData& b) { return a.startFrame < b.startFrame; });
        return v;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(testmedia::makeTwoColorVideo(p("redblue.mp4")));                                  // 2 s, audio 440 Hz
        QVERIFY(testmedia::makeTwoColorVideo(p("yellowwhite.mp4"), "yellow", "white", false, "160x120", 25));   // 2 s, 25 fps, silent
        QVERIFY(testmedia::makeSine(p("tone.wav"), 2, 880));
        QVERIFY(testmedia::makePng(p("green.png"), "green", "400x200"));
        paths::setOverrideRoot(p("app"));
        m_settings = std::make_unique<SettingsManager>();
        m_settings->load();
        m_settings->set("copyMediaToProject", false);        // these tests move / rename the original files
        m_recents = std::make_unique<RecentProjectsModel>(m_settings.get());
        m_recents->load();
        m_pm = std::make_unique<ProjectManager>(m_settings.get(), m_recents.get());
    }
    void cleanupTestCase()
    {
        m_pm.reset();
        paths::setOverrideRoot({});
    }

    // ------------------------------------------------------------------------------------------------
    void aCreateProjectImportAndEdit()
    {
        QCOMPARE(m_pm->createProject("Pipeline", p("projects"), 640, 360, "30", 48000), QString());
        const QString rb = importAndWait(p("redblue.mp4"));
        const QString yw = importAndWait(p("yellowwhite.mp4"));
        QVERIFY(!rb.isEmpty() && !yw.isEmpty());
        QCOMPARE(m_pm->media()->count(), 2);

        TimelineModel* tl = m_pm->timeline();
        QCOMPARE(tl->addMedia(rb, -1, 0), QString());                 // V1 0..60 + A1 0..60
        QCOMPARE(tl->mediaFrames(rb), qint64(60));
        QCOMPARE(tl->trimEnd(onTrack("V1")[0].id, 45), QString());    // keep red 1.0 s + blue 0.5 s
        QCOMPARE(onTrack("A1")[0].durationFrames, qint64(45));        // linked audio trimmed with it
        QCOMPARE(tl->addMedia(yw, -1, 45), QString());                // silent 25 fps clip right behind it
        QCOMPARE(tl->mediaFrames(yw), qint64(60));
        const QString yellowClip = onTrack("V1")[1].id;
        QCOMPARE(tl->trimEnd(yellowClip, 75), QString());             // 30 frames = 1 s of yellow
        QCOMPARE(tl->durationFrames(), qint64(75));
        QVERIFY(m_pm->dirty());

        // Undo / redo the last trim and the add.
        tl->undo();
        QCOMPARE(onTrack("V1")[1].durationFrames, qint64(60));
        tl->redo();
        QCOMPARE(onTrack("V1")[1].durationFrames, qint64(30));
    }

    void bSaveAndReopenKeepsMediaClipsAndRelativePaths()
    {
        QVERIFY(m_pm->save().isEmpty());
        QVERIFY(!m_pm->dirty());
        const QString path = m_pm->filePath();

        ProjectData onDisk;
        QString error;
        QVERIFY2(ProjectFile::load(path, &onDisk, &error), qPrintable(error));
        QCOMPARE(onDisk.media.size(), 2);
        QCOMPARE(onDisk.clips.size(), 3);                 // V1 x2 + A1 x1
        QVERIFY(!onDisk.media[0].path.isEmpty());

        const QList<ClipData> before = m_pm->timeline()->clips();
        m_pm->closeProject();
        QCOMPARE(m_pm->media()->count(), 0);
        QCOMPARE(m_pm->timeline()->clipCount(), 0);
        QVERIFY2(m_pm->openProject(path).isEmpty(), "reopen");
        QCOMPARE(m_pm->timeline()->clips(), before);
        QCOMPARE(m_pm->media()->count(), 2);
        QCOMPARE(m_pm->media()->missingCount(), 0);
        QVERIFY(!m_pm->dirty());
        QTRY_VERIFY_WITH_TIMEOUT(m_pm->media()->busyCount() == 0, 15000);
    }

    void cMovedMediaIsMissingUntilRelinked()
    {
        const QString moved = p("moved");
        QVERIFY(QDir().mkpath(moved));
        QVERIFY(QFile::rename(p("yellowwhite.mp4"), moved + "/yellowwhite.mp4"));
        QVERIFY(m_pm->save().isEmpty());
        const QString path = m_pm->filePath();
        m_pm->closeProject();
        QVERIFY(m_pm->openProject(path).isEmpty());
        QCOMPARE(m_pm->media()->missingCount(), 1);
        QVERIFY(m_pm->snapshot()->clips.size() == 3);
        QVERIFY(!Exporter::offlineMedia(*m_pm->snapshot()).isEmpty());

        // Export refuses while media is missing.
        ExportController ctl(m_pm.get());
        QVERIFY(!ctl.start("x", p("out"), "mp4", "720p", 0, 0, 75, 192, "").isEmpty());

        QCOMPARE(m_pm->media()->relinkInFolder(moved), 1);
        QCOMPARE(m_pm->media()->missingCount(), 0);
        QVERIFY(Exporter::offlineMedia(*m_pm->snapshot()).isEmpty());
        QVERIFY(m_pm->save().isEmpty());   // the project now points at the relinked file
        m_pm->media()->waitForIdle();
    }

    void dCompositorShowsTheEditedSequence()
    {
        const SnapshotPtr snap = m_pm->snapshot();
        QCOMPARE(snap->durationFrames, qint64(75));
        Compositor c;
        const QSize s(320, 180);
        QVERIFY(near(centre(c.render(*snap, 10, s)), 255, 0, 0));        // red
        QVERIFY(near(centre(c.render(*snap, 29, s)), 255, 0, 0));
        QVERIFY(near(centre(c.render(*snap, 30, s)), 0, 0, 255));        // blue starts at frame 30
        QVERIFY(near(centre(c.render(*snap, 44, s)), 0, 0, 255));
        QVERIFY(near(centre(c.render(*snap, 45, s)), 255, 255, 0));      // yellow clip begins
        QVERIFY(near(centre(c.render(*snap, 74, s)), 255, 255, 0));
        // The source is 4:3 inside a 16:9 canvas: pillar boxes are black.
        const QImage img = c.render(*snap, 10, s);
        QCOMPARE(qRed(img.pixel(2, 90)), 0);
        QCOMPARE(img.size(), s);
        // Past the end: black.
        QVERIFY(near(centre(c.render(*snap, 80, s)), 0, 0, 0, 10));
    }
    void eHiddenTracksAreNotComposited()
    {
        m_pm->tracks()->toggle(1, "visible");   // V1 hidden
        Compositor c;
        QVERIFY(near(centre(c.render(*m_pm->snapshot(), 10, QSize(160, 90))), 0, 0, 0, 10));
        m_pm->tracks()->toggle(1, "visible");
        QVERIFY(near(centre(c.render(*m_pm->snapshot(), 10, QSize(160, 90))), 255, 0, 0));
    }

    void fAudioMixerFollowsClipsAndMuting()
    {
        AudioMixer mixer;
        mixer.setSnapshot(m_pm->snapshot());
        std::vector<float> a(4800 * 2), b(4800 * 2);
        mixer.render(0, 4800, a.data());                          // inside the audio clip (0..1.5 s)
        QVERIFY(rms(a) > 0.05);
        mixer.render(48000 * 2, 4800, b.data());                  // 2.0 s: after the trimmed audio clip
        QCOMPARE(rms(b), 0.0);

        m_pm->tracks()->toggle(2, "muted");                       // A1
        mixer.setSnapshot(m_pm->snapshot());
        mixer.render(0, 4800, a.data());
        QCOMPARE(rms(a), 0.0);
        m_pm->tracks()->toggle(2, "muted");
    }

    void gExportProducesTheSequenceNotACopyOfTheSource()
    {
        ExportController ctl(m_pm.get());
        const QString folder = p("exports");
        const QString err = ctl.start("ทดสอบ ส่งออก", folder, "mp4", "720p", 0, 0, 80, 192, "");
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(ctl.state(), QStringLiteral("running"));
        ctl.waitForFinished();
        QVERIFY2(ctl.state() == "done", qPrintable(ctl.message()));
        const QString out = ctl.outputPath();
        QVERIFY(QFile::exists(out));
        QVERIFY(!QFile::exists(out + ".dvpart"));
        QVERIFY(!ctl.encoderUsed().isEmpty());
        qInfo() << "encoder:" << ctl.encoderUsed() << "throughput fps:" << ctl.throughputFps();

        // Output properties.
        MediaInfo info;
        QString error;
        QVERIFY2(MediaProbe::probe(out, &info, &error), qPrintable(error));
        QCOMPARE(info.width, 1280);
        QCOMPARE(info.height, 720);
        QCOMPARE(info.fpsNum * 1, int64_t(30) * info.fpsDen);
        QVERIFY(qAbs(info.durationUs - 2500000) < 100000);          // 75 frames at 30 fps
        QVERIFY(info.hasAudio);
        QCOMPARE(info.audioRate, 48000);
        QCOMPARE(info.audioCodec, QStringLiteral("aac"));
        QCOMPARE(info.videoCodec, QStringLiteral("h264"));
        QVERIFY(ctl.result().value("ok").toBool());

        // Picture: red, then blue, then yellow - the edited sequence, in order.
        VideoReader v;
        QVERIFY(v.open(out));
        const QSize s(160, 90);
        QVERIFY(near(centre(v.frameAt(500000, s)), 255, 0, 0, 80));
        QVERIFY(near(centre(v.frameAt(1250000, s)), 0, 0, 255, 80));
        QVERIFY(near(centre(v.frameAt(2000000, s)), 255, 255, 0, 80));

        // Sound: tone while the first clip plays, silence afterwards.
        AudioReader a(48000);
        QVERIFY(a.open(out));
        std::vector<float> early(4800 * 2), late(4800 * 2);
        a.read(24000, 4800, early.data());                     // 0.5 s
        a.read(48000 * 2, 4800, late.data());                  // 2.0 s
        QVERIFY2(rms(early) > 0.04, "exported audio is silent where the clip plays");
        QVERIFY2(rms(late) < 0.005, "exported audio is not silent after the audio clip ends");
    }

    void hPreviewAndExportAgree()
    {
        // The exported frame equals the previewed frame within codec tolerance (same compositor).
        const QString out = p("exports/ทดสอบ ส่งออก.mp4");
        VideoReader v;
        QVERIFY(v.open(out));
        Compositor c;
        const SnapshotPtr snap = m_pm->snapshot();
        const QSize s(320, 180);
        for (int frame : {5, 40, 60}) {
            const QImage preview = c.render(*snap, frame, s);
            const QImage exported = v.frameAt(timecode::frameToMicroseconds(frame, snap->fps) + 1000, s);
            QVERIFY(!exported.isNull());
            const QRgb a = centre(preview);
            const QRgb b = centre(exported);
            QVERIFY2(std::abs(qRed(a) - qRed(b)) < 50 && std::abs(qGreen(a) - qGreen(b)) < 50 && std::abs(qBlue(a) - qBlue(b)) < 50,
                     qPrintable(QString("frame %1 differs between preview and export").arg(frame)));
        }
    }

    void iOtherContainersAndExplicitEncoder()
    {
        ExportController ctl(m_pm.get());
        for (const QString container : {"mkv", "mov"}) {
            QVERIFY(ctl.start(QString("c_") + container, p("exports"), container, "custom", 400, 224, 50, 128, "").isEmpty());
            ctl.waitForFinished();
            QVERIFY2(ctl.state() == "done", qPrintable(ctl.message()));
            MediaInfo info;
            QVERIFY(MediaProbe::probe(ctl.outputPath(), &info, nullptr));
            QCOMPARE(info.width, 400);
            QCOMPARE(info.height, 224);
            ctl.reset();
        }
        QVERIFY(!ctl.start("bad", p("exports"), "mp4", "720p", 0, 0, 50, 128, "no_such_encoder").isEmpty()
                || [&] { ctl.waitForFinished(); return ctl.state() == "failed"; }());
    }

    void iiEveryDetectedEncoderProducesAValidFile()
    {
        ExportController ctl(m_pm.get());
        const QList<EncoderInfo> encoders = Exporter::availableEncoders();
        QVERIFY2(!encoders.isEmpty(), "no H.264 encoder available");
        for (const EncoderInfo& e : encoders) {
            qInfo() << "encoder" << e.id << e.label;
            QVERIFY(ctl.start("enc_" + e.id, p("exports"), "mp4", "custom", 320, 180, 60, 128, e.id).isEmpty());
            ctl.waitForFinished();
            QVERIFY2(ctl.state() == "done", qPrintable(e.id + ": " + ctl.message()));
            QCOMPARE(ctl.encoderUsed(), e.id);
            ctl.reset();
        }
    }

    void jExportCancelLeavesNoFile()
    {
        // A longer sequence so the cancel arrives while rendering.
        m_pm->timeline()->clearSelection();
        const QString id = m_pm->media()->idForPath(p("redblue.mp4"));
        for (int i = 0; i < 6; ++i)
            QCOMPARE(m_pm->timeline()->addMedia(id, 0, 200 + i * 60), QString());   // V2/A1 extra clips
        ExportController ctl(m_pm.get());
        QVERIFY(ctl.start("cancelled", p("exports"), "mp4", "1080p", 0, 0, 90, 192, "").isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(ctl.framesDone() > 3, 20000);
        ctl.cancel();
        ctl.waitForFinished();
        QCOMPARE(ctl.state(), QStringLiteral("cancelled"));
        QVERIFY(!QFile::exists(p("exports/cancelled.mp4")));
        QVERIFY(!QFile::exists(p("exports/cancelled.mp4.dvpart")));
        for (int i = 0; i < 6; ++i)
            m_pm->timeline()->undo();
    }
    void jjRangeAudioOnlyAndFrameExport()
    {
        TimelineModel* tl = m_pm->timeline();
        ExportController ctl(m_pm.get());

        // In / Out range: 10..39 inclusive = 30 frames = 1 s.
        tl->setInPoint(10);
        tl->setOutPoint(39);
        ctl.setRange("inout");
        QCOMPARE(ctl.rangeFrames(), (QVariantList{qint64(10), qint64(40)}));
        QVERIFY(ctl.start("range", p("exports"), "mp4", "custom", 320, 180, 60, 128, "").isEmpty());
        ctl.waitForFinished();
        QVERIFY2(ctl.state() == "done", qPrintable(ctl.message()));
        QCOMPARE(ctl.totalFrames(), qint64(30));
        MediaInfo info;
        QVERIFY(MediaProbe::probe(ctl.outputPath(), &info, nullptr));
        QVERIFY2(std::abs(info.durationUs - 1000000) < 100000, qPrintable(QString::number(info.durationUs)));
        QVERIFY(info.hasAudio);
        ctl.reset();

        // Audio only (m4a): no picture, the range is honoured, the sound is real.
        QVERIFY(ctl.start("sound", p("exports"), "m4a", "1080p", 0, 0, 60, 192, "").isEmpty());
        ctl.waitForFinished();
        QVERIFY2(ctl.state() == "done", qPrintable(ctl.message()));
        MediaInfo audio;
        QVERIFY(MediaProbe::probe(p("exports/sound.m4a"), &audio, nullptr));
        QVERIFY(!audio.hasVideo);
        QVERIFY(audio.hasAudio);
        QCOMPARE(audio.audioRate, 48000);
        QVERIFY2(std::abs(audio.durationUs - 1000000) < 100000, qPrintable(QString::number(audio.durationUs)));
        QVERIFY(!QFile::exists(p("exports/sound.m4a.dvpart")));
        ctl.reset();

        // Whole sequence again.
        ctl.setRange("all");
        QCOMPARE(ctl.rangeFrames(), (QVariantList{qint64(0), qint64(75)}));
        tl->clearRange();

        // A still frame with a title on top.
        QCOMPARE(tl->addTitle(-1, 0, "title"), QString());
        QCOMPARE(ctl.exportFrame("still", p("exports"), 5), QString());
        QImage png(p("exports/still.png"));
        QVERIFY(!png.isNull());
        QCOMPARE(png.size(), QSize(640, 360));
        int bright = 0;
        for (int y = 140; y < 220; y += 2)
            for (int x = 100; x < 540; x += 2)
                if (qGray(png.pixel(x, y)) > 235)
                    ++bright;
        QVERIFY2(bright > 50, "the title text should be visible in the exported frame");
        tl->undo();
    }

    void kExportRejectsEmptyTimelineAndBadNames()
    {
        ExportController ctl(m_pm.get());
        QVERIFY(!ctl.start("", p("exports"), "mp4", "720p", 0, 0, 75, 192, "").isEmpty());
        QCOMPARE(ctl.outputSize("1080p", 0, 0), QSize(1920, 1080));
        QCOMPARE(ctl.outputSize("720p", 0, 0), QSize(1280, 720));
        QCOMPARE(ctl.outputSize("custom", 641, 361), QSize(640, 360));
        QVERIFY(!ctl.estimatedSize("1080p", 0, 0, 75, 192, "mp4").isEmpty());
        QVERIFY(Exporter::defaultBitrateKbps(1920, 1080, 30, 100) > Exporter::defaultBitrateKbps(1920, 1080, 30, 0));

        m_pm->closeProject();
        QVERIFY(!ctl.start("x", p("exports"), "mp4", "720p", 0, 0, 75, 192, "").isEmpty());
    }

    void lPlaybackAdvancesInRealTimeAndStopsAtTheEnd()
    {
        QVERIFY(m_pm->openProject(p("projects/Pipeline/Pipeline.dvproj")).isEmpty());
        QVERIFY(m_pm->media()->missingCount() == 0);
        m_pm->media()->waitForIdle();
        m_settings->set("previewQuality", "quarter");
        PlaybackEngine engine(m_pm.get(), m_settings.get());
        QSignalSpy frames(&engine, &PlaybackEngine::frameReady);
        QSignalSpy ended(&engine, &PlaybackEngine::reachedEnd);

        // Seeking shows the right picture (no playback needed).
        m_pm->setPlayheadFrame(40);
        QTRY_VERIFY_WITH_TIMEOUT(!engine.lastImage().isNull(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(near(centre(engine.lastImage()), 0, 0, 255), 10000);
        m_pm->setPlayheadFrame(5);
        QTRY_VERIFY_WITH_TIMEOUT(near(centre(engine.lastImage()), 255, 0, 0), 10000);

        QCOMPARE(m_pm->playheadFrame(), qint64(5));
        const int framesBefore = frames.count();
        engine.play();
        QVERIFY(engine.playing());
        QElapsedTimer t;
        t.start();
        QTest::qWait(1000);
        const qint64 elapsedMs = t.elapsed();
        const qint64 advanced = m_pm->playheadFrame() - 5;
        qInfo() << "advanced" << advanced << "frames in" << elapsedMs << "ms; audio clock:" << engine.audioBackend()
                << "delivered" << (frames.count() - framesBefore) << "dropped" << engine.droppedFrames();
        // 30 fps: within ~20 % of real time.
        QVERIFY2(advanced > 0.8 * 0.03 * elapsedMs && advanced < 1.2 * 0.03 * elapsedMs + 3, "playback is not running in real time");
        QVERIFY(frames.count() - framesBefore >= 10);

        engine.pause();
        QVERIFY(!engine.playing());
        const qint64 stoppedAt = m_pm->playheadFrame();
        QTest::qWait(150);
        QCOMPARE(m_pm->playheadFrame(), stoppedAt);

        // Play to the end: it stops by itself at the end of the timeline.
        m_pm->setPlayheadFrame(55);
        engine.play();
        QTRY_VERIFY_WITH_TIMEOUT(ended.count() == 1, 10000);
        QVERIFY(!engine.playing());
        QCOMPARE(m_pm->playheadFrame(), m_pm->timeline()->durationFrames());
    }
    void mEditsRefreshThePreview()
    {
        PlaybackEngine engine(m_pm.get(), m_settings.get());
        m_pm->setPlayheadFrame(10);
        QTRY_VERIFY_WITH_TIMEOUT(near(centre(engine.lastImage()), 255, 0, 0), 10000);
        m_pm->tracks()->toggle(1, "visible");     // hide V1 -> preview turns black
        QTRY_VERIFY_WITH_TIMEOUT(near(centre(engine.lastImage()), 0, 0, 0, 10), 10000);
        m_pm->tracks()->toggle(1, "visible");
        QTRY_VERIFY_WITH_TIMEOUT(near(centre(engine.lastImage()), 255, 0, 0), 10000);
    }
};

QTEST_MAIN(TstPipeline)
#include "tst_pipeline.moc"
