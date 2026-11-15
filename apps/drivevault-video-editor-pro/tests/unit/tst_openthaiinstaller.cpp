// OpenThai model download: against a small local HTTP server that supports Range requests - complete download, unpacking of a stored zip, SHA-256
// check (a changed file is refused and deleted), resume after a stop, a server that ignores Range, cancel, skipping files that are already installed,
// and the zip reader on bad input. The real 2.6 GB download is not part of the tests.

#include "ai/OpenThaiAsr.h"
#include "ai/OpenThaiInstaller.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

#include <QElapsedTimer>

using namespace dv;

namespace {

// A stored (uncompressed) zip with the given entries (CRC is left 0: the reader relies on the SHA-256 of the whole archive).
QByteArray storedZip(const QMap<QString, QByteArray>& files)
{
    auto le32 = [](quint32 v) { QByteArray b(4, 0); for (int i = 0; i < 4; ++i) b[i] = char((v >> (8 * i)) & 0xFF); return b; };
    auto le16 = [](quint16 v) { QByteArray b(2, 0); b[0] = char(v & 0xFF); b[1] = char(v >> 8); return b; };
    QByteArray data, central;
    for (auto it = files.cbegin(); it != files.cend(); ++it) {
        const QByteArray name = it.key().toUtf8();
        const quint32 offset = quint32(data.size());
        data += le32(0x04034b50) + le16(20) + le16(0) + le16(0) + le16(0) + le16(0) + le32(0) + le32(quint32(it.value().size())) + le32(quint32(it.value().size()))
                + le16(quint16(name.size())) + le16(0) + name + it.value();
        central += le32(0x02014b50) + le16(20) + le16(20) + le16(0) + le16(0) + le16(0) + le16(0) + le32(0) + le32(quint32(it.value().size()))
                   + le32(quint32(it.value().size())) + le16(quint16(name.size())) + le16(0) + le16(0) + le16(0) + le16(0) + le32(0) + le32(offset) + name;
    }
    const quint32 cdOffset = quint32(data.size());
    return data + central + le32(0x06054b50) + le16(0) + le16(0) + le16(quint16(files.size())) + le16(quint16(files.size())) + le32(quint32(central.size()))
           + le32(cdOffset) + le16(0);
}

QByteArray pattern(int size, int seed)
{
    QByteArray b(size, 0);
    for (int i = 0; i < size; ++i)
        b[i] = char((i * 31 + seed) & 0xFF);
    return b;
}
QString sha(const QByteArray& b) { return QString::fromLatin1(QCryptographicHash::hash(b, QCryptographicHash::Sha256).toHex()); }

// Serves fixed files over HTTP; honours "Range: bytes=N-" unless told not to; can cut a response short after `cutAfter` bytes.
class Server : public QObject {
public:
    explicit Server(QObject* parent = nullptr) : QObject(parent)
    {
        listen();
    }
    QMap<QString, QByteArray> files;
    bool honourRange = true;
    qint64 cutAfter = -1;           // close the connection after this many body bytes (once)
    int requests = 0;
    QStringList ranges;
    quint16 port() const { return m_server.serverPort(); }

private:
    void listen()
    {
        QVERIFY(m_server.listen(QHostAddress::LocalHost, 0));
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = m_server.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, this, [this, s] { handle(s); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
    void handle(QTcpSocket* s)
    {
        m_buf[s] += s->readAll();
        if (!m_buf[s].contains("\r\n\r\n"))
            return;
        const QByteArray req = m_buf.take(s);
        ++requests;
        const QList<QByteArray> lines = req.split('\n');
        const QString path = QString::fromLatin1(lines.value(0).split(' ').value(1)).mid(1);
        qint64 from = 0;
        for (const QByteArray& l : lines) {
            if (l.toLower().startsWith("range:")) {
                ranges << QString::fromLatin1(l.trimmed());
                if (honourRange)
                    from = l.mid(l.indexOf('=') + 1).split('-').value(0).trimmed().toLongLong();
            }
        }
        if (!files.contains(path)) {
            s->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            s->disconnectFromHost();
            return;
        }
        const QByteArray all = files.value(path);
        QByteArray body = all.mid(int(from));
        const bool partial = from > 0;
        QByteArray head = partial ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
        if (partial)
            head += "Content-Range: bytes " + QByteArray::number(from) + "-" + QByteArray::number(all.size() - 1) + "/" + QByteArray::number(all.size()) + "\r\n";
        head += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
        if (cutAfter >= 0) {
            body = body.left(int(cutAfter));
            cutAfter = -1;
            s->write(head + body);
            s->flush();
            s->abort();                   // the connection dies in the middle of the file
            return;
        }
        s->write(head + body);
        s->disconnectFromHost();
    }
    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_buf;
};

} // namespace

class TstOpenThaiInstaller : public QObject {
    Q_OBJECT

