#pragma once
#include <QObject>
#include <QJsonObject>
#include <QUrl>

class QTimer;
class QDialog;

namespace dv::br {

class ProfileSession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString email READ email CONSTANT)
    Q_PROPERTY(QUrl picture READ picture CONSTANT)
    Q_PROPERTY(bool signedIn READ signedIn CONSTANT)
    Q_PROPERTY(bool locked READ locked NOTIFY lockedChanged)
public:
    ProfileSession(QString root, bool english);
    bool start();
    QString directory() const;
    bool signedIn() const;
    bool locked() const { return m_locked; }
    bool restartRequested() const { return m_restartRequested; }
    QString name() const;
    QString email() const;
    QUrl picture() const;
    void enableLock();
    Q_INVOKABLE void showOptions();
    Q_INVOKABLE void signIn();

signals:
    void lockedChanged();

protected:
    bool eventFilter(QObject*, QEvent*) override;

private:
    bool load();
    bool save() const;
    bool authenticate();
    bool unlock();
    void lock();
    void resetTimer();
    int execProfileDialog(QDialog& dialog);
    bool signOut();
    QString trText(const char* en, const char* th) const;
    const QString m_root;
    const bool m_english;
    QJsonObject m_account;
    QTimer* m_timer = nullptr;
    QDialog* m_profileDialog = nullptr;
    bool m_locked = false;
    bool m_signingIn = false;
    bool m_restartRequested = false;
};

} // namespace dv::br
