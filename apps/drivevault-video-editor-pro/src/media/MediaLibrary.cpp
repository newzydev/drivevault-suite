#include "MediaLibrary.h"

#include "core/Logger.h"
#include "core/Timecode.h"
#include "media/MediaProbe.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QCollator>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QRunnable>
#include <QUrl>
#include <QUuid>

namespace dv {

namespace {
const QStringList& mediaExtensions()
{
    static const QStringList list = {"mp4", "mov", "mkv", "avi", "webm", "m4v", "mpg", "mpeg", "wmv", "flv", "ts", "mts", "m2ts",
                                     "mp3", "wav", "aac", "flac", "m4a", "ogg", "opus", "wma",
                                     "png", "jpg", "jpeg", "webp", "bmp", "tif", "tiff"};
    return list;
}

QString toLocal(const QString& pathOrUrl)
{
    if (pathOrUrl.startsWith(QLatin1String("file:"), Qt::CaseInsensitive))
        return QUrl(pathOrUrl).toLocalFile();
    return pathOrUrl;
}

// The paths behind one value handed over from QML: a QUrl, a string (file: URL or plain path), or a list of those (the file dialog's
// selectedFiles can arrive as a QList<QUrl> or as a nested list). Empty entries are skipped.
void collectLocalPaths(const QVariant& v, QStringList* out)
{
    if (v.metaType() == QMetaType::fromType<QVariant>()) {         // QML hands over each list entry as a QVariant inside a QVariant
        collectLocalPaths(v.value<QVariant>(), out);
    } else if (v.typeId() == QMetaType::QUrl) {
        const QUrl u = v.toUrl();
        const QString p = u.isLocalFile() ? u.toLocalFile() : u.toString();
        if (!p.isEmpty())
            out->append(p);
    } else if (v.typeId() == QMetaType::QString) {
        if (const QString p = toLocal(v.toString()); !p.isEmpty())
            out->append(p);
    } else if (v.canConvert<QVariantList>()) {
        for (const QVariant& item : v.toList())
            collectLocalPaths(item, out);
    } else if (const QString p = toLocal(v.toString()); !p.isEmpty()) {
        out->append(p);
    }
}

QString formatDuration(int64_t us)
{
    const qint64 totalSeconds = us / 1000000;
    const qint64 h = totalSeconds / 3600;
    const qint64 m = (totalSeconds / 60) % 60;
    const qint64 s = totalSeconds % 60;
    return h > 0 ? QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'))
                 : QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

// Runs a lambda on the thread pool.
template <typename F>
void runAsync(QThreadPool& pool, F&& f)
{
    struct Task : QRunnable {
        explicit Task(F fn) : fn(std::move(fn)) { setAutoDelete(true); }
        void run() override { fn(); }
        F fn;
    };
    pool.start(new Task(std::forward<F>(f)));
}
} // namespace

// ------------------------------------------------------------------------------------------------

MediaLibrary::MediaLibrary(QObject* parent)
    : QAbstractListModel(parent)
{
    m_pool.setMaxThreadCount(std::max(2, QThread::idealThreadCount() / 2));
}

MediaLibrary::~MediaLibrary()
{
    m_alive->store(false);
    cancelAll();
    m_pool.waitForDone(5000);
}

int MediaLibrary::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QString MediaLibrary::statusText(const QString& id) const
{
    const int row = rowOf(id);
    if (row < 0)
        return {};
    switch (m_items.at(row).status) {
    case Status::Importing: return QStringLiteral("importing");
    case Status::Ready: return QStringLiteral("ready");
    case Status::Error: return QStringLiteral("error");
    case Status::Missing: return QStringLiteral("missing");
    }
    return {};
}

QVariantMap MediaLibrary::describe(const QString& id) const
{
    const int row = rowOf(id);
    if (row < 0)
        return {};
    const QModelIndex idx = index(row);
    return {{"id", id}, {"name", data(idx, NameRole)}, {"path", data(idx, PathRole)}, {"kind", data(idx, KindRole)},
            {"status", data(idx, StatusRole)}, {"durationText", data(idx, DurationTextRole)},
            {"infoText", data(idx, InfoTextRole)}, {"hasAudio", data(idx, HasAudioRole)}, {"hasVideo", data(idx, HasVideoRole)},
            {"error", data(idx, ErrorRole)}, {"width", m_items.at(row).ref.info.width}, {"height", m_items.at(row).ref.info.height}};
}

QString MediaLibrary::thumbnailUrl(const QString& id) const
{
    const int row = rowOf(id);
    return row < 0 || m_items.at(row).thumbnail.isEmpty() ? QString() : QUrl::fromLocalFile(m_items.at(row).thumbnail).toString();
}

qint64 MediaLibrary::cacheSizeBytes() const
{
    qint64 total = 0;
    QDirIterator it(m_cacheDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

qint64 MediaLibrary::clearCache()
{
    const qint64 before = cacheSizeBytes();
    QDir(m_cacheDir + QStringLiteral("/thumbs")).removeRecursively();
    QDir(m_cacheDir + QStringLiteral("/peaks")).removeRecursively();
    for (Item& it : m_items) {
        it.thumbnail.clear();
        it.peaks.reset();
    }
    if (!m_items.isEmpty())
        emit dataChanged(index(0), index(int(m_items.size()) - 1));
    emit libraryChanged();
    for (const Item& it : std::as_const(m_items))
        if (it.status == Status::Ready)
            startDerived(it.ref.id);
    return before;
}

QVariant MediaLibrary::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    const Item& it = m_items.at(index.row());
    const MediaInfo& info = it.ref.info;
    switch (role) {
    case IdRole: return it.ref.id;
    case NameRole:
    case Qt::DisplayRole: return it.ref.name;
    case PathRole: return QDir::toNativeSeparators(it.ref.path);
    case KindRole: return info.kind;
    case StatusRole: return statusText(it.ref.id);
    case ErrorRole: return it.error;
    case ThumbnailRole: return it.thumbnail.isEmpty() ? QString() : QUrl::fromLocalFile(it.thumbnail).toString();
    case DurationRole: return qlonglong(info.durationUs);
    case DurationTextRole: return info.isStill() ? QStringLiteral("—") : formatDuration(info.durationUs);
    case HasAudioRole: return info.hasAudio;
    case HasVideoRole: return info.hasVideo;
    case AddedRole: return qlonglong(it.added);
    case InfoTextRole: {
        QStringList parts;
        if (info.hasVideo)
            parts << QStringLiteral("%1×%2").arg(info.width).arg(info.height);
        if (info.fpsNum > 0)
            parts << QStringLiteral("%1 fps").arg(timecode::label({info.fpsNum, info.fpsDen}));
        if (info.hasVideo && !info.videoCodec.isEmpty())
            parts << info.videoCodec.toUpper();
        if (info.hasAudio)
            parts << QStringLiteral("%1 kHz").arg(info.audioRate / 1000.0, 0, 'f', info.audioRate % 1000 ? 1 : 0);
        return parts.join(QStringLiteral("  ·  "));
    }
    default: return {};
    }
}

QHash<int, QByteArray> MediaLibrary::roleNames() const
{
    return {{IdRole, "mediaId"}, {NameRole, "name"}, {PathRole, "path"}, {KindRole, "kind"}, {StatusRole, "status"},
            {ErrorRole, "errorText"}, {ThumbnailRole, "thumbnail"}, {DurationRole, "durationUs"},
            {DurationTextRole, "durationText"}, {InfoTextRole, "infoText"}, {HasAudioRole, "hasAudio"},
            {HasVideoRole, "hasVideo"}, {AddedRole, "added"}};
}

int MediaLibrary::missingCount() const
{
    int n = 0;
    for (const Item& it : m_items)
        n += it.status == Status::Missing ? 1 : 0;
    return n;
}

void MediaLibrary::setCacheDir(const QString& dir)
{
    m_cacheDir = dir;
}

QString MediaLibrary::newId()
{
    return QStringLiteral("m_") + QUuid::createUuid().toString(QUuid::Id128).left(10);
}

int MediaLibrary::rowOf(const QString& id) const
{
    const auto it = m_index.constFind(id);
    return it == m_index.constEnd() ? -1 : *it;
}

const MediaRef* MediaLibrary::find(const QString& id) const
{
    const int row = rowOf(id);
    return row < 0 ? nullptr : &m_items.at(row).ref;
}

bool MediaLibrary::isReady(const QString& id) const
{
    const int row = rowOf(id);
    return row >= 0 && m_items.at(row).status == Status::Ready;
}

QString MediaLibrary::pathOf(const QString& id) const
{
    const MediaRef* ref = find(id);
    return ref ? ref->path : QString();
}

QList<MediaRef> MediaLibrary::refs() const
{
    QList<MediaRef> out;
    for (const Item& it : m_items)
        if (it.status != Status::Error && it.status != Status::Importing)
            out.append(it.ref);
    return out;
}

std::shared_ptr<const mediacache::Peaks> MediaLibrary::peaks(const QString& id) const
{
    const int row = rowOf(id);
    return row < 0 ? nullptr : m_items.at(row).peaks;
}

QString MediaLibrary::idForPath(const QString& path) const
{
    const QString wanted = QDir::cleanPath(toLocal(path)).toLower();
    for (const Item& it : m_items)
        if (QDir::cleanPath(it.ref.path).toLower() == wanted)
            return it.ref.id;
    if (const QString copied = m_copiedSources.value(wanted); !copied.isEmpty() && m_index.contains(copied))      // the original of a copied file
        return copied;
    return {};
}

void MediaLibrary::emitRowChanged(int row, const QList<int>& roles)
{
    if (row >= 0 && row < m_items.size())
        emit dataChanged(index(row), index(row), roles);
    emit libraryChanged();
}

void MediaLibrary::jobStarted()
{
    ++m_busy;
    emit busyCountChanged();
}

void MediaLibrary::jobFinished()
{
    m_busy = std::max(0, m_busy - 1);
    emit busyCountChanged();
}

// ---- Import --------------------------------------------------------------------------------------

QString MediaLibrary::importFile(const QString& pathOrUrl)
{
    const QString path = QDir::cleanPath(toLocal(pathOrUrl));
    const QFileInfo fi(path);
    if (const QString existing = idForPath(path); !existing.isEmpty()) {
        emit importSkipped(fi.fileName());
        return existing;
    }
    // A file that only exists in the cloud (Google Drive / OneDrive "online only") may not report as existing until it is opened:
    // opening it is also what makes the cloud client download it.
    const bool present = (fi.exists() && !fi.isDir()) || (!fi.isDir() && QFile(path).open(QIODevice::ReadOnly));
    if (!present) {
        qCWarning(lcProject) << "Import: file not found:" << pathOrUrl << "->" << path;
        emit importFailed(fi.fileName().isEmpty() ? pathOrUrl : fi.fileName(),
                          tr("The file was not found: %1").arg(QDir::toNativeSeparators(path.isEmpty() ? pathOrUrl : path)));
        return {};
    }

    // copy into the project's medias folder (unless the file is already inside it)
    QString copyFrom;
    QString target = fi.absoluteFilePath();
    if (!m_mediaFolder.isEmpty() && !QDir::cleanPath(target).startsWith(QDir::cleanPath(m_mediaFolder) + QLatin1Char('/'), Qt::CaseInsensitive)) {
        const QString key = QDir::cleanPath(target).toLower();
        if (const QString known = m_copiedSources.value(key); !known.isEmpty() && contains(known)) {
            emit importSkipped(fi.fileName());
            return known;
        }
        copyFrom = target;
        const QDir folder(m_mediaFolder);
        target = folder.filePath(fi.fileName());
        for (int n = 2; QFileInfo::exists(target) || !idForPath(target).isEmpty(); ++n)     // never overwrite another file
            target = folder.filePath(QStringLiteral("%1 (%2).%3").arg(fi.completeBaseName()).arg(n).arg(fi.suffix()));
    }

    Item item;
    item.ref.id = newId();
    item.ref.path = QDir::cleanPath(target);
    item.ref.name = QFileInfo(target).fileName();
    if (!copyFrom.isEmpty())
        m_copiedSources.insert(QDir::cleanPath(copyFrom).toLower(), item.ref.id);
    item.status = Status::Importing;
    item.added = QDateTime::currentMSecsSinceEpoch() * 1000 + (m_counter++ % 1000);

    const int row = int(m_items.size());
    beginInsertRows({}, row, row);
    m_items.append(item);
    m_index.insert(item.ref.id, row);
    endInsertRows();
    emit countChanged();
    emit libraryChanged();
    startProbe(item.ref.id, item.ref.path, copyFrom);
    return item.ref.id;
}

QStringList MediaLibrary::mediaFilesIn(const QString& folderOrUrl)
{
    QStringList files;
    QDirIterator it(toLocal(folderOrUrl), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        if (mediaExtensions().contains(QFileInfo(file).suffix().toLower()))
            files << file;
    }
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(files.begin(), files.end(), collator);
    return files;
}

int MediaLibrary::importFolder(const QString& pathOrUrl)
{
    int queued = 0;
    QDirIterator it(toLocal(pathOrUrl), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        if (mediaExtensions().contains(QFileInfo(file).suffix().toLower())) {
            importFile(file);
            ++queued;
        }
    }
    return queued;
}

int MediaLibrary::importUrls(const QVariantList& urls)
{
    int queued = 0;
    QStringList paths;
    for (const QVariant& v : urls)
        collectLocalPaths(v, &paths);
    if (paths.isEmpty())
        qCWarning(lcProject) << "Import: nothing usable in" << urls;
    for (const QString& path : paths) {
        if (QFileInfo(path).isDir())
            queued += importFolder(path);
        else {
            importFile(path);
            ++queued;
        }
    }
    return queued;
}

void MediaLibrary::startProbe(const QString& id, const QString& path, const QString& copyFrom)
{
    jobStarted();
    const auto cancel = m_items.at(rowOf(id)).cancel;
    const auto alive = m_alive;
    QPointer<MediaLibrary> self(this);
    runAsync(m_pool, [self, id, path, copyFrom, cancel, alive] {
        MediaInfo info;
        QString error;
        bool copied = true;
        if (!copyFrom.isEmpty() && !cancel->load()) {          // copy the file into the project first (a big file takes a while: not on the UI thread)
            QDir().mkpath(QFileInfo(path).absolutePath());
            copied = QFile::copy(copyFrom, path);
            if (!copied) {
                QFile::remove(path);
                error = tr("The file could not be copied into the project folder (%1).").arg(QDir::toNativeSeparators(QFileInfo(path).absolutePath()));
            }
        }
        const bool ok = copied && !cancel->load() && MediaProbe::probe(path, &info, &error);
        if (!ok && copied && !copyFrom.isEmpty())
            QFile::remove(path);                               // do not leave a copy of a file that could not be read
        if (!alive->load())
            return;
        QMetaObject::invokeMethod(self.data(), [self, id, ok, info, error, cancel] {
            if (!self)
                return;
            self->jobFinished();
            const int row = self->rowOf(id);
            if (row < 0 || cancel->load())
                return;
            Item& item = self->m_items[row];
            if (!ok) {
                item.status = Status::Error;
                item.error = error;
                qCWarning(lcProject) << "Import failed:" << item.ref.path << error;
                emit self->importFailed(item.ref.name, error);
                // Failed imports are not kept in the project; remove the placeholder row.
                self->remove(id);
                return;
            }
            item.ref.info = info;
            item.status = Status::Ready;
            self->emitRowChanged(row);
            self->startDerived(id);
            emit self->mediaReady(id);
        }, Qt::QueuedConnection);
    });
}

void MediaLibrary::startDerived(const QString& id)
{
    const int row = rowOf(id);
    if (row < 0 || m_cacheDir.isEmpty())
        return;
    Item& item = m_items[row];
    const QString path = item.ref.path;
    const MediaInfo info = item.ref.info;
    const QString key = mediacache::keyFor(path, info);
    const QString thumbFile = mediacache::thumbnailPath(m_cacheDir, key);
    const QString peaksFile = mediacache::peaksPath(m_cacheDir, key);
    const auto cancel = item.cancel;
    const auto alive = m_alive;
    QPointer<MediaLibrary> self(this);

    if (info.hasVideo) {
        jobStarted();
        runAsync(m_pool, [self, id, path, info, thumbFile, cancel, alive] {
            bool ok = QFileInfo::exists(thumbFile);
            if (!ok && !cancel->load()) {
                QString error;
                ok = mediacache::buildThumbnail(path, info, thumbFile, *cancel, &error);
                if (!ok && !cancel->load())
                    qCWarning(lcProject) << "Thumbnail failed for" << path << ":" << error;
            }
            if (!alive->load())
                return;
            QMetaObject::invokeMethod(self.data(), [self, id, ok, thumbFile, cancel] {
                if (!self)
                    return;
                self->jobFinished();
                const int row = self->rowOf(id);
                if (row < 0 || cancel->load())
                    return;
                if (ok)
                    self->m_items[row].thumbnail = thumbFile;
                self->emitRowChanged(row, {ThumbnailRole});
            }, Qt::QueuedConnection);
        });
    }
    if (info.hasAudio) {
        jobStarted();
        runAsync(m_pool, [self, id, path, peaksFile, cancel, alive] {
            auto peaks = std::make_shared<mediacache::Peaks>();
            bool ok = mediacache::loadPeaks(peaksFile, peaks.get());
            if (!ok && !cancel->load()) {
                ok = mediacache::buildPeaks(path, peaks.get(), *cancel);
                if (ok)
                    mediacache::savePeaks(peaksFile, *peaks);
            }
            if (!alive->load())
                return;
            QMetaObject::invokeMethod(self.data(), [self, id, ok, peaks, cancel] {
                if (!self)
                    return;
                self->jobFinished();
                const int row = self->rowOf(id);
                if (row < 0 || cancel->load())
                    return;
                if (ok)
                    self->m_items[row].peaks = peaks;
                self->emitRowChanged(row);
            }, Qt::QueuedConnection);
        });
    }
}

// ---- Project integration -----------------------------------------------------------------------

QString MediaLibrary::relativePathFor(const QString& absolutePath, const QString& projectDir)
{
    if (projectDir.isEmpty())
        return {};
    const QString rel = QDir(projectDir).relativeFilePath(absolutePath);
    return QDir::isAbsolutePath(rel) ? QString() : rel;   // different drive -> no relative path
}

void MediaLibrary::clear()
{
    cancelAll();
    m_pool.waitForDone(3000);
    beginResetModel();
    m_items.clear();
    m_index.clear();
    endResetModel();
    m_busy = 0;
    emit busyCountChanged();
    emit countChanged();
    emit libraryChanged();
}

void MediaLibrary::load(const QList<MediaRef>& refs, const QString& projectDir)
{
    clear();
    beginResetModel();
    for (const MediaRef& ref : refs) {
        Item item;
        item.ref = ref;
        item.added = QDateTime::currentMSecsSinceEpoch() * 1000 + (m_counter++ % 1000);
        QString resolved;
        if (QFileInfo::exists(ref.path))
            resolved = ref.path;
        else if (!ref.relativePath.isEmpty() && !projectDir.isEmpty()
                 && QFileInfo::exists(QDir(projectDir).absoluteFilePath(ref.relativePath)))
            resolved = QDir::cleanPath(QDir(projectDir).absoluteFilePath(ref.relativePath));
        if (resolved.isEmpty()) {
            item.status = Status::Missing;
        } else {
            item.ref.path = resolved;
            item.status = Status::Ready;
        }
        m_index.insert(item.ref.id, int(m_items.size()));
        m_items.append(item);
    }
    endResetModel();
    emit countChanged();
    emit libraryChanged();
    for (const Item& it : std::as_const(m_items))
        if (it.status == Status::Ready)
            startDerived(it.ref.id);
}

bool MediaLibrary::remove(const QString& id)
{
    const int row = rowOf(id);
    if (row < 0)
        return false;
    m_items.at(row).cancel->store(true);
    beginRemoveRows({}, row, row);
    m_items.removeAt(row);
    endRemoveRows();
    m_index.clear();
    for (int i = 0; i < m_items.size(); ++i)
        m_index.insert(m_items.at(i).ref.id, i);
    emit countChanged();
    emit libraryChanged();
    return true;
}

QString MediaLibrary::relink(const QString& id, const QString& pathOrUrl)
{
    const int row = rowOf(id);
    if (row < 0)
        return tr("This media is not part of the project.");
    const QString path = QDir::cleanPath(toLocal(pathOrUrl));
    MediaInfo info;
    QString error;
    if (!MediaProbe::probe(path, &info, &error))
        return error;
    Item& item = m_items[row];
    if (info.kind != item.ref.info.kind)
        return tr("The selected file is a different kind of media.");
    // Same content is expected: allow 1 s of difference for re-muxed copies.
    if (!info.isStill() && std::abs(info.durationUs - item.ref.info.durationUs) > 1000000)
        return tr("The selected file has a different duration than the original media.");
    item.ref.path = QFileInfo(path).absoluteFilePath();
    item.ref.info = info;
    item.status = Status::Ready;
    item.error.clear();
    item.thumbnail.clear();
    item.peaks.reset();
    emitRowChanged(row);
    startDerived(id);
    return {};
}

int MediaLibrary::relinkInFolder(const QString& folderOrUrl)
{
    QHash<QString, QString> found;   // lower-case file name -> path
    QDirIterator it(toLocal(folderOrUrl), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        found.insert(QFileInfo(file).fileName().toLower(), file);
    }
    int relinked = 0;
    const QList<Item> snapshot = m_items;
    for (const Item& item : snapshot) {
        if (item.status != Status::Missing)
            continue;
        const auto hit = found.constFind(item.ref.name.toLower());
        if (hit != found.constEnd() && relink(item.ref.id, *hit).isEmpty())
            ++relinked;
    }
    return relinked;
}

void MediaLibrary::cancelAll()
{
    for (Item& it : m_items)
        it.cancel->store(true);
}

void MediaLibrary::waitForIdle(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (m_busy > 0 && timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

// ------------------------------------------------------------------------------------------------

MediaFilterModel::MediaFilterModel(MediaLibrary* source, QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setSourceModel(source);
    setDynamicSortFilter(true);
    sort(0, Qt::DescendingOrder);
    connect(this, &QAbstractItemModel::rowsInserted, this, &MediaFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &MediaFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &MediaFilterModel::countChanged);
}

void MediaFilterModel::setFilterText(const QString& text)
{
    if (m_text == text)
        return;
    m_text = text;
    invalidateFilter();
    emit filterChanged();
    emit countChanged();
}

void MediaFilterModel::setSortKey(const QString& key)
{
    if (m_key == key)
        return;
    m_key = key;
    resort();
    emit filterChanged();
}

void MediaFilterModel::setAscending(bool ascending)
{
    if (m_ascending == ascending)
        return;
    m_ascending = ascending;
    resort();
    emit filterChanged();
}

void MediaFilterModel::resort()
{
    invalidate();
    sort(0, m_ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
}

bool MediaFilterModel::filterAcceptsRow(int row, const QModelIndex& parent) const
{
    if (m_text.trimmed().isEmpty())
        return true;
    const QModelIndex idx = sourceModel()->index(row, 0, parent);
    return idx.data(MediaLibrary::NameRole).toString().contains(m_text.trimmed(), Qt::CaseInsensitive);
}

bool MediaFilterModel::lessThan(const QModelIndex& left, const QModelIndex& right) const
{
    auto text = [&](const QModelIndex& i, int role) { return i.data(role); };
    if (m_key == QLatin1String("name"))
        return QString::localeAwareCompare(text(left, MediaLibrary::NameRole).toString(), text(right, MediaLibrary::NameRole).toString()) < 0;
    if (m_key == QLatin1String("duration"))
        return text(left, MediaLibrary::DurationRole).toLongLong() < text(right, MediaLibrary::DurationRole).toLongLong();
    if (m_key == QLatin1String("kind"))
        return text(left, MediaLibrary::KindRole).toString() < text(right, MediaLibrary::KindRole).toString();
    return text(left, MediaLibrary::AddedRole).toLongLong() < text(right, MediaLibrary::AddedRole).toLongLong();
}

} // namespace dv
