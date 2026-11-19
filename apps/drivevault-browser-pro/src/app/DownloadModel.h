#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QPointer>
#include <QVariantMap>
#include <functional>

class QQuickWebEngineDownloadRequest;

namespace dv::br {

// The downloads of this run (newest first). A file the user's click started is saved in the Downloads folder under a name that never overwrites a file;
// the program never opens a downloaded file. Used by the normal and the private windows.
class DownloadModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int activeCount READ activeCount NOTIFY activeCountChanged)
    Q_PROPERTY(QString folder READ folder CONSTANT)
public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, PathRole, ReceivedRole, TotalRole, StateRole, RiskyRole, UnitRole, PausableRole, ErrorRole };

    explicit DownloadModel(QObject* parent = nullptr);

    void setFolder(const QString& folder) { m_folder = folder; }
    QString folder() const;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int activeCount() const;

    Q_INVOKABLE void handle(QQuickWebEngineDownloadRequest* request);       // accept into the Downloads folder
    Q_INVOKABLE void cancel(int row);
    Q_INVOKABLE void pause(int row);
    Q_INVOKABLE void resume(int row);
    Q_INVOKABLE void showInFolder(int row);
    Q_INVOKABLE void remove(int row);                                       // from the list only, never the file
    Q_INVOKABLE void clearFinished();
    Q_INVOKABLE QVariantMap get(int row) const;                              // {name, path, state, received, total, unit, error}

    // downloads made by this program itself (a saved video), not by the engine: the entry is listed like any other
    int addExternal(const QString& name, const QString& path, std::function<void()> cancel);          // the id
    void updateExternal(int id, qint64 received, qint64 total, const QString& unit);
    void finishExternal(int id, bool ok, const QString& path, const QString& error);

signals:
    void activeCountChanged();

private:
    struct Entry {
        int id = 0;
        QString name, path, state;       // state: running | paused | done | failed | cancelled
        qint64 received = 0, total = 0;
        bool risky = false;
        QString unit = QStringLiteral("bytes"), error;
        bool external = false;
        std::function<void()> cancelFn;
        QPointer<QQuickWebEngineDownloadRequest> request;
    };
    int rowOfId(int id) const;
    void refresh(int id);

    QList<Entry> m_entries;
    QString m_folder;
    int m_nextId = 1;
};

} // namespace dv::br
