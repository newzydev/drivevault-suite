#pragma once

#include "AdBlockController.h"
#include "BookmarkStore.h"
#include "BrowserSettings.h"
#include "DownloadModel.h"
#include "HistoryStore.h"

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

#include <QtWebEngineQuick/QQuickWebEngineProfile>

namespace dv::br {

// The one object the QML pages talk to (QML singleton "Browser"): the stores, the downloads, the shared profile and a few helpers.
class BrowserApp : public QObject {
    Q_OBJECT
    Q_PROPERTY(dv::br::BrowserSettings* settings READ settings CONSTANT)
    Q_PROPERTY(dv::br::BookmarkStore* bookmarks READ bookmarks CONSTANT)
    Q_PROPERTY(dv::br::HistoryStore* history READ history CONSTANT)
    Q_PROPERTY(dv::br::DownloadModel* downloads READ downloads CONSTANT)
    Q_PROPERTY(dv::br::AdBlockController* adblock READ adblock CONSTANT)
    Q_PROPERTY(QQuickWebEngineProfile* profile READ profile CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString engineInfo READ engineInfo CONSTANT)
    Q_PROPERTY(QString dataDir READ dataDir CONSTANT)
    Q_PROPERTY(QStringList initialUrls READ initialUrls CONSTANT)
    Q_PROPERTY(bool startPrivate READ startPrivate CONSTANT)
public:
    BrowserApp(const QString& dataDir, QQuickWebEngineProfile* profile, AdBlockInterceptor* interceptor, QObject* parent = nullptr);

    BrowserSettings* settings() { return &m_settings; }
    BookmarkStore* bookmarks() { return &m_bookmarks; }
    HistoryStore* history() { return &m_history; }
    DownloadModel* downloads() { return &m_downloads; }
    AdBlockController* adblock() { return &m_adblock; }
    QQuickWebEngineProfile* profile() const { return m_profile; }
    QString version() const;
    QString engineInfo() const;
    QString dataDir() const { return m_dataDir; }
    QStringList initialUrls() const { return m_initialUrls; }
    void setInitialUrls(const QStringList& urls) { m_initialUrls = urls; }
    bool startPrivate() const { return m_startPrivate; }
    void setStartPrivate(bool v) { m_startPrivate = v; }

    Q_INVOKABLE QUrl resolve(const QString& typed) const;                              // address bar text -> address
    Q_INVOKABLE QString prettyUrl(const QString& url) const;
    Q_INVOKABLE QString hostOf(const QString& url) const;
    Q_INVOKABLE QVariantList suggest(const QString& text, int limit = 6) const;        // bookmarks and history that match: [{title, url, kind}]
    Q_INVOKABLE QString localPath(const QUrl& url) const;                             // file: address -> path
    Q_INVOKABLE void openWindow(bool privateMode, const QString& url = QString());
    Q_INVOKABLE QStringList loadSession() const;
    Q_INVOKABLE void saveSession(const QStringList& urls) const;
    Q_INVOKABLE void copyText(const QString& text) const;
    Q_INVOKABLE void clearBrowsingData(bool history, bool cache);                      // history + the page cache (cookies are not cleared yet)

signals:
    void windowRequested(bool privateMode, const QString& url);

private:
    QString m_dataDir;
    QQuickWebEngineProfile* m_profile;
    BrowserSettings m_settings;
    BookmarkStore m_bookmarks;
    HistoryStore m_history;
    AdBlockController m_adblock;
    DownloadModel m_downloads;
    QStringList m_initialUrls;
    bool m_startPrivate = false;
};

} // namespace dv::br
