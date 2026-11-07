#include "Hub.h"
#include "WinUtil.h"

#include <QSettings>
#include <QSignalSpy>
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
        QCOMPARE(hub.products().size(), 3);
        const QVariantMap video = find(hub, "drivevault.videoeditor");
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
        QCOMPARE(hub.products().size(), 4);
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
