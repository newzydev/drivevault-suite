#include "BookmarkStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QUuid>
#include <QVariantMap>

namespace dv::br {

namespace {
QString localPathOf(const QString& pathOrUrl)
{
    return pathOrUrl.startsWith(QLatin1String("file:"), Qt::CaseInsensitive) ? QUrl(pathOrUrl).toLocalFile() : pathOrUrl;
}
QString unescapeHtml(QString s)
{
    s.replace(QLatin1String("&lt;"), QLatin1String("<")).replace(QLatin1String("&gt;"), QLatin1String(">"))
     .replace(QLatin1String("&quot;"), QLatin1String("\"")).replace(QLatin1String("&#39;"), QLatin1String("'")).replace(QLatin1String("&amp;"), QLatin1String("&"));
    return s;
}
QString escapeHtml(QString s)
{
    s.replace(QLatin1Char('&'), QLatin1String("&amp;")).replace(QLatin1Char('<'), QLatin1String("&lt;")).replace(QLatin1Char('>'), QLatin1String("&gt;"))
     .replace(QLatin1Char('"'), QLatin1String("&quot;"));
    return s;
}
} // namespace

BookmarkStore::BookmarkStore(const QString& file, QObject* parent) : QObject(parent), m_file(file)
{
    load();
}

bool BookmarkStore::allowedUrl(const QString& url)
{
    const QString scheme = QUrl(url).scheme().toLower();
    return scheme == QLatin1String("http") || scheme == QLatin1String("https") || scheme == QLatin1String("file");
}

QVariantList BookmarkStore::items() const
{
    QVariantList out;
    for (const Item& i : m_items)
        out.append(QVariantMap{{QStringLiteral("id"), i.id}, {QStringLiteral("title"), i.title}, {QStringLiteral("url"), i.url},
                               {QStringLiteral("folder"), i.folder}, {QStringLiteral("added"), i.added}});
    return out;
}

QString BookmarkStore::add(const QString& title, const QString& url, const QString& folder)
{
    if (!allowedUrl(url) || contains(url))
        return {};
    Item item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    item.title = title.trimmed().isEmpty() ? url : title.trimmed();
    item.url = url;
    item.folder = folder == QLatin1String("other") ? folder : QStringLiteral("bar");
    item.added = QDateTime::currentMSecsSinceEpoch();
    m_items.append(item);
    save();
    emit changed();
    return item.id;
}

void BookmarkStore::remove(const QString& id)
{
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i].id == id) {
            m_items.removeAt(i);
            save();
            emit changed();
            return;
        }
}

void BookmarkStore::removeUrl(const QString& url)
{
    bool any = false;
    for (int i = int(m_items.size()) - 1; i >= 0; --i)
        if (m_items[i].url == url) {
            m_items.removeAt(i);
            any = true;
        }
    if (any) {
        save();
        emit changed();
    }
}

bool BookmarkStore::contains(const QString& url) const
{
    for (const Item& i : m_items)
        if (i.url == url)
            return true;
    return false;
}

void BookmarkStore::rename(const QString& id, const QString& title)
{
    for (Item& i : m_items)
        if (i.id == id && !title.trimmed().isEmpty()) {
            i.title = title.trimmed();
            save();
            emit changed();
            return;
        }
}

bool BookmarkStore::edit(const QString& id, const QString& title, const QString& url)
{
    if (!allowedUrl(url) || title.trimmed().isEmpty())
        return false;
    for (Item& item : m_items) {
        if (item.id != id)
            continue;
        if (item.url != url && contains(url))
            return false;
        const Item original = item;
        item.title = title.trimmed();
        item.url = url;
        if (!save()) {
            item = original;
            return false;
        }
        emit changed();
        return true;
    }
    return false;
}

void BookmarkStore::move(const QString& id, const QString& folder)
{
    for (Item& i : m_items)
        if (i.id == id) {
            i.folder = folder == QLatin1String("other") ? folder : QStringLiteral("bar");
            save();
            emit changed();
            return;
        }
}

int BookmarkStore::importChrome(const QString& pathOrFileUrl)
{
    QFile file(localPathOf(pathOrFileUrl));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 32 * 1024 * 1024)
        return -1;
    const auto roots = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("roots")).toObject();
    if (roots.isEmpty())
        return -1;

    const qsizetype originalSize = m_items.size();
    QSet<QString> urls;
    for (const Item& item : m_items)
        urls.insert(item.url);
    for (auto root = roots.begin(); root != roots.end(); ++root) {
        const QString folder = root.key() == QStringLiteral("bookmark_bar") ? QStringLiteral("bar") : QStringLiteral("other");
        QList<QJsonObject> pending{root.value().toObject()};
        while (!pending.isEmpty()) {
            const auto node = pending.takeLast();
            const auto children = node.value(QStringLiteral("children")).toArray();
            for (qsizetype i = children.size(); i > 0; --i)
                pending.append(children.at(i - 1).toObject());
            if (node.value(QStringLiteral("type")).toString() != QStringLiteral("url"))
                continue;
            const auto url = node.value(QStringLiteral("url")).toString();
            if (!allowedUrl(url) || urls.contains(url))
                continue;
            urls.insert(url);
            const auto title = node.value(QStringLiteral("name")).toString().trimmed();
            m_items.append({QUuid::createUuid().toString(QUuid::WithoutBraces).left(8),
                            title.isEmpty() ? url : title, url, folder, QDateTime::currentMSecsSinceEpoch()});
        }
    }
    const int added = int(m_items.size() - originalSize);
    if (added > 0) {
        if (!save()) {
            m_items.resize(originalSize);
            return -1;
        }
        emit changed();
    }
    return added;
}

