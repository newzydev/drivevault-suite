#include "MediaController.h"

#include "MediaPolicy.h"

#include <QUrl>

namespace dv::br {

MediaController::MediaController(AdBlockInterceptor* interceptor, DownloadModel* downloads, QObject* parent)
    : QObject(parent), m_interceptor(interceptor), m_downloads(downloads)
{
    m_poll.setInterval(700);                                   // the network thread only notes; this tells the pages
    connect(&m_poll, &QTimer::timeout, this, [this] {
        const int v = m_interceptor->mediaVersion();
        if (v != m_lastVersion) {
            m_lastVersion = v;
            ++m_tick;
            emit tickChanged();
        }
    });
    m_poll.start();

    connect(&m_saver, &media::MediaSaver::started, this, [this](int id, const QString& name, const QString& path) {
        m_ids.insert(id, m_downloads->addExternal(name, path, [this, id] { m_saver.cancel(id); }));
    });
    connect(&m_saver, &media::MediaSaver::progress, this, [this](int id, qint64 got, qint64 total, const QString& unit) {
        if (m_ids.contains(id))
            m_downloads->updateExternal(m_ids.value(id), got, total, unit);
    });
    connect(&m_saver, &media::MediaSaver::finished, this, [this](int id, bool ok, const QString& error, const QString& path) {
        if (m_ids.contains(id))
            m_downloads->finishExternal(m_ids.take(id), ok, path, error);
        emit saveFinished(ok, error, path);
    });
}

bool MediaController::save(const QString& url, const QString& kind, const QString& pageUrl)
{
    const QUrl u(url), page(pageUrl);
    if (media::hostIsExcluded(u.host()) || media::hostIsExcluded(page.host())) {
        emit refused(QStringLiteral("excluded"), page.host());
        return false;
    }
    if ((kind == QLatin1String("hls") || kind == QLatin1String("dash")) && !ffmpegAvailable()) {
        emit refused(QStringLiteral("noffmpeg"), QString());
        return false;
    }
    media::MediaSaver::Request r;
    r.url = url;
    r.kind = kind;
    r.pageUrl = pageUrl;
    r.userAgent = m_userAgent;
    r.folder = m_downloads->folder();
    if (m_saver.start(r) == 0) {
        emit refused(QStringLiteral("failed"), m_saver.lastError());
        return false;
    }
    return true;
}

} // namespace dv::br
