#pragma once

#include "core/MediaTypes.h"
#include "core/Timecode.h"
#include "core/TimelineTypes.h"

#include <QAbstractListModel>
#include <QSet>
#include <QUndoStack>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>

namespace dv {

// What the timeline needs to know about a media item (provided by the media library).
struct MediaLite {
    QString name;
    QString kind;           // video | audio | image
    int64_t durationUs = 0;
    bool hasVideo = false;
    bool hasAudio = false;
    bool online = false;    // imported successfully and the file is present
};
using MediaLookup = std::function<std::optional<MediaLite>(const QString&)>;

// The clips of the open project, their selection and every editing operation.
//
// All positions are frame indices at the project frame rate. Every operation is expressed as
// "remove these clip versions, add those" and pushed on the QUndoStack, so Undo / Redo is exact.
// Clips that share a linkId (a video clip and its audio, or a user group) are edited together.
class TimelineModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int clipCount READ clipCount NOTIFY clipsChanged FINAL)
    Q_PROPERTY(qint64 durationFrames READ durationFrames NOTIFY clipsChanged FINAL)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoStateChanged FINAL)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY undoStateChanged FINAL)
    Q_PROPERTY(QString undoText READ undoText NOTIFY undoStateChanged FINAL)
    Q_PROPERTY(QString redoText READ redoText NOTIFY undoStateChanged FINAL)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged FINAL)
    Q_PROPERTY(QString primarySelectedId READ primarySelectedId NOTIFY selectionChanged FINAL)
    Q_PROPERTY(qint64 playheadFrame READ playheadFrame WRITE setPlayheadFrame NOTIFY playheadChanged FINAL)
    Q_PROPERTY(QVariantList markers READ markerList NOTIFY markersChanged FINAL)
    Q_PROPERTY(qint64 inPoint READ inPoint NOTIFY rangeChanged FINAL)
    Q_PROPERTY(qint64 outPoint READ outPoint NOTIFY rangeChanged FINAL)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY clipboardChanged FINAL)
    Q_PROPERTY(int subtitleCount READ subtitleCount NOTIFY clipsChanged FINAL)

