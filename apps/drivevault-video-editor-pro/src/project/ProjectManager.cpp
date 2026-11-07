#include "ProjectManager.h"

#include "core/AppPaths.h"
#include "core/FileUtils.h"
#include "core/Logger.h"
#include "project/ProjectFile.h"
#include "project/RecentProjectsModel.h"
#include "settings/SettingsManager.h"

#include <QCoreApplication>
#include <atomic>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace dv {

ProjectManager::ProjectManager(SettingsManager* settings, RecentProjectsModel* recents, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_recents(recents)
    , m_mediaView(&m_library)
{
    m_timeline.setMediaLookup([this](const QString& id) -> std::optional<MediaLite> {
        const MediaRef* ref = m_library.find(id);
        if (!ref)
            return std::nullopt;
        MediaLite m;
        m.name = ref->name;
        m.kind = ref->info.kind;
        m.durationUs = ref->info.durationUs;
        m.hasVideo = ref->info.hasVideo;
        m.hasAudio = ref->info.hasAudio;
        m.online = m_library.isReady(id);
        return m;
    });

    // Track toggles (hide / mute / lock) are saved with the project and feed the render snapshot.
    connect(&m_tracks, &TrackListModel::userChanged, this, [this] {
        if (!m_open)
            return;
        syncTracksToData();
        m_timeline.setTracks(m_data.tracks);
        markExtraDirty();
        emit snapshotChanged();
    });
    connect(&m_timeline, &TimelineModel::clipsChanged, this, [this] {
        if (m_open)
            emit snapshotChanged();
    });
    connect(&m_timeline, &TimelineModel::edited, this, [this] { updateDirty(); });
    connect(&m_timeline, &TimelineModel::markersChanged, this, [this] { markExtraDirty(); });
    connect(&m_timeline, &TimelineModel::rangeChanged, this, [this] { markExtraDirty(); });
    connect(m_timeline.undoStack(), &QUndoStack::cleanChanged, this, [this] { updateDirty(); });
    connect(&m_library, &MediaLibrary::libraryChanged, this, [this] {
        if (!m_open)
            return;
        m_timeline.refreshMedia();
        emit snapshotChanged();
    });
    connect(&m_library, &MediaLibrary::mediaReady, this, [this] { markExtraDirty(); });

    m_autosaveTimer.setTimerType(Qt::VeryCoarseTimer);
    connect(&m_autosaveTimer, &QTimer::timeout, this, &ProjectManager::autosaveNow);
    if (m_settings) {
        connect(m_settings, &SettingsManager::valueChanged, this, [this](const QString& key) {
            if (key.startsWith(QLatin1String("autoSave")))
                configureAutosave();
            else if (key == QLatin1String("cacheLocation"))
                configureCache();
        });
    }
    configureAutosave();
    configureCache();
}

ProjectManager::~ProjectManager()
{
    m_library.cancelAll();
}

QString ProjectManager::frameRateLabel() const
{
    return dv::timecode::label(m_data.frameRate);
}

QString ProjectManager::timecode() const
{
    return dv::timecode::format(m_playhead, m_data.frameRate);
}

QString ProjectManager::timecodeFor(qint64 frame) const
{
    return dv::timecode::format(frame, m_data.frameRate);
}

void ProjectManager::setPlayheadFrame(qint64 frame)
{
    frame = qMax<qint64>(0, frame);
    if (frame == m_playhead)
        return;
    m_playhead = frame;
    m_timeline.setPlayheadFrame(frame);
    emit playheadChanged();
}

void ProjectManager::stepFrames(int delta)
{
    setPlayheadFrame(m_playhead + delta);
}

QString ProjectManager::toLocalPath(const QString& pathOrUrl)
{
    if (pathOrUrl.startsWith(QLatin1String("file:"), Qt::CaseInsensitive))
        return QUrl(pathOrUrl).toLocalFile();
    return pathOrUrl;
}

QString ProjectManager::defaultLocation(const QString& configured)
{
    return configured.trimmed().isEmpty() ? paths::defaultProjectsDir() : configured;
}

QString ProjectManager::validateName(const QString& name)
{
    if (name.trimmed().isEmpty())
        return tr("Please enter a project name.");
    if (fileutils::sanitizeFileName(name).isEmpty())
        return tr("This name cannot be used as a file name.");
    return {};
}

QString ProjectManager::projectDir() const
{
    return m_path.isEmpty() ? QString() : QFileInfo(m_path).absolutePath();
}

// ---- dirty / state -------------------------------------------------------------------------------

void ProjectManager::updateDirty()
{
    const bool dirty = m_open && (m_extraDirty || !m_timeline.undoStack()->isClean());
    if (dirty == m_dirty)
        return;
    m_dirty = dirty;
    emit dirtyChanged();
}

void ProjectManager::markExtraDirty()
{
    if (!m_open)
        return;
    m_extraDirty = true;
    updateDirty();
}

void ProjectManager::syncTracksToData()
{
    m_data.tracks = m_tracks.tracks();
}

ProjectData ProjectManager::currentData(const QString& projectPath) const
{
    ProjectData d = m_data;
    d.tracks = m_tracks.tracks();
    d.clips = m_timeline.clips();
    d.markers = m_timeline.markerData();
    d.inPoint = m_timeline.inPoint();
    d.outPoint = m_timeline.outPoint();
    const QString dir = projectPath.isEmpty() ? projectDir() : QFileInfo(projectPath).absolutePath();
    d.media = m_library.refs();
    for (MediaRef& ref : d.media)
        ref.relativePath = MediaLibrary::relativePathFor(ref.path, dir);
    return d;
}

SnapshotPtr ProjectManager::snapshot() const
{
    static std::atomic<uint64_t> s_revision{0};
    auto snap = std::make_shared<RenderSnapshot>();
    snap->revision = ++s_revision;
    snap->width = m_data.width;
    snap->height = m_data.height;
    snap->fps = m_data.frameRate;
    snap->sampleRate = m_data.sampleRate;
    snap->tracks = m_tracks.tracks();
    for (const ClipData& c : m_timeline.clips()) {
        SnapshotClip sc;
        sc.clip = c;
        for (int i = 0; i < snap->tracks.size(); ++i)
            if (snap->tracks.at(i).id == c.trackId)
                sc.trackIndex = i;
        if (const MediaRef* ref = m_library.find(c.mediaId)) {
            sc.mediaPath = ref->path;
            if (m_proxyLookup && ref->info.hasVideo)
                sc.proxyPath = m_proxyLookup(ref->path);
            sc.media = ref->info;
            sc.online = m_library.isReady(c.mediaId);
        } else {
            sc.online = false;
        }
        snap->durationFrames = std::max<int64_t>(snap->durationFrames, c.endFrame());
        snap->clips.append(sc);
    }
    return snap;
}

void ProjectManager::applyData(const ProjectData& data, const QString& path, bool dirty)
{
    m_data = data;
    m_path = path;
    m_open = true;
    m_playhead = 0;
    m_tracks.setTracks(m_data.tracks);
    m_timeline.setFrameRate(m_data.frameRate);
    m_timeline.setTracks(m_data.tracks);
    m_library.load(m_data.media, projectDir());
    m_timeline.load(m_data.clips);
    m_timeline.setMarkers(m_data.markers, m_data.inPoint, m_data.outPoint);
    m_timeline.setPlayheadFrame(0);
    m_extraDirty = dirty;
    m_dirty = false;
    updateDirty();
    emit projectChanged();
    emit playheadChanged();
    emit snapshotChanged();
}

// ---- create / open / save ----------------------------------------------------------------------

QString ProjectManager::createProject(const QString& name, const QString& folder, int width, int height,
                                      const QString& frameRate, int sampleRate)
{
    if (const QString nameProblem = validateName(name); !nameProblem.isEmpty())
        return nameProblem;

    const dv::timecode::FrameRate rate = dv::timecode::parse(frameRate);
    if (const QString problem = ProjectFile::validate(name, width, height, rate, sampleRate); !problem.isEmpty())
        return problem;

    const QString baseFolder = toLocalPath(folder).trimmed();
    if (baseFolder.isEmpty())
        return tr("Please choose a location for the project.");

    const QString fileName = fileutils::sanitizeFileName(name);
    const QString projectFolder = QDir::cleanPath(baseFolder + QLatin1Char('/') + fileName);
    const QString projectFile = projectFolder + QLatin1Char('/') + fileName + QLatin1Char('.')
                                + QLatin1String(ProjectFile::kExtension);
    if (QFileInfo::exists(projectFile))
        return tr("A project with this name already exists in the selected location.");

    ProjectData data = ProjectFile::makeDefault(name, width, height, rate, sampleRate);
    QString error;
    if (!ProjectFile::save(projectFile, data, &error))
        return tr("Cannot create the project: %1").arg(error);

    applyData(data, projectFile, false);
    m_recoveryAvailable = false;
    if (m_recents)
        m_recents->add(projectFile, data.name);
    qCInfo(lcProject) << "Created project" << projectFile;
    return {};
}

QString ProjectManager::openProject(const QString& pathOrUrl)
{
    const QString path = QDir::cleanPath(toLocalPath(pathOrUrl));
    if (!ProjectFile::hasProjectExtension(path))
        return tr("Please choose a DriveVault project file (.dvproj).");

    ProjectData data;
    QString error;
    if (!ProjectFile::load(path, &data, &error)) {
        qCWarning(lcProject) << "Cannot open" << path << ":" << error;
        return error;
    }

    applyData(data, path, false);
    if (m_recents)
        m_recents->add(path, data.name);

    const QString autosave = autosavePathFor(path);
    m_recoveryAvailable = QFileInfo::exists(autosave)
                          && QFileInfo(autosave).lastModified() > QFileInfo(path).lastModified();
    if (m_recoveryAvailable)
        emit recoveryAvailable(path);
    if (m_library.missingCount() > 0)
        qCWarning(lcProject) << m_library.missingCount() << "media file(s) of the project are missing";
    qCInfo(lcProject) << "Opened project" << path;
    return {};
}

QString ProjectManager::restoreRecovery()
{
    if (!m_open || !m_recoveryAvailable)
        return {};
    ProjectData data;
    QString error;
    if (!ProjectFile::load(autosavePathFor(m_path), &data, &error))
        return error;
    applyData(data, m_path, true);   // restored content is unsaved until the user saves
    m_recoveryAvailable = false;
    return {};
}

void ProjectManager::discardRecovery()
{
    if (m_open)
        QFile::remove(autosavePathFor(m_path));
    m_recoveryAvailable = false;
}

QString ProjectManager::save()
{
    if (!m_open)
        return tr("There is no open project.");
    if (m_path.isEmpty())
        return tr("The project has no file yet. Use Save As.");
    return saveAs(m_path);
}

QString ProjectManager::saveAs(const QString& pathOrUrl)
{
    if (!m_open)
        return tr("There is no open project.");

    QString path = QDir::cleanPath(toLocalPath(pathOrUrl));
    if (path.isEmpty())
        return tr("Please choose a file name.");
    if (!ProjectFile::hasProjectExtension(path))
        path += QLatin1Char('.') + QLatin1String(ProjectFile::kExtension);

    ProjectData data = currentData(path);
    data.modifiedAt = QDateTime::currentDateTimeUtc();

    QString error;
    if (!ProjectFile::save(path, data, &error)) {
        qCWarning(lcProject) << "Save failed:" << path << error;
        return tr("Cannot save the project: %1").arg(error);
    }

    const bool pathChanged = (path != m_path);
    const QString oldAutosave = m_path.isEmpty() ? QString() : autosavePathFor(m_path);
    m_data = data;
    m_path = path;
    if (!oldAutosave.isEmpty())
        QFile::remove(oldAutosave);
    QFile::remove(autosavePathFor(path));
    m_recoveryAvailable = false;
    m_extraDirty = false;
    m_timeline.undoStack()->setClean();
    updateDirty();
    if (pathChanged)
        emit projectChanged();
    if (m_recents)
        m_recents->add(path, m_data.name);
    qCInfo(lcProject) << "Saved project" << path;
    return {};
}

void ProjectManager::closeProject()
{
    if (!m_open)
        return;
    // A deliberate close (after the user chose Save or Discard) removes any stale autosave.
    if (!m_path.isEmpty())
        QFile::remove(autosavePathFor(m_path));
    m_open = false;
    m_dirty = false;
    m_extraDirty = false;
    m_path.clear();
    m_data = ProjectData();
    m_playhead = 0;
    m_tracks.setTracks({});
    m_library.clear();
    m_timeline.load({});
    m_timeline.setMarkers({}, -1, -1);
    m_timeline.setTracks({});
    m_recoveryAvailable = false;
    emit projectChanged();
    emit dirtyChanged();
    emit playheadChanged();
    emit snapshotChanged();
}

QString ProjectManager::addTrack(const QString& kind)
{
    if (!m_open)
        return tr("There is no open project.");
    if (m_tracks.addTrack(kind) < 0)
        return kind == QLatin1String("subtitle") ? tr("The project already has a subtitle track.") : tr("No more tracks can be added.");
    return {};
}

QString ProjectManager::removeTrack(int index)
{
    if (!m_open || index < 0 || index >= m_tracks.count())
        return tr("There is no such track.");
    if (m_timeline.trackUsed(index))
        return tr("The track still contains clips. Remove them first.");
    if (!m_tracks.removeTrack(index))
        return tr("A project needs at least one video and one audio track.");
    return {};
}

QString ProjectManager::removeTrackWithClips(int index)
{
    if (!m_open || index < 0 || index >= m_tracks.count())
        return tr("There is no such track.");
    if (!m_tracks.canRemove(index))
        return tr("A project needs at least one video and one audio track.");
    if (const QString err = m_timeline.deleteClipsOnTrack(index); !err.isEmpty())
        return err;
    if (!m_tracks.removeTrack(index))
        return tr("A project needs at least one video and one audio track.");
    m_timeline.clearHistory();          // an undo could bring back clips of a track that no longer exists
    return {};
}

QString ProjectManager::removeMedia(const QString& mediaId)
{
    if (!m_library.contains(mediaId))
        return tr("This media is not part of the project.");
    if (m_timeline.mediaUsed(mediaId))
        return tr("This media is used on the timeline. Remove its clips first.");
    m_library.remove(mediaId);
    markExtraDirty();
    return {};
}

// ---- autosave / cache ---------------------------------------------------------------------------

QString ProjectManager::autosavePathFor(const QString& projectPath) const
{
    const QByteArray key = QCryptographicHash::hash(QDir::cleanPath(projectPath).toLower().toUtf8(),
                                                    QCryptographicHash::Sha1).toHex().left(16);
    return paths::autosaveDir() + QLatin1Char('/') + QString::fromLatin1(key) + QStringLiteral(".dvproj.autosave");
}

void ProjectManager::configureAutosave()
{
    const bool enabled = m_settings && m_settings->get(QStringLiteral("autoSaveEnabled")).toBool();
    if (!enabled) {
        m_autosaveTimer.stop();
        return;
    }
    m_autosaveTimer.setInterval(m_settings->get(QStringLiteral("autoSaveMinutes")).toInt() * 60 * 1000);
    m_autosaveTimer.start();
}

void ProjectManager::configureCache()
{
    QString dir = m_settings ? m_settings->get(QStringLiteral("cacheLocation")).toString() : QString();
    if (dir.isEmpty())
        dir = paths::defaultCacheDir();
    m_library.setCacheDir(dir);
}

void ProjectManager::autosaveNow()
{
    if (!m_open || !m_dirty || m_path.isEmpty())
        return;
    ProjectData data = currentData();
    data.modifiedAt = QDateTime::currentDateTimeUtc();
    QString error;
    // Written next to the app data, never over the user's project file.
    const QString file = autosavePathFor(m_path);
    if (fileutils::writeAtomic(file, QJsonDocument(ProjectFile::toJson(data)).toJson(QJsonDocument::Indented), &error))
        qCInfo(lcProject) << "Autosaved" << file;
    else
        qCWarning(lcProject) << "Autosave failed:" << error;
}

} // namespace dv
