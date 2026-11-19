#include "MediaPolicy.h"
#include "MediaSaver.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QMap>
#include <QProcess>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace dv::br::media;

namespace {

struct Reply {
    int status = 200;
    QByteArray type, body;
    int delayMs = 0;
};

// a small web server on this computer: path -> answer; counts the requests and remembers the headers of the last one
class FakeWeb : public QTcpServer {
public:
    QMap<QString, Reply> files;
    QMap<QString, int> hits;
    QByteArray lastRequest;
    QHash<QTcpSocket*, QByteArray> pending;
    FakeWeb()
    {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = nextPendingConnection())
                connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    QByteArray& buf = pending[s];
                    buf += s->readAll();
                    if (!buf.contains("\r\n\r\n"))
                        return;
                    lastRequest = buf;
                    pending.remove(s);
                    const QString path = QString::fromLatin1(lastRequest.split(' ').value(1)).section(QLatin1Char('?'), 0, 0);
                    ++hits[path];
                    const Reply r = files.value(path, Reply{404, "text/plain", "missing", 0});
                    s->write("HTTP/1.1 " + QByteArray::number(r.status) + " X\r\nContent-Type: " + r.type + "\r\nContent-Length: " + QByteArray::number(r.body.size()) + "\r\nConnection: close\r\n\r\n" + r.body);
                    s->disconnectFromHost();
                });
        });
    }
    QString url(const QString& path) const { return QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path); }
};

QByteArray bigBlob(int size)
{
    QByteArray b(size, 0);
    for (int i = 0; i < size; ++i)
        b[i] = char((i * 31 + 7) & 0xFF);
    return b;
}

MediaSaver::Request req(const QString& url, const QString& kind, const QString& folder)
{
    MediaSaver::Request r;
    r.url = url;
    r.kind = kind;
    r.pageUrl = QStringLiteral("https://page.example/watch");
    r.userAgent = QStringLiteral("TestAgent/1.0");
    r.folder = folder;
    return r;
}

struct Outcome {
    bool done = false, ok = false;
    QString error, path;
};

// runs the event loop until the job ends
Outcome wait(MediaSaver& s, int id, int timeoutMs = 60000)
{
    Outcome o;
    QObject::connect(&s, &MediaSaver::finished, &s, [&o, id](int got, bool ok, const QString& err, const QString& path) {
        if (got == id) {
            o.done = true;
            o.ok = ok;
            o.error = err;
            o.path = path;
        }
    });
    QElapsedTimer t;
    t.start();
    while (!o.done && t.elapsed() < timeoutMs)
        QTest::qWait(20);
    return o;
}

} // namespace

