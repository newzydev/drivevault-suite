// Phase 5 tests: frame cache, proxy editing, hardware decoding and a long project. Numbers are printed so regressions are visible.

#include "TestMedia.h"

#include "core/AppPaths.h"
#include "export/Exporter.h"
#include "export/ProxyManager.h"
#include "media/HardwareDecode.h"
#include "media/MediaProbe.h"
#include "media/VideoReader.h"
#include "playback/Compositor.h"
#include "project/ProjectManager.h"
#include "project/RecentProjectsModel.h"
#include "settings/SettingsManager.h"

#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <cstdio>

using namespace dv;

namespace {
double meanAbsDiff(const QImage& a, const QImage& b)
{
    if (a.size() != b.size())
        return 1e9;
    const QImage x = a.convertToFormat(QImage::Format_RGB32), y = b.convertToFormat(QImage::Format_RGB32);
    double sum = 0;
    for (int row = 0; row < x.height(); ++row) {
        const QRgb* p = reinterpret_cast<const QRgb*>(x.constScanLine(row));
        const QRgb* q = reinterpret_cast<const QRgb*>(y.constScanLine(row));
        for (int i = 0; i < x.width(); ++i)
            sum += std::abs(qRed(p[i]) - qRed(q[i])) + std::abs(qGreen(p[i]) - qGreen(q[i])) + std::abs(qBlue(p[i]) - qBlue(q[i]));
    }
    return sum / (3.0 * x.width() * x.height());
}
void report(const char* what, double value, const char* unit) { std::fprintf(stderr, "PERF %-44s %10.2f %s\n", what, value, unit); }
} // namespace

