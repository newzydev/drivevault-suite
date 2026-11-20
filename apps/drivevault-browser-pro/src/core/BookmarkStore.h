#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

namespace dv::br {

// Bookmarks of the user: a flat list, each in the folder "bar" (shown under the address bar) or "other". Saved as JSON; import / export as the
// Netscape bookmark HTML that Chrome, Edge and Firefox use.
class BookmarkStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)
public:
    explicit BookmarkStore(const QString& file, QObject* parent = nullptr);

    QVariantList items() const;                      // [{id, title, url, folder, added}]
    Q_INVOKABLE QString add(const QString& title, const QString& url, const QString& folder = QStringLiteral("bar"));   // "" when the url is not allowed or exists
    Q_INVOKABLE void remove(const QString& id);
    Q_INVOKABLE void removeUrl(const QString& url);
    Q_INVOKABLE bool contains(const QString& url) const;
    Q_INVOKABLE void rename(const QString& id, const QString& title);
    Q_INVOKABLE bool edit(const QString& id, const QString& title, const QString& url);
    Q_INVOKABLE void move(const QString& id, const QString& folder);
    Q_INVOKABLE int importHtml(const QString& pathOrFileUrl);          // number of bookmarks added
    Q_INVOKABLE int importChrome(const QString& pathOrFileUrl);
    Q_INVOKABLE bool exportHtml(const QString& pathOrFileUrl) const;
    int count() const { return int(m_items.size()); }

    static bool allowedUrl(const QString& url);       // http, https, file only (no javascript: / data: bookmarks)

signals:
    void changed();

private:
    struct Item {
        QString id, title, url, folder;
        qint64 added = 0;
    };
    void load();
    bool save() const;

    QString m_file;
    QList<Item> m_items;
};

} // namespace dv::br
