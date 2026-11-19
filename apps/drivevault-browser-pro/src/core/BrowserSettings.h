#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>

namespace dv::br {

// The user's settings (JSON file, saved at once). Language "th" | "en"; startup "newtab" | "restore".
class BrowserSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY changed)
    Q_PROPERTY(QString homePage READ homePage WRITE setHomePage NOTIFY changed)
    Q_PROPERTY(QString searchEngine READ searchEngine WRITE setSearchEngine NOTIFY changed)
    Q_PROPERTY(bool showBookmarksBar READ showBookmarksBar WRITE setShowBookmarksBar NOTIFY changed)
    Q_PROPERTY(QString startup READ startup WRITE setStartup NOTIFY changed)
    Q_PROPERTY(bool blockNotifications READ blockNotifications WRITE setBlockNotifications NOTIFY changed)
    Q_PROPERTY(bool adblockEnabled READ adblockEnabled WRITE setAdblockEnabled NOTIFY changed)
public:
    explicit BrowserSettings(const QString& file, QObject* parent = nullptr);

    QString language() const;
    void setLanguage(const QString& v);
    QString homePage() const;                 // "" = the new tab page
    void setHomePage(const QString& v);
    QString searchEngine() const;
    void setSearchEngine(const QString& v);
    bool showBookmarksBar() const;
    void setShowBookmarksBar(bool v);
    QString startup() const;
    void setStartup(const QString& v);
    bool blockNotifications() const;          // websites that ask to show notifications are refused without asking (default on)
    void setBlockNotifications(bool v);

    Q_INVOKABLE double zoomFor(const QString& host) const;               // 1.0 when the user never changed it
    Q_INVOKABLE void setZoomFor(const QString& host, double factor);     // 0.25 .. 5.0; 1.0 forgets the host
    Q_INVOKABLE QVariantList engines() const;                            // [{id, name}]

    bool adblockEnabled() const;               // the ad blocker as a whole (default on)
    void setAdblockEnabled(bool v);
    Q_INVOKABLE bool listOn(const QString& id) const;                 // lists are on unless the user switched them off
    Q_INVOKABLE void setListOn(const QString& id, bool on);
    Q_INVOKABLE bool adblockPaused(const QString& host) const;        // paused on this site
    Q_INVOKABLE void setAdblockPaused(const QString& host, bool paused);
    QStringList pausedHosts() const;

signals:
    void changed();

private:
    void set(const QString& key, const QJsonValue& value);
    bool save() const;

    QString m_file;
    QJsonObject m_data;
};

} // namespace dv::br