public:
    enum Role {
        IdRole = Qt::UserRole + 1, MediaIdRole, TrackIndexRole, TrackIdRole, StartRole, DurationRole, SourceInRole,
        LinkRole, NameRole, KindRole, SelectedRole, OfflineRole, ClipTypeRole, TransInRole, TransOutRole, AnimatedRole
    };

    explicit TimelineModel(QObject* parent = nullptr);

    // ---- configuration ------------------------------------------------------------------------
    void setFrameRate(const timecode::FrameRate& rate) { m_rate = rate; }
    timecode::FrameRate frameRate() const { return m_rate; }
    void setTracks(const QList<TrackInfo>& tracks);
    void setMediaLookup(MediaLookup lookup) { m_media = std::move(lookup); }
    // Clips and undo history are replaced (project load / close).
    void load(const QList<ClipData>& clips);
    void setMarkers(const QList<MarkerData>& markers, qint64 inPoint, qint64 outPoint);
    QList<ClipData> clips() const { return m_clips; }
    QList<MarkerData> markerData() const { return m_markers; }
    // Media availability or names changed: refresh labels and the offline flag.
    void refreshMedia();

    // ---- model ----------------------------------------------------------------------------------
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int clipCount() const { return int(m_clips.size()); }
    qint64 durationFrames() const;
    qint64 playheadFrame() const { return m_playhead; }
    void setPlayheadFrame(qint64 frame);

    // ---- undo -----------------------------------------------------------------------------------
    QUndoStack* undoStack() { return &m_undo; }
    bool canUndo() const { return m_undo.canUndo(); }
    bool canRedo() const { return m_undo.canRedo(); }
    QString undoText() const { return m_undo.undoText(); }
    QString redoText() const { return m_undo.redoText(); }
    Q_INVOKABLE void undo() { m_undo.undo(); }
    Q_INVOKABLE void redo() { m_undo.redo(); }

    // ---- selection ------------------------------------------------------------------------------
    int selectedCount() const { return int(m_selected.size()); }
    QString primarySelectedId() const;
    Q_INVOKABLE QStringList selectedIds() const;
    // First selected clip that lives on a track of `kind` ("video" | "audio" | "subtitle"), or "".
    Q_INVOKABLE QString selectedClipOfKind(const QString& kind) const;
    Q_INVOKABLE void select(const QString& clipId, bool additive = false);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool isSelected(const QString& clipId) const { return m_selected.contains(clipId); }

    // ---- basic edits (each returns "" on success, otherwise a user-facing reason) --------------------
    // Adds a media item at `startFrame` on `trackIndex` (-1 = automatic). A video with audio becomes a
    // linked video + audio pair. Occupied space is skipped: the clip lands at the first free position.
    Q_INVOKABLE QString addMedia(const QString& mediaId, int trackIndex, qint64 startFrame);
    // Moves the clip (and its linked partners / the whole multi-selection) by the same amount. The dragged clip
    // may change track when it moves alone.
    Q_INVOKABLE QString moveClip(const QString& clipId, qint64 newStart, int newTrackIndex);
    Q_INVOKABLE QString trimStart(const QString& clipId, qint64 newStart);
    Q_INVOKABLE QString trimEnd(const QString& clipId, qint64 newEnd);
    // Splits at `frame`: the selection if any, otherwise every unlocked clip under the frame.
    Q_INVOKABLE QString splitAt(qint64 frame);
    Q_INVOKABLE QString deleteSelected();
    // Removes the gap-closing space too (Shift+Delete): later clips on the affected tracks move left.
    Q_INVOKABLE QString rippleDeleteSelected();

    // ---- professional tools -------------------------------------------------------------------------
    // Ripple trim: like trim, but the clips behind the edit shift by the same amount (no gaps / no overlap).
    Q_INVOKABLE QString rippleTrimEnd(const QString& clipId, qint64 newEnd);
    Q_INVOKABLE QString rippleTrimStart(const QString& clipId, qint64 newStart);
    // Roll: moves the cut between this clip and the clip that starts where it ends; the total length is unchanged.
    Q_INVOKABLE QString rollEdit(const QString& clipId, qint64 newCut);
    // Slip: changes which part of the media the clip shows (position and length stay).
    Q_INVOKABLE QString slip(const QString& clipId, qint64 deltaFrames);
    // Slide: moves the clip between its neighbours; the neighbours' cuts move with it.
    Q_INVOKABLE QString slide(const QString& clipId, qint64 deltaFrames);
    Q_INVOKABLE QString groupSelected();
    Q_INVOKABLE QString ungroupSelected();
    Q_INVOKABLE QString copySelected();
    Q_INVOKABLE QString cutSelected();
    Q_INVOKABLE QString paste(qint64 frame);
    bool canPaste() const { return !m_clipboard.isEmpty(); }

    // ---- parameters, keyframes, effects ---------------------------------------------------------------
    Q_INVOKABLE QVariantMap paramInfo(const QString& key) const;                 // {min, max, def, group}
    Q_INVOKABLE double paramValue(const QString& clipId, const QString& key, qint64 frame) const;   // frame on the timeline
    Q_INVOKABLE bool paramAnimated(const QString& clipId, const QString& key) const;
    Q_INVOKABLE bool keyframeAt(const QString& clipId, const QString& key, qint64 frame) const;
    Q_INVOKABLE QVariantList keyframes(const QString& clipId, const QString& key) const;   // [{frame, value, interp}] clip-relative
    // Sets a value. On an animated parameter this writes (or updates) a keyframe at `frame`; otherwise the constant.
    Q_INVOKABLE QString setParam(const QString& clipId, const QString& key, double value, qint64 frame);
    Q_INVOKABLE QString toggleKeyframe(const QString& clipId, const QString& key, qint64 frame);
    Q_INVOKABLE QString setKeyInterpolation(const QString& clipId, const QString& key, qint64 frame, const QString& interp);
    Q_INVOKABLE QString clearKeyframes(const QString& clipId, const QString& key);
    Q_INVOKABLE QString resetParams(const QString& clipId, const QString& group);   // transform | color | grade | effects | audio
    Q_INVOKABLE bool paramsModified(const QString& clipId, const QString& group) const;
    Q_INVOKABLE QString setCurve(const QString& clipId, const QString& channel, const QVariantList& points);   // [[x, y], ...]; empty = reset
    Q_INVOKABLE QVariantList curve(const QString& clipId, const QString& channel) const;
    Q_INVOKABLE QString setLut(const QString& clipId, const QString& path);          // "" removes it
    Q_INVOKABLE QString setFades(const QString& clipId, qint64 fadeInFrames, qint64 fadeOutFrames);
    // direction "in" | "out"; type "" removes the transition.
    Q_INVOKABLE QString setTransition(const QString& clipId, const QString& direction, const QString& type, qint64 frames);

    // ---- titles and subtitles -----------------------------------------------------------------------
    // preset: title | lowerThird | caption. Lands on `trackIndex` (a video track) at the first free position.
    Q_INVOKABLE QString addTitle(int trackIndex, qint64 startFrame, const QString& preset);
    Q_INVOKABLE QVariantMap titleInfo(const QString& clipId) const;
    Q_INVOKABLE QString setTitle(const QString& clipId, const QVariantMap& fields);
    Q_INVOKABLE QString addSubtitle(qint64 startFrame, qint64 durationFrames, const QString& text);
    Q_INVOKABLE QString importSrt(const QString& pathOrUrl);
    Q_INVOKABLE QString exportSrt(const QString& pathOrUrl) const;
    int subtitleCount() const;

    // ---- edits for the AI helpers (each one undo step) ----------------------------------------------------
    // Removes [start, end) frame ranges from every track and closes the gaps (all tracks shift together).
    Q_INVOKABLE QString removeRanges(const QVariantList& ranges);          // [{start, end}]
    Q_INVOKABLE QString replaceMedia(const QString& clipId, const QString& mediaId);
    Q_INVOKABLE QString setKeyframes(const QString& clipId, const QString& key, const QVariantList& keys);   // [{frame (timeline), value}]
    Q_INVOKABLE QString addSubtitles(const QVariantList& items);           // [{start, end, text}] in frames

    // ---- markers and the export range ------------------------------------------------------------------
    QVariantList markerList() const;
    qint64 inPoint() const { return m_in; }
    qint64 outPoint() const { return m_out; }
    Q_INVOKABLE QString addMarker(qint64 frame, const QString& name = {});
    Q_INVOKABLE void removeMarker(const QString& id);
    Q_INVOKABLE void renameMarker(const QString& id, const QString& name);
    Q_INVOKABLE qint64 nextMarker(qint64 frame) const;   // -1 when none
    Q_INVOKABLE qint64 prevMarker(qint64 frame) const;
    Q_INVOKABLE void setInPoint(qint64 frame);
    Q_INVOKABLE void setOutPoint(qint64 frame);
    Q_INVOKABLE void clearRange();

    // ---- drag feedback (no state change) ---------------------------------------------------------
    // {start, trackIndex, snapped}: nearest valid placement for a dragged clip.
    Q_INVOKABLE QVariantMap clampMove(const QString& clipId, qint64 desiredStart, int desiredTrack, bool snap, int toleranceFrames) const;
    Q_INVOKABLE qint64 clampTrimStart(const QString& clipId, qint64 desired, bool snap, int toleranceFrames) const;
    Q_INVOKABLE qint64 clampTrimEnd(const QString& clipId, qint64 desired, bool snap, int toleranceFrames) const;
    // First free start >= `frame` for a clip of `duration` frames on `trackIndex` (for drop previews).
    Q_INVOKABLE qint64 firstFreeStart(int trackIndex, qint64 frame, qint64 duration) const;
    Q_INVOKABLE qint64 mediaFrames(const QString& mediaId) const;      // media length in project frames (5 s for stills)
    Q_INVOKABLE int trackIndexForKind(const QString& kind) const;      // preferred track for a new clip of this kind
    Q_INVOKABLE QString trackKind(int trackIndex) const;
    Q_INVOKABLE bool trackLocked(int trackIndex) const;
    Q_INVOKABLE int trackCount() const { return int(m_tracks.size()); }
    Q_INVOKABLE bool mediaUsed(const QString& mediaId) const;
    Q_INVOKABLE bool trackUsed(int trackIndex) const;
    Q_INVOKABLE QString deleteClipsOnTrack(int trackIndex);   // one undo step; the linked clips of other tracks go with them
    Q_INVOKABLE void clearHistory();                          // forget undo / redo (after the track list changed)
    Q_INVOKABLE QVariantMap clipInfo(const QString& clipId) const;
    Q_INVOKABLE QString clipAt(int trackIndex, qint64 frame) const;

    const ClipData* findClip(const QString& id) const;
    QList<TrackInfo> tracks() const { return m_tracks; }
    int trackIndexOf(const QString& trackId) const;

    // Used by the undo commands (public so that the command classes can call them).
    void replaceClips(const QList<ClipData>& remove, const QList<ClipData>& add);

