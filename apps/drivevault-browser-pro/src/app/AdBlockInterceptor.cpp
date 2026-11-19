#include "AdBlockInterceptor.h"

#include "ListManager.h"

#include <QMutexLocker>
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
        m_perHost.insert(url.host().toLower(), 0);                 // a new page starts counting again
        return;
    }
    ab::ListManager* manager = m_manager.load();
    if (!manager || !m_enabled.load())
        return;
    {
        QMutexLocker lock(&m_lock);
        if (m_paused.contains(pageHost))
            return;
    }
    const ab::ListManager::Decision d = manager->match(url.toString(QUrl::FullyEncoded), info.firstPartyUrl().toString(QUrl::FullyEncoded), type);
    if (!d.blocked)
        return;
    info.block(true);
    ++m_total;
    QMutexLocker lock(&m_lock);
    ++m_perHost[pageHost];
}

} // namespace dv::br
