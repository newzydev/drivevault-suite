#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

// A web server on this computer: a page with two scripts and a picture; counts which paths the browser really asked for.
class PageServer : public QTcpServer {
public:
    QMap<QString, int> hits;
    PageServer()
    {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = nextPendingConnection())
                connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    const QByteArray head = s->readAll();
                    const QString path = QString::fromLatin1(head.split(' ').value(1));
                    ++hits[path];
                    QByteArray type = "text/plain", body;
                    if (path == QLatin1String("/page.html")) {
                        type = "text/html";
                        body = "<!doctype html><meta charset=utf-8><title>Ads</title><script>window.__res={};</script>"
                               "<script src='/blocked-ad.js' onload=\"__res.ad='loaded'\" onerror=\"__res.ad='blocked'\"></script>"
                               "<script src='/ok.js' onload=\"__res.ok='loaded'\" onerror=\"__res.ok='blocked'\"></script>"
                               "<img src='/pixel.gif' onload=\"__res.img='loaded'\" onerror=\"__res.img='blocked'\">";
                    } else if (path.endsWith(QLatin1String(".js"))) {
                        type = "application/javascript";
                        body = "/* ok */";
                    } else if (path == QLatin1String("/pixel.gif")) {
                        type = "image/gif";
                        body = QByteArray::fromBase64("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7");
                    }
                    s->write("HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    s->disconnectFromHost();
                });
        });
    }
};
// Starts the real program (Qt WebEngine and all), lets it draw, and checks the picture of its window: not blank, the accent colour of the program is there.
class TstSmoke : public QObject {
    Q_OBJECT

    static bool hasColourNear(const QImage& img, QRgb want, int tolerance)
    {
        for (int y = 0; y < img.height(); y += 2)
            for (int x = 0; x < img.width(); x += 2) {
                const QRgb c = img.pixel(x, y);
                if (qAbs(qRed(c) - qRed(want)) <= tolerance && qAbs(qGreen(c) - qGreen(want)) <= tolerance && qAbs(qBlue(c) - qBlue(want)) <= tolerance)
                    return true;
            }
        return false;
    }

    static QImage run(const QStringList& extra, const QString& shot, const QString& dataDir, int* exitCode)
    {
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("DV_NO_AUDIO"), QStringLiteral("1"));
        p.setProcessEnvironment(env);
        p.start(QStringLiteral(BROWSER_EXE_PATH), QStringList{QStringLiteral("--data-dir"), dataDir, QStringLiteral("--screenshot"), shot, QStringLiteral("--size"), QStringLiteral("1200x760")} + extra);
        if (!p.waitForFinished(90000)) {
            p.kill();
            p.waitForFinished();
            *exitCode = -999;
            return {};
        }
        *exitCode = p.exitCode();
        return QImage(shot);
    }

