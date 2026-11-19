#include "BrowserSettings.h"

#include "UrlUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QVariantMap>
#include <QJsonArray>
#include <algorithm>
#include <cmath>

namespace dv::br {

BrowserSettings::BrowserSettings(const QString& file, QObject* parent) : QObject(parent), m_file(file)
{
    QFile f(m_file);
    if (f.open(QIODevice::ReadOnly))
        m_data = QJsonDocument::fromJson(f.readAll()).object();
}

bool BrowserSettings::save() const
{
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    QSaveFile f(m_file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(m_data).toJson(QJsonDocument::Indented));
    return f.commit();
}

void BrowserSettings::set(const QString& key, const QJsonValue& value)
{
    if (m_data.value(key) == value)
        return;
    m_data.insert(key, value);
    save();
    emit changed();
}

QString BrowserSettings::language() const { return m_data.value(QLatin1String("language")).toString() == QLatin1String("en") ? QStringLiteral("en") : QStringLiteral("th"); }
void BrowserSettings::setLanguage(const QString& v) { set(QStringLiteral("language"), v == QLatin1String("en") ? QStringLiteral("en") : QStringLiteral("th")); }

QString BrowserSettings::homePage() const { return m_data.value(QLatin1String("homePage")).toString(); }
void BrowserSettings::setHomePage(const QString& v) { set(QStringLiteral("homePage"), v.trimmed()); }

QString BrowserSettings::searchEngine() const
{
    const QString id = m_data.value(QLatin1String("searchEngine")).toString();
    for (const SearchEngine& e : searchEngines())
        if (e.id == id)
            return id;
    return defaultSearchEngineId();
}
void BrowserSettings::setSearchEngine(const QString& v) { set(QStringLiteral("searchEngine"), v); }

QVariantMap BrowserSettings::windowState() const
{
    return m_data.value(QLatin1String("window")).toObject().toVariantMap();
}

void BrowserSettings::saveWindowState(const QVariantMap& state)
{
    set(QStringLiteral("window"), QJsonObject::fromVariantMap(state));
}

bool BrowserSettings::showBookmarksBar() const { return m_data.value(QLatin1String("showBookmarksBar")).toBool(true); }
void BrowserSettings::setShowBookmarksBar(bool v) { set(QStringLiteral("showBookmarksBar"), v); }

QString BrowserSettings::startup() const { return m_data.value(QLatin1String("startup")).toString() == QLatin1String("restore") ? QStringLiteral("restore") : QStringLiteral("newtab"); }
void BrowserSettings::setStartup(const QString& v) { set(QStringLiteral("startup"), v == QLatin1String("restore") ? QStringLiteral("restore") : QStringLiteral("newtab")); }

bool BrowserSettings::blockNotifications() const { return m_data.value(QLatin1String("blockNotifications")).toBool(true); }
void BrowserSettings::setBlockNotifications(bool v) { set(QStringLiteral("blockNotifications"), v); }

double BrowserSettings::zoomFor(const QString& host) const
{
    const double z = m_data.value(QLatin1String("zoom")).toObject().value(host).toDouble(1.0);
    return z >= 0.25 && z <= 5.0 ? z : 1.0;
}

void BrowserSettings::setZoomFor(const QString& host, double factor)
{
    if (host.isEmpty())
        return;
    QJsonObject zoom = m_data.value(QLatin1String("zoom")).toObject();
    const double f = std::clamp(factor, 0.25, 5.0);
    if (std::abs(f - 1.0) < 0.001)
        zoom.remove(host);
    else
        zoom.insert(host, std::round(f * 100.0) / 100.0);
    set(QStringLiteral("zoom"), zoom);
}

QVariantList BrowserSettings::engines() const
{
    QVariantList out;
    for (const SearchEngine& e : searchEngines())
        out.append(QVariantMap{{QStringLiteral("id"), e.id}, {QStringLiteral("name"), e.name}});
    return out;
}

bool BrowserSettings::adblockEnabled() const { return m_data.value(QLatin1String("adblockEnabled")).toBool(true); }
void BrowserSettings::setAdblockEnabled(bool v) { set(QStringLiteral("adblockEnabled"), v); }
bool BrowserSettings::cosmeticEnabled() const { return m_data.value(QLatin1String("cosmeticEnabled")).toBool(true); }
void BrowserSettings::setCosmeticEnabled(bool v) { set(QStringLiteral("cosmeticEnabled"), v); }
bool BrowserSettings::antiDetectEnabled() const { return m_data.value(QLatin1String("antiDetectEnabled")).toBool(true); }
void BrowserSettings::setAntiDetectEnabled(bool v) { set(QStringLiteral("antiDetectEnabled"), v); }

bool BrowserSettings::listOn(const QString& id) const
{
    for (const QJsonValue& v : m_data.value(QLatin1String("adblockListsOff")).toArray())
        if (v.toString() == id)
            return false;
    return true;
}

void BrowserSettings::setListOn(const QString& id, bool on)
{
    QJsonArray off = m_data.value(QLatin1String("adblockListsOff")).toArray();
    QJsonArray next;
    for (const QJsonValue& v : off)
        if (v.toString() != id)
            next.append(v);
    if (!on)
        next.append(id);
    set(QStringLiteral("adblockListsOff"), next);
}

bool BrowserSettings::adblockPaused(const QString& host) const
{
    for (const QJsonValue& v : m_data.value(QLatin1String("adblockPaused")).toArray())
        if (v.toString() == host)
            return true;
    return false;
}

void BrowserSettings::setAdblockPaused(const QString& host, bool paused)
{
    if (host.isEmpty())
        return;
    QJsonArray next;
    for (const QJsonValue& v : m_data.value(QLatin1String("adblockPaused")).toArray())
        if (v.toString() != host)
            next.append(v);
    if (paused)
        next.append(host);
    set(QStringLiteral("adblockPaused"), next);
}

QStringList BrowserSettings::pausedHosts() const
{
    QStringList out;
    for (const QJsonValue& v : m_data.value(QLatin1String("adblockPaused")).toArray())
        out << v.toString();
    return out;
}

} // namespace dv::br
