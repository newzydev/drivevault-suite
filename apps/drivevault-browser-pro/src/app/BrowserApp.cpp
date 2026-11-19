#include "BrowserApp.h"
#include <QImage>
#include <QQuickItemGrabResult>

#include "UrlUtil.h"
#include "Version.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QVariantMap>
#include <QtWebEngineCore/qtwebenginecoreglobal.h>
#include <QtWebEngineQuick/QQuickWebEngineProfile>

namespace dv::br {

BrowserApp::BrowserApp(const QString& dataDir, QQuickWebEngineProfile* profile, AdBlockInterceptor* interceptor, StandInHandler* standIns, QObject* parent)
    : QObject(parent), m_dataDir(dataDir), m_profile(profile),
      m_settings(dataDir + QStringLiteral("/settings.json")), m_bookmarks(dataDir + QStringLiteral("/bookmarks.json")),
      m_history(dataDir + QStringLiteral("/history.jsonl")),
      m_adblock(&m_settings, interceptor, standIns, dataDir, QCoreApplication::applicationDirPath() + QStringLiteral("/filters")),
      m_media(interceptor, &m_downloads)
{
    m_media.setUserAgent(profile ? profile->httpUserAgent() : QString());
    QFile library(QStringLiteral(":/qt/qml/DriveVault/Browser/resources/adblock-page.js"));       // the script that runs inside pages (hiding, scriptlets)
    if (library.open(QIODevice::ReadOnly))
        m_adblock.setPageLibrary(QString::fromUtf8(library.readAll()));
    auto readResource = [](const char* name) {
        QFile f(QStringLiteral(":/qt/qml/DriveVault/Browser/resources/") + QString::fromLatin1(name));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };
    m_pipOn = readResource("pip-on.js");
    m_pipOff = readResource("pip-off.js");
}

QString BrowserApp::version() const { return QString::fromLatin1(version::kVersion); }

QString BrowserApp::engineInfo() const
{
    return QStringLiteral("Qt %1 · Chromium %2").arg(QString::fromLatin1(qVersion()), QString::fromLatin1(qWebEngineChromiumVersion()));
}

QUrl BrowserApp::resolve(const QString& typed) const { return resolveInput(typed, m_settings.searchEngine()); }
QString BrowserApp::prettyUrl(const QString& url) const { return dv::br::prettyUrl(url); }
QString BrowserApp::hostOf(const QString& url) const { return dv::br::hostOf(url); }

bool BrowserApp::hasIconPixels(QObject* grabResult) const
{
    const auto* result = qobject_cast<QQuickItemGrabResult*>(grabResult);
    if (!result)
        return false;
    const QImage image = result->image().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        const auto* pixels = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(pixels[x]) > 0)
                return true;
    }
    return false;
}

QVariantList BrowserApp::suggest(const QString& text, int limit) const
{
    QVariantList out;
    const QString q = text.trimmed();
    if (q.isEmpty())
        return out;
    QSet<QString> seen;
    auto push = [&](const QString& title, const QString& url, const char* kind) {
        if (out.size() >= limit || seen.contains(url))
            return;
        seen.insert(url);
        out.append(QVariantMap{{QStringLiteral("title"), title}, {QStringLiteral("url"), url}, {QStringLiteral("kind"), QString::fromLatin1(kind)}});
    };
    for (const QVariant& v : m_bookmarks.items()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("title")).toString().contains(q, Qt::CaseInsensitive) || m.value(QStringLiteral("url")).toString().contains(q, Qt::CaseInsensitive))
            push(m.value(QStringLiteral("title")).toString(), m.value(QStringLiteral("url")).toString(), "bookmark");
    }
    for (const QVariant& v : m_history.search(q, 30)) {
        const QVariantMap m = v.toMap();
        push(m.value(QStringLiteral("title")).toString(), m.value(QStringLiteral("url")).toString(), "history");
    }
    return out;
}

QString BrowserApp::localPath(const QUrl& url) const { return url.toLocalFile(); }
qint64 BrowserApp::fileSize(const QString& path) const
{
    const QFileInfo fi(path);
    return fi.exists() ? fi.size() : -1;
}

void BrowserApp::openWindow(bool privateMode, const QString& url) { emit windowRequested(privateMode, url); }

QStringList BrowserApp::loadSession() const
{
    QFile f(m_dataDir + QStringLiteral("/session.json"));
    QStringList urls;
    if (f.open(QIODevice::ReadOnly))
        for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).array())
            if (BookmarkStore::allowedUrl(v.toString()))
                urls << v.toString();
    return urls;
}

void BrowserApp::saveSession(const QStringList& urls) const
{
    QJsonArray arr;
    for (const QString& u : urls)
        if (BookmarkStore::allowedUrl(u))
            arr.append(u);
    QSaveFile f(m_dataDir + QStringLiteral("/session.json"));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

void BrowserApp::copyText(const QString& text) const
{
    if (QClipboard* c = QGuiApplication::clipboard())
        c->setText(text);
}

void BrowserApp::clearBrowsingData(bool history, bool cache)
{
    if (history)
        m_history.clear();
    if (cache && m_profile)
        m_profile->clearHttpCache();
}

} // namespace dv::br