class TstPhase5 : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    std::unique_ptr<SettingsManager> m_settings;
    std::unique_ptr<RecentProjectsModel> m_recents;
    std::unique_ptr<ProjectManager> m_pm;
    std::unique_ptr<ProxyManager> m_proxies;
    QString m_bigId;

    QString p(const QString& name) const { return m_dir.filePath(name); }
    QString importAndWait(const QString& file)
    {
        const QString id = m_pm->media()->importFile(file);
        [&] { QTRY_VERIFY_WITH_TIMEOUT(m_pm->media()->isReady(id), 20000); }();
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
        m_settings->set("copyMediaToProject", false);        // these tests move / rename the original files
        m_recents = std::make_unique<RecentProjectsModel>(m_settings.get());
        m_recents->load();
        m_pm = std::make_unique<ProjectManager>(m_settings.get(), m_recents.get());
        m_proxies = std::make_unique<ProxyManager>(m_pm.get(), m_settings.get());
        // 2.0 s of a 3840x2160 test pattern (moving, so the pictures differ from frame to frame), 30 fps
        QVERIFY(testmedia::runFfmpeg({"-f", "lavfi", "-i", "testsrc2=size=3840x2160:rate=30:duration=2", "-c:v", "mpeg4", "-q:v", "3",
                                      "-pix_fmt", "yuv420p", p("big.mp4")}));
        QCOMPARE(m_pm->createProject("Perf", p("projects"), 1280, 720, "30", 48000), QString());
        m_bigId = importAndWait(p("big.mp4"));
        QVERIFY(!m_bigId.isEmpty());
    }
    void cleanupTestCase()
    {
        m_proxies.reset();
        m_pm.reset();
        paths::setOverrideRoot({});
    }

    // ---- frame cache -------------------------------------------------------------------------------------------------
    void frameCacheServesRepeatedFramesAndNeverShowsStaleOnes()
    {
        QCOMPARE(m_pm->timeline()->addMedia(m_bigId, -1, 0), QString());
        const SnapshotPtr snap = m_pm->snapshot();
        QVERIFY(snap->revision != 0);
        const QSize size(640, 360);

        Compositor plain;
        Compositor cached;
        cached.setCacheBudget(256ll * 1024 * 1024);
        const QImage reference = plain.render(*snap, 20, size);
        QElapsedTimer t;
        t.start();
        const QImage first = cached.render(*snap, 20, size);
        const double missMs = double(t.nsecsElapsed()) / 1e6;
        t.restart();
        const QImage second = cached.render(*snap, 20, size);
        const double hitMs = double(t.nsecsElapsed()) / 1e6;
        report("frame render (cache miss)", missMs, "ms");
        report("frame render (cache hit)", hitMs, "ms");
        QCOMPARE(cached.cacheMisses(), quint64(1));
        QCOMPARE(cached.cacheHits(), quint64(1));
        QVERIFY(first == reference);                                 // cached pictures are identical to uncached ones
        QVERIFY(second == reference);
        QVERIFY2(hitMs < missMs / 3, "a cache hit should be far cheaper than rendering");
        QVERIFY(cached.isCached(*snap, 20, size));
        QVERIFY(!cached.isCached(*snap, 21, size));
        QVERIFY(!cached.isCached(*snap, 20, QSize(320, 180)));       // another size is another picture

        // an edit builds a new snapshot: the old frames must not be reused
        const QString clip = m_pm->timeline()->clips().first().id;
        QCOMPARE(m_pm->timeline()->setParam(clip, "opacity", 0.0, 0), QString());
        const SnapshotPtr edited = m_pm->snapshot();
        QVERIFY(edited->revision != snap->revision);
        const QImage after = cached.render(*edited, 20, size);
        QVERIFY2(after != reference, "stale frame served after an edit");
        QCOMPARE(qGray(after.pixel(100, 100)), 0);                   // opacity 0 -> black
        m_pm->timeline()->undo();

        // the budget is respected (LRU eviction)
        Compositor small;
        small.setCacheBudget(2 * size.width() * size.height() * 4 + 100);
        const SnapshotPtr s3 = m_pm->snapshot();
        for (int f = 0; f < 10; ++f)
            small.render(*s3, f, size);
        QVERIFY(small.cacheBytes() <= 2 * size.width() * size.height() * 4 + 100);
        QVERIFY(small.isCached(*s3, 9, size));                        // the newest stay, the oldest went
        QVERIFY(!small.isCached(*s3, 0, size));

        // scrubbing back and forth over a range is served from the cache after the first pass
        Compositor scrub;
        scrub.setCacheBudget(256ll * 1024 * 1024);
        t.restart();
        for (int f = 0; f < 30; ++f) scrub.render(*s3, f, size);
        const double pass1 = double(t.nsecsElapsed()) / 1e6;
        t.restart();
        for (int f = 29; f >= 0; --f) scrub.render(*s3, f, size);
        const double pass2 = double(t.nsecsElapsed()) / 1e6;
        report("30 frames first pass", pass1, "ms");
        report("30 frames backwards (cached)", pass2, "ms");
        QVERIFY(pass2 < pass1 / 5);
        m_pm->timeline()->undo();
        QCOMPARE(m_pm->timeline()->clipCount(), 0);
    }

    // ---- proxies -------------------------------------------------------------------------------------------------------------
    void proxyIsBuiltMatchesTheSourceAndOnlyTheSnapshotCarriesIt()
    {
        QCOMPARE(m_proxies->state(m_bigId), QStringLiteral("none"));
        QCOMPARE(m_proxies->proxyCount(), 0);
        QVERIFY(m_proxies->createProxy(m_bigId).isEmpty());
        QCOMPARE(m_proxies->state(m_bigId), QStringLiteral("building"));
        QVERIFY(!m_proxies->createProxy(m_bigId).isEmpty());          // already queued
        m_proxies->waitForIdle(120000);
        QCOMPARE(m_proxies->state(m_bigId), QStringLiteral("ready"));
        QCOMPARE(m_proxies->proxyCount(), 1);
        const QString proxy = m_proxies->proxyPathFor(p("big.mp4"));
        QVERIFY(!proxy.isEmpty());
        QVERIFY(!QFile::exists(proxy + ".dvpart"));

        MediaInfo src, px;
        QVERIFY(MediaProbe::probe(p("big.mp4"), &src, nullptr));
        QVERIFY(MediaProbe::probe(proxy, &px, nullptr));
        QVERIFY(px.width <= 960 && px.width > 0);
        QCOMPARE(px.width * src.height / src.width, px.height);       // same aspect ratio
        QCOMPARE(px.fpsNum * src.fpsDen, src.fpsNum * px.fpsDen);     // same frame rate
        QVERIFY2(std::abs(px.durationUs - src.durationUs) < 40000, qPrintable(QString::number(px.durationUs - src.durationUs)));
        QVERIFY(QFileInfo(proxy).size() < QFileInfo(p("big.mp4")).size());

        QCOMPARE(m_pm->timeline()->addMedia(m_bigId, -1, 0), QString());
        const SnapshotPtr snap = m_pm->snapshot();
        QCOMPARE(snap->clips.first().proxyPath, proxy);
        const QSize size(640, 360);
        double worst = 0;
        {                                                             // decoders are closed at the end of this block (Windows cannot delete open files)
            Compositor original;                                      // default: the original media (what export uses)
            Compositor viaProxy;
            viaProxy.setUseProxy(true);
            for (int f : {0, 1, 15, 30, 59}) {
                const QImage a = original.render(*snap, f, size);
                const QImage b = viaProxy.render(*snap, f, size);
                worst = std::max(worst, meanAbsDiff(a, b));
            }
        }
        report("proxy vs original, mean abs difference", worst, "/255");
        QVERIFY2(worst < 12.0, "the proxy picture must look like the original");

        // decode cost: 60 frames at 640x360 from the original vs from the proxy
        auto decodeMs = [&](const QString& file) {
            VideoReader r;
            if (!r.open(file))
                return -1.0;
            QElapsedTimer t;
            t.start();
            for (int f = 0; f < 60; ++f)
                if (r.frameAt(int64_t(f) * 33333 + 1000, size).isNull())
                    return -1.0;
            return double(t.nsecsElapsed()) / 1e6;
        };
        const double msOrig = decodeMs(p("big.mp4"));
        const double msProxy = decodeMs(proxy);
        report("decode 60 frames, original 4K", msOrig, "ms");
        report("decode 60 frames, proxy", msProxy, "ms");
        QVERIFY(msOrig > 0 && msProxy > 0);
        QVERIFY2(msProxy < msOrig, "the proxy should decode faster than the 4K original");

        // a media file that changed gets no proxy of the old file
        QCOMPARE(m_proxies->clearProxies() > 0, true);
        QCOMPARE(m_proxies->state(m_bigId), QStringLiteral("none"));
        QVERIFY(m_pm->snapshot()->clips.first().proxyPath.isEmpty());
        m_pm->timeline()->undo();
    }

    void proxyBuildCanBeCancelledAndLeavesNothing()
    {
        m_proxies->clearProxies();
        QVERIFY(testmedia::runFfmpeg({"-f", "lavfi", "-i", "testsrc2=size=1280x720:rate=30:duration=180", "-c:v", "mpeg4", "-q:v", "3",
                                      "-pix_fmt", "yuv420p", p("long.mp4")}));
        const QString id = importAndWait(p("long.mp4"));
        QVERIFY(m_proxies->createProxy(id).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(m_proxies->progress() > 0.0, 20000);
        m_proxies->cancel();
        m_proxies->waitForIdle(60000);
        QVERIFY(!m_proxies->busy());
        QCOMPARE(m_proxies->state(id), QStringLiteral("none"));
        QCOMPARE(m_proxies->proxyCount(), 0);
        QVERIFY(QDir(m_proxies->directory()).entryList(QDir::Files).isEmpty());
    }

    // ---- hardware decoding ---------------------------------------------------------------------------------------------------
    void hardwareDecodeGivesTheSamePictureAndFallsBackSafely()
    {
        // an H.264 file: the proxy encoder writes one
        m_proxies->cancel();
        m_proxies->waitForIdle(60000);
        m_proxies->clearProxies();
        m_proxies->setMaxWidth(3840);
        m_pm->media()->waitForIdle();
        QVERIFY(m_proxies->createProxy(m_bigId).isEmpty());
        m_proxies->waitForIdle(120000);
        const QString h264 = m_proxies->proxyPathFor(p("big.mp4"));
        QVERIFY(!h264.isEmpty());

        const auto decoders = hw::detectDecoders();
        QString names;
        bool d3d = false;
        for (const auto& d : decoders) {
            names += d.id + (d.available ? "(yes) " : "(no) ");
            d3d = d3d || (d.id == "d3d11va" && d.available);
        }
        std::fprintf(stderr, "PERF hardware decoders: %s\n", qPrintable(names));

        const QSize size(640, 360);
        auto run = [&](bool useHw, QList<QImage>* frames) {
            hw::setEnabled(useHw);
            VideoReader r;
            if (!r.open(h264))
                return -1.0;
            QElapsedTimer t;
            t.start();
            for (int f = 0; f < 60; ++f) {
                const QImage img = r.frameAt(int64_t(f) * 33333 + 1000, size);
                if (img.isNull())
                    return -1.0;
                if (frames && f % 15 == 0)
                    frames->append(img);
            }
            return double(t.nsecsElapsed()) / 1e6;
        };
        QList<QImage> swFrames, hwFrames;
        
        run(true, nullptr);                                           // warm-up: creates the GPU device once
        const quint64 hwBase = hw::hardwareFrames();
        const double swMs = run(false, &swFrames);
        QCOMPARE(hw::hardwareFrames(), hwBase);                       // switched off: nothing runs on the GPU
        const double hwMs = run(true, &hwFrames);
        hw::setEnabled(true);
        report("decode 60 frames, software", swMs, "ms");
        report("decode 60 frames, hardware setting on", hwMs, "ms");
        QVERIFY(swMs > 0 && hwMs > 0);
        for (int i = 0; i < swFrames.size(); ++i)
            QVERIFY2(meanAbsDiff(swFrames[i], hwFrames[i]) < 4.0, "hardware and software decoding must give the same picture");
        if (d3d) {
            report("frames decoded on the GPU", double(hw::hardwareFrames() - hwBase), "frames");
            QVERIFY2(hw::hardwareFrames() > hwBase, "D3D11VA is available but no frame was decoded on the GPU");
        } else {
            QCOMPARE(hw::hardwareFrames(), hwBase);                  // no device: honest software fallback
        }
        m_proxies->setMaxWidth(960);
        m_proxies->clearProxies();
    }

    // ---- a long project -------------------------------------------------------------------------------------------------------
    void aFortyMinuteProjectStaysResponsiveAndSurvivesSaveAndReopen()
    {
        m_pm->timeline()->clearSelection();
        while (m_pm->timeline()->canUndo())
            m_pm->timeline()->undo();
        QVERIFY(testmedia::makeTwoColorVideo(p("clip2s.mp4"), "red", "blue", true, "320x180", 30, 1));   // 2 s, 60 frames
        const QString id = importAndWait(p("clip2s.mp4"));
        TimelineModel* tl = m_pm->timeline();
        const int N = 1200;                                           // 1200 x 2 s = 40 minutes on V1 (+ A1)
        QElapsedTimer t;
        t.start();
        for (int i = 0; i < N; ++i)
            QCOMPARE(tl->addMedia(id, -1, qint64(i) * 60), QString());
        const double addMs = double(t.nsecsElapsed()) / 1e6;
        report("add 1200 clips (2400 with audio)", addMs, "ms");
        QCOMPARE(tl->clipCount(), N * 2);
        QCOMPARE(tl->durationFrames(), qint64(N) * 60);

        t.restart();
        const SnapshotPtr snap = m_pm->snapshot();
        const double snapMs = double(t.nsecsElapsed()) / 1e6;
        report("build snapshot of 2400 clips", snapMs, "ms");
        QVERIFY2(snapMs < 500, "snapshots are built on every edit and must stay cheap");

        // frames far into the timeline render as fast as at the start
        Compositor c;
        c.setCacheBudget(128ll * 1024 * 1024);
        t.restart();
        const QImage far = c.render(*snap, 40 * 60 * 30 - 100, QSize(320, 180));
        const double farMs = double(t.nsecsElapsed()) / 1e6;
        report("render a frame 40 minutes in", farMs, "ms");
        QVERIFY(!far.isNull());
        QVERIFY(farMs < 1500);
        t.restart();
        for (int i = 0; i < 40; ++i)
            c.render(*snap, (i * 7919) % (N * 60), QSize(320, 180));    // random scrubbing
        const double scrubMs = double(t.nsecsElapsed()) / 1e6 / 40;
        report("random scrub, per frame", scrubMs, "ms");
        QVERIFY(scrubMs < 400);

        // edits stay cheap with many clips
        t.restart();
        QCOMPARE(tl->splitAt(30 * 60 * 30 + 10), QString());
        QCOMPARE(tl->setParam(tl->clips().first().id, "opacity", 0.5, 0), QString());
        const double editMs = double(t.nsecsElapsed()) / 1e6;
        report("split + parameter edit with 2400 clips", editMs, "ms");
        QVERIFY(editMs < 1000);
        tl->undo();
        tl->undo();

        // save, close, reopen
        t.restart();
        QVERIFY(m_pm->save().isEmpty());
        const double saveMs = double(t.nsecsElapsed()) / 1e6;
        const QString path = m_pm->filePath();
        m_pm->closeProject();
        t.restart();
        QVERIFY(m_pm->openProject(path).isEmpty());
        const double openMs = double(t.nsecsElapsed()) / 1e6;
        report("save", saveMs, "ms");
        report("reopen", openMs, "ms");
        QCOMPARE(m_pm->timeline()->clipCount(), N * 2);
        QVERIFY(saveMs < 3000 && openMs < 3000);
    }
};

QTEST_MAIN(TstPhase5)
#include "tst_phase5.moc"
