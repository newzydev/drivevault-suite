#pragma once

#include <QHash>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QVariantList>
#include <QVector>
#include <QWebEngineUrlRequestInterceptor>
#include <atomic>

namespace dv::br::ab {
class ListManager;
}

namespace dv::br {

// Looks at every request of every page (called on the network thread of the engine) and blocks what the filter lists name.
// Phase 2: network requests only. The page itself (a navigation) is never blocked, only what it loads.
class AdBlockInterceptor : public QWebEngineUrlRequestInterceptor {
    Q_OBJECT
public:
    explicit AdBlockInterceptor(QObject* parent = nullptr);

    void setManager(ab::ListManager* manager) { m_manager.store(manager); }
    void setEnabled(bool on) { m_enabled.store(on); }
    void setPausedHosts(const QSet<QString>& hosts);
    void interceptRequest(QWebEngineUrlRequestInfo& info) override;

    int blockedOn(const QString& host) const;       // blocked requests of the page now shown for this host (starts again at every navigation)
    int blockedTotal() const { return m_total.load(); }

    // The videos / audio files / playlists a page has loaded (not blocked ones), for "save this video": [{url, kind, pageUrl, name}], newest last. Starts again at every navigation.
    QVariantList mediaFor(const QString& host) const;
    int mediaVersion() const { return m_mediaVersion.load(); }

private:
    bool filter(QWebEngineUrlRequestInfo& info, const QUrl& url, quint16 type, const QString& pageHost);      // true: the request was stopped or answered by a stand-in
    void noteMedia(const QWebEngineUrlRequestInfo& info, const QUrl& url, bool fromMediaElement, const QString& pageHost);
    struct MediaItem {
        QString url, kind, pageUrl;
    };
    QHash<QString, QVector<MediaItem>> m_media;
    std::atomic<int> m_mediaVersion{0};
    std::atomic<ab::ListManager*> m_manager{nullptr};
    std::atomic<bool> m_enabled{true};
    std::atomic<int> m_total{0};
    mutable QMutex m_lock;
    QSet<QString> m_paused;
    QHash<QString, int> m_perHost;
};

} // namespace dv::br
