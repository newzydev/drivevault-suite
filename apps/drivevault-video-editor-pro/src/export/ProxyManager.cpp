#include "ProxyManager.h"

#include "core/AppPaths.h"
#include "core/Logger.h"
#include "export/Exporter.h"
#include "media/MediaLibrary.h"
#include "project/ProjectManager.h"
#include "settings/SettingsManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QThread>

#include <memory>

namespace dv {

ProxyManager::ProxyManager(ProjectManager* project, SettingsManager* settings, QObject* parent)
    : QObject(parent)
    , m_project(project)
    , m_settings(settings)
{
    // Snapshots (preview, export, AI) ask where the proxy of a file is.
    m_project->setProxyLookup([this](const QString& path) { return proxyPathFor(path); });
}

ProxyManager::~ProxyManager()
{
    m_project->setProxyLookup({});
    if (m_thread) {
        m_cancel = true;
        m_thread->wait(60000);
    }
}

QString ProxyManager::directory() const
{
    QString dir = m_settings->get(QStringLiteral("cacheLocation")).toString();
    if (dir.isEmpty())
        dir = paths::defaultCacheDir();
    return QDir(dir).filePath(QStringLiteral("proxy"));
}

QString ProxyManager::fileFor(const QString& mediaPath) const
{
    const QFileInfo fi(mediaPath);
    const QString key = QStringLiteral("%1|%2|%3").arg(fi.absoluteFilePath()).arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch());
    const QString name = QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(24));
    return QDir(directory()).filePath(name + QStringLiteral(".mp4"));
}

QString ProxyManager::proxyPathFor(const QString& mediaPath) const
{
    if (mediaPath.isEmpty() || !QFileInfo::exists(mediaPath))
        return {};
    const QString f = fileFor(mediaPath);
    return QFileInfo(f).size() > 0 ? f : QString();
}

int ProxyManager::proxyCount() const
{
    return int(QDir(directory()).entryList({QStringLiteral("*.mp4")}, QDir::Files).size());
}

qint64 ProxyManager::proxyBytes() const
{
    qint64 total = 0;
    for (const QFileInfo& fi : QDir(directory()).entryInfoList({QStringLiteral("*.mp4")}, QDir::Files))
        total += fi.size();
    return total;
}

qint64 ProxyManager::clearProxies()
{
    if (m_busy)
        return 0;
    qint64 freed = 0;
    for (const QFileInfo& fi : QDir(directory()).entryInfoList(QDir::Files)) {
        freed += fi.size();
        QFile::remove(fi.absoluteFilePath());
    }
    emit proxiesChanged();
    return freed;
}

QString ProxyManager::state(const QString& id) const
{
    const MediaRef* ref = m_project->media()->find(id);
    if (!ref || !ref->info.hasVideo || ref->info.width <= 0 || !m_project->media()->isReady(id))
        return QStringLiteral("notNeeded");
    if ((id == m_current && m_busy) || m_queue.contains(id))
        return QStringLiteral("building");
    if (!proxyPathFor(ref->path).isEmpty())
        return QStringLiteral("ready");
    if (ref->info.variableFrameRate)
        return QStringLiteral("unsupported");
    if (ref->info.durationUs <= 0 || ref->info.fpsNum <= 0 || ref->info.width < 640)
        return QStringLiteral("notNeeded");      // pictures and small videos
    return QStringLiteral("none");
}

QString ProxyManager::createProxy(const QString& id)
{
    const QString s = state(id);
    if (s == QLatin1String("ready"))
        return tr("This media already has a proxy.");
    if (s == QLatin1String("building"))
        return tr("A proxy is already being built.");
    if (s == QLatin1String("unsupported"))
        return tr("Variable frame rate media cannot get a proxy.");
    if (s != QLatin1String("none"))
        return tr("This media does not need a proxy.");
    m_queue.append(id);
    if (!m_busy)
        startNext();
    return {};
}

int ProxyManager::createMissingProxies()
{
    int started = 0;
    for (const MediaRef& ref : m_project->media()->refs())
        if (ref.info.hasVideo && ref.info.width > 1280 && state(ref.id) == QLatin1String("none") && createProxy(ref.id).isEmpty())
            ++started;
    return started;
}

void ProxyManager::startNext()
{
    while (!m_queue.isEmpty()) {
        m_current = m_queue.takeFirst();
        const MediaRef* ref = m_project->media()->find(m_current);
        if (!ref || !QFileInfo::exists(ref->path))
            continue;
        const QString src = ref->path;
        const QString dst = fileFor(src);
        m_busy = true;
        m_cancel = false;
        m_progress = 0;
        m_message = tr("Building proxy for %1…").arg(ref->name);
        emit busyChanged();
        emit messageChanged();
        emit progressChanged();
        auto error = std::make_shared<QString>();
        QPointer<ProxyManager> self(this);
        const int maxWidth = m_maxWidth;
        m_thread = QThread::create([self, src, dst, maxWidth, error, this] {
            *error = Exporter::buildProxy(src, dst, maxWidth, [self](qint64 done, qint64 total) {
                if (!self || total <= 0 || (done % 8 != 0 && done != total))
                    return;
                QMetaObject::invokeMethod(self.data(), [self, done, total] {
                    if (self) {
                        self->m_progress = double(done) / double(total);
                        emit self->progressChanged();
                    }
                }, Qt::QueuedConnection);
            }, m_cancel);
        });
        connect(m_thread, &QThread::finished, this, [this, error] {
            m_thread = nullptr;
            const bool cancelled = m_cancel.load();
            if (!error->isEmpty() && !cancelled)
                qCWarning(lcProject) << "Proxy failed:" << *error;
            m_message = cancelled ? tr("Proxy creation cancelled.") : error->isEmpty() ? tr("Proxy ready.") : *error;
            m_busy = false;
            m_current.clear();
            emit messageChanged();
            emit busyChanged();
            emit proxiesChanged();
            if (cancelled)
                m_queue.clear();
            else
                startNext();
        }, Qt::QueuedConnection);
        connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
        m_thread->start();
        return;
    }
}

void ProxyManager::cancel()
{
    m_queue.clear();
    if (m_busy)
        m_cancel = true;
}

void ProxyManager::waitForIdle(int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while ((m_busy || !m_queue.isEmpty()) && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents();
}

} // namespace dv
