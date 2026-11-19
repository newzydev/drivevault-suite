#include <QDir>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

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
    void theSidePanelsOpen()
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
