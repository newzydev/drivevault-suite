#include "HistoryStore.h"

#include "BookmarkStore.h"      // allowedUrl

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QVariantMap>
#include <algorithm>

namespace dv::br {

HistoryStore::HistoryStore(const QString& file, QObject* parent) : QObject(parent), m_file(file)
{
    load();
}

void HistoryStore::load()
{
    QFile f(m_file);
    if (!f.open(QIODevice::ReadOnly))
        return;
    while (!f.atEnd()) {
        const QJsonObject o = QJsonDocument::fromJson(f.readLine()).object();
        Entry e;
        e.url = o.value(QLatin1String("u")).toString();
        e.title = o.value(QLatin1String("t")).toString();
        e.ms = qint64(o.value(QLatin1String("m")).toDouble());
        if (!e.url.isEmpty())
            m_entries.append(e);
    }
    f.close();
    if (m_entries.size() > kMaxEntries) {
        m_entries = m_entries.mid(m_entries.size() - kMaxEntries);
        rewrite();
    }
}

bool HistoryStore::rewrite() const
{
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    QSaveFile f(m_file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    for (const Entry& e : m_entries)
        f.write(QJsonDocument(QJsonObject{{QLatin1String("u"), e.url}, {QLatin1String("t"), e.title}, {QLatin1String("m"), double(e.ms)}}).toJson(QJsonDocument::Compact) + '\n');
    return f.commit();
}

void HistoryStore::add(const QString& url, const QString& title)
{
    if (!BookmarkStore::allowedUrl(url))
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!m_entries.isEmpty() && m_entries.last().url == url && now - m_entries.last().ms < 60000)
        return;
    Entry e{url, title, now};
    m_entries.append(e);
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    QFile f(m_file);
    if (f.open(QIODevice::Append))
        f.write(QJsonDocument(QJsonObject{{QLatin1String("u"), url}, {QLatin1String("t"), title}, {QLatin1String("m"), double(now)}}).toJson(QJsonDocument::Compact) + '\n');
    if (m_entries.size() > kMaxEntries + 500) {            // trim in steps so the file is not rewritten on every visit
        m_entries = m_entries.mid(m_entries.size() - kMaxEntries);
        rewrite();
    }
    emit changed();
}

QVariantList HistoryStore::recent(int limit) const
{
    QVariantList out;
    for (qsizetype i = m_entries.size() - 1; i >= 0 && out.size() < limit; --i)
        out.append(QVariantMap{{QStringLiteral("url"), m_entries[i].url}, {QStringLiteral("title"), m_entries[i].title}, {QStringLiteral("ms"), m_entries[i].ms}});
    return out;
}

QVariantList HistoryStore::search(const QString& text, int limit) const
{
    const QString q = text.trimmed();
    if (q.isEmpty())
        return recent(limit);
    QVariantList out;
    for (qsizetype i = m_entries.size() - 1; i >= 0 && out.size() < limit; --i) {
        const Entry& e = m_entries[i];
        if (e.title.contains(q, Qt::CaseInsensitive) || e.url.contains(q, Qt::CaseInsensitive))
            out.append(QVariantMap{{QStringLiteral("url"), e.url}, {QStringLiteral("title"), e.title}, {QStringLiteral("ms"), e.ms}});
    }
    return out;
}

QVariantList HistoryStore::topSites(int limit) const
{
    struct Count { QString title; int n = 0; qint64 last = 0; };
    QHash<QString, Count> counts;
    for (const Entry& e : m_entries) {
        Count& c = counts[e.url];
        ++c.n;
        c.last = e.ms;
        if (!e.title.isEmpty())
            c.title = e.title;
    }
    QList<QPair<QString, Count>> list;
    for (auto it = counts.cbegin(); it != counts.cend(); ++it)
        list.append({it.key(), it.value()});
    std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.second.n != b.second.n ? a.second.n > b.second.n : a.second.last > b.second.last; });
    QVariantList out;
    for (const auto& p : list) {
        if (out.size() >= limit)
            break;
        out.append(QVariantMap{{QStringLiteral("url"), p.first}, {QStringLiteral("title"), p.second.title}, {QStringLiteral("count"), p.second.n}});
    }
    return out;
}

void HistoryStore::remove(const QString& url)
{
    const qsizetype before = m_entries.size();
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) { return e.url == url; }), m_entries.end());
    if (m_entries.size() != before) {
        rewrite();
        emit changed();
    }
}

void HistoryStore::clear()
{
    m_entries.clear();
    rewrite();
    emit changed();
}

void HistoryStore::clearSince(qint64 sinceMs)
{
    const qsizetype before = m_entries.size();
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [&](const Entry& e) { return e.ms >= sinceMs; }), m_entries.end());
    if (m_entries.size() != before) {
        rewrite();
        emit changed();
    }
}

} // namespace dv::br
