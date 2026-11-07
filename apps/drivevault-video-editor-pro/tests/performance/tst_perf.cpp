// Performance measurements on 1080p material (numbers are printed; the thresholds only catch regressions
// of an order of magnitude, they are not a benchmark).

#include "../unit/TestMedia.h"

#include "core/AppPaths.h"
#include "export/ExportController.h"
#include "export/Exporter.h"
#include "media/VideoReader.h"
#include "playback/Compositor.h"
#include "playback/PlaybackEngine.h"
#include "project/ProjectManager.h"
#include "project/RecentProjectsModel.h"
#include "settings/SettingsManager.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace dv;

class TstPerf : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    std::unique_ptr<SettingsManager> m_settings;
    std::unique_ptr<RecentProjectsModel> m_recents;
    std::unique_ptr<ProjectManager> m_pm;
    QString m_clip;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_clip = m_dir.filePath("hd.mp4");
        // 6 s of 1080p30 testsrc2 with a tone (mpeg4 stands in for camera footage)
        QVERIFY(testmedia::runFfmpeg({"-f", "lavfi", "-i", "testsrc2=s=1920x1080:r=30:d=6", "-f", "lavfi", "-i",
                                      "sine=frequency=440:sample_rate=48000:duration=6", "-c:v", "mpeg4", "-q:v", "4", "-pix_fmt",
                                      "yuv420p", "-c:a", "aac", m_clip}));
        paths::setOverrideRoot(m_dir.filePath("app"));
        m_settings = std::make_unique<SettingsManager>();
        m_settings->load();
        m_recents = std::make_unique<RecentProjectsModel>(m_settings.get());
        m_pm = std::make_unique<ProjectManager>(m_settings.get(), m_recents.get());
        QCOMPARE(m_pm->createProject("Perf", m_dir.filePath("p"), 1920, 1080, "30", 48000), QString());
        const QString id = m_pm->media()->importFile(m_clip);
        QTRY_VERIFY_WITH_TIMEOUT(m_pm->media()->isReady(id), 20000);
        m_pm->media()->waitForIdle();
        QCOMPARE(m_pm->timeline()->addMedia(id, -1, 0), QString());     // 0..180
        QCOMPARE(m_pm->timeline()->addMedia(id, 0, 60), QString());     // V2 over it: two decoders per frame
    }
    void cleanupTestCase()
    {
        m_pm.reset();
        paths::setOverrideRoot({});
    }

    void sequentialRenderAt1080p()
    {
        Compositor c;
        const SnapshotPtr snap = m_pm->snapshot();
        c.render(*snap, 0, QSize(1920, 1080));
        QElapsedTimer t;
        t.start();
        const int frames = 90;
        for (int f = 0; f < frames; ++f)
            c.render(*snap, f, QSize(1920, 1080));
        const double fps = frames * 1000.0 / double(t.elapsed());
        qInfo().noquote() << QString("PERF sequential composite 1080p (1-2 layers): %1 fps").arg(fps, 0, 'f', 1);
        QVERIFY2(fps > 20.0, "composite throughput collapsed");
    }
    void randomSeekLatency()
    {
        VideoReader r;
        QVERIFY(r.open(m_clip));
        r.frameAt(0, QSize(960, 540));
        QElapsedTimer t;
        double worst = 0, total = 0;
        const int positions[] = {4500000, 800000, 3100000, 5200000, 1700000, 2600000, 400000, 4100000};
        for (int us : positions) {
            t.restart();
            QVERIFY(!r.frameAt(us, QSize(960, 540)).isNull());
            const double ms = double(t.nsecsElapsed()) / 1e6;
            worst = std::max(worst, ms);
            total += ms;
        }
        qInfo().noquote() << QString("PERF random seek: average %1 ms, worst %2 ms").arg(total / 8, 0, 'f', 1).arg(worst, 0, 'f', 1);
        QVERIFY2(worst < 1500.0, "a seek took longer than 1.5 s");
    }
    void realTimePlaybackAt1080p()
    {
        m_settings->set("previewQuality", "full");
        PlaybackEngine engine(m_pm.get(), m_settings.get());
        QSignalSpy frames(&engine, &PlaybackEngine::frameReady);
        m_pm->setPlayheadFrame(0);
        engine.refresh();   // the playhead did not move, so ask for the first frame explicitly
        QTRY_VERIFY_WITH_TIMEOUT(!engine.lastImage().isNull(), 10000);
        const int before = frames.count();
        engine.play();
        QElapsedTimer t;
        t.start();
        QTest::qWait(2000);
        const qint64 ms = t.elapsed();
        const qint64 advanced = m_pm->playheadFrame();
        engine.pause();
        const int delivered = frames.count() - before;
        qInfo().noquote() << QString("PERF playback 1080p full: playhead advanced %1 frames in %2 ms (%3 fps), delivered %4, dropped %5, clock %6")
                                 .arg(advanced).arg(ms).arg(advanced * 1000.0 / double(ms), 0, 'f', 1).arg(delivered).arg(engine.droppedFrames()).arg("audio");
        QVERIFY2(advanced > 0.8 * 0.03 * double(ms), "playback fell behind real time");
        QVERIFY(delivered > 20);
    }
    void exportThroughputAt1080p()
    {
        ExportController ctl(m_pm.get());
        QVERIFY(ctl.start("perf", m_dir.filePath("out"), "mp4", "1080p", 0, 0, 75, 192, "").isEmpty());
        ctl.waitForFinished(180000);
        QVERIFY2(ctl.state() == "done", qPrintable(ctl.message()));
        qInfo().noquote() << QString("PERF export 1080p %1 frames with %2: %3 fps (%4x real time)")
                                 .arg(ctl.totalFrames()).arg(ctl.encoderUsed()).arg(ctl.result().value("throughputFps").toDouble(), 0, 'f', 1)
                                 .arg(ctl.result().value("throughputFps").toDouble() / 30.0, 0, 'f', 2);
        QVERIFY(ctl.result().value("throughputFps").toDouble() > 10.0);
    }
};

QTEST_MAIN(TstPerf)
#include "tst_perf.moc"
