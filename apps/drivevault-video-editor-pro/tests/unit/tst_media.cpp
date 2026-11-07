#include "TestMedia.h"

#include "media/AudioReader.h"
#include "media/MediaCache.h"
#include "media/MediaLibrary.h"
#include "media/MediaProbe.h"
#include "media/VideoReader.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace dv;

namespace {
bool isReddish(QRgb c) { return qRed(c) > 150 && qGreen(c) < 90 && qBlue(c) < 90; }
bool isBluish(QRgb c) { return qBlue(c) > 150 && qRed(c) < 90 && qGreen(c) < 90; }
bool isGreenish(QRgb c) { return qGreen(c) > 90 && qRed(c) < 90 && qBlue(c) < 90; }
double rms(const std::vector<float>& v)
{
    double s = 0;
    for (float x : v) s += double(x) * x;
    return v.empty() ? 0 : std::sqrt(s / double(v.size()));
}
}

class TstMedia : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    QString p(const QString& name) const { return m_dir.filePath(name); }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(testmedia::makeTwoColorVideo(p("clip.mp4")));
        QVERIFY(testmedia::makeTwoColorVideo(p("silent.mp4"), "yellow", "white", false, "160x120", 25));
        QVERIFY(testmedia::makeSine(p("tone.wav")));
        QVERIFY(testmedia::makePng(p("green.png")));
        QVERIFY(QDir().mkpath(p("โฟลเดอร์ ไทย")));
        QVERIFY(QFile::copy(p("clip.mp4"), p("โฟลเดอร์ ไทย/คลิป ทดสอบ.mp4")));
        QFile bad(p("broken.mp4"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write(QByteArray(4096, '\x5a'));
        bad.close();
        QFile empty(p("empty.mp4"));
        QVERIFY(empty.open(QIODevice::WriteOnly));
    }

    void probeVideoWithAudio()
    {
        MediaInfo info;
        QString err;
        QVERIFY2(MediaProbe::probe(p("clip.mp4"), &info, &err), qPrintable(err));
        QCOMPARE(info.kind, QStringLiteral("video"));
        QCOMPARE(info.width, 320);
        QCOMPARE(info.height, 240);
        QCOMPARE(info.fpsNum, int64_t(30));
        QCOMPARE(info.fpsDen, int64_t(1));
        QVERIFY(info.hasAudio && info.hasVideo);
        QCOMPARE(info.audioRate, 48000);
        QVERIFY(qAbs(info.durationUs - 2000000) < 100000);
        QVERIFY(!info.variableFrameRate);
    }
    void probeSilentVideo25fps()
    {
        MediaInfo info;
        QVERIFY(MediaProbe::probe(p("silent.mp4"), &info, nullptr));
        QVERIFY(!info.hasAudio);
        QCOMPARE(info.fpsNum, int64_t(25));
    }
    void probeAudioImageAndThaiPath()
    {
        MediaInfo info;
        QVERIFY(MediaProbe::probe(p("tone.wav"), &info, nullptr));
        QCOMPARE(info.kind, QStringLiteral("audio"));
        QVERIFY(!info.hasVideo);
        QVERIFY(MediaProbe::probe(p("green.png"), &info, nullptr));
        QCOMPARE(info.kind, QStringLiteral("image"));
        QCOMPARE(info.width, 200);
        QVERIFY(MediaProbe::probe(p("โฟลเดอร์ ไทย/คลิป ทดสอบ.mp4"), &info, nullptr));
    }
    void probeRejectsBadFiles()
    {
        MediaInfo info;
        QString err;
        QVERIFY(!MediaProbe::probe(p("missing.mp4"), &info, &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!MediaProbe::probe(p("broken.mp4"), &info, &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(!MediaProbe::probe(p("empty.mp4"), &info, &err));
    }

    void videoReaderIsFrameAccurateAndSeeks()
    {
        VideoReader r;
        QString err;
        QVERIFY2(r.open(p("clip.mp4"), &err), qPrintable(err));
        const QSize s(160, 120);
        QVERIFY(isReddish(r.frameAt(0, s).pixel(80, 60)));
        QVERIFY(isReddish(r.frameAt(500000, s).pixel(80, 60)));
        QVERIFY(isReddish(r.frameAt(966000, s).pixel(80, 60)));      // last red frame (frame 29)
        QVERIFY(isBluish(r.frameAt(1000000, s).pixel(80, 60)));      // first blue frame (frame 30)
        QVERIFY(isBluish(r.frameAt(1900000, s).pixel(80, 60)));
        QVERIFY(isReddish(r.frameAt(200000, s).pixel(80, 60)));      // backwards seek
        QVERIFY(isBluish(r.frameAt(1500000, s).pixel(80, 60)));      // forward seek
        QCOMPARE(r.frameAt(1500000, s).size(), s);
    }
    void videoReaderStillImage()
    {
        VideoReader r;
        QVERIFY(r.open(p("green.png")));
        QVERIFY(r.isStill());
        QVERIFY(isGreenish(r.frameAt(0, QSize(100, 50)).pixel(50, 25)));
        QVERIFY(isGreenish(r.frameAt(9000000, QSize(100, 50)).pixel(50, 25)));
    }
    void videoReaderFailsCleanly()
    {
        VideoReader r;
        QString err;
        QVERIFY(!r.open(p("broken.mp4"), &err));
        QVERIFY(!r.open(p("tone.wav"), &err)); // no video stream
        QVERIFY(r.frameAt(0, QSize(10, 10)).isNull());
    }

    void audioReaderSequentialMatchesRandomAccess()
    {
        AudioReader seq(48000);
        QVERIFY(seq.open(p("tone.wav")));
        std::vector<float> a(9600 * 2), b(4800 * 2), c(4800 * 2);
        QVERIFY(seq.read(0, 9600, a.data()));
        AudioReader seq2(48000);
        QVERIFY(seq2.open(p("tone.wav")));
        QVERIFY(seq2.read(0, 4800, b.data()));
        QVERIFY(seq2.read(4800, 4800, c.data()));
        for (int i = 0; i < 4800 * 2; ++i) {
            QVERIFY(qAbs(a[size_t(i)] - b[size_t(i)]) < 1e-5f);
            QVERIFY(qAbs(a[size_t(i) + 9600] - c[size_t(i)]) < 1e-5f);
        }
        QVERIFY(rms(a) > 0.05);   // the sine has real content (ffmpeg sine amplitude = 1/8)

        AudioReader jump(48000);
        QVERIFY(jump.open(p("tone.wav")));
        std::vector<float> j(1000 * 2), ref(1000 * 2);
        QVERIFY(jump.read(60000, 1000, j.data()));
        AudioReader full(48000);
        QVERIFY(full.open(p("tone.wav")));
        std::vector<float> skip(60000 * 2);
        full.read(0, 60000, skip.data());
        QVERIFY(full.read(60000, 1000, ref.data()));
        for (size_t i = 0; i < j.size(); ++i)
            QVERIFY2(qAbs(j[i] - ref[i]) < 1e-3f, "random access differs from sequential decode");
    }
    void audioReaderSilenceOutsideMedia()
    {
        AudioReader r(48000);
        QVERIFY(r.open(p("tone.wav")));
        std::vector<float> past(2000 * 2, 1.f);
        r.read(48000 * 10, 2000, past.data());
        QCOMPARE(rms(past), 0.0);
        std::vector<float> lead(2000 * 2, 1.f);
        r.read(-1000, 2000, lead.data());   // first half silence, second half audio
        std::vector<float> firstHalf(lead.begin(), lead.begin() + 2000);
        QCOMPARE(rms(firstHalf), 0.0);
    }
    void audioReaderResamplesToProjectRate()
    {
        AudioReader r(44100);
        QVERIFY(r.open(p("clip.mp4")));
        std::vector<float> v(44100 * 2);
        QVERIFY(r.read(0, 44100, v.data()));
        QVERIFY(rms(v) > 0.05);
    }

    void peaksAndThumbnail()
    {
        std::atomic_bool cancel{false};
        mediacache::Peaks peaks;
        QVERIFY(mediacache::buildPeaks(p("tone.wav"), &peaks, cancel));
        QVERIFY(peaks.size() >= 190 && peaks.size() <= 220);   // 2 s at 100 peaks/s
        QVERIFY(peaks[100] > 20);

        const QString file = p("cache/peaks/x.peaks");
        QVERIFY(mediacache::savePeaks(file, peaks));
        mediacache::Peaks loaded;
        QVERIFY(mediacache::loadPeaks(file, &loaded));
        QCOMPARE(loaded, peaks);

        MediaInfo info;
        QVERIFY(MediaProbe::probe(p("clip.mp4"), &info, nullptr));
        const QString thumb = p("cache/thumbs/x.png");
        QVERIFY(mediacache::buildThumbnail(p("clip.mp4"), info, thumb, cancel));
        QImage img(thumb);
        QVERIFY(!img.isNull());
        QCOMPARE(img.width(), 192);   // 320x240 fitted into 256x144
        QVERIFY(isReddish(img.pixel(img.width() / 2, img.height() / 2)));

        cancel = true;
        QVERIFY(!mediacache::buildPeaks(p("tone.wav"), &peaks, cancel));
    }
    void cacheTrimRemovesOldestFirst()
    {
        QDir().mkpath(p("trim"));
        for (int i = 0; i < 5; ++i) {
            QFile f(p(QString("trim/f%1.bin").arg(i)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(1000, 'x'));
            f.close();
            f.setFileTime(QDateTime::currentDateTime().addSecs(-100 + i), QFileDevice::FileModificationTime);
        }
        QCOMPARE(mediacache::trim(p("trim"), 2500), qint64(3000));
        QVERIFY(!QFile::exists(p("trim/f0.bin")));
        QVERIFY(QFile::exists(p("trim/f4.bin")));
    }

    void libraryImportsAsynchronouslyAndBuildsDerivedData()
    {
        MediaLibrary lib;
        lib.setCacheDir(p("libcache"));
        QSignalSpy ready(&lib, &MediaLibrary::mediaReady);
        const QString id = lib.importFile(p("clip.mp4"));
        QVERIFY(!id.isEmpty());
        QCOMPARE(lib.statusText(id), QStringLiteral("importing"));
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 10000);
        lib.waitForIdle();
        QCOMPARE(lib.statusText(id), QStringLiteral("ready"));
        QVERIFY(lib.peaks(id) != nullptr);
        QVERIFY(!lib.peaks(id)->empty());
        const QModelIndex idx = lib.index(0);
        QVERIFY(!idx.data(MediaLibrary::ThumbnailRole).toString().isEmpty());
        QCOMPARE(idx.data(MediaLibrary::KindRole).toString(), QStringLiteral("video"));

        // Duplicate import is skipped, not added twice.
        QSignalSpy skipped(&lib, &MediaLibrary::importSkipped);
        QCOMPARE(lib.importFile(p("clip.mp4")), id);
        QCOMPARE(skipped.count(), 1);
        QCOMPARE(lib.count(), 1);
    }
    void filesChosenInTheDialogAreImportedWhateverTheirNames()
    {
        // The file dialog hands over QUrl values. Names with #, %, spaces and Thai letters must survive the trip (a '#' or '%'
        // used to be read as a URL fragment / escape and the import said "file not found" with an empty name).
        const QStringList names = { QStringLiteral("clip #1 50%.mp4"), QStringLiteral("แนะนำน้องใหม่ โครงการ #2.mp4"), QStringLiteral("a%20b.mp4") };
        MediaLibrary lib;
        lib.setCacheDir(p("libcache-names"));
        QSignalSpy failed(&lib, &MediaLibrary::importFailed);
        QVariantList urls;
        for (const QString& n : names) {
            QVERIFY(QFile::copy(p("clip.mp4"), p(n)));
            urls << QUrl::fromLocalFile(p(n));
        }
        urls << QVariant(QUrl::fromLocalFile(p(names.at(0))).toString());      // the same file as a plain string (QML `.toString()`): skipped as a duplicate
        QCOMPARE(lib.importUrls(urls), 4);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(lib.count(), 3);
        lib.waitForIdle();
        for (int i = 0; i < lib.count(); ++i)
            QCOMPARE(lib.index(i).data(MediaLibrary::KindRole).toString(), QStringLiteral("video"));

        // the selection can also arrive as a list inside the list (QList<QUrl> / QVariantList) - it used to become an empty name
        QVERIFY(QFile::copy(p("clip.mp4"), p("nested one.mp4")));
        QVERIFY(QFile::copy(p("clip.mp4"), p("nested two.mp4")));
        QCOMPARE(lib.importUrls(QVariantList{ QVariantList{ QUrl::fromLocalFile(p("nested one.mp4")) },
                                              QVariant::fromValue(QList<QUrl>{ QUrl::fromLocalFile(p("nested two.mp4")) }) }), 2);
        // exactly what the real file dialog delivers (seen in the log): a QVariant that holds a QVariant that holds the QUrl
        QVERIFY(QFile::copy(p("clip.mp4"), p("wrapped.mp4")));
        QVariant inner = QUrl::fromLocalFile(p("wrapped.mp4"));
        const QVariant wrapped(QMetaType::fromType<QVariant>(), &inner);          // QVariant::fromValue(QVariant) would not nest
        QVERIFY(wrapped.metaType() == QMetaType::fromType<QVariant>());
        QCOMPARE(lib.importUrls(QVariantList{ wrapped }), 1);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(lib.count(), 6);
    }
    void libraryReportsBrokenFilesAndDropsThem()
    {
        MediaLibrary lib;
        lib.setCacheDir(p("libcache2"));
        QSignalSpy failed(&lib, &MediaLibrary::importFailed);
        lib.importFile(p("broken.mp4"));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 10000);
        QVERIFY(!failed.first().at(1).toString().isEmpty());
        QCOMPARE(lib.count(), 0);
        lib.importFile(p("nonexistent.mp4"));
        QCOMPARE(failed.count(), 2);
        QVERIFY2(failed.last().at(1).toString().contains("nonexistent.mp4"), qPrintable(failed.last().at(1).toString()));      // the message names the path
    }
    void libraryLoadFlagsMissingAndRelinks()
    {
        MediaLibrary lib;
        lib.setCacheDir(p("libcache3"));
        MediaInfo info;
        QVERIFY(MediaProbe::probe(p("clip.mp4"), &info, nullptr));
        MediaRef ok{"m_ok", p("clip.mp4"), "clip.mp4", "clip.mp4", info};
        MediaRef moved{"m_moved", p("elsewhere/clip.mp4"), "elsewhere/clip.mp4", "clip.mp4", info};
        lib.load({ok, moved}, m_dir.path());
        QCOMPARE(lib.statusText("m_ok"), QStringLiteral("ready"));
        QCOMPARE(lib.statusText("m_moved"), QStringLiteral("missing"));
        QCOMPARE(lib.missingCount(), 1);

        QVERIFY(!lib.relink("m_moved", p("tone.wav")).isEmpty());          // different kind
        QCOMPARE(lib.relinkInFolder(m_dir.path()), 1);   // finds clip.mp4 by name below the folder
        QCOMPARE(lib.statusText("m_moved"), QStringLiteral("ready"));
        QCOMPARE(lib.missingCount(), 0);
        lib.waitForIdle();
    }
    void libraryFilterAndSort()
    {
        MediaLibrary lib;
        lib.setCacheDir(p("libcache4"));
        lib.importFile(p("tone.wav"));
        lib.importFile(p("green.png"));
        lib.importFile(p("clip.mp4"));
        QTRY_COMPARE_WITH_TIMEOUT(lib.busyCount(), 0, 10000);
        QCOMPARE(lib.count(), 3);
        MediaFilterModel view(&lib);
        view.setSortKey("name");
        view.setAscending(true);
        QCOMPARE(view.index(0, 0).data(MediaLibrary::NameRole).toString(), QStringLiteral("clip.mp4"));
        view.setFilterText("png");
        QCOMPARE(view.count(), 1);
        view.setFilterText("");
        view.setSortKey("name");
        view.setAscending(false);
        QCOMPARE(view.index(0, 0).data(MediaLibrary::NameRole).toString(), QStringLiteral("tone.wav"));
        view.setSortKey("kind");   // audio < image < video
        view.setAscending(true);
        QCOMPARE(view.index(0, 0).data(MediaLibrary::KindRole).toString(), QStringLiteral("audio"));
    }
};

QTEST_MAIN(TstMedia)
#include "tst_media.moc"
