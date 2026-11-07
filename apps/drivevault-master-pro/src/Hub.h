#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

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

public:
    struct Options {
        QString registryRoot = QStringLiteral("Software\\DriveVault");   // tests use another key
        QString dataDir;                                                  // settings; default %LOCALAPPDATA%\DriveVault\MasterPro
        QString appDir;                                                   // where installers\ and licenses\ live; default: the program folder
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

    // Re-reads the registry and the installer folders.
    Q_INVOKABLE void refresh();
    // Each returns "" on success (or when the work was started), otherwise why not (already in the user's language).
    Q_INVOKABLE QString launch(const QString& productId);
    Q_INVOKABLE QString openFolder(const QString& productId);
    Q_INVOKABLE QString uninstall(const QString& productId);                       // runs the product's own uninstaller
    Q_INVOKABLE QString installProduct(const QString& productId);                  // setup file found in an installer folder
    Q_INVOKABLE QString installFromFile(const QString& fileOrUrl);                 // setup file picked by the user
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

signals:
    void productsChanged();
    void busyChanged();
    void messageChanged();
    void languageChanged();
    void taskFinished(int exitCode);

private:
    struct Catalog;
    QVariantMap productById(const QString& id) const;
    QString run(const QString& file, const QString& arguments, const QString& workDir, const QString& startMessage, const QString& doneMessage);
    void setMessage(const QString& m);
    QStringList userFolders() const;
    void saveFolders(const QStringList& folders);

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
};

} // namespace dm
