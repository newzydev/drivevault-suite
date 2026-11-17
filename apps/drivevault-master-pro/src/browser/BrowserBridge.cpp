#include "BrowserBridge.h"

#include "WebTools.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QImage>
#include <QTimer>

namespace dm::browser {

namespace {
bool isThisComputer(const QString& host)
{
    const QString h = host.toLower();
    if (h == QLatin1String("localhost") || h.endsWith(QLatin1String(".localhost")))
        return true;
    QHostAddress a;
    return a.setAddress(h) && a.isLoopback();
}
} // namespace

BrowserBridge::BrowserBridge(QObject* parent)
    : QObject(parent)
{
}


void BrowserBridge::setViewReady(bool ready)
{
    if (m_viewReady == ready)
        return;
    m_viewReady = ready;
    emit viewReadyChanged();
    if (ready)
        flush();
}

void BrowserBridge::request(const QString& op, const QVariantMap& args, Done done)
{
    const int id = m_nextId++;
    Pending p;
    p.op = op;
    p.args = args;
    p.done = std::move(done);
    p.timer = new QTimer(this);
    p.timer->setSingleShot(true);
    p.timer->setInterval(kTimeoutMs);
    connect(p.timer, &QTimer::timeout, this, [this, id] {
        finish(id, QVariantMap{{"ok", false}, {"error", QStringLiteral("the browser did not answer in time")}});
    });
    p.timer->start();
    m_pending.insert(id, std::move(p));
    m_order << id;
    emit viewWanted();
    flush();
}

void BrowserBridge::flush()
{
    if (!m_viewReady)
        return;
    for (const int id : std::as_const(m_order)) {
        auto it = m_pending.find(id);
        if (it == m_pending.end() || it->sent)
            continue;
        it->sent = true;
        emit command(id, it->op, it->args);
    }
}

void BrowserBridge::reply(int id, const QVariantMap& result)
{
    finish(id, result);
}

void BrowserBridge::setStoragePath(const QString& path)
{
    if (m_storagePath == path)
        return;
    m_storagePath = path;
    emit storagePathChanged();
}

QString BrowserBridge::encodeImage(const QVariant& image, int maxWidth) const
{
    QImage img = image.value<QImage>();
    if (img.isNull() || maxWidth < 64)
        return {};
    if (img.width() > maxWidth)
        img = img.scaledToWidth(maxWidth, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!img.convertToFormat(QImage::Format_RGB32).save(&buffer, "JPEG", 72))
        return {};
    return QString::fromLatin1(bytes.toBase64());
}

void BrowserBridge::finish(int id, const QVariantMap& result)
{
    auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    Pending p = std::move(it.value());
    m_pending.erase(it);
    m_order.removeAll(id);
    if (p.timer) {
        p.timer->stop();
        p.timer->deleteLater();
    }
    if (p.done)
        p.done(result);
}

void BrowserBridge::cancelAll()
{
    const QList<int> ids = m_order;
    for (const int id : ids)
        finish(id, QVariantMap{{"ok", false}, {"error", QStringLiteral("cancelled")}});
}

QString BrowserBridge::checkAgentUrl(const QUrl& url, const QString& folder, bool webOn)
{
    if (!url.isValid() || url.isRelative())
        return QStringLiteral("not a full address (use file:///..., http://localhost:port or https://...)");
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("file")) {
        if (folder.isEmpty())
            return QStringLiteral("no folder is chosen, so there is no file to open");
        const QString local = QDir::cleanPath(url.toLocalFile());
        const QString root = QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
        if (local.isEmpty() || !QFileInfo::exists(local))
            return QStringLiteral("that file does not exist");
        const QString canonical = QFileInfo(local).canonicalFilePath();
        const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
        if (canonical.isEmpty() || canonicalRoot.isEmpty())
            return QStringLiteral("that file cannot be checked");
        const QString prefix = canonicalRoot.endsWith(QLatin1Char('/')) ? canonicalRoot : canonicalRoot + QLatin1Char('/');
        if (!canonical.startsWith(prefix, Qt::CaseInsensitive))
            return QStringLiteral("a file outside the working folder cannot be opened");
        return {};
    }
    if (scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return QStringLiteral("only file, http and https addresses can be opened");
    if (!url.userInfo().isEmpty())
        return QStringLiteral("an address with a user name or password is not allowed");
    if (isThisComputer(url.host()))
        return {};                                                      // a dev server of the job
    if (!webOn)
        return QStringLiteral("web access is switched off by the user (only files of the folder and pages on this computer can be opened)");
    return ai::WebTools::checkUrl(url);
}

} // namespace dm::browser
