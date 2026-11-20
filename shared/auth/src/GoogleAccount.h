#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

class QFileSystemWatcher;
class QNetworkAccessManager;
class QTcpServer;
class QTimer;

namespace dv::account {

// The Google account of the person using the DriveVault programs. One account for the whole suite: it is kept in ONE file per Windows user
// (%LOCALAPPDATA%\DriveVault\Account\account.bin, encrypted with Windows DPAPI), every program watches that file, so signing in or out in
// one program shows in the others at once (they are running or started later).
//
// What is kept: the Google id ("sub"), name, e-mail and a 128 px copy of the picture. NO token is kept and nothing is sent anywhere by
// this class except to Google while signing in. It is a local identity for display, not a login to any DriveVault server.
class GoogleAccount final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool signedIn READ signedIn NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString email READ email NOTIFY changed)
    Q_PROPERTY(QUrl picture READ picture NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString error READ error NOTIFY busyChanged)   // "" | "unavailable" | "failed" | "denied" | "timeout"
public:
    explicit GoogleAccount(QObject* parent = nullptr);
    ~GoogleAccount() override;

    bool signedIn() const { return m_identity.contains(QLatin1String("sub")); }
    QString name() const;
    QString email() const { return m_identity.value(QLatin1String("email")).toString(); }
    QUrl picture() const;
    bool busy() const { return m_server != nullptr || m_working; }
    QString error() const { return m_error; }

    // The folder of the shared file. DV_ACCOUNT_DIR replaces it (tests and smoke starts must never touch the real account; in that folder
    // the file is plain JSON "account.json" so a test can write one).
    static QString storeDir();

    Q_INVOKABLE void signIn();        // opens Google in the system browser and waits (3 minutes) for it to call back
    Q_INVOKABLE void cancelSignIn();
    Q_INVOKABLE void signOut();       // for every program: the shared file is removed
    Q_INVOKABLE void clearError();

    // For tests: read the shared file now.
    void reload();

signals:
    void changed();
    void authorizeUrl(const QUrl& url);   // the address that was handed to the browser (tests read the loopback port and state from it)
    void busyChanged();

private:
    void finish(const QString& error);
    void exchange(const QString& code);
    void fetchProfile(const QByteArray& accessToken);
    void fetchPicture(const QJsonObject& identity, const QUrl& address);
    void store(const QJsonObject& identity);
    QString filePath() const;
    bool readFile(QJsonObject* out) const;
    void watchFile();

    QJsonObject m_identity;
    QString m_error;
    QTcpServer* m_server = nullptr;
    QNetworkAccessManager* m_net = nullptr;
    QTimer* m_deadline = nullptr;
    QTimer* m_poll = nullptr;
    QFileSystemWatcher* m_watcher = nullptr;
    QByteArray m_state;
    QByteArray m_verifier;
    QString m_redirect;
    qint64 m_stamp = -1;
    bool m_working = false;
    bool m_own = false;
};

} // namespace dv::account