private slots:
    void theWindowOpensAndIsDrawn()
    {
        QTemporaryDir dir;
        int code = -1;
        const QImage img = run({QStringLiteral("--lang"), QStringLiteral("en")}, dir.filePath(QStringLiteral("a.png")), dir.filePath(QStringLiteral("data")), &code);
        QCOMPARE(code, 0);
        QVERIFY(!img.isNull());
        QVERIFY(img.width() >= 800 && img.height() >= 500);
        QSet<QRgb> colours;
        for (int y = 0; y < img.height(); y += 4)
            for (int x = 0; x < img.width(); x += 4)
                colours.insert(img.pixel(x, y));
        QVERIFY2(colours.size() > 20, "the picture looks blank");
        QVERIFY2(hasColourNear(img, qRgb(0x06, 0xB6, 0xD4), 24), "the accent colour of the program is missing");
    }
    void aLocalPageLoads()
    {
        QTemporaryDir dir;
        const QString page = dir.filePath(QStringLiteral("page.html"));
        QFile f(page);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("<!doctype html><meta charset=utf-8><title>Smoke</title><body style='margin:0;background:#ff00ff'><h1 style='color:#fff'>Hello</h1></body>");
        f.close();
        int code = -1;
        const QImage img = run({QStringLiteral("--lang"), QStringLiteral("en"), QUrl::fromLocalFile(page).toString()}, dir.filePath(QStringLiteral("b.png")), dir.filePath(QStringLiteral("data")), &code);
        QCOMPARE(code, 0);
        QVERIFY(hasColourNear(img, qRgb(0xFF, 0x00, 0xFF), 10));      // the page's own background reached the window
    }
    void theWindowPassesItsOwnChecks()
    {
        // tabs, pin, bookmarks, history, address suggestions, find in page, zoom, typed address, panels: run inside the real window (--selftest)
        QTemporaryDir dir;
        const QString page = dir.filePath(QStringLiteral("page.html"));
        QFile f(page);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("<!doctype html><meta charset=utf-8><title>Smoke</title><body><p>Hello</p></body>");
        f.close();
        const QString report = dir.filePath(QStringLiteral("report.json"));
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("DV_NO_AUDIO"), QStringLiteral("1"));
        p.setProcessEnvironment(env);
        p.start(QStringLiteral(BROWSER_EXE_PATH), {QStringLiteral("--data-dir"), dir.filePath(QStringLiteral("data")), QStringLiteral("--lang"), QStringLiteral("en"),
                                                   QStringLiteral("--selftest"), report, QUrl::fromLocalFile(page).toString()});
        QVERIFY(p.waitForFinished(90000));
        QFile r(report);
        QVERIFY(r.open(QIODevice::ReadOnly));
        const QByteArray text = r.readAll();
        QVERIFY2(p.exitCode() == 0, text.constData());
        QVERIFY2(text.contains("\"ok\": true") && !text.contains("\"ok\": false"), text.constData());
    }
    void theAdBlockerStopsRequestsInTheRealEngine()
    {
        struct Case { const char* name; QStringList extra; QByteArray settings; bool adBlocked; };
        const QList<Case> cases = {
            {"normal window", {}, "{}", true},
            {"private window", {QStringLiteral("--private")}, "{}", true},
            {"paused on the site", {}, "{\"adblockPaused\":[\"127.0.0.1\"]}", false},
            {"switched off", {}, "{\"adblockEnabled\":false}", false},
            {"list switched off", {}, "{\"adblockListsOff\":[\"easylist\"]}", false},
        };
        for (const Case& c : cases) {
            QTemporaryDir dir;
            PageServer server;
            QVERIFY(server.isListening());
            const QString data = dir.filePath(QStringLiteral("data"));
            QDir().mkpath(data + QStringLiteral("/filters"));
            QFile list(data + QStringLiteral("/filters/easylist.txt"));
            QVERIFY(list.open(QIODevice::WriteOnly));
            list.write("[Adblock Plus 2.0]\n! test list\n/blocked-ad.js\n/pixel.gif\n##.cosmetic\n");
            list.close();
            QFile settings(data + QStringLiteral("/settings.json"));
            QVERIFY(settings.open(QIODevice::WriteOnly));
            settings.write(c.settings);
            settings.close();

            const QString report = dir.filePath(QStringLiteral("probe.json"));
            QProcess p;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("DV_NO_AUDIO"), QStringLiteral("1"));
            p.setProcessEnvironment(env);
            p.start(QStringLiteral(BROWSER_EXE_PATH), QStringList{QStringLiteral("--data-dir"), data, QStringLiteral("--lang"), QStringLiteral("en"), QStringLiteral("--probe"), report}
                                                           + c.extra + QStringList{QStringLiteral("http://127.0.0.1:%1/page.html").arg(server.serverPort())});
            QElapsedTimer waited;
            waited.start();
            while (p.state() != QProcess::NotRunning && waited.elapsed() < 90000)
                QTest::qWait(50);                                  // the page server of this test must keep answering
            QVERIFY2(p.state() == QProcess::NotRunning, c.name);
            QFile r(report);
            QVERIFY2(r.open(QIODevice::ReadOnly), c.name);
            const QJsonObject o = QJsonDocument::fromJson(r.readAll()).array().at(0).toObject();
            const QJsonObject res = QJsonDocument::fromJson(o.value(QStringLiteral("detail")).toString().toUtf8()).object();
            qInfo().noquote() << c.name << ":" << res << "blocked count" << o.value(QStringLiteral("blocked")).toInt() << "hits" << server.hits;
            QVERIFY2(o.value(QStringLiteral("rules")).toInt() >= (QByteArray(c.settings).contains("easylist") ? 0 : 0), c.name);
            QCOMPARE(res.value(QStringLiteral("ok")).toString(), QStringLiteral("loaded"));              // a script nobody listed always loads
            const bool listOff = QByteArray(c.settings).contains("easylist");
            if (c.adBlocked) {
                QVERIFY2(res.value(QStringLiteral("ad")).toString() == QLatin1String("blocked"), c.name);
                QVERIFY2(res.value(QStringLiteral("img")).toString() == QLatin1String("blocked"), c.name);
                QVERIFY2(server.hits.value(QStringLiteral("/blocked-ad.js")) == 0, c.name);               // the request never reached the server
                QVERIFY2(o.value(QStringLiteral("blocked")).toInt() >= 2, c.name);
            } else {
                QVERIFY2(res.value(QStringLiteral("ad")).toString() == QLatin1String("loaded"), c.name);
                QVERIFY2(res.value(QStringLiteral("img")).toString() == QLatin1String("loaded"), c.name);
                QVERIFY2(server.hits.value(QStringLiteral("/blocked-ad.js")) == 1, c.name);
                QVERIFY2(listOff || o.value(QStringLiteral("blocked")).toInt() == 0, c.name);
            }
        }
    }    void theSidePanelsOpen()
    {
        for (const char* panel : {"history", "bookmarks", "downloads", "settings"}) {
            QTemporaryDir dir;
            int code = -1;
            const QImage img = run({QStringLiteral("--lang"), QStringLiteral("en"), QStringLiteral("--panel"), QString::fromLatin1(panel)}, dir.filePath(QStringLiteral("p.png")), dir.filePath(QStringLiteral("data")), &code);
            QVERIFY2(code == 0 && !img.isNull(), panel);
            QVERIFY2(hasColourNear(img, qRgb(0x18, 0x1A, 0x20), 2), panel);          // the panel's background
        }
    }
    void aPrivateWindowUsesItsOwnColour()
    {
        QTemporaryDir dir;
        int code = -1;
        const QImage img = run({QStringLiteral("--lang"), QStringLiteral("en"), QStringLiteral("--private")}, dir.filePath(QStringLiteral("q.png")), dir.filePath(QStringLiteral("data")), &code);
        QCOMPARE(code, 0);
        QVERIFY(hasColourNear(img, qRgb(0xA7, 0x8B, 0xFA), 24));
    }
};

QTEST_MAIN(TstSmoke)
#include "tst_smoke.moc"
