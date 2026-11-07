#pragma once

#include "core/MediaTypes.h"
#include "media/MediaCache.h"

#include <QAbstractListModel>
#include <QHash>
#include <QSortFilterProxyModel>
#include <QThreadPool>
#include <QVariantList>

#include <atomic>
#include <memory>

namespace dv {

// Imported media of the open project. Probing, thumbnails and waveforms run on a worker pool and can
// be cancelled; the model shows Importing / Ready / Error / Missing states.
class MediaLibrary : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(int busyCount READ busyCount NOTIFY busyCountChanged FINAL)
    Q_PROPERTY(int missingCount READ missingCount NOTIFY libraryChanged FINAL)

public:
    enum Role {
        IdRole = Qt::UserRole + 1, NameRole, PathRole, KindRole, StatusRole, ErrorRole, ThumbnailRole,
        DurationRole, DurationTextRole, InfoTextRole, HasAudioRole, HasVideoRole, AddedRole
    };
    enum class Status { Importing, Ready, Error, Missing };

    explicit MediaLibrary(QObject* parent = nullptr);
    ~MediaLibrary() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_items.size()); }
    int busyCount() const { return m_busy; }
    int missingCount() const;

    void setCacheDir(const QString& dir);
    // Imported files are copied into this folder first (the project's "medias" folder); empty = use the files where they are.
    void setMediaFolder(const QString& dir) { m_mediaFolder = dir; }
    QString mediaFolder() const { return m_mediaFolder; }
    QString cacheDir() const { return m_cacheDir; }

    // ---- Import ----------------------------------------------------------------------------
    // Returns the id of the (new or already imported) media, empty for unreadable input.
    Q_INVOKABLE QString importFile(const QString& pathOrUrl);
    Q_INVOKABLE int importUrls(const QVariantList& urls);       // files and folders; returns files queued
    Q_INVOKABLE int importFolder(const QString& pathOrUrl);

    // ---- Queries ---------------------------------------------------------------------------
    bool contains(const QString& id) const { return m_index.contains(id); }
    const MediaRef* find(const QString& id) const;
    bool isReady(const QString& id) const;
    QString pathOf(const QString& id) const;
    QList<MediaRef> refs() const;
    std::shared_ptr<const mediacache::Peaks> peaks(const QString& id) const;
    Q_INVOKABLE QString idForPath(const QString& path) const;
    Q_INVOKABLE QString statusText(const QString& id) const;
    // {name, path, kind, status, durationText, infoText, hasAudio, hasVideo} for the inspector.
    Q_INVOKABLE QVariantMap describe(const QString& id) const;
    Q_INVOKABLE QString thumbnailUrl(const QString& id) const;
    Q_INVOKABLE qint64 cacheSizeBytes() const;
    Q_INVOKABLE qint64 clearCache();

    // ---- Project integration ---------------------------------------------------------------
    // Replaces the library with `refs` (paths resolved against `projectDir`); missing files are flagged.
    void load(const QList<MediaRef>& refs, const QString& projectDir);
    void clear();
    bool remove(const QString& id);                              // caller checks the timeline first
    Q_INVOKABLE QString relink(const QString& id, const QString& pathOrUrl);   // "" on success
    Q_INVOKABLE int relinkInFolder(const QString& folderOrUrl);  // relinks every missing file found; returns count
    // Called by the project when its folder changes (Save As): refresh relative paths on save.
    static QString relativePathFor(const QString& absolutePath, const QString& projectDir);

    void cancelAll();
    void waitForIdle(int timeoutMs = 10000);

signals:
    void countChanged();
    void busyCountChanged();
    void libraryChanged();                         // anything about the set of media changed
    void importFailed(const QString& name, const QString& message);
    void importSkipped(const QString& name);       // already in the project
    void mediaReady(const QString& id);

private:
    struct Item {
        MediaRef ref;
        Status status = Status::Importing;
        QString error;
        QString thumbnail;
        qint64 added = 0;
        std::shared_ptr<const mediacache::Peaks> peaks;
        std::shared_ptr<std::atomic_bool> cancel = std::make_shared<std::atomic_bool>(false);
        int pendingJobs = 0;
    };

    int rowOf(const QString& id) const;
    void startProbe(const QString& id, const QString& path, const QString& copyFrom = QString());
    void startDerived(const QString& id);              // thumbnail + waveform
    void jobStarted();
    void jobFinished();
    void emitRowChanged(int row, const QList<int>& roles = {});
    static QString newId();

    QList<Item> m_items;
    QHash<QString, int> m_index;
    QString m_cacheDir;
    QString m_mediaFolder;
    QHash<QString, QString> m_copiedSources;        // original path (lower case) -> media id, so that a file is not copied twice
    QThreadPool m_pool;
    int m_busy = 0;
    qint64 m_counter = 0;
    std::shared_ptr<std::atomic_bool> m_alive = std::make_shared<std::atomic_bool>(true);
};

// Search / sort view used by the media panel.
class MediaFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged FINAL)
    Q_PROPERTY(QString sortKey READ sortKey WRITE setSortKey NOTIFY filterChanged FINAL)   // name|added|duration|kind
    Q_PROPERTY(bool ascending READ ascending WRITE setAscending NOTIFY filterChanged FINAL)
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    explicit MediaFilterModel(MediaLibrary* source, QObject* parent = nullptr);

    QString filterText() const { return m_text; }
    QString sortKey() const { return m_key; }
    bool ascending() const { return m_ascending; }
    int count() const { return rowCount(); }
    void setFilterText(const QString& text);
    void setSortKey(const QString& key);
    void setAscending(bool ascending);

signals:
    void filterChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

private:
    void resort();
    QString m_text;
    QString m_key = QStringLiteral("added");
    bool m_ascending = false;
};

} // namespace dv
