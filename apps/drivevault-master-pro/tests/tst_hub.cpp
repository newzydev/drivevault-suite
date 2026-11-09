#include "Hub.h"
#include "WinUtil.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>

#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

using namespace dm;

namespace {
const QString kRoot = QStringLiteral("Software\\DriveVaultMasterTest");
void writeProduct(const QString& key, const QMap<QString, QString>& values)
{
    QSettings s(QStringLiteral("HKEY_CURRENT_USER\\%1\\Products\\%2").arg(kRoot, key), QSettings::NativeFormat);
    for (auto it = values.begin(); it != values.end(); ++it)
        s.setValue(it.key(), it.value());
    s.sync();
}
void wipeRegistry() { QSettings(QStringLiteral("HKEY_CURRENT_USER\\%1").arg(kRoot), QSettings::NativeFormat).remove(QString()); }
QVariantMap find(const Hub& hub, const QString& id)
{
    for (const QVariant& v : hub.products())
        if (v.toMap().value("id").toString() == id)
            return v.toMap();
    return {};
}
} // namespace

class TstHub : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    QString p(const QString& n) const { return m_dir.filePath(n); }
    Hub::Options options() const
    {
        Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = p("data");
        o.appDir = p("app");
        return o;
    }
    QString fakeSetup(const QString& where)
    {
        QDir().mkpath(QFileInfo(where).absolutePath());
        QFile::remove(where);
        return QFile::copy(QStringLiteral(FAKE_SETUP_PATH), where) ? where : QString();
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); wipeRegistry(); }
    void cleanupTestCase() { wipeRegistry(); }
    void cleanup() { wipeRegistry(); }

    void versionsAreComparedNumerically()
    {
        QVERIFY(win::compareVersions("1.0.0", "1.0.1") < 0);
        QVERIFY(win::compareVersions("1.10.0", "1.9.9") > 0);
        QCOMPARE(win::compareVersions("2.0", "2.0.0.0"), 0);
        QVERIFY(win::compareVersions("1.0.0", "2.0.0") < 0);
    }

    void versionResourcesAreRead()
    {
        const QString fake = fakeSetup(p("tools/setup.exe"));
        QVERIFY(!fake.isEmpty());
        QCOMPARE(win::versionInfo(fake, "ProductName"), QStringLiteral("DriveVault Video Editor Pro"));
        QCOMPARE(win::versionInfo(fake, "ProductVersion"), QStringLiteral("2.0.0"));
        QCOMPARE(win::versionInfo(fake, "CompanyName"), QStringLiteral("DriveVault"));
        QVERIFY(win::versionInfo(QStringLiteral("C:/Windows/System32/cmd.exe"), "ProductName").contains("Windows"));
        QVERIFY(win::versionInfo(p("nothing.exe"), "ProductName").isEmpty());
    }

    void thePlannedSuiteIsListedHonestly()
    {
        Hub hub(options());
        hub.setLanguage("en");
        QCOMPARE(hub.products().size(), 7);                                 // 3 multimedia + 4 office programs, all but one still planned
        const QVariantMap video = find(hub, "drivevault.videoeditor");
        QCOMPARE(video.value("section").toString(), QStringLiteral("multimedia"));
        QCOMPARE(video.value("glyph").toString(), QStringLiteral("V"));
        for (const QString& id : {"drivevault.document", "drivevault.presentation", "drivevault.spreadsheet", "drivevault.pdf"}) {
            const QVariantMap m = find(hub, id);
            QCOMPARE(m.value("section").toString(), QStringLiteral("office"));
            QCOMPARE(m.value("status").toString(), QStringLiteral("comingSoon"));      // they do not exist: never offered as installable
            QVERIFY(!m.value("canInstall").toBool());
            QVERIFY(!m.value("onlineInstall").toBool());
        }
        QCOMPARE(video.value("status").toString(), QStringLiteral("notInstalled"));
        QCOMPARE(video.value("installerPath").typeId(), int(QMetaType::QString));     // never "undefined" in QML
        QCOMPARE(video.value("accent").toString(), QStringLiteral("#8B5CF6"));
        QVERIFY(!video.value("installed").toBool());
        QVERIFY(!video.value("canInstall").toBool());                  // no setup file anywhere
        for (const QString& id : {"drivevault.imageeditor", "drivevault.audioeditor"}) {
            const QVariantMap m = find(hub, id);
            QCOMPARE(m.value("status").toString(), QStringLiteral("comingSoon"));
            QVERIFY(!m.value("installed").toBool());
            QVERIFY(!m.value("canInstall").toBool());
        }
        QVERIFY(!hub.launch("drivevault.videoeditor").isEmpty());      // cannot launch what is not installed
        QVERIFY(!hub.launch("drivevault.audioeditor").isEmpty());
    }

    void installedProgramsAreFoundInTheRegistry()
    {
        const QString exe = fakeSetup(p("Program Files/DriveVault/DriveVault Video Editor Pro/DriveVaultVideoEditorPro.exe"));
        writeProduct("VideoEditorPro", {{"DisplayName", "DriveVault Video Editor Pro"}, {"Version", "1.0.0"},
                                         {"InstallDir", QFileInfo(exe).absolutePath()}, {"Executable", exe}, {"AccentColor", "#8B5CF6"},
                                         {"Uninstaller", exe}, {"Publisher", "DriveVault"}});
        // a program that this hub version does not know, described by a manifest
        QFile manifest(p("other/product.json"));
        QDir().mkpath(p("other"));
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write(R"({"id":"drivevault.newtool","name":"DriveVault New Tool"})");
        manifest.close();
        writeProduct("NewTool", {{"DisplayName", "DriveVault New Tool"}, {"Version", "0.9"}, {"Executable", exe}, {"InstallDir", p("other")},
                                  {"Manifest", manifest.fileName()}, {"AccentColor", "#EC4899"}});
        // an entry whose program was deleted by hand
        writeProduct("AudioEditorPro", {{"DisplayName", "DriveVault Audio Editor Pro"}, {"Version", "1.0.0"}, {"Executable", p("gone/none.exe")}});

        Hub hub(options());
        hub.setLanguage("en");
        const QVariantMap video = find(hub, "drivevault.videoeditor");
        QCOMPARE(video.value("status").toString(), QStringLiteral("installed"));
        QCOMPARE(video.value("version").toString(), QStringLiteral("1.0.0"));
        QCOMPARE(video.value("scope").toString(), QStringLiteral("currentUser"));
        QVERIFY(video.value("canUninstall").toBool());
        QCOMPARE(find(hub, "drivevault.audioeditor").value("status").toString(), QStringLiteral("broken"));
        const QVariantMap extra = find(hub, "drivevault.newtool");
        QVERIFY(!extra.isEmpty());
        QCOMPARE(extra.value("status").toString(), QStringLiteral("installed"));
        QCOMPARE(extra.value("accent").toString(), QStringLiteral("#EC4899"));
        QCOMPARE(hub.products().size(), 8);
        QCOMPARE(extra.value("section").toString(), QStringLiteral("other"));       // a program the hub does not know gets its own section
    }

    void theProgramsFolderIsTheCommonFolder()
    {
        auto opts = [&](const QString& app) { Hub::Options o = options(); o.appDir = p(app); return o; };
        {   // nothing installed, no Common next to the hub: no folder, and "open" says so instead of opening something else
            Hub hub(opts("hubA"));
            hub.setLanguage("en");
            QVERIFY(hub.suiteDirectory().isEmpty());
            QVERIFY(!hub.openInstallersFolder().isEmpty());
        }
        {   // a build folder run: the folder is where the registered programs really are (...\Common\<program>)
            const QString exe = fakeSetup(p("PF/DriveVault/Common/DriveVault Video Editor Pro/DriveVaultVideoEditorPro.exe"));
            writeProduct("VideoEditorPro", {{"DisplayName", "DriveVault Video Editor Pro"}, {"Version", "1.0.0"}, {"InstallDir", QFileInfo(exe).absolutePath()},
                                             {"Executable", exe}, {"Publisher", "DriveVault"}});
            Hub hub(opts("hubB"));
            QCOMPARE(hub.suiteDirectory(), QDir::cleanPath(p("PF/DriveVault/Common")));
        }
        {   // installed layout: Common sits next to Master Pro and wins
            QDir().mkpath(p("hubC/Common"));
            Hub hub(opts("hubC"));
            QCOMPARE(hub.suiteDirectory(), QDir::cleanPath(p("hubC/Common")));
        }
        wipeRegistry();
        {   // a program outside any Common folder does not make its parent "the programs folder"
            const QString exe = fakeSetup(p("elsewhere/DriveVault Video Editor Pro/DriveVaultVideoEditorPro.exe"));
            writeProduct("VideoEditorPro", {{"DisplayName", "DriveVault Video Editor Pro"}, {"Version", "1.0.0"}, {"InstallDir", QFileInfo(exe).absolutePath()},
                                             {"Executable", exe}, {"Publisher", "DriveVault"}});
            Hub hub(opts("hubD"));
            QVERIFY(hub.suiteDirectory().isEmpty());
        }
    }

    void setupFilesInTheFoldersOfferInstallAndUpdateAndRunWithTheRightResult()
    {
        const QString exe = fakeSetup(p("Program Files/DriveVault/DriveVault Video Editor Pro/DriveVaultVideoEditorPro.exe"));
        const QString setup = fakeSetup(p("app/installers/DriveVault-Video-Editor-Pro-Setup-x64.exe"));
        QVERIFY(!exe.isEmpty() && !setup.isEmpty());
        QVERIFY(QFile::copy(QStringLiteral("C:/Windows/System32/cmd.exe"), p("app/installers/not-drivevault.exe")));     // other company: ignored
        qputenv("DM_FAKE_MARKER", p("marker.txt").toLocal8Bit());
        qputenv("DM_FAKE_EXIT", "0");

        Hub hub(options());
        hub.setLanguage("en");
        hub.setSilentInstalls(true);
        QCOMPARE(hub.scanInstallers().size(), 1);
        QVariantMap video = find(hub, "drivevault.videoeditor");
        QCOMPARE(video.value("installerVersion").toString(), QStringLiteral("2.0.0"));
        QVERIFY(video.value("canInstall").toBool());                  // not installed + setup found -> Install

        QSignalSpy finished(&hub, &Hub::taskFinished);
        QVERIFY(hub.installProduct("drivevault.videoeditor").isEmpty());
        QVERIFY(hub.busy());
        QVERIFY(!hub.installProduct("drivevault.videoeditor").isEmpty());   // one task at a time
        hub.waitForIdle();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().first().toInt(), 0);
        QFile marker(p("marker.txt"));
        QVERIFY(marker.open(QIODevice::ReadOnly));
        const QString args = QString::fromLocal8Bit(marker.readAll());
        QVERIFY2(args.contains("/VERYSILENT") && args.contains("/NORESTART"), qPrintable(args));
        marker.close();

        // now it "is installed" in version 1.0.0 -> the 2.0.0 setup is an update
        writeProduct("VideoEditorPro", {{"DisplayName", "DriveVault Video Editor Pro"}, {"Version", "1.0.0"}, {"InstallDir", QFileInfo(exe).absolutePath()},
                                         {"Executable", exe}, {"Uninstaller", exe}});
        hub.refresh();
        video = find(hub, "drivevault.videoeditor");
        QVERIFY(video.value("installed").toBool());
        QVERIFY(video.value("updateAvailable").toBool());
        QVERIFY(!video.value("canInstall").toBool());
        writeProduct("VideoEditorPro", {{"Version", "2.0.0"}});
        hub.refresh();
        QVERIFY(!find(hub, "drivevault.videoeditor").value("updateAvailable").toBool());      // same version: nothing to update

        // exit codes are reported, not hidden
        qputenv("DM_FAKE_EXIT", "7");
        finished.clear();
        QVERIFY(hub.uninstall("drivevault.videoeditor").isEmpty());
        hub.waitForIdle();
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().first().toInt(), 7);
        QVERIFY2(hub.message().contains("7"), qPrintable(hub.message()));
        qunsetenv("DM_FAKE_EXIT");
        qunsetenv("DM_FAKE_MARKER");
    }

    void onlyDriveVaultSetupFilesAreStarted()
    {
        Hub hub(options());
        hub.setLanguage("en");
        hub.setSilentInstalls(true);
        QVERIFY(!hub.installFromFile(QStringLiteral("C:/Windows/System32/cmd.exe")).isEmpty());   // another company
        QVERIFY(!hub.installFromFile(p("does-not-exist.exe")).isEmpty());
        QFile txt(p("readme.txt"));
        QVERIFY(txt.open(QIODevice::WriteOnly));
        txt.write("x");
        txt.close();
        QVERIFY(!hub.installFromFile(txt.fileName()).isEmpty());
        QVERIFY(!hub.busy());
    }

    void installerFoldersAndLanguageArePersisted()
    {
        QDir().mkpath(p("my installers"));
        {
            Hub hub(options());
            hub.setLanguage("th");
            QCOMPARE(hub.addInstallerFolder(QUrl::fromLocalFile(p("my installers")).toString()), QString());
            QVERIFY(!hub.addInstallerFolder(p("no such folder")).isEmpty());
            QVERIFY(hub.installerFolders().contains(QDir::cleanPath(p("my installers"))));
            // Thai descriptions
            QVERIFY(find(hub, "drivevault.videoeditor").value("description").toString().contains(QString::fromUtf8("ตัดต่อ")));
        }
        Hub again(options());
        QCOMPARE(again.language(), QStringLiteral("th"));
        QVERIFY(again.installerFolders().contains(QDir::cleanPath(p("my installers"))));
        QCOMPARE(again.removeInstallerFolder(p("my installers")), QString());
        QVERIFY(!again.installerFolders().contains(QDir::cleanPath(p("my installers"))));
    }

    // ---- online updates (the feed and the "downloads" are local files here; no network is used) ----------------------------------
    void releaseFeedIsParsedStrictly()
    {
        auto asset = [](const QString& name) { return QJsonObject{{"name", name}, {"browser_download_url", "https://github.com/x/y/releases/download/t/" + name}, {"size", 1000}}; };
        auto release = [&](const QString& tag, bool draft, bool pre, const QJsonArray& assets) {
            return QJsonObject{{"tag_name", tag}, {"draft", draft}, {"prerelease", pre}, {"html_url", "https://github.com/x/y/releases/tag/" + tag}, {"assets", assets}};
        };
        const QJsonArray both{asset("Setup.exe"), asset("Setup.exe.sha256")};
        QJsonArray feed;
        feed << release("video-editor-v1.0.1", false, false, both)
             << release("video-editor-v1.10.0", false, false, both)             // 1.10 > 1.9 (numeric, not text)
             << release("video-editor-v1.9.0", false, false, both)
             << release("video-editor-v9.0.0", false, true, both)              // pre-release: ignored
             << release("video-editor-v8.0.0", true, false, both)              // draft: ignored
             << release("master-pro-v1.2.0", false, false, both)
             << release("v3.0.0", false, false, both)                          // not <slug>-v<x.y.z>: ignored
             << release("audio-editor-v1.0", false, false, both)               // not three numbers: ignored
             << release("image-editor-v1.0.0", false, false, QJsonArray{asset("Setup.exe")});   // no .sha256 next to it: ignored
        const auto found = releases::parse(QJsonDocument(feed).toJson());
        QCOMPARE(found.size(), 2);
        QCOMPARE(found.value("video-editor").version, QStringLiteral("1.10.0"));
        QCOMPARE(found.value("video-editor").assetName, QStringLiteral("Setup.exe"));
        QVERIFY(found.value("video-editor").hashUrl.endsWith("Setup.exe.sha256"));
        QCOMPARE(found.value("master-pro").version, QStringLiteral("1.2.0"));
        QVERIFY(releases::parse("not json").isEmpty());
        QVERIFY(releases::parse("{}").isEmpty());

        const QString h(64, QLatin1Char('A'));
        QCOMPARE(releases::parseSha256(h + " *Setup.exe\r\n", "Setup.exe"), h.toLower());
        QCOMPARE(releases::parseSha256(h, "Setup.exe"), h.toLower());
        QVERIFY(releases::parseSha256(h + " *Other.exe", "Setup.exe").isEmpty());      // a hash for another file
        QVERIFY(releases::parseSha256("abc *Setup.exe", "Setup.exe").isEmpty());
        QVERIFY(releases::urlAllowed("https://github.com/a/b/releases/download/x", "https://github.com/a/b/releases/download/"));
        QVERIFY(!releases::urlAllowed("https://evil.example/a/b/releases/download/x", "https://github.com/a/b/releases/download/"));
        QVERIFY(!releases::urlAllowed("https://github.com/a/b", QString()));
    }

    void onlineUpdateIsCheckedDownloadedVerifiedAndRun()
    {
        const QString exe = fakeSetup(p("Program Files/DriveVault/DriveVault Video Editor Pro/DriveVaultVideoEditorPro.exe"));
        const QString name = QStringLiteral("DriveVault-Video-Editor-Pro-Setup-x64.exe");
        QFile::remove(p("app/installers/" + name));                 // left over from an earlier test: a local 2.0.0 setup would hide the online one
        const QString served = fakeSetup(p("rel/" + name));         // what "GitHub" serves: the fake setup, version 2.0.0
        QVERIFY(!exe.isEmpty() && !served.isEmpty());
        QFile bytes(served);
        QVERIFY(bytes.open(QIODevice::ReadOnly));
        const QString goodHash = QString::fromLatin1(QCryptographicHash::hash(bytes.readAll(), QCryptographicHash::Sha256).toHex());
        bytes.close();
        auto writeHash = [&](const QString& hex) {
            QFile f(p("rel/" + name + ".sha256"));
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(QStringLiteral("%1 *%2\n").arg(hex, name).toLatin1());
        };
        writeHash(goodHash);
        auto url = [&](const QString& file) { return QUrl::fromLocalFile(p(file)).toString(); };
        QFile feed(p("feed.json"));
        QVERIFY(feed.open(QIODevice::WriteOnly));
        feed.write(QJsonDocument(QJsonArray{QJsonObject{
            {"tag_name", "drivevault-video-editor-pro-v2.0.0"}, {"draft", false}, {"prerelease", false}, {"html_url", "https://example.invalid"},
            {"assets", QJsonArray{QJsonObject{{"name", name}, {"browser_download_url", url("rel/" + name)}, {"size", 1}},
                                  QJsonObject{{"name", name + ".sha256"}, {"browser_download_url", url("rel/" + name + ".sha256")}, {"size", 1}}}}}}).toJson());
        feed.close();
        writeProduct("VideoEditorPro", {{"DisplayName", "DriveVault Video Editor Pro"}, {"Version", "1.0.0"}, {"InstallDir", QFileInfo(exe).absolutePath()},
                                         {"Executable", exe}, {"Uninstaller", exe}});
        qputenv("DM_FAKE_MARKER", p("online-marker.txt").toLocal8Bit());
        qputenv("DM_FAKE_EXIT", "0");
        QFile::remove(p("online-marker.txt"));

        Hub::Options o = options();
        o.releasesUrl = url("feed.json");
        o.downloadPrefix = url("rel") + QLatin1Char('/');
        Hub hub(o);
        hub.setLanguage("en");
        hub.setSilentInstalls(true);

        // nothing online is known (and nothing is contacted) until the user checks
        QVERIFY(!hub.onlineChecked());
        QVERIFY(!find(hub, "drivevault.videoeditor").value("onlineUpdate").toBool());
        QVERIFY(!hub.downloadAndInstall("drivevault.videoeditor").isEmpty());

        QSignalSpy checked(&hub, &Hub::checkFinished);
        QVERIFY(hub.checkForUpdates().isEmpty());
        QVERIFY(hub.checking());
        QVERIFY(!hub.checkForUpdates().isEmpty());                    // not twice at once
        QTRY_COMPARE(checked.size(), 1);
        QVERIFY(checked.first().first().toBool());
        QVERIFY(hub.onlineChecked());
        QVariantMap video = find(hub, "drivevault.videoeditor");
        QVERIFY(video.value("onlineUpdate").toBool());
        QCOMPARE(video.value("onlineVersion").toString(), QStringLiteral("2.0.0"));
        QVERIFY(!video.value("onlineInstall").toBool());              // installed: it is an update, not an install
        QVERIFY2(hub.message().contains("1 update"), qPrintable(hub.message()));
        QCOMPARE(find(hub, "drivevault.audioeditor").value("onlineVersion").toString(), QString());

        // a download whose checksum does not match is deleted and never started
        writeHash(QString(64, QLatin1Char('0')));
        QSignalSpy downloaded(&hub, &Hub::downloadFinished);
        QVERIFY(hub.downloadAndInstall("drivevault.videoeditor").isEmpty());
        QVERIFY(hub.busy());
        QTRY_COMPARE(downloaded.size(), 1);
        QVERIFY(!downloaded.first().first().toBool());
        QVERIFY2(hub.message().contains("SHA-256"), qPrintable(hub.message()));
        QVERIFY(!hub.busy());
        QCOMPARE(hub.downloadProgress(), -1.0);
        QVERIFY(!QFileInfo::exists(p("data/installers/" + name)));
        QVERIFY(!QFileInfo::exists(p("data/installers/" + name + ".part")));
        QVERIFY(!QFileInfo::exists(p("online-marker.txt")));

        // the right checksum: downloaded, version resource checked, run silently, and the file removed after success
        writeHash(goodHash);
        downloaded.clear();
        QSignalSpy finished(&hub, &Hub::taskFinished);
        QVERIFY(hub.downloadAndInstall("drivevault.videoeditor").isEmpty());
        QTRY_COMPARE(downloaded.size(), 1);
        QVERIFY(downloaded.first().first().toBool());
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.first().first().toInt(), 0);
        QFile marker(p("online-marker.txt"));
        QVERIFY(marker.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromLocal8Bit(marker.readAll()).contains("/VERYSILENT"));
        marker.close();
        QVERIFY(!QFileInfo::exists(p("data/installers/" + name)));

        // a download address that is not the official one is refused before anything is fetched
        Hub::Options strict = options();
        strict.releasesUrl = o.releasesUrl;                          // default downloadPrefix = the official GitHub address
        Hub other(strict);
        QSignalSpy checked2(&other, &Hub::checkFinished);
        QVERIFY(other.checkForUpdates().isEmpty());
        QTRY_COMPARE(checked2.size(), 1);
        QVERIFY(!other.downloadAndInstall("drivevault.videoeditor").isEmpty());
        QVERIFY(!other.busy());
        qunsetenv("DM_FAKE_EXIT");
        qunsetenv("DM_FAKE_MARKER");
    }

    // GitHub sometimes answers 504 for a release file: the hub tries again, and when it gives up it says what the server said.
    void aBusyServerIsRetriedAndTheHttpStatusIsShown()
    {
        const QString name = QStringLiteral("DriveVault-Video-Editor-Pro-Setup-x64.exe");
        const QString exe = fakeSetup(p("busy-app/DriveVaultVideoEditorPro.exe"));
        const QString served = fakeSetup(p("busy/" + name));
        QVERIFY(!exe.isEmpty() && !served.isEmpty());
        QFile bytes(served);
        QVERIFY(bytes.open(QIODevice::ReadOnly));
        const QByteArray body = bytes.readAll();
        bytes.close();
        QFile hashFile(p("busy/" + name + ".sha256"));
        QVERIFY(hashFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        hashFile.write(QStringLiteral("%1 *%2\n").arg(QString::fromLatin1(QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex()), name).toLatin1());
        hashFile.close();

        // a tiny HTTP server: the first `failFirst` requests of every path get 504, the next ones the file
        int failFirst = 2;
        QMap<QString, int> hits;
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        connect(&server, &QTcpServer::newConnection, this, [&] {
            while (QTcpSocket* s = server.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [&, s] {
                    const QByteArray req = s->readAll();
                    const QString path = QString::fromLatin1(req.split(' ').value(1));
                    const bool fail = ++hits[path] <= failFirst;
                    QByteArray out;
                    if (fail) {
                        out = "HTTP/1.1 504 Gateway Time-out\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                    } else {
                        QFile f(p("busy/" + path.section('/', -1)));
                        const QByteArray data = f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
                        out = "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(data.size()) + "\r\nConnection: close\r\n\r\n" + data;
                    }
                    s->write(out);
                    s->disconnectFromHost();
                });
            }
        });
        const QString base = QStringLiteral("http://127.0.0.1:%1/rel/").arg(server.serverPort());
        QFile feed(p("busy-feed.json"));
        QVERIFY(feed.open(QIODevice::WriteOnly | QIODevice::Truncate));
        feed.write(QJsonDocument(QJsonArray{QJsonObject{
            {"tag_name", "drivevault-video-editor-pro-v2.0.0"}, {"draft", false}, {"prerelease", false},
            {"html_url", "https://example.invalid/releases/tag/drivevault-video-editor-pro-v2.0.0"},
            {"assets", QJsonArray{QJsonObject{{"name", name}, {"browser_download_url", base + name}, {"size", body.size()}},
                                  QJsonObject{{"name", name + ".sha256"}, {"browser_download_url", base + name + ".sha256"}, {"size", 1}}}}}}).toJson());
        feed.close();
        writeProduct("VideoEditorPro", {{"DisplayName", "DriveVault Video Editor Pro"}, {"Version", "1.0.0"}, {"InstallDir", QFileInfo(exe).absolutePath()},
                                         {"Executable", exe}, {"Uninstaller", exe}});
        qputenv("DM_FAKE_MARKER", p("busy-marker.txt").toLocal8Bit());
        qputenv("DM_FAKE_EXIT", "0");
        QFile::remove(p("busy-marker.txt"));
        QFile::remove(p("app/installers/" + name));

        Hub::Options o = options();
        o.releasesUrl = QUrl::fromLocalFile(p("busy-feed.json")).toString();
        o.downloadPrefix = base;
        o.retryDelayMs = 20;
        o.maxRetries = 2;
        o.pagePrefix = QStringLiteral("https://example.invalid/releases/");
        o.openPageOnFailure = false;                                     // never start a real browser in a test

        {   // two 504 answers per file, then the file: the checksum and the setup are both fetched on the third try
            Hub hub(o);
            hub.setLanguage("en");
            hub.setSilentInstalls(true);
            QSignalSpy checked(&hub, &Hub::checkFinished);
            QVERIFY(hub.checkForUpdates().isEmpty());
            QTRY_COMPARE(checked.size(), 1);
            QSignalSpy downloaded(&hub, &Hub::downloadFinished);
            QSignalSpy pageOpened(&hub, &Hub::releasePageOpened);
            QVERIFY(hub.downloadAndInstall("drivevault.videoeditor").isEmpty());
            QTRY_COMPARE_WITH_TIMEOUT(downloaded.size(), 1, 20000);
            QVERIFY2(downloaded.first().first().toBool(), qPrintable(hub.message()));
            QCOMPARE(pageOpened.size(), 0);                           // a download that worked does not open the browser
            QTRY_VERIFY(QFileInfo::exists(p("busy-marker.txt")));
            QCOMPARE(hits.value("/rel/" + name + ".sha256"), 3);
            QCOMPARE(hits.value("/rel/" + name), 3);
            QTRY_VERIFY(!hub.busy());
        }

        {   // the server stays busy: after the allowed attempts the user is told the HTTP status
            failFirst = 100;
            hits.clear();
            o.maxRetries = 1;
            Hub hub(o);
            hub.setLanguage("en");
            QSignalSpy checked(&hub, &Hub::checkFinished);
            QVERIFY(hub.checkForUpdates().isEmpty());
            QTRY_COMPARE(checked.size(), 1);
            QSignalSpy downloaded(&hub, &Hub::downloadFinished);
            QSignalSpy pageOpened(&hub, &Hub::releasePageOpened);
            QVERIFY(hub.downloadAndInstall("drivevault.videoeditor").isEmpty());
            QTRY_COMPARE_WITH_TIMEOUT(downloaded.size(), 1, 20000);
            QVERIFY(!downloaded.first().first().toBool());
            QVERIFY2(hub.message().contains("HTTP 504"), qPrintable(hub.message()));
            // the user is sent to the release page to download by hand
            QCOMPARE(pageOpened.size(), 1);
            QCOMPARE(pageOpened.first().first().toString(), QStringLiteral("https://example.invalid/releases/tag/drivevault-video-editor-pro-v2.0.0"));
            QVERIFY2(hub.message().contains("release page"), qPrintable(hub.message()));
            QCOMPARE(hits.value("/rel/" + name + ".sha256"), 2);       // the first try and one more
            QVERIFY(!hub.busy());
        }
        qunsetenv("DM_FAKE_EXIT");
        qunsetenv("DM_FAKE_MARKER");
    }

    void windowStateAndTheStartCheckSettingArePersisted()
    {
        {
            Hub hub(options());
            QVERIFY(hub.autoCheck());                                   // on by default
            QCOMPARE(hub.windowState().value("width").toInt(), 0);      // nothing remembered yet
            hub.saveWindowState(true, 120, 80, 1000, 700);
            hub.setAutoCheck(false);
        }
        Hub again(options());
        QVERIFY(!again.autoCheck());
        const QVariantMap w = again.windowState();
        QVERIFY(w.value("maximized").toBool());
        QCOMPARE(w.value("width").toInt(), 1000);
        QCOMPARE(w.value("height").toInt(), 700);
        QCOMPARE(w.value("x").toInt(), 120);
        again.saveWindowState(false, 10, 20, 100, 100);                // too small to be a real window: only the maximized flag changes
        QVERIFY(!again.windowState().value("maximized").toBool());
        QCOMPARE(again.windowState().value("width").toInt(), 1000);
        // with the setting off the start-up check does nothing
        QSignalSpy checked(&again, &Hub::checkFinished);
        again.checkForUpdatesOnStart();
        QVERIFY(!again.checking());
        QCOMPARE(checked.size(), 0);
    }

    void aNewerMasterProReleaseIsOfferedAndTheStartCheckIsQuiet()
    {
        auto url = [&](const QString& file) { return QUrl::fromLocalFile(p(file)).toString(); };
        const QString name = QStringLiteral("DriveVault-Master-Pro-Setup-x64.exe");
        QFile feed(p("master-feed.json"));
        QVERIFY(feed.open(QIODevice::WriteOnly));
        feed.write(QJsonDocument(QJsonArray{QJsonObject{
            {"tag_name", "drivevault-master-pro-v9.9.9"}, {"draft", false}, {"prerelease", false}, {"html_url", "https://example.invalid"},
            {"assets", QJsonArray{QJsonObject{{"name", name}, {"browser_download_url", url("rel/" + name)}, {"size", 1}},
                                  QJsonObject{{"name", name + ".sha256"}, {"browser_download_url", url("rel/" + name + ".sha256")}, {"size", 1}}}}}}).toJson());
        feed.close();
        Hub::Options o = options();
        o.releasesUrl = url("master-feed.json");
        Hub hub(o);
        hub.setLanguage("en");
        hub.setAutoCheck(true);                                        // an earlier test switched it off in the shared settings file
        QVERIFY(hub.selfUpdateVersion().isEmpty());
        QVERIFY(!hub.updateSelf().isEmpty());                          // nothing known yet
        QSignalSpy checked(&hub, &Hub::checkFinished);
        hub.checkForUpdatesOnStart();                                  // auto check is on by default
        QVERIFY(hub.checking());
        QTRY_COMPARE(checked.size(), 1);
        QCOMPARE(hub.selfUpdateVersion(), QStringLiteral("9.9.9"));
        QVERIFY2(hub.message().contains("1 update"), qPrintable(hub.message()));   // Master Pro itself counts
        // a quiet check that finds nothing new says nothing
        Hub::Options none = options();
        none.releasesUrl = QUrl::fromLocalFile(p("empty-feed.json")).toString();
        QFile empty(p("empty-feed.json"));
        QVERIFY(empty.open(QIODevice::WriteOnly));
        empty.write("[]");
        empty.close();
        Hub quiet(none);
        quiet.setLanguage("en");
        QSignalSpy done(&quiet, &Hub::checkFinished);
        quiet.checkForUpdatesOnStart();
        QTRY_COMPARE(done.size(), 1);
        QVERIFY2(quiet.message().isEmpty(), qPrintable(quiet.message()));
    }

    void anEmptyReleaseListIsNotReportedAsUpToDate()
    {
        QFile feed(p("empty-feed.json"));
        QVERIFY(feed.open(QIODevice::WriteOnly));
        feed.write("[]");
        feed.close();
        Hub::Options o = options();
        o.releasesUrl = QUrl::fromLocalFile(p("empty-feed.json")).toString();
        Hub hub(o);
        hub.setLanguage("th");
        QSignalSpy checked(&hub, &Hub::checkFinished);
        QVERIFY(hub.checkForUpdates().isEmpty());
        QTRY_COMPARE(checked.size(), 1);
        QVERIFY(checked.first().first().toBool());
        QVERIFY2(hub.message().contains(QString::fromUtf8("ยังไม่มีการเผยแพร่")), qPrintable(hub.message()));     // real Thai, not mojibake
        hub.setLanguage("en");
        checked.clear();
        QVERIFY(hub.checkForUpdates().isEmpty());
        QTRY_COMPARE(checked.size(), 1);
        QVERIFY2(hub.message().contains("No program releases"), qPrintable(hub.message()));
    }

    void aFailedCheckSaysSoAndChangesNothing()
    {
        Hub::Options o = options();
        o.releasesUrl = QUrl::fromLocalFile(p("no-such-feed.json")).toString();
        Hub hub(o);
        hub.setLanguage("en");
        QSignalSpy checked(&hub, &Hub::checkFinished);
        QVERIFY(hub.checkForUpdates().isEmpty());
        QTRY_COMPARE(checked.size(), 1);
        QVERIFY(!checked.first().first().toBool());
        QVERIFY(!hub.checking());
        QVERIFY(!hub.onlineChecked());
        QVERIFY2(hub.message().contains("Could not check"), qPrintable(hub.message()));
    }

    void documentsAreOnlyLookedUpByPlainName()
    {
        QDir().mkpath(p("app/licenses"));
        QFile f(p("app/licenses/THIRD-PARTY-NOTICES.txt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("notices");
        f.close();
        Hub hub(options());
        QVERIFY(!hub.documentPath("THIRD-PARTY-NOTICES.txt").isEmpty());
        QVERIFY(hub.documentPath("../x.txt").isEmpty());
        QVERIFY(hub.documentPath("a\\b.txt").isEmpty());
        QVERIFY(hub.documentPath("missing.txt").isEmpty());
    }
};

QTEST_MAIN(TstHub)
#include "tst_hub.moc"
