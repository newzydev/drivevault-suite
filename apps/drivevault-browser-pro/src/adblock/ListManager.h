#pragma once

#include "RuleSet.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVector>
#include <atomic>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace dv::br::ab {

struct ListDef {
    QString id;           // file name without .txt
    QString name;
    QString category;     // ads | tracking | social | annoyances | cookies | language
    QString url;          // the official address the "Update" button reads
    QString licence;
};

// The filter lists: which exist, which are on this PC, the parsed rules (published as one immutable RuleSet that the network thread reads),
// the on / off switches, and the update from the official addresses - ONLY when the user pressed the button (update()).
// Files: <data>/filters/<id>.txt (+ <id>.meta.json: date, size, SHA-256, address) or, when missing there, <shipped>/<id>.txt (the snapshot of the installer).
class ListManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY statusChanged)
    Q_PROPERTY(bool updating READ updating NOTIFY statusChanged)
    Q_PROPERTY(int totalRules READ totalRules NOTIFY statusChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY statusChanged)
    Q_PROPERTY(QString current READ current NOTIFY statusChanged)
    Q_PROPERTY(int updatedCount READ updatedCount NOTIFY statusChanged)
    Q_PROPERTY(int failedCount READ failedCount NOTIFY statusChanged)
    Q_PROPERTY(QString failures READ failures NOTIFY statusChanged)
public:
    explicit ListManager(const QString& dataDir, const QString& shippedDir = QString(), QObject* parent = nullptr);

    static QVector<ListDef> defaultCatalog();
    void setCatalog(const QVector<ListDef>& catalog) { m_catalog = catalog; }
    const QVector<ListDef>& catalog() const { return m_catalog; }
    void setAllowInsecureForTests(bool on) { m_allowHttp = on; }

    void setEnabled(const QStringList& ids);           // the lists that are on
    QStringList enabled() const;
    void reload();                                     // parse the files on a worker thread, then publish
    Q_INVOKABLE void update(const QStringList& ids = QStringList());     // download these lists (all that are on, when empty)

    // thread safe; the answer owns its texts (the rule set may be replaced at any moment)
    struct Decision {
        bool blocked = false;
        bool allowedByException = false;
        QString rule;            // the filter that blocked (or the one that was overruled)
        QString redirect;        // $redirect=name of the blocking filter (stand-in resource, Phase 3)
        QString listId;
    };
    Decision match(const QString& url, const QString& sourceUrl, quint16 type) const;
    bool siteExempt(const QString& sourceUrl) const;

    Q_INVOKABLE QVariantList status() const;           // [{id, name, category, licence, present, enabled, rules, cosmetic, unsupported, date, bytes}]
    bool loading() const { return m_loading; }
    bool updating() const { return m_updating; }
    int totalRules() const;
    QString phase() const { return m_phase; }          // idle | loading | updating | nothingOn
    QString current() const { return m_current; }      // name of the list being downloaded
    int updatedCount() const { return m_updated; }
    int failedCount() const { return m_failed; }
    QString failures() const { return m_failures.join(QStringLiteral("; ")); }
    QString filtersDir() const { return m_dataDir + QStringLiteral("/filters"); }

signals:
    void statusChanged();
    void reloaded();
    void updateFinished(const QString& summary);

private:
    struct Stats {
        bool present = false;
        int rules = 0, cosmetic = 0, unsupported = 0;
        qint64 bytes = 0;
        QString date;
    };
    struct Built {
        std::shared_ptr<const RuleSet> set;
        QVector<Stats> stats;
    };
    static Built build(const QVector<ListDef>& catalog, const QString& dataDir, const QString& shippedDir);
    quint32 maskOf(const QStringList& ids) const;
    void startNextDownload();
    void finishDownloads();

    QString m_dataDir, m_shippedDir;
    QVector<ListDef> m_catalog;
    std::atomic<std::shared_ptr<const RuleSet>> m_set;
    std::atomic<quint32> m_mask{0};
    QStringList m_enabled;
    QVector<Stats> m_stats;
    bool m_loading = false, m_updating = false, m_allowHttp = false, m_reloadAgain = false;
    QString m_phase = QStringLiteral("idle"), m_current;

    QNetworkAccessManager* m_net = nullptr;
    QStringList m_queue;
    int m_updated = 0, m_failed = 0;
    QStringList m_failures;
};

} // namespace dv::br::ab