void BookmarkStore::load()
{
    QFile f(m_file);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        Item item;
        item.id = o.value(QLatin1String("id")).toString();
        item.title = o.value(QLatin1String("title")).toString();
        item.url = o.value(QLatin1String("url")).toString();
        item.folder = o.value(QLatin1String("folder")).toString() == QLatin1String("other") ? QStringLiteral("other") : QStringLiteral("bar");
        item.added = qint64(o.value(QLatin1String("added")).toDouble());
        if (!item.id.isEmpty() && allowedUrl(item.url))
            m_items.append(item);
    }
}

bool BookmarkStore::save() const
{
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    QJsonArray arr;
    for (const Item& i : m_items)
        arr.append(QJsonObject{{QLatin1String("id"), i.id}, {QLatin1String("title"), i.title}, {QLatin1String("url"), i.url},
                               {QLatin1String("folder"), i.folder}, {QLatin1String("added"), double(i.added)}});
    QSaveFile f(m_file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    return f.commit();
}

int BookmarkStore::importHtml(const QString& pathOrFileUrl)
{
    QFile f(localPathOf(pathOrFileUrl));
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    const QString html = QString::fromUtf8(f.readAll());
    // Walk the tags in order: a <H3> names the folder of the links that follow (the one marked PERSONAL_TOOLBAR_FOLDER is the bar).
    static const QRegularExpression tag(QStringLiteral(R"(<H3([^>]*)>(.*?)</H3>|<A\s([^>]*)>(.*?)</A>)"),
                                        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression href(QStringLiteral(R"re(HREF\s*=\s*"([^"]*)")re"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression stripTags(QStringLiteral(R"(<[^>]*>)"));
    QString folder = QStringLiteral("other");
    int added = 0;
    auto it = tag.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (m.capturedLength(1) > 0 || m.captured(0).startsWith(QLatin1String("<H3"), Qt::CaseInsensitive)) {
            folder = m.captured(1).contains(QLatin1String("PERSONAL_TOOLBAR_FOLDER"), Qt::CaseInsensitive) ? QStringLiteral("bar") : QStringLiteral("other");
            continue;
        }
        const QRegularExpressionMatch h = href.match(m.captured(3));
        if (!h.hasMatch())
            continue;
        const QString url = unescapeHtml(h.captured(1));
        if (!allowedUrl(url) || contains(url))
            continue;
        Item item;
        item.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        item.title = unescapeHtml(m.captured(4).remove(stripTags)).trimmed();
        if (item.title.isEmpty())
            item.title = url;
        item.url = url;
        item.folder = folder;
        item.added = QDateTime::currentMSecsSinceEpoch();
        m_items.append(item);
        ++added;
    }
    if (added) {
        save();
        emit changed();
    }
    return added;
}

bool BookmarkStore::exportHtml(const QString& pathOrFileUrl) const
{
    QSaveFile f(localPathOf(pathOrFileUrl));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QString out = QStringLiteral("<!DOCTYPE NETSCAPE-Bookmark-file-1>\n<META HTTP-EQUIV=\"Content-Type\" CONTENT=\"text/html; charset=UTF-8\">\n"
                                 "<TITLE>Bookmarks</TITLE>\n<H1>Bookmarks</H1>\n<DL><p>\n");
    for (const QString& folder : {QStringLiteral("bar"), QStringLiteral("other")}) {
        out += folder == QLatin1String("bar") ? QStringLiteral("    <DT><H3 PERSONAL_TOOLBAR_FOLDER=\"true\">Bookmarks bar</H3>\n    <DL><p>\n")
                                              : QStringLiteral("    <DT><H3>Other bookmarks</H3>\n    <DL><p>\n");
        for (const Item& i : m_items)
            if (i.folder == folder)
                out += QStringLiteral("        <DT><A HREF=\"%1\" ADD_DATE=\"%2\">%3</A>\n").arg(escapeHtml(i.url)).arg(i.added / 1000).arg(escapeHtml(i.title));
        out += QStringLiteral("    </DL><p>\n");
    }
    out += QStringLiteral("</DL><p>\n");
    f.write(out.toUtf8());
    return f.commit();
}

} // namespace dv::br
