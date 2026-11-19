#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

namespace dv::br {

// The pages the user visited (normal windows only - a private window never calls add). One JSON object per line, newest at the end; at most kMaxEntries are kept.
class HistoryStore : public QObject {
    Q_OBJECT
public:
    static constexpr int kMaxEntries = 20000;

    explicit HistoryStore(const QString& file, QObject* parent = nullptr);

    Q_INVOKABLE void add(const QString& url, const QString& title);          // only http(s) / file pages; the same page again within 60 s is not repeated
    Q_INVOKABLE QVariantList recent(int limit = 200) const;                  // newest first: [{url, title, ms}]
    Q_INVOKABLE QVariantList search(const QString& text, int limit = 200) const;
    Q_INVOKABLE QVariantList topSites(int limit = 8) const;                  // the pages visited most: [{url, title, count}]
    Q_INVOKABLE void remove(const QString& url);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void clearSince(qint64 sinceMs);
    int count() const { return int(m_entries.size()); }

signals:
    void changed();

private:
    struct Entry {
        QString url, title;
        qint64 ms = 0;
    };
    void load();
    bool rewrite() const;

    QString m_file;
    QList<Entry> m_entries;
};

} // namespace dv::br
