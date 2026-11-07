#pragma once

#include "core/RenderSnapshot.h"
#include "media/MediaLibrary.h"
#include "project/ProjectData.h"
#include "project/TrackListModel.h"
#include "timeline/TimelineModel.h"

#include <QObject>
#include <functional>
#include <QTimer>
#include <QUrl>

namespace dv {

class SettingsManager;
class RecentProjectsModel;

// Owns the currently open project: media library, timeline, tracks, undo history, dirty tracking,
// auto save and the playhead. Every method that can fail returns a user facing error message;
// an empty string means success.
class ProjectManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY projectChanged FINAL)
    Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged FINAL)
    Q_PROPERTY(QString name READ name NOTIFY projectChanged FINAL)
    Q_PROPERTY(QString filePath READ filePath NOTIFY projectChanged FINAL)
    Q_PROPERTY(int width READ width NOTIFY projectChanged FINAL)
    Q_PROPERTY(int height READ height NOTIFY projectChanged FINAL)
    Q_PROPERTY(QString frameRateLabel READ frameRateLabel NOTIFY projectChanged FINAL)
    Q_PROPERTY(int nominalFps READ nominalFps NOTIFY projectChanged FINAL)
    Q_PROPERTY(int sampleRate READ sampleRate NOTIFY projectChanged FINAL)
    Q_PROPERTY(TrackListModel* tracks READ tracks CONSTANT FINAL)
    Q_PROPERTY(TimelineModel* timeline READ timeline CONSTANT FINAL)
    Q_PROPERTY(MediaLibrary* media READ media CONSTANT FINAL)
    Q_PROPERTY(MediaFilterModel* mediaView READ mediaView CONSTANT FINAL)
    Q_PROPERTY(qint64 playheadFrame READ playheadFrame WRITE setPlayheadFrame NOTIFY playheadChanged FINAL)
    Q_PROPERTY(QString timecode READ timecode NOTIFY playheadChanged FINAL)

public:
    ProjectManager(SettingsManager* settings, RecentProjectsModel* recents, QObject* parent = nullptr);
    ~ProjectManager() override;

    bool isOpen() const { return m_open; }
    bool dirty() const { return m_dirty; }
    QString name() const { return m_data.name; }
    QString filePath() const { return m_path; }
    int width() const { return m_data.width; }
    int height() const { return m_data.height; }
    QString frameRateLabel() const;
    int nominalFps() const { return dv::timecode::nominalFps(m_data.frameRate); }
    dv::timecode::FrameRate frameRate() const { return m_data.frameRate; }
    int sampleRate() const { return m_data.sampleRate; }
    TrackListModel* tracks() { return &m_tracks; }
    TimelineModel* timeline() { return &m_timeline; }
    MediaLibrary* media() { return &m_library; }
    MediaFilterModel* mediaView() { return &m_mediaView; }
    qint64 playheadFrame() const { return m_playhead; }
    void setPlayheadFrame(qint64 frame);
    QString timecode() const;

    // The document as it would be saved right now (relative media paths computed for `projectPath`).
    ProjectData currentData(const QString& projectPath = {}) const;
    // Immutable render description of the current timeline.
    SnapshotPtr snapshot() const;

    // Creates <folder>/<name>/<name>.dvproj. `folder` may be a local path or a file:// URL.
    Q_INVOKABLE QString createProject(const QString& name, const QString& folder, int width, int height,
                                      const QString& frameRate, int sampleRate);
    Q_INVOKABLE QString openProject(const QString& pathOrUrl);
    Q_INVOKABLE QString save();
    Q_INVOKABLE QString saveAs(const QString& pathOrUrl);
    Q_INVOKABLE void closeProject();

    // Tracks: new video tracks go on top of the video group, audio tracks at the bottom, one subtitle track on top.
    Q_INVOKABLE QString addTrack(const QString& kind);
    Q_INVOKABLE QString removeTrack(int index);                // only empty tracks, never the last video / audio track
    Q_INVOKABLE QString removeTrackWithClips(int index);       // deletes the clips on the track too; clears the undo history

    // Removes media that is not used on the timeline.
    Q_INVOKABLE QString removeMedia(const QString& mediaId);

    // Autosave recovery: after openProject() a newer autosave may exist.
    Q_INVOKABLE bool hasRecovery() const { return m_recoveryAvailable; }
    Q_INVOKABLE QString restoreRecovery();
    Q_INVOKABLE void discardRecovery();

    Q_INVOKABLE void stepFrames(int delta);
    Q_INVOKABLE QString timecodeFor(qint64 frame) const;
    Q_INVOKABLE qint64 framesToMicroseconds(qint64 frames) const { return dv::timecode::frameToMicroseconds(frames, m_data.frameRate); }
    Q_INVOKABLE static QString toLocalPath(const QString& pathOrUrl);
    Q_INVOKABLE static QString defaultLocation(const QString& configured);
    Q_INVOKABLE static QString validateName(const QString& name);
    // Maps a media file to its preview proxy (empty when there is none); consulted when snapshots are built.
    void setProxyLookup(std::function<QString(const QString&)> lookup) { m_proxyLookup = std::move(lookup); }

    void autosaveNow();

signals:
    void projectChanged();
    void dirtyChanged();
    void playheadChanged();
    void recoveryAvailable(const QString& path);
    // Emitted whenever the render description changed (edits, track toggles, media status).
    void snapshotChanged();

private:
    std::function<QString(const QString&)> m_proxyLookup;
    QString autosavePathFor(const QString& projectPath) const;
    void applyData(const ProjectData& data, const QString& path, bool dirty);
    void markExtraDirty();
    void updateDirty();
    void syncTracksToData();
    void configureAutosave();
    void configureCache();
    QString projectDir() const;

    SettingsManager* m_settings;
    RecentProjectsModel* m_recents;
    ProjectData m_data;
    TrackListModel m_tracks;
    MediaLibrary m_library;
    MediaFilterModel m_mediaView;
    TimelineModel m_timeline;
    QString m_path;
    bool m_open = false;
    bool m_dirty = false;
    bool m_extraDirty = false;   // changes that are not on the undo stack (tracks, media list)
    bool m_recoveryAvailable = false;
    qint64 m_playhead = 0;
    QTimer m_autosaveTimer;
};

} // namespace dv
