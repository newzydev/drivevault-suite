#pragma once
#include <QObject>
#include <QJsonObject>
#include <QUrl>

class QTcpServer;
class QTimer;

namespace dv::br {

class ProfileSession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString email READ email CONSTANT)
    Q_PROPERTY(QUrl picture READ picture CONSTANT)
    Q_PROPERTY(bool signedIn READ signedIn CONSTANT)
    Q_PROPERTY(bool locked READ locked NOTIFY lockedChanged)
    Q_PROPERTY(bool guestSession READ guestSession CONSTANT)
    Q_PROPERTY(bool signingIn READ signingIn NOTIFY signInChanged)
    Q_PROPERTY(bool googleSignInWaiting READ googleSignInWaiting NOTIFY googleSignInChanged)
    Q_PROPERTY(bool settingPin READ settingPin NOTIFY signInChanged)
    Q_PROPERTY(QString signInError READ signInError NOTIFY signInChanged)
    Q_PROPERTY(QString profileError READ profileError NOTIFY profileChanged)
    Q_PROPERTY(int idleMinutes READ idleMinutes NOTIFY profileChanged)
public:
    explicit ProfileSession(QString root, bool guestSession = false);
    bool guestSession() const { return m_guestSession; }
    bool start();
    QString directory() const;
    bool signedIn() const;
    bool locked() const { return m_locked; }
    bool signingIn() const { return m_signingIn; }
    bool googleSignInWaiting() const { return m_googleServer != nullptr; }
    bool settingPin() const { return !m_pendingProfile.isEmpty(); }
    QString signInError() const { return m_signInError; }
    QString profileError() const { return m_profileError; }
    int idleMinutes() const;
    bool restartRequested() const { return m_restartRequested; }
    QString name() const;
    QString email() const;
    QUrl picture() const;
    void enableLock();
    Q_INVOKABLE void unlock(const QString& pin);
    Q_INVOKABLE void openGuest(const QString& language);
    Q_INVOKABLE void setIdleMinutes(int minutes);
    Q_INVOKABLE void requestSignOut();
    Q_INVOKABLE void signIn();
    Q_INVOKABLE void cancelSignIn();
    // Google refuses to sign in inside the embedded engine: open Google in the system browser (with the signed-in profile, if any, as the hint) and wait for it to call back.
    // The system browser's sign-in is NOT shared with the pages of this browser; this only brings the user back here. false = could not start.
    Q_INVOKABLE bool signInOnGoogle();
    Q_INVOKABLE void setInitialPin(const QString& pin, const QString& confirmation);

signals:
    void lockedChanged();
    void profileChanged();
    void signInChanged();
    void signInCancelled();
    void googleSignInChanged();
    void googleSignInEnded(bool completed);

protected:
    bool eventFilter(QObject*, QEvent*) override;

private:
    bool load();
    bool save() const;
    bool authenticate();
    void lock();
    void resetTimer();
    bool signOut();
    const QString m_root;
    const bool m_guestSession;
    QJsonObject m_account;
    QJsonObject m_pendingProfile;
    QTimer* m_timer = nullptr;
    bool m_locked = false;
    bool m_signingIn = false;
    bool m_cancelled = false;
    QString m_signInError;
    QString m_profileError;
    bool m_restartRequested = false;
    QTcpServer* m_googleServer = nullptr;
    QString m_googleState;
};

} // namespace dv::br
