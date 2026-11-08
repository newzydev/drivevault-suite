#pragma once

#include "Releases.h"

#include <QMap>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class QNetworkAccessManager;
class QNetworkReply;
class QThread;

namespace dm {

// The brain of DriveVault Master Pro: finds the installed DriveVault programs (their registry entries under
// Software\DriveVault\Products, written by every product installer), launches / uninstalls them, and installs or updates
// them from setup files in the installer folders. Nothing here pretends to be an online store: only setup files that exist
// on this computer are offered.
class Hub : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList products READ products NOTIFY productsChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged FINAL)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged FINAL)     // "th" | "en"
    Q_PROPERTY(QStringList installerFolders READ installerFolders NOTIFY productsChanged FINAL)
    Q_PROPERTY(QString version READ version CONSTANT FINAL)
    Q_PROPERTY(QString buildNumber READ buildNumber CONSTANT FINAL)
    Q_PROPERTY(QString suiteDirectory READ suiteDirectory CONSTANT FINAL)
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged FINAL)                        // asking GitHub for releases
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadProgressChanged FINAL)   // 0..1 while downloading, -1 otherwise
    Q_PROPERTY(bool onlineChecked READ onlineChecked NOTIFY productsChanged FINAL)
    Q_PROPERTY(bool autoCheck READ autoCheck WRITE setAutoCheck NOTIFY autoCheckChanged FINAL)    // check for updates when the program starts
    Q_PROPERTY(QString selfUpdateVersion READ selfUpdateVersion NOTIFY productsChanged FINAL)    // newer Master Pro release, "" when none

public:
    struct Options {
        QString registryRoot = QStringLiteral("Software\\DriveVault");   // tests use another key
        QString dataDir;                                                  // settings; default %LOCALAPPDATA%\DriveVault\MasterPro
        QString appDir;
        // Where the releases come from. The defaults are the official repository; tests point both at local files.
        QString releasesUrl = QStringLiteral("https://api.github.com/repos/newzydev/drivevault-suite/releases?per_page=30");
        QString downloadPrefix = QStringLiteral("https://github.com/newzydev/drivevault-suite/releases/download/");                                                   // where installers\ and licenses\ live; default: the program folder
        // A download that fails because the server is busy (HTTP 502 / 503 / 504, timeout, connection cut) is tried again.
        int maxRetries = 2;                                               // extra attempts after the first one
        int retryDelayMs = 2500;
    };
    explicit Hub(const Options& options, QObject* parent = nullptr);
    ~Hub() override;

    QVariantList products() const { return m_products; }
    bool busy() const { return m_busy; }
    QString message() const { return m_message; }
    QString language() const { return m_language; }
    void setLanguage(const QString& language);
    QStringList installerFolders() const;
    QString version() const;
    QString buildNumber() const;
    QString suiteDirectory() const { return m_appDir; }
    bool checking() const { return m_checking; }
    double downloadProgress() const { return m_progress; }
    bool onlineChecked() const { return m_onlineChecked; }
    bool autoCheck() const { return m_autoCheck; }
    void setAutoCheck(bool on);
    QString selfUpdateVersion() const;

    // Re-reads the registry and the installer folders.
    Q_INVOKABLE void refresh();
    // Each returns "" on success (or when the work was started), otherwise why not (already in the user's language).
    Q_INVOKABLE QString launch(const QString& productId);
    Q_INVOKABLE QString openFolder(const QString& productId);
    Q_INVOKABLE QString uninstall(const QString& productId);                       // runs the product's own uninstaller
    Q_INVOKABLE QString installProduct(const QString& productId);                  // setup file found in an installer folder
    Q_INVOKABLE QString installFromFile(const QString& fileOrUrl);                 // setup file picked by the user
    // Online (only when the user presses the button): asks GitHub Releases which versions exist, then downloads one setup file,
    // checks its SHA-256 against the release's .sha256 file and its version resource, and runs it like any other setup file.
    Q_INVOKABLE QString checkForUpdates();
    Q_INVOKABLE QString downloadAndInstall(const QString& productId);
    Q_INVOKABLE QString updateSelf();                                               // Master Pro's own newer release
    Q_INVOKABLE void checkForUpdatesOnStart();                                      // quiet check, only when `autoCheck` is on
    Q_INVOKABLE QVariantMap windowState() const;                                    // {maximized, width, height, x, y}
    Q_INVOKABLE void saveWindowState(bool maximized, int x, int y, int width, int height);
    Q_INVOKABLE QString addInstallerFolder(const QString& folderOrUrl);
    Q_INVOKABLE QString removeInstallerFolder(const QString& folder);
    Q_INVOKABLE QString openInstallersFolder();
    Q_INVOKABLE QString documentPath(const QString& fileName) const;               // licenses\ and docs\ next to the program
    Q_INVOKABLE bool openDocument(const QString& fileName) const;
    Q_INVOKABLE QString tr2(const QString& english, const QString& thai) const { return m_language == QLatin1String("th") ? thai : english; }

    // Blocks until the running task has finished (tests).
    void waitForIdle(int timeoutMs = 60000);
    // The setup files that qualify (company DriveVault, product name known) in all folders, newest first: [{path, product, version}].
    QVariantList scanInstallers() const;
    // Silent switch of the install: tests use it so that no wizard opens.
    void setSilentInstalls(bool silent) { m_silent = silent; }
    // The start-up check is off for screenshots and diagnostics (they must never touch the network).
    void setStartCheckEnabled(bool on) { m_startCheckEnabled = on; }

signals:
    void productsChanged();
    void busyChanged();
    void messageChanged();
    void languageChanged();
    void taskFinished(int exitCode);
    void checkingChanged();
    void autoCheckChanged();
    void downloadProgressChanged();
    void checkFinished(bool ok);
    void downloadFinished(bool ok);

private:
    struct Catalog;
    QVariantMap productById(const QString& id) const;
    QString run(const QString& file, const QString& arguments, const QString& workDir, const QString& startMessage, const QString& doneMessage);
    void setMessage(const QString& m);
    QStringList userFolders() const;
    void saveFolders(const QStringList& folders);
    QNetworkAccessManager* net();
    void failDownload(const QString& why);
    QString startCheck(bool quiet);
    QString beginDownload(const QString& slug, const QString& name, bool self);
    void fetchChecksum(const releases::Release& rel, const QString& name, bool self, int attempt);
    void startAssetDownload(const releases::Release& rel, const QString& productName, const QString& expectedHash, bool self, int attempt = 0);
    QString httpProblem(QNetworkReply* reply) const;       // "HTTP 504 (server busy)" or the network error text
    static bool retryable(QNetworkReply* reply);

    Options m_options;
    QString m_appDir;
    QString m_dataDir;
    QString m_language;
    QString m_message;
    QVariantList m_products;
    QThread* m_thread = nullptr;
    bool m_busy = false;
    bool m_silent = false;
    int m_lastExit = 0;
    QNetworkAccessManager* m_net = nullptr;
    QMap<QString, releases::Release> m_online;     // best release per slug, from the last check
    bool m_checking = false;
    bool m_onlineChecked = false;
    bool m_autoCheck = true;
    bool m_startCheckEnabled = true;
    double m_progress = -1;
    QString m_cleanupFile;                          // a downloaded setup file that is deleted after a successful run
};

} // namespace dm