signals:
    void clipsChanged();       // any clip changed (rebuild snapshots, mark the project dirty)
    void edited();             // a user edit finished (add / move / trim / split / delete / undo / redo)
    void selectionChanged();
    void undoStateChanged();
    void playheadChanged();
    void markersChanged();
    void rangeChanged();
    void clipboardChanged();

private:
    friend class ReplaceClipsCommand;

    QList<ClipData> group(const QString& clipId) const;           // clip + linked partners
    QList<ClipData> groupOfAll(const QStringList& ids) const;
    QList<ClipData> movementSet(const QString& clipId) const;     // the clips that move together with this one
    bool trackLockedForClip(const ClipData& c) const;
    qint64 mediaFramesFor(const ClipData& c) const;
    bool isStill(const QString& mediaId) const;
    bool isUnbounded(const ClipData& c) const;                    // stills and titles can be stretched freely
    QString newClipId();
    QString newLinkId();
    void push(const QString& text, const QList<ClipData>& remove, const QList<ClipData>& add, const QString& mergeKey = {});
    QList<qint64> snapPoints(const QSet<QString>& exclude) const;
    static qint64 snapTo(qint64 value, const QList<qint64>& points, int tolerance, bool* snapped = nullptr);
    qint64 neighbourEndBefore(const ClipData& c, const QSet<QString>& exclude) const;     // end of the previous clip on the track
    qint64 neighbourStartAfter(const ClipData& c, const QSet<QString>& exclude) const;    // start of the next clip, or max
    const ClipData* adjacentAfter(const ClipData& c) const;       // clip on the same track that starts exactly where c ends
    bool overlaps(const QString& trackId, qint64 start, qint64 end, const QSet<QString>& exclude) const;
    int rowOfClip(const QString& id) const;
    QString editParams(const QString& clipId, const QString& text, const QString& mergeKey,
                       const std::function<bool(ClipData&)>& mutate);
    QString rippleTrim(const QString& clipId, qint64 desired, bool endEdge);
    QString addGeneratedClip(const ClipData& clip, int trackIndex, qint64 startFrame);

    QList<ClipData> m_clips;
    QList<ClipData> m_clipboard;
    QList<MarkerData> m_markers;
    QSet<QString> m_selected;
    QList<TrackInfo> m_tracks;
    timecode::FrameRate m_rate{30, 1};
    MediaLookup m_media;
    QUndoStack m_undo;
    qint64 m_playhead = 0;
    qint64 m_idCounter = 0;
    qint64 m_in = -1;
    qint64 m_out = -1;
};

} // namespace dv
