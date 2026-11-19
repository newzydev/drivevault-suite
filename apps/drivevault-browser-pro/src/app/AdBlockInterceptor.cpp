#include "AdBlockInterceptor.h"

#include "ListManager.h"
#include "StandInHandler.h"
#include "MediaPolicy.h"
#include "StandIns.h"

#include <QMutexLocker>
#include <QVariantMap>
#include <QUrl>
#include <QWebEngineUrlRequestInfo>

namespace dv::br {

namespace {

quint16 typeOf(QWebEngineUrlRequestInfo::ResourceType t)
{
    using R = QWebEngineUrlRequestInfo;
    switch (t) {
    case R::ResourceTypeMainFrame: case R::ResourceTypeNavigationPreloadMainFrame: return ab::Document;
    case R::ResourceTypeSubFrame: case R::ResourceTypeNavigationPreloadSubFrame: return ab::Subdocument;
    case R::ResourceTypeStylesheet: return ab::Stylesheet;
    case R::ResourceTypeScript: return ab::Script;
    case R::ResourceTypeImage: case R::ResourceTypeFavicon: return ab::Image;
    case R::ResourceTypeFontResource: return ab::Font;
    case R::ResourceTypeObject: case R::ResourceTypePluginResource: return ab::Object;
    case R::ResourceTypeMedia: return ab::Media;
    case R::ResourceTypeXhr: return ab::Xhr;
    case R::ResourceTypePing: case R::ResourceTypeCspReport: return ab::Ping;
    default: return ab::Other;
    }
}

} // namespace

AdBlockInterceptor::AdBlockInterceptor(QObject* parent) : QWebEngineUrlRequestInterceptor(parent) {}

void AdBlockInterceptor::setPausedHosts(const QSet<QString>& hosts)
{
    QMutexLocker lock(&m_lock);
    m_paused = hosts;
}

int AdBlockInterceptor::blockedOn(const QString& host) const
{
    QMutexLocker lock(&m_lock);
    return m_perHost.value(host);
}

QVariantList AdBlockInterceptor::mediaFor(const QString& host) const
{
    QVariantList out;
    QMutexLocker lock(&m_lock);
    for (const MediaItem& m : m_media.value(host))
        out.append(QVariantMap{{QStringLiteral("url"), m.url}, {QStringLiteral("kind"), m.kind}, {QStringLiteral("pageUrl"), m.pageUrl},
                               {QStringLiteral("name"), media::mediaFileName(m.url, m.kind)}});
    return out;
}

void AdBlockInterceptor::noteMedia(const QWebEngineUrlRequestInfo& info, const QUrl& url, bool fromMediaElement, const QString& pageHost)
{
    const QString address = url.toString(QUrl::FullyEncoded);
    const QString kind = media::classifyMediaUrl(address, fromMediaElement);
    if (kind.isEmpty() || media::hostIsExcluded(pageHost) || media::hostIsExcluded(url.host()))
        return;
    QMutexLocker lock(&m_lock);
    QVector<MediaItem>& list = m_media[pageHost];
    for (const MediaItem& m : std::as_const(list))
        if (m.url == address)
            return;                                                // a player asks for the same file again and again
    if (list.size() >= 16)
        list.removeFirst();
    list.append({address, kind, info.firstPartyUrl().toString(QUrl::FullyEncoded)});
    ++m_mediaVersion;
}

void AdBlockInterceptor::interceptRequest(QWebEngineUrlRequestInfo& info)
{
    const QUrl url = info.requestUrl();
    const QString scheme = url.scheme();
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return;                                                    // our own pages, file:, data:, devtools:
    const quint16 type = typeOf(info.resourceType());
    const QString pageHost = info.firstPartyUrl().host().toLower();
    if (type == ab::Document) {
        QMutexLocker lock(&m_lock);
        const QString host = url.host().toLower();
        m_perHost.insert(host, 0);                                 // a new page starts counting again
        if (m_media.remove(host) > 0)
            ++m_mediaVersion;
        return;
    }
    if (filter(info, url, type, pageHost))
        return;
    noteMedia(info, url, info.resourceType() == QWebEngineUrlRequestInfo::ResourceTypeMedia, pageHost);
}

bool AdBlockInterceptor::filter(QWebEngineUrlRequestInfo& info, const QUrl& url, quint16 type, const QString& pageHost)
{
    ab::ListManager* manager = m_manager.load();
    if (!manager || !m_enabled.load())
        return false;
    {
        QMutexLocker lock(&m_lock);
        if (m_paused.contains(pageHost))
            return false;
    }
    const QString address = url.toString(QUrl::FullyEncoded);
    const ab::ListManager::Decision d = manager->match(address, info.firstPartyUrl().toString(QUrl::FullyEncoded), type);
    if (!d.blocked)
        return false;
    // A request is answered with a small stand-in file when the list says so ($redirect=) or when it is an ad / analytics library that pages test for;
    // otherwise it is stopped.
    ab::StandIn standInFile;
    QString name;
    if (!d.redirect.isEmpty() && ab::standIn(d.redirect, &standInFile))
        name = d.redirect;
    else
        name = ab::standInNameForUrl(address, type);
    if (name.isEmpty() && type == ab::Image)
        name = QStringLiteral("1x1.gif");                        // a blocked picture "loads" as a transparent dot: no error event for a page that watches for one
    if (!name.isEmpty())
        info.redirect(StandInHandler::urlFor(name));
    else
        info.block(true);
    ++m_total;
    QMutexLocker lock(&m_lock);
    ++m_perHost[pageHost];
    return true;
}

} // namespace dv::br
