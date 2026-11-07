#pragma once

#include <QAbstractListModel>
#include <QDateTime>

namespace dv {

class SettingsManager;

// Recently opened projects shown on the Home screen. Persisted to recent-projects.json.
class RecentProjectsModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role { NameRole = Qt::UserRole + 1, PathRole, LastOpenedRole, LastOpenedTextRole, ExistsRole };

    explicit RecentProjectsModel(SettingsManager* settings, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_items.size()); }

    bool load(const QString& file = {});
    bool save();

    Q_INVOKABLE void add(const QString& path, const QString& name);
    Q_INVOKABLE void removeAt(int row);
    Q_INVOKABLE void clear();
    // Re-reads file existence and locale dependent text (e.g. after a language change).
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString pathAt(int row) const;
    Q_INVOKABLE bool existsAt(int row) const;

signals:
    void countChanged();

private:
    struct Item {
        QString name;
        QString path;
        QDateTime lastOpened;
    };

    void trimToLimit();

    SettingsManager* m_settings;
    QList<Item> m_items;
    QString m_file;
};

} // namespace dv
