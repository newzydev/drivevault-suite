#pragma once

#include "AdBlockInterceptor.h"
#include "BrowserSettings.h"
#include "ListManager.h"
#include "StandInHandler.h"

#include <QObject>
#include <QTimer>
#include <QtWebEngineQuick/QQuickWebEngineProfile>

namespace dv::br {

// What the QML pages see of the ad blocker (QML: Browser.adblock): the switches, the lists, the count of blocked requests of the page shown.
class AdBlockController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(int tick READ tick NOTIFY tickChanged)                         // changes when the counts changed (bind to it)
    Q_PROPERTY(dv::br::ab::ListManager* lists READ lists CONSTANT)
public:
    AdBlockController(BrowserSettings* settings, AdBlockInterceptor* interceptor, StandInHandler* standIns, const QString& dataDir, const QString& shippedDir, QObject* parent = nullptr);
    ~AdBlockController() override;

    bool enabled() const { return m_settings->adblockEnabled(); }
    void setEnabled(bool on);
    int tick() const { return m_tick; }
    ab::ListManager* lists() { return &m_lists; }

    void setPageLibrary(const QString& js) { m_library = js; }                // resources/adblock-page.js
    void start();                                                             // read the lists that are on this PC (no network)
    Q_INVOKABLE void attach(QQuickWebEngineProfile* profile);                 // also block in this (private) profile
    // The script for the page that is about to load (QML puts it into the view before the navigation); empty when there is nothing to do or the blocker is off / paused.
    Q_INVOKABLE QString scriptFor(const QString& pageUrl) const;
    Q_INVOKABLE int blockedOn(const QString& host) const { return m_interceptor->blockedOn(host.toLower()); }
    Q_INVOKABLE bool isPaused(const QString& host) const { return m_settings->adblockPaused(host.toLower()); }
    Q_INVOKABLE void setPaused(const QString& host, bool paused);
    Q_INVOKABLE bool listEnabled(const QString& id) const { return m_settings->listOn(id); }
    Q_INVOKABLE void setListEnabled(const QString& id, bool on);
    Q_INVOKABLE void updateLists() { m_lists.update(enabledIds()); }          // the user pressed "Update filter lists"
    Q_INVOKABLE QVariantList listStatus() const { return m_lists.status(); }

signals:
    void changed();
    void tickChanged();

private:
    QStringList enabledIds() const;
    void applySettings();

    BrowserSettings* m_settings;
    AdBlockInterceptor* m_interceptor;
    StandInHandler* m_standIns;
    QString m_library;
    ab::ListManager m_lists;
    QTimer m_poll;
    int m_tick = 0, m_lastTotal = 0;
};

} // namespace dv::br
