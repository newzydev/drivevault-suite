#include "BrowserApp.h"

#include "UrlUtil.h"
#include "Version.h"

#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QVariantMap>
#include <QtWebEngineCore/qtwebenginecoreglobal.h>
#include <QtWebEngineQuick/QQuickWebEngineProfile>

namespace dv::br {

BrowserApp::BrowserApp(const QString& dataDir, QQuickWebEngineProfile* profile, QObject* parent)
    : QObject(parent), m_dataDir(dataDir), m_profile(profile),
      m_settings(dataDir + QStringLiteral("/settings.json")), m_bookmarks(dataDir + QStringLiteral("/bookmarks.json")),
      m_history(dataDir + QStringLiteral("/history.jsonl"))
{
}

QString BrowserApp::version() const { return QString::fromLatin1(version::kVersion); }

QString BrowserApp::engineInfo() const
{
    return QStringLiteral("Qt %1 · Chromium %2").arg(QString::fromLatin1(qVersion()), QString::fromLatin1(qWebEngineChromiumVersion()));
}

QUrl BrowserApp::resolve(const QString& typed) const { return resolveInput(typed, m_settings.searchEngine()); }
QString BrowserApp::prettyUrl(const QString& url) const { return dv::br::prettyUrl(url); }
QString BrowserApp::hostOf(const QString& url) const { return dv::br::hostOf(url); }

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