    struct Fixture {
        QByteArray license = "OpenMDW test licence\n";
        QByteArray encoder = pattern(70000, 1), decoder = pattern(90000, 2), tokens = pattern(3000, 3), data = pattern(1500000, 4);
        QByteArray zip;
        QList<OpenThaiInstaller::Item> items(quint16 port) const
        {
            const QString base = QStringLiteral("http://127.0.0.1:%1/").arg(port);
            return {{"LICENSE", base + "LICENSE", license.size(), sha(license), {}},
                    {"model.asr.zip", base + "model.asr.zip", zip.size(), sha(zip),
                     {{"encoder.onnx", encoder.size()}, {"decoder_joint.onnx", decoder.size()}, {"tokens.txt", tokens.size()}}},
                    {"encoder.onnx.data", base + "encoder.onnx.data", data.size(), sha(data), {}}};
        }
        void serve(Server& s) const
        {
            s.files = {{"LICENSE", license}, {"model.asr.zip", zip}, {"encoder.onnx.data", data}};
        }
    };
    Fixture fx() const
    {
        Fixture f;
        f.zip = storedZip({{"encoder.onnx", f.encoder}, {"decoder_joint.onnx", f.decoder}, {"tokens.txt", f.tokens}});
        return f;
    }
    static bool waitDone(OpenThaiInstaller& inst, QSignalSpy& done, int ms = 20000)
    {
        [&] { QTRY_VERIFY_WITH_TIMEOUT(!inst.busy() && done.count() > 0, ms); }();      // a failed wait marks the test as failed
        return !inst.busy() && done.count() > 0;
    }
    static QByteArray read(const QString& path)
    {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

private slots:
    void downloadsVerifiesAndUnpacksEverything()
    {
        const Fixture f = fx();
        Server server;
        f.serve(server);
        QTemporaryDir tmp;
        OpenThaiInstaller inst;
        QSignalSpy done(&inst, &OpenThaiInstaller::finished);
        QCOMPARE(inst.downloadItems(f.items(server.port()), tmp.filePath("model")), QString());
        QVERIFY(inst.busy());
        QVERIFY(!inst.downloadItems(f.items(server.port()), tmp.filePath("model")).isEmpty());          // not twice at once
        QVERIFY(waitDone(inst, done));
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        const QString m = tmp.filePath("model");
        QCOMPARE(read(m + "/LICENSE"), f.license);
        QCOMPARE(read(m + "/encoder.onnx"), f.encoder);
        QCOMPARE(read(m + "/decoder_joint.onnx"), f.decoder);
        QCOMPARE(read(m + "/tokens.txt"), f.tokens);
        QCOMPARE(read(m + "/encoder.onnx.data"), f.data);
        QVERIFY(!QFileInfo::exists(m + "/model.asr.zip"));
        QVERIFY(!QFileInfo::exists(m + "/.part"));                                                       // nothing left over
        QCOMPARE(inst.progress(), 1.0);
        // everything is there: a second run downloads nothing
        const int before = server.requests;
        QSignalSpy again(&inst, &OpenThaiInstaller::finished);
        QCOMPARE(inst.downloadItems(f.items(server.port()), m), QString());
        QTRY_VERIFY_WITH_TIMEOUT(again.count() == 1, 5000);
        QVERIFY(again.first().at(0).toBool());
        QCOMPARE(server.requests, before);
    }
    void aChangedFileIsRefusedAndDeleted()
    {
        Fixture f = fx();
        Server server;
        f.serve(server);
        server.files["encoder.onnx.data"] = pattern(1500000, 99);            // same size, different content
        QTemporaryDir tmp;
        OpenThaiInstaller inst;
        QSignalSpy done(&inst, &OpenThaiInstaller::finished);
        QCOMPARE(inst.downloadItems(f.items(server.port()), tmp.filePath("model")), QString());
        QVERIFY(waitDone(inst, done));
        QVERIFY(!done.first().at(0).toBool());
        QVERIFY2(done.first().at(1).toString().contains("checksum"), qPrintable(done.first().at(1).toString()));
        QVERIFY(!QFileInfo::exists(tmp.filePath("model/encoder.onnx.data")));
        QVERIFY(!QFileInfo::exists(tmp.filePath("model/.part/encoder.onnx.data.part")));                 // the bad file is deleted, not kept
        QCOMPARE(read(tmp.filePath("model/LICENSE")), f.license);                                          // files that were fine stay
    }
    void aMissingFileIsAnErrorNotACrash()
    {
        const Fixture f = fx();
        Server server;                                                       // serves nothing
        QTemporaryDir tmp;
        OpenThaiInstaller inst;
        QSignalSpy done(&inst, &OpenThaiInstaller::finished);
        QCOMPARE(inst.downloadItems(f.items(server.port()), tmp.filePath("model")), QString());
        QVERIFY(waitDone(inst, done));
        QVERIFY(!done.first().at(0).toBool());
    }
    void aStoppedDownloadContinuesWhereItStopped()
    {
        const Fixture f = fx();
        Server server;
        f.serve(server);
        server.cutAfter = 600000;                                            // the big file dies after 600 kB (the first two files are small and not cut: see below)
        server.files.remove("LICENSE");
        server.files.remove("model.asr.zip");
        QTemporaryDir tmp;
        const QString m = tmp.filePath("model");
        // the two small files are already installed, only the big one is missing
        QDir().mkpath(m);
        { QFile l(m + "/LICENSE"); QVERIFY(l.open(QIODevice::WriteOnly)); l.write(f.license); }
        for (auto [name, bytes] : {std::pair<QString, QByteArray>{"encoder.onnx", f.encoder}, {"decoder_joint.onnx", f.decoder}, {"tokens.txt", f.tokens}}) {
            QFile o(m + "/" + name);
            QVERIFY(o.open(QIODevice::WriteOnly));
            o.write(bytes);
        }
        OpenThaiInstaller inst;
        QSignalSpy done(&inst, &OpenThaiInstaller::finished);
        QCOMPARE(inst.downloadItems(f.items(server.port()), m), QString());
        QVERIFY(waitDone(inst, done));
        QVERIFY(!done.first().at(0).toBool());                               // stopped
        const qint64 part = QFileInfo(m + "/.part/encoder.onnx.data.part").size();
        QVERIFY2(part > 0 && part < f.data.size(), qPrintable(QString::number(part)));
        // again: it asks only for the rest
        QSignalSpy second(&inst, &OpenThaiInstaller::finished);
        server.ranges.clear();
        QCOMPARE(inst.downloadItems(f.items(server.port()), m), QString());
        QVERIFY(waitDone(inst, second));
        QVERIFY2(second.first().at(0).toBool(), qPrintable(second.first().at(1).toString()));
        QVERIFY(!server.ranges.isEmpty() && server.ranges.first().contains("bytes=" + QString::number(part) + "-"));
        QCOMPARE(read(m + "/encoder.onnx.data"), f.data);
    }
    void aServerThatIgnoresRangeStillWorks()
    {
        const Fixture f = fx();
        Server server;
        f.serve(server);
        server.honourRange = false;
        QTemporaryDir tmp;
        const QString m = tmp.filePath("model");
        QDir().mkpath(m + "/.part");
        { QFile p(m + "/.part/encoder.onnx.data.part"); QVERIFY(p.open(QIODevice::WriteOnly)); p.write(f.data.left(5000)); }      // an old part
        OpenThaiInstaller inst;
        QSignalSpy done(&inst, &OpenThaiInstaller::finished);
        QCOMPARE(inst.downloadItems(f.items(server.port()), m), QString());
        QVERIFY(waitDone(inst, done));
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        QCOMPARE(read(m + "/encoder.onnx.data"), f.data);                    // not 5000 bytes twice
    }
    void cancelKeepsThePartAndReportsIt()
    {
        const Fixture f = fx();
        Server server;
        f.serve(server);
        QTemporaryDir tmp;
        OpenThaiInstaller inst;
        QSignalSpy done(&inst, &OpenThaiInstaller::finished);
        QCOMPARE(inst.downloadItems(f.items(server.port()), tmp.filePath("model")), QString());
        inst.cancel();
        QVERIFY(waitDone(inst, done));
        QVERIFY(!done.first().at(0).toBool());
        QVERIFY(inst.message().contains("Cancelled"));
        QVERIFY(!inst.busy());
    }
    void onlyHttpsOrFileUrlsAreAllowed()
    {
        QTemporaryDir tmp;
        OpenThaiInstaller inst;
        QSignalSpy done(&inst, &OpenThaiInstaller::finished);
        const QList<OpenThaiInstaller::Item> items = {{"x", "ftp://example.com/x", 10, QString(64, '0'), {}}};
        QCOMPARE(inst.downloadItems(items, tmp.filePath("model")), QString());
        QVERIFY(waitDone(inst, done));
        QVERIFY(!done.first().at(0).toBool());
    }
    void zipReaderRefusesBadInput()
    {
        QTemporaryDir tmp;
        QString err;
        { QFile z(tmp.filePath("junk.zip")); QVERIFY(z.open(QIODevice::WriteOnly)); z.write(QByteArray(500, 'x')); }
        QVERIFY(!OpenThaiInstaller::extractStoredZip(tmp.filePath("junk.zip"), tmp.path(), {{"a", 1}}, &err));
        QVERIFY(!err.isEmpty());
        // a wanted file that is not in the archive, and a wrong size
        const QByteArray zip = storedZip({{"a.txt", QByteArray("hello")}});
        { QFile z(tmp.filePath("ok.zip")); QVERIFY(z.open(QIODevice::WriteOnly)); z.write(zip); }
        QVERIFY(!OpenThaiInstaller::extractStoredZip(tmp.filePath("ok.zip"), tmp.path(), {{"b.txt", 5}}, &err));
        QVERIFY(err.contains("missing"));
        QVERIFY(!OpenThaiInstaller::extractStoredZip(tmp.filePath("ok.zip"), tmp.path(), {{"a.txt", 6}}, &err));
        QVERIFY(OpenThaiInstaller::extractStoredZip(tmp.filePath("ok.zip"), tmp.path(), {{"a.txt", 5}}, &err));
        QCOMPARE(read(tmp.filePath("a.txt")), QByteArray("hello"));
        // a name with a path never leaves the folder: only the plain names that were asked for are written
        const QByteArray evil = storedZip({{"../evil.txt", QByteArray("bad")}, {"a.txt", QByteArray("hello")}});
        { QFile z(tmp.filePath("evil.zip")); QVERIFY(z.open(QIODevice::WriteOnly)); z.write(evil); }
        QVERIFY(QDir().mkpath(tmp.filePath("sub/inner")));
        QVERIFY(OpenThaiInstaller::extractStoredZip(tmp.filePath("evil.zip"), tmp.filePath("sub/inner"), {{"a.txt", 5}}, &err));
        QVERIFY(QFileInfo::exists(tmp.filePath("sub/inner/a.txt")));
        QVERIFY(!QFileInfo::exists(tmp.filePath("sub/evil.txt")));                  // "../evil.txt" would have landed here
        QVERIFY(!QFileInfo::exists(tmp.filePath("sub/inner/evil.txt")));
    }
    // The real 2.6 GB download from Hugging Face, only when DV_TEST_REAL_DOWNLOAD=1 (slow; proves the pinned URLs, sizes and checksums are right).
    void realDownloadFromHuggingFace()
    {
        if (qEnvironmentVariable("DV_TEST_REAL_DOWNLOAD") != QLatin1String("1"))
            QSKIP("set DV_TEST_REAL_DOWNLOAD=1 to download the real model (2.6 GB)");
        QTemporaryDir tmp;
        OpenThaiInstaller inst;
        QSignalSpy done(&inst, &OpenThaiInstaller::finished);
        QCOMPARE(inst.downloadItems(OpenThaiInstaller::defaultItems(), tmp.filePath("openthai-asr")), QString());
        QElapsedTimer timer;
        timer.start();
        QVERIFY(waitDone(inst, done, 3 * 3600 * 1000));
        fprintf(stderr, "REAL download finished in %.0f s: %s\n", double(timer.elapsed()) / 1000.0, inst.message().toUtf8().constData());
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        QVERIFY(ai::openThaiModelComplete(tmp.filePath("openthai-asr")));
        const QString wav = qEnvironmentVariable("DV_TEST_THAI_WAV");
        if (!wav.isEmpty() && ai::openThaiCompiledIn()) {                      // the downloaded copy must recognise speech like the first one
            QFile f(wav);
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QByteArray all = f.readAll();
            const int dataAt = all.indexOf("data");
            std::vector<float> pcm;
            for (qsizetype i = dataAt + 8; i + 1 < all.size() && pcm.size() < 16000 * 12; i += 2)
                pcm.push_back(float(*reinterpret_cast<const qint16*>(all.constData() + i)) / 32768.f);
            std::atomic_bool cancel{false};
            const ai::OpenThaiResult r = ai::openThaiTranscribe(pcm, tmp.filePath("openthai-asr"), "th", {}, cancel);
            QVERIFY2(r.ok() && !r.segments.isEmpty(), qPrintable(r.error));
            fprintf(stderr, "REAL first phrase: %s\n", r.segments.first().text.toUtf8().constData());
        }
    }
    void pinnedManifestIsComplete()
    {
        const auto items = OpenThaiInstaller::defaultItems();
        QCOMPARE(items.size(), 3);
        qint64 total = 0;
        for (const auto& it : items) {
            QVERIFY(it.url.startsWith("https://huggingface.co/iapp/OpenThai2.0-ASR-Streaming-ONNX/resolve/"));
            QVERIFY(!it.url.contains("/main/"));                          // a fixed revision, not whatever "main" is today
            QCOMPARE(it.sha256.size(), 64);
            total += it.size;
        }
        QVERIFY(total > 2500LL * 1000 * 1000 && total < 2700LL * 1000 * 1000);     // about 2.6 GB
    }
};

QTEST_MAIN(TstOpenThaiInstaller)
#include "tst_openthaiinstaller.moc"
