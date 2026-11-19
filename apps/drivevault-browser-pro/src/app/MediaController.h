#pragma once

#include "AdBlockInterceptor.h"
#include "DownloadModel.h"
#include "MediaSaver.h"

#include <QHash>
#include <QObject>
#include <QTimer>

namespace dv::br {

// "Save this video" (QML: Browser.media): what the page being shown has loaded, and the saving itself (listed in the Downloads panel like any download).
// Nothing is saved unless the user pressed a button for one entry. YouTube and subscription streaming sites are never offered; DRM is never touched (media/MediaPolicy.h).
class MediaController : public QObject {
    Q_OBJECT
    Q_PROPERTY(int tick READ tick NOTIFY tickChanged)                          // changes when a page loaded another video (bind to it)
    Q_PROPERTY(bool ffmpegAvailable READ ffmpegAvailable CONSTANT)
public:
    MediaController(AdBlockInterceptor* interceptor, DownloadModel* downloads, QObject* parent = nullptr);

    int tick() const { return m_tick; }
    bool ffmpegAvailable() const { return !m_saver.ffmpegPath().isEmpty(); }
    void setUserAgent(const QString& ua) { m_userAgent = ua; }
    media::MediaSaver* saver() { return &m_saver; }

    Q_INVOKABLE QVariantList listFor(const QString& host) const { return m_interceptor->mediaFor(host.toLower()); }
    Q_INVOKABLE int countFor(const QString& host) const { return int(m_interceptor->mediaFor(host.toLower()).size()); }
    // the user asked to save this entry: true when it started; false when refused (the reason comes with the signal)
    Q_INVOKABLE bool save(const QString& url, const QString& kind, const QString& pageUrl);

signals:
    void tickChanged();
    void refused(const QString& reasonCode, const QString& detail);               // "excluded" | "noffmpeg" | "failed"
    void saveFinished(bool ok, const QString& error, const QString& path);

private:
    AdBlockInterceptor* m_interceptor;
    DownloadModel* m_downloads;
    media::MediaSaver m_saver;
    QHash<int, int> m_ids;                       // saver job id -> id of the entry in the downloads list
    QTimer m_poll;
    QString m_userAgent;
    int m_tick = 0, m_lastVersion = 0;
};

} // namespace dv::br