class TstMedia : public QObject {
    Q_OBJECT
private slots:
    // ---- policy --------------------------------------------------------------------------------------------------------
    void requestsAreClassified_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("fromElement");
        QTest::addColumn<QString>("kind");
        QTest::newRow("mp4") << "https://cdn.example/v/clip.mp4" << false << "file";
        QTest::newRow("webm query") << "https://cdn.example/v/clip.webm?token=abc" << false << "file";
        QTest::newRow("mp3") << "https://cdn.example/a/song.MP3" << false << "file";
        QTest::newRow("hls") << "https://cdn.example/live/index.m3u8?x=1" << false << "hls";
        QTest::newRow("dash") << "https://cdn.example/v/manifest.mpd" << false << "dash";
        QTest::newRow("ts piece") << "https://cdn.example/live/seg12.ts" << false << "";
        QTest::newRow("m4s piece") << "https://cdn.example/v/chunk-3.m4s" << false << "";
        QTest::newRow("init piece") << "https://cdn.example/v/init.mp4" << false << "";
        QTest::newRow("segment mp4") << "https://cdn.example/v/seg-4.mp4" << false << "";
        QTest::newRow("range request") << "https://cdn.example/v/clip.mp4?range=0-999" << false << "";
        QTest::newRow("subtitle") << "https://cdn.example/v/clip.vtt" << false << "";
        QTest::newRow("key") << "https://cdn.example/v/enc.key" << false << "";
        QTest::newRow("page") << "https://example.com/watch" << false << "";
        QTest::newRow("media element without extension") << "https://cdn.example/stream/12345" << true << "file";
        QTest::newRow("media element on the root") << "https://cdn.example/" << true << "";
        QTest::newRow("not http") << "file:///C:/x.mp4" << false << "";
    }
    void requestsAreClassified()
    {
        QFETCH(QString, url);
        QFETCH(bool, fromElement);
        QFETCH(QString, kind);
        QCOMPARE(classifyMediaUrl(url, fromElement), kind);
    }
    void videoPlatformsAndSubscriptionServicesAreNeverOffered()
    {
        for (const char* h : {"youtube.com", "www.youtube.com", "rr3---sn-abc.googlevideo.com", "youtu.be", "www.netflix.com", "occ-0-1.nflxvideo.net", "disneyplus.com", "open.spotify.com", "tv.apple.com"})
            QVERIFY2(hostIsExcluded(QString::fromLatin1(h)), h);
        for (const char* h : {"example.com", "cdn.example.org", "notyoutube.com", "myyoutube.com.evil.test"})
            QVERIFY2(!hostIsExcluded(QString::fromLatin1(h)) || QString::fromLatin1(h).endsWith(QLatin1String("youtube.com")), h);
        QVERIFY(!hostIsExcluded(QStringLiteral("notyoutube.com")));
        QVERIFY(!hostIsExcluded(QStringLiteral("cdn.example.org")));
    }
    void fileNamesAreMadeFromTheAddress()
    {
        QCOMPARE(mediaFileName(QStringLiteral("https://c.example/v/My%20Clip.mp4?x=1"), QStringLiteral("file")), QStringLiteral("My Clip.mp4"));
        QCOMPARE(mediaFileName(QStringLiteral("https://c.example/stream/12345"), QStringLiteral("file"), QStringLiteral("video/webm; codecs=vp9")), QStringLiteral("12345.webm"));
        QCOMPARE(mediaFileName(QStringLiteral("https://c.example/stream/12345"), QStringLiteral("file"), QStringLiteral("application/x-weird")), QStringLiteral("12345.bin"));
        QCOMPARE(mediaFileName(QStringLiteral("https://c.example/stream/12345"), QStringLiteral("file")), QStringLiteral("12345"));          // type not known yet: no extension is made up
        QCOMPARE(mediaFileName(QStringLiteral("https://c.example/live/show.m3u8"), QStringLiteral("hls")), QStringLiteral("show.mp4"));
        QCOMPARE(mediaFileName(QStringLiteral("https://c.example/live/index.m3u8"), QStringLiteral("hls")), QStringLiteral("c-example-video.mp4"));
        QCOMPARE(mediaFileName(QStringLiteral("https://c.example/v/a%3Cb%3E.mp4"), QStringLiteral("file")), QStringLiteral("a_b_.mp4"));
    }
    void drmIsRefused()
    {
        QString why;
        QVERIFY(hlsDownloadable("#EXTM3U\n#EXT-X-VERSION:3\n#EXTINF:4,\nseg1.ts\n#EXT-X-ENDLIST\n", &why));
        QVERIFY2(hlsDownloadable("#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"https://k.example/key\"\n#EXTINF:4,\nseg1.ts\n", &why), "plain AES-128 (the key is given to every player) is not DRM");
        QVERIFY(hlsDownloadable("#EXTM3U\n#EXT-X-KEY:METHOD=NONE\n#EXTINF:4,\nseg1.ts\n", &why));
        QVERIFY(!hlsDownloadable("#EXTM3U\n#EXT-X-KEY:METHOD=SAMPLE-AES,URI=\"skd://x\",KEYFORMAT=\"com.apple.streamingkeydelivery\"\n#EXTINF:4,\nseg1.ts\n", &why));
        QVERIFY(why.contains(QLatin1String("DRM")));
        QVERIFY(!hlsDownloadable("#EXTM3U\n#EXT-X-SESSION-KEY:METHOD=SAMPLE-AES,URI=\"data:text/plain;base64,AAAA\"\n", &why));
        QVERIFY(!hlsDownloadable("#EXTM3U\n#EXT-X-KEY:METHOD=AES-128,URI=\"https://k/x\",KEYFORMAT=\"urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed\"\n", &why));
        QVERIFY(!hlsDownloadable("<html>not a playlist</html>", &why));
        QVERIFY(dashDownloadable("<?xml version=\"1.0\"?><MPD type=\"static\"><Period/></MPD>", &why));
        QVERIFY(!dashDownloadable("<MPD><Period><AdaptationSet><ContentProtection schemeIdUri=\"urn:uuid:edef8ba9\"/></AdaptationSet></Period></MPD>", &why));
        QVERIFY(why.contains(QLatin1String("DRM")));
        QVERIFY(!dashDownloadable("<html/>", &why));
        QCOMPARE(hlsDurationSeconds("#EXTM3U\n#EXTINF:4.5,\na.ts\n#EXTINF:3,\nb.ts\n#EXT-X-ENDLIST\n"), 7.5);
        QCOMPARE(hlsDurationSeconds("#EXTM3U\n#EXTINF:4.5,\na.ts\n"), 0.0);                         // live: unknown
        QCOMPARE(hlsDurationSeconds("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nlow.m3u8\n#EXT-X-ENDLIST\n"), 0.0);
    }

    // ---- saving a file -----------------------------------------------------------------------------------------------------
    void aFileIsSavedAndNeverOverwritten()
    {
        QTemporaryDir dir;
        FakeWeb web;
        QVERIFY(web.isListening());
        const QByteArray data = bigBlob(300000);
        web.files[QStringLiteral("/v/clip.mp4")] = {200, "video/mp4", data, 0};
        MediaSaver s;
        QSignalSpy started(&s, &MediaSaver::started), progress(&s, &MediaSaver::progress);
        const int id = s.start(req(web.url(QStringLiteral("/v/clip.mp4?token=1")), QStringLiteral("file"), dir.path()));
        QVERIFY(id > 0);
        const Outcome o = wait(s, id);
        QVERIFY2(o.done && o.ok, qPrintable(o.error));
        QCOMPARE(QFileInfo(o.path).fileName(), QStringLiteral("clip.mp4"));
        QFile f(o.path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), data);
        QVERIFY(!QFileInfo::exists(o.path + QStringLiteral(".part")));
        QCOMPARE(started.count(), 1);
        QVERIFY(progress.count() >= 1);
        const QString head = QString::fromLatin1(web.lastRequest);                      // header names are case insensitive
        QVERIFY2(head.contains(QLatin1String("referer: https://page.example/watch"), Qt::CaseInsensitive), qPrintable(head));
        QVERIFY2(head.contains(QLatin1String("testagent/1.0"), Qt::CaseInsensitive), qPrintable(head));
        const int id2 = s.start(req(web.url(QStringLiteral("/v/clip.mp4")), QStringLiteral("file"), dir.path()));
        const Outcome o2 = wait(s, id2);
        QVERIFY(o2.ok);
        QCOMPARE(QFileInfo(o2.path).fileName(), QStringLiteral("clip (1).mp4"));                     // the first file is untouched
        QCOMPARE(QFileInfo(o.path).size(), qint64(data.size()));
    }
    void anExtensionComesFromTheContentType()
    {
        QTemporaryDir dir;
        FakeWeb web;
        web.files[QStringLiteral("/stream/777")] = {200, "video/webm", bigBlob(2000), 0};
        MediaSaver s;
        const Outcome o = wait(s, s.start(req(web.url(QStringLiteral("/stream/777")), QStringLiteral("file"), dir.path())));
        QVERIFY2(o.ok, qPrintable(o.error));
        QCOMPARE(QFileInfo(o.path).fileName(), QStringLiteral("777.webm"));
    }
    void errorsLeaveNoFile()
    {
        QTemporaryDir dir;
        FakeWeb web;
        web.files[QStringLiteral("/gone.mp4")] = {404, "text/plain", "nope", 0};
        web.files[QStringLiteral("/page.mp4")] = {200, "text/html", "<html>sorry</html>", 0};
        web.files[QStringLiteral("/empty.mp4")] = {200, "video/mp4", "", 0};
        MediaSaver s;
        const Outcome a = wait(s, s.start(req(web.url(QStringLiteral("/gone.mp4")), QStringLiteral("file"), dir.path())));
        QVERIFY(a.done && !a.ok);
        QVERIFY2(a.error.contains(QLatin1String("404")), qPrintable(a.error));
        const Outcome b = wait(s, s.start(req(web.url(QStringLiteral("/page.mp4")), QStringLiteral("file"), dir.path())));
        QVERIFY(b.done && !b.ok);
        QVERIFY2(b.error.contains(QLatin1String("media file")), qPrintable(b.error));
        const Outcome c = wait(s, s.start(req(web.url(QStringLiteral("/empty.mp4")), QStringLiteral("file"), dir.path())));
        QVERIFY(c.done && !c.ok);
        QVERIFY2(QDir(dir.path()).entryList(QDir::Files).isEmpty(), "a failed download must leave nothing behind (not even a .part file)");
    }
    void onlyWebAddressesAreAccepted()
    {
        QTemporaryDir dir;
        MediaSaver s;
        QCOMPARE(s.start(req(QStringLiteral("file:///C:/Windows/notepad.exe"), QStringLiteral("file"), dir.path())), 0);
        QVERIFY(s.lastError().contains(QLatin1String("http")));
        QCOMPARE(s.start(req(QStringLiteral("ftp://x/y.mp4"), QStringLiteral("file"), dir.path())), 0);
    }
    void aDownloadCanBeCancelled()
    {
        QTemporaryDir dir;
        QTcpServer slow;                                           // sends the head and some bytes, then waits
        QVERIFY(slow.listen(QHostAddress::LocalHost));
        QObject::connect(&slow, &QTcpServer::newConnection, &slow, [&slow] {
            QTcpSocket* c = slow.nextPendingConnection();
            QObject::connect(c, &QTcpSocket::readyRead, c, [c] { c->readAll(); c->write("HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\nContent-Length: 10000000\r\n\r\n"); c->write(QByteArray(50000, 'x')); c->flush(); });
        });
        MediaSaver s;
        QSignalSpy progress(&s, &MediaSaver::progress);
        const int id = s.start(req(QStringLiteral("http://127.0.0.1:%1/big.mp4").arg(slow.serverPort()), QStringLiteral("file"), dir.path()));
        QVERIFY(id > 0);
        QElapsedTimer t;
        t.start();
        while (progress.count() == 0 && t.elapsed() < 10000)
            QTest::qWait(20);
        QSignalSpy ended(&s, &MediaSaver::finished);              // the end may come at once, inside cancel()
        s.cancel(id);
        QVERIFY(ended.count() == 1 || ended.wait(10000));
        QVERIFY(!ended.at(0).at(1).toBool());
        QCOMPARE(ended.at(0).at(2).toString(), QStringLiteral("cancelled"));
        QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
    }

    // ---- saving a stream (FFmpeg) --------------------------------------------------------------------------------------------------
    void aStreamWithDrmIsRefusedAndFfmpegIsNeverStarted()
    {
        QTemporaryDir dir;
        FakeWeb web;
        web.files[QStringLiteral("/protected.m3u8")] = {200, "application/vnd.apple.mpegurl", "#EXTM3U\n#EXT-X-KEY:METHOD=SAMPLE-AES,URI=\"skd://k\",KEYFORMAT=\"com.apple.streamingkeydelivery\"\n#EXTINF:4,\nseg1.ts\n", 0};
        web.files[QStringLiteral("/seg1.ts")] = {200, "video/mp2t", bigBlob(1000), 0};
        MediaSaver s;
        s.setFfmpegPath(QStringLiteral("C:/does/not/exist/ffmpeg.exe"));        // even a working FFmpeg must not be asked
        const Outcome o = wait(s, s.start(req(web.url(QStringLiteral("/protected.m3u8")), QStringLiteral("hls"), dir.path())));
        QVERIFY(o.done && !o.ok);
        QVERIFY2(o.error.contains(QLatin1String("DRM")), qPrintable(o.error));
        QCOMPARE(web.hits.value(QStringLiteral("/seg1.ts")), 0);
        QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
    }
    void aStreamNeedsFfmpegAndSaysSo()
    {
        QTemporaryDir dir;
        FakeWeb web;
        web.files[QStringLiteral("/ok.m3u8")] = {200, "application/vnd.apple.mpegurl", "#EXTM3U\n#EXTINF:4,\nseg1.ts\n#EXT-X-ENDLIST\n", 0};
        MediaSaver s;
        s.setFfmpegPath(QStringLiteral("C:/does/not/exist/ffmpeg.exe"));
        const Outcome o = wait(s, s.start(req(web.url(QStringLiteral("/ok.m3u8")), QStringLiteral("hls"), dir.path())));
        QVERIFY(o.done && !o.ok);
        QVERIFY2(o.error.contains(QLatin1String("FFmpeg")), qPrintable(o.error));
        QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
    }
    void anHlsStreamIsCopiedIntoAnMp4()
    {
        const QString ffmpeg = MediaSaver::findFfmpeg(QCoreApplication::applicationDirPath());
        if (ffmpeg.isEmpty())
            QSKIP("FFmpeg is not available here");
        QTemporaryDir src, out;
        // make a 3 second test stream: H.264 video in transport stream pieces of about 1 second (the usual HLS)
        QProcess gen;
        gen.setWorkingDirectory(src.path());
        gen.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                           QStringLiteral("testsrc=size=160x90:rate=10:duration=3"), QStringLiteral("-c:v"), QStringLiteral("libopenh264"), QStringLiteral("-f"), QStringLiteral("hls"), QStringLiteral("-hls_time"), QStringLiteral("1"),
                           QStringLiteral("-hls_list_size"), QStringLiteral("0"), QStringLiteral("-hls_segment_filename"), QStringLiteral("seg%d.ts"), QStringLiteral("index.m3u8")});
        QVERIFY(gen.waitForFinished(60000));
        QCOMPARE(gen.exitCode(), 0);
        FakeWeb web;
        QVERIFY(web.isListening());
        for (const QString& name : QDir(src.path()).entryList(QDir::Files)) {
            QFile f(src.filePath(name));
            QVERIFY(f.open(QIODevice::ReadOnly));
            web.files[QStringLiteral("/live/") + name] = {200, name.endsWith(QLatin1String(".m3u8")) ? QByteArray("application/vnd.apple.mpegurl") : QByteArray("video/mp2t"), f.readAll(), 0};
        }
        QVERIFY(web.files.contains(QStringLiteral("/live/index.m3u8")));
        MediaSaver s;
        QSignalSpy progress(&s, &MediaSaver::progress);
        const int id = s.start(req(web.url(QStringLiteral("/live/index.m3u8")), QStringLiteral("hls"), out.path()));
        QVERIFY(id > 0);
        const Outcome o = wait(s, id, 90000);
        QVERIFY2(o.done && o.ok, qPrintable(o.error));
        QVERIFY(o.path.endsWith(QLatin1String(".mp4")));
        QVERIFY(QFileInfo(o.path).size() > 1000);
        QVERIFY(!QFileInfo::exists(o.path + QStringLiteral(".part")));
        QVERIFY2(web.hits.value(QStringLiteral("/live/seg0.ts")) >= 1, "the pieces were fetched from the server");
        QVERIFY2(QString::fromLatin1(web.lastRequest).contains(QLatin1String("referer: https://page.example/watch"), Qt::CaseInsensitive), "FFmpeg was sent the page's address as Referer");
        // the result is a real video of about 3 seconds
        QProcess probe;
        probe.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-i"), o.path});
        QVERIFY(probe.waitForFinished(30000));
        const QString info = QString::fromUtf8(probe.readAllStandardError());
        QVERIFY2(info.contains(QLatin1String("Duration: 00:00:03")) || info.contains(QLatin1String("Duration: 00:00:02")), qPrintable(info));
        QVERIFY2(info.contains(QLatin1String("Video:")), qPrintable(info));
        QVERIFY(progress.count() >= 1);
    }
};

QTEST_MAIN(TstMedia)
#include "tst_media.moc"
