#include "TimelineModel.h"

#include <QUndoCommand>
#include <QUuid>

#include <algorithm>
#include <limits>

namespace dv {

namespace {
constexpr qint64 kUnbounded = std::numeric_limits<qint64>::max() / 4;
constexpr int kStillSeconds = 5;

struct Interval { qint64 lo; qint64 hi; };   // open interval of forbidden clip starts
} // namespace

class ReplaceClipsCommand : public QUndoCommand {
public:
    ReplaceClipsCommand(TimelineModel* model, const QString& text, QList<ClipData> remove, QList<ClipData> add,
                        QString mergeKey = {})
        : m_model(model), m_remove(std::move(remove)), m_add(std::move(add)), m_key(std::move(mergeKey))
    {
        setText(text);
    }
    void redo() override { m_model->replaceClips(m_remove, m_add); }
    void undo() override { m_model->replaceClips(m_add, m_remove); }
    // Dragging a slider produces many edits of the same parameter: they become one undo step.
    int id() const override { return m_key.isEmpty() ? -1 : 0x4d52; }
    bool mergeWith(const QUndoCommand* other) override
    {
        const auto* o = dynamic_cast<const ReplaceClipsCommand*>(other);
        if (!o || o->m_key != m_key || m_key.isEmpty())
            return false;
        m_add = o->m_add;
        return true;
    }

private:
    TimelineModel* m_model;
    QList<ClipData> m_remove;
    QList<ClipData> m_add;
    QString m_key;
};

TimelineModel::TimelineModel(QObject* parent)
    : QAbstractListModel(parent)
{
    connect(&m_undo, &QUndoStack::indexChanged, this, [this] {
        emit undoStateChanged();
        emit edited();
    });
    connect(&m_undo, &QUndoStack::canUndoChanged, this, &TimelineModel::undoStateChanged);
    connect(&m_undo, &QUndoStack::canRedoChanged, this, &TimelineModel::undoStateChanged);
    connect(&m_undo, &QUndoStack::undoTextChanged, this, &TimelineModel::undoStateChanged);
    connect(&m_undo, &QUndoStack::redoTextChanged, this, &TimelineModel::undoStateChanged);
}

// ---- model -------------------------------------------------------------------------------------------

int TimelineModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_clips.size());
}

QVariant TimelineModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_clips.size())
        return {};
    const ClipData& c = m_clips.at(index.row());
    switch (role) {
    case IdRole: return c.id;
    case MediaIdRole: return c.mediaId;
    case TrackIdRole: return c.trackId;
    case TrackIndexRole: return trackIndexOf(c.trackId);
    case StartRole: return qlonglong(c.startFrame);
    case DurationRole: return qlonglong(c.durationFrames);
    case SourceInRole: return qlonglong(c.sourceInFrame);
    case LinkRole: return c.linkId;
    case SelectedRole: return m_selected.contains(c.id);
    case KindRole: {
        const int t = trackIndexOf(c.trackId);
        return t >= 0 ? m_tracks.at(t).kind : QString();
    }
    case NameRole:
    case Qt::DisplayRole: {
        if (c.isTitle())
            return c.title.text.section(QLatin1Char(0x0A), 0, 0).left(60);
        const auto media = m_media ? m_media(c.mediaId) : std::nullopt;
        return media ? media->name : QStringLiteral("?");
    }
    case OfflineRole: {
        if (c.isTitle())
            return false;
        const auto media = m_media ? m_media(c.mediaId) : std::nullopt;
        return !media || !media->online;
    }
    case ClipTypeRole: return c.type;
    case TransInRole: return c.transIn.active() ? c.transIn.type : QString();
    case TransOutRole: return c.transOut.active() ? c.transOut.type : QString();
    case AnimatedRole: {
        for (const ParamTrack& p : c.params)
            if (p.animated())
                return true;
        return false;
    }
    default: return {};
    }
}

QHash<int, QByteArray> TimelineModel::roleNames() const
{
    return {{IdRole, "clipId"}, {MediaIdRole, "mediaId"}, {TrackIndexRole, "trackIndex"}, {TrackIdRole, "trackId"},
            {StartRole, "startFrame"}, {DurationRole, "durationFrames"}, {SourceInRole, "sourceInFrame"},
            {LinkRole, "linkId"}, {NameRole, "name"}, {KindRole, "kind"}, {SelectedRole, "selected"},
            {OfflineRole, "offline"}, {ClipTypeRole, "clipType"}, {TransInRole, "transIn"}, {TransOutRole, "transOut"},
            {AnimatedRole, "animated"}};
}

qint64 TimelineModel::durationFrames() const
{
    qint64 end = 0;
    for (const ClipData& c : m_clips)
        end = std::max<qint64>(end, c.endFrame());
    return end;
}

void TimelineModel::setPlayheadFrame(qint64 frame)
{
    frame = std::max<qint64>(0, frame);
    if (frame == m_playhead)
        return;
    m_playhead = frame;
    emit playheadChanged();
}

void TimelineModel::setTracks(const QList<TrackInfo>& tracks)
{
    const bool structural = tracks.size() != m_tracks.size()
                            || !std::equal(tracks.begin(), tracks.end(), m_tracks.begin(),
                                           [](const TrackInfo& a, const TrackInfo& b) { return a.id == b.id; });
    m_tracks = tracks;
    if (structural && !m_clips.isEmpty())
        emit dataChanged(index(0), index(rowCount() - 1));
}

void TimelineModel::load(const QList<ClipData>& clips)
{
    beginResetModel();
    m_clips = clips;
    m_selected.clear();
    m_idCounter = 0;
    for (const ClipData& c : m_clips) {
        for (const QString& s : {c.id, c.linkId}) {
            bool ok = false;
            const qint64 n = s.mid(1).toLongLong(&ok);
            if (ok)
                m_idCounter = std::max(m_idCounter, n);
        }
    }
    endResetModel();
    m_undo.clear();
    emit clipsChanged();
    emit selectionChanged();
}

void TimelineModel::refreshMedia()
{
    if (!m_clips.isEmpty())
        emit dataChanged(index(0), index(rowCount() - 1), {NameRole, OfflineRole});
    emit clipsChanged();
}

// ---- lookups --------------------------------------------------------------------------------------------

const ClipData* TimelineModel::findClip(const QString& id) const
{
    for (const ClipData& c : m_clips)
        if (c.id == id)
            return &c;
    return nullptr;
}

int TimelineModel::rowOfClip(const QString& id) const
{
    for (int i = 0; i < m_clips.size(); ++i)
        if (m_clips.at(i).id == id)
            return i;
    return -1;
}

int TimelineModel::trackIndexOf(const QString& trackId) const
{
    for (int i = 0; i < m_tracks.size(); ++i)
        if (m_tracks.at(i).id == trackId)
            return i;
    return -1;
}

QString TimelineModel::trackKind(int trackIndex) const
{
    return trackIndex >= 0 && trackIndex < m_tracks.size() ? m_tracks.at(trackIndex).kind : QString();
}

bool TimelineModel::trackLocked(int trackIndex) const
{
    return trackIndex >= 0 && trackIndex < m_tracks.size() && m_tracks.at(trackIndex).locked;
}

bool TimelineModel::trackLockedForClip(const ClipData& c) const
{
    return trackLocked(trackIndexOf(c.trackId));
}

int TimelineModel::trackIndexForKind(const QString& kind) const
{
    int firstUnlocked = -1;
    for (int i = 0; i < m_tracks.size(); ++i) {
        const TrackInfo& t = m_tracks.at(i);
        if (t.kind != kind || t.locked)
            continue;
        if (t.id.endsWith(QLatin1Char('1')))
            return i;                      // V1 / A1 are the primary tracks
        if (firstUnlocked < 0)
            firstUnlocked = i;
    }
    return firstUnlocked;
}

bool TimelineModel::isUnbounded(const ClipData& c) const
{
    return c.isTitle() || isStill(c.mediaId);
}

bool TimelineModel::isStill(const QString& mediaId) const
{
    const auto media = m_media ? m_media(mediaId) : std::nullopt;
    return media && media->kind == QLatin1String("image");
}

qint64 TimelineModel::mediaFrames(const QString& mediaId) const
{
    const auto media = m_media ? m_media(mediaId) : std::nullopt;
    if (!media || !m_rate.isValid())
        return 0;
    if (media->kind == QLatin1String("image"))
        return std::max<qint64>(1, qRound64(double(kStillSeconds) * m_rate.toDouble()));
    // floor(durationUs * fps / 1e6) without floating point
    const qint64 whole = (media->durationUs / 1000000) * m_rate.num / m_rate.den;
    const qint64 rest = ((media->durationUs % 1000000) * m_rate.num) / (m_rate.den * 1000000);
    return std::max<qint64>(1, whole + rest);
}

qint64 TimelineModel::mediaFramesFor(const ClipData& c) const
{
    return isUnbounded(c) ? kUnbounded : mediaFrames(c.mediaId);
}

bool TimelineModel::mediaUsed(const QString& mediaId) const
{
    return std::any_of(m_clips.begin(), m_clips.end(), [&](const ClipData& c) { return c.mediaId == mediaId; });
}

QString TimelineModel::clipAt(int trackIndex, qint64 frame) const
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size())
        return {};
    for (const ClipData& c : m_clips)
        if (c.trackId == m_tracks.at(trackIndex).id && frame >= c.startFrame && frame < c.endFrame())
            return c.id;
    return {};
}

QVariantMap TimelineModel::clipInfo(const QString& clipId) const
{
    const ClipData* c = findClip(clipId);
    if (!c)
        return {};
    const auto media = m_media ? m_media(c->mediaId) : std::nullopt;
    return {{"id", c->id},
            {"mediaId", c->mediaId},
            {"type", c->type},
            {"name", c->isTitle() ? c->title.text.section(QLatin1Char(0x0A), 0, 0) : (media ? media->name : QString())},
            {"transIn", c->transIn.type},
            {"transInFrames", qlonglong(c->transIn.frames)},
            {"transOut", c->transOut.type},
            {"transOutFrames", qlonglong(c->transOut.frames)},
            {"fadeIn", qlonglong(c->fadeInFrames)},
            {"fadeOut", qlonglong(c->fadeOutFrames)},
            {"lut", c->lutPath},
            {"kind", trackKind(trackIndexOf(c->trackId))},
            {"trackId", c->trackId},
            {"start", qlonglong(c->startFrame)},
            {"duration", qlonglong(c->durationFrames)},
            {"end", qlonglong(c->endFrame())},
            {"sourceIn", qlonglong(c->sourceInFrame)},
            {"startTimecode", timecode::format(c->startFrame, m_rate)},
            {"durationTimecode", timecode::format(c->durationFrames, m_rate)},
            {"endTimecode", timecode::format(c->endFrame(), m_rate)},
            {"sourceInTimecode", timecode::format(c->sourceInFrame, m_rate)},
            {"linked", !c->linkId.isEmpty() && group(c->id).size() > 1},
            {"offline", !c->isTitle() && (!media || !media->online)}};
}

// ---- selection ------------------------------------------------------------------------------------------

QString TimelineModel::primarySelectedId() const
{
    for (const ClipData& c : m_clips)
        if (m_selected.contains(c.id))
            return c.id;
    return {};
}

QStringList TimelineModel::selectedIds() const
{
    QStringList ids;
    for (const ClipData& c : m_clips)
        if (m_selected.contains(c.id))
            ids << c.id;
    return ids;
}

QString TimelineModel::selectedClipOfKind(const QString& kind) const
{
    for (const ClipData& c : m_clips)
        if (m_selected.contains(c.id) && trackKind(trackIndexOf(c.trackId)) == kind)
            return c.id;
    return {};
}

QList<ClipData> TimelineModel::group(const QString& clipId) const
{
    const ClipData* c = findClip(clipId);
    if (!c)
        return {};
    if (c->linkId.isEmpty())
        return {*c};
    QList<ClipData> out;
    for (const ClipData& o : m_clips)
        if (o.id == c->id || o.linkId == c->linkId)
            out.append(o);
    return out;
}

QList<ClipData> TimelineModel::groupOfAll(const QStringList& ids) const
{
    QList<ClipData> out;
    QSet<QString> seen;
    for (const QString& id : ids) {
        for (const ClipData& c : group(id)) {
            if (!seen.contains(c.id)) {
                seen.insert(c.id);
                out.append(c);
            }
        }
    }
    return out;
}

void TimelineModel::select(const QString& clipId, bool additive)
{
    QSet<QString> next = additive ? m_selected : QSet<QString>();
    const QList<ClipData> g = group(clipId);
    if (!g.isEmpty()) {
        const bool allSelected = std::all_of(g.begin(), g.end(), [&](const ClipData& c) { return next.contains(c.id); });
        for (const ClipData& c : g) {
            if (additive && allSelected)
                next.remove(c.id);
            else
                next.insert(c.id);
        }
    }
    if (next == m_selected)
        return;
    const QSet<QString> changed = (next - m_selected) + (m_selected - next);
    m_selected = next;
    for (const QString& id : changed) {
        const int row = rowOfClip(id);
        if (row >= 0)
            emit dataChanged(index(row), index(row), {SelectedRole});
    }
    emit selectionChanged();
}

void TimelineModel::selectAll()
{
    QSet<QString> all;
    for (const ClipData& c : m_clips)
        all.insert(c.id);
    if (all == m_selected)
        return;
    m_selected = all;
    if (!m_clips.isEmpty())
        emit dataChanged(index(0), index(rowCount() - 1), {SelectedRole});
    emit selectionChanged();
}

void TimelineModel::clearSelection()
{
    if (m_selected.isEmpty())
        return;
    m_selected.clear();
    if (!m_clips.isEmpty())
        emit dataChanged(index(0), index(rowCount() - 1), {SelectedRole});
    emit selectionChanged();
}

// ---- applying changes -----------------------------------------------------------------------------------

QString TimelineModel::newClipId() { return QStringLiteral("c%1").arg(++m_idCounter); }
QString TimelineModel::newLinkId() { return QStringLiteral("l%1").arg(++m_idCounter); }

void TimelineModel::replaceClips(const QList<ClipData>& remove, const QList<ClipData>& add)
{
    QSet<QString> addIds;
    for (const ClipData& a : add)
        addIds.insert(a.id);

    for (const ClipData& r : remove) {
        if (addIds.contains(r.id))
            continue;
        const int row = rowOfClip(r.id);
        if (row < 0)
            continue;
        beginRemoveRows({}, row, row);
        m_clips.removeAt(row);
        endRemoveRows();
    }
    for (const ClipData& a : add) {
        const int row = rowOfClip(a.id);
        if (row >= 0) {
            m_clips[row] = a;
            emit dataChanged(index(row), index(row));
        } else {
            const int at = int(m_clips.size());
            beginInsertRows({}, at, at);
            m_clips.append(a);
            endInsertRows();
        }
    }

    // Selection can only refer to clips that still exist.
    const int before = int(m_selected.size());
    QSet<QString> alive;
    for (const ClipData& c : m_clips)
        alive.insert(c.id);
    m_selected.intersect(alive);
    emit clipsChanged();
    if (m_selected.size() != before)
        emit selectionChanged();
}

void TimelineModel::push(const QString& text, const QList<ClipData>& remove, const QList<ClipData>& add, const QString& mergeKey)
{
    m_undo.push(new ReplaceClipsCommand(this, text, remove, add, mergeKey));
}

// ---- geometry helpers ---------------------------------------------------------------------------------

bool TimelineModel::overlaps(const QString& trackId, qint64 start, qint64 end, const QSet<QString>& exclude) const
{
    for (const ClipData& c : m_clips)
        if (c.trackId == trackId && !exclude.contains(c.id) && start < c.endFrame() && end > c.startFrame)
            return true;
    return false;
}

qint64 TimelineModel::neighbourEndBefore(const ClipData& c, const QSet<QString>& exclude) const
{
    qint64 best = 0;
    for (const ClipData& o : m_clips)
        if (o.trackId == c.trackId && !exclude.contains(o.id) && o.endFrame() <= c.startFrame)
            best = std::max(best, o.endFrame());
    return best;
}

qint64 TimelineModel::neighbourStartAfter(const ClipData& c, const QSet<QString>& exclude) const
{
    qint64 best = kUnbounded;
    for (const ClipData& o : m_clips)
        if (o.trackId == c.trackId && !exclude.contains(o.id) && o.startFrame >= c.endFrame())
            best = std::min(best, o.startFrame);
    return best;
}

QList<qint64> TimelineModel::snapPoints(const QSet<QString>& exclude) const
{
    QList<qint64> pts{0, m_playhead};
    for (const ClipData& c : m_clips) {
        if (exclude.contains(c.id))
            continue;
        pts << c.startFrame << c.endFrame();
    }
    return pts;
}

qint64 TimelineModel::snapTo(qint64 value, const QList<qint64>& points, int tolerance, bool* snapped)
{
    qint64 best = value;
    qint64 bestDist = tolerance + 1;
    for (qint64 p : points) {
        const qint64 d = std::abs(p - value);
        if (d < bestDist) {
            bestDist = d;
            best = p;
        }
    }
    if (snapped)
        *snapped = bestDist <= tolerance;
    return bestDist <= tolerance ? best : value;
}

qint64 TimelineModel::firstFreeStart(int trackIndex, qint64 frame, qint64 duration) const
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size())
        return std::max<qint64>(0, frame);
    const QString id = m_tracks.at(trackIndex).id;
    qint64 candidate = std::max<qint64>(0, frame);
    for (bool moved = true; moved;) {
        moved = false;
        for (const ClipData& c : m_clips) {
            if (c.trackId == id && candidate < c.endFrame() && candidate + duration > c.startFrame) {
                candidate = c.endFrame();
                moved = true;
            }
        }
    }
    return candidate;
}

// ---- edits --------------------------------------------------------------------------------------------

QString TimelineModel::addMedia(const QString& mediaId, int trackIndex, qint64 startFrame)
{
    const auto media = m_media ? m_media(mediaId) : std::nullopt;
    if (!media)
        return tr("This media is not part of the project.");
    if (!media->online)
        return tr("The media file is missing or still being imported.");

    const qint64 frames = mediaFrames(mediaId);
    if (frames <= 0)
        return tr("The media has no usable duration.");

    const bool wantVideo = media->hasVideo;
    const bool wantAudio = media->hasAudio && media->kind != QLatin1String("image");

    int videoTrack = -1;
    int audioTrack = -1;
    if (wantVideo)
        videoTrack = (trackKind(trackIndex) == QLatin1String("video") && !trackLocked(trackIndex)) ? trackIndex : trackIndexForKind("video");
    if (wantAudio)
        audioTrack = (trackKind(trackIndex) == QLatin1String("audio") && !trackLocked(trackIndex)) ? trackIndex : trackIndexForKind("audio");
    if ((wantVideo && videoTrack < 0) || (wantAudio && audioTrack < 0))
        return tr("All suitable tracks are locked.");

    // Find the first position where every part fits.
    qint64 start = std::max<qint64>(0, startFrame);
    for (bool moved = true; moved;) {
        moved = false;
        for (int t : {videoTrack, audioTrack}) {
            if (t < 0)
                continue;
            const qint64 free = firstFreeStart(t, start, frames);
            if (free != start) {
                start = free;
                moved = true;
            }
        }
    }

    QList<ClipData> add;
    const QString link = (wantVideo && wantAudio) ? newLinkId() : QString();
    for (int t : {videoTrack, audioTrack}) {
        if (t < 0)
            continue;
        ClipData c;
        c.id = newClipId();
        c.mediaId = mediaId;
        c.trackId = m_tracks.at(t).id;
        c.startFrame = start;
        c.durationFrames = frames;
        c.sourceInFrame = 0;
        c.linkId = link;
        add.append(c);
    }
    push(tr("Add clip"), {}, add);

    clearSelection();
    for (const ClipData& c : add)
        m_selected.insert(c.id);
    if (!m_clips.isEmpty())
        emit dataChanged(index(0), index(rowCount() - 1), {SelectedRole});
    emit selectionChanged();
    return {};
}

QVariantMap TimelineModel::clampMove(const QString& clipId, qint64 desiredStart, int desiredTrack, bool snap, int toleranceFrames) const
{
    const ClipData* dragged = findClip(clipId);
    if (!dragged)
        return {{"start", qlonglong(desiredStart)}, {"trackIndex", desiredTrack}, {"snapped", false}};
    const QList<ClipData> members = movementSet(clipId);
    const bool multi = members.size() > group(clipId).size();
    QSet<QString> ids;
    for (const ClipData& m : members)
        ids.insert(m.id);

    // Track: only a track of the same kind that is not locked.
    const int ownTrack = trackIndexOf(dragged->trackId);
    int track = ownTrack;
    if (!multi && desiredTrack >= 0 && desiredTrack < m_tracks.size() && m_tracks.at(desiredTrack).kind == m_tracks.at(ownTrack).kind
        && !m_tracks.at(desiredTrack).locked)
        track = desiredTrack;

    // Earliest start keeping every member at >= 0.
    qint64 minStart = 0;
    for (const ClipData& m : members)
        minStart = std::max(minStart, dragged->startFrame - m.startFrame);
    qint64 start = std::max(desiredStart, minStart);

    bool snapped = false;
    if (snap) {
        const QList<qint64> pts = snapPoints(ids);
        bool s1 = false;
        bool s2 = false;
        const qint64 a = snapTo(start, pts, toleranceFrames, &s1);
        const qint64 b = snapTo(start + dragged->durationFrames, pts, toleranceFrames, &s2) - dragged->durationFrames;
        if (s1 && (!s2 || std::abs(a - start) <= std::abs(b - start)))
            start = a, snapped = true;
        else if (s2)
            start = b, snapped = true;
        start = std::max(start, minStart);
    }

    // Forbidden start ranges (open intervals) caused by clips on the destination tracks.
    QList<Interval> forbidden;
    for (const ClipData& m : members) {
        const qint64 offset = m.startFrame - dragged->startFrame;
        const QString trackId = (m.id == dragged->id) ? m_tracks.at(track).id : m.trackId;
        for (const ClipData& o : m_clips) {
            if (ids.contains(o.id) || o.trackId != trackId)
                continue;
            forbidden.append({o.startFrame - m.durationFrames - offset, o.endFrame() - offset});
        }
    }
    auto inside = [&](qint64 s) {
        return std::any_of(forbidden.begin(), forbidden.end(), [&](const Interval& i) { return s > i.lo && s < i.hi; });
    };
    if (inside(start)) {
        qint64 best = -1;
        auto consider = [&](qint64 c) {
            if (c < minStart || inside(c))
                return;
            if (best < 0 || std::abs(c - start) < std::abs(best - start))
                best = c;
        };
        for (const Interval& i : forbidden) {
            consider(i.lo);
            consider(i.hi);
        }
        if (best >= 0)
            start = best;
        snapped = false;
    }
    return {{"start", qlonglong(start)}, {"trackIndex", track}, {"snapped", snapped}};
}

QString TimelineModel::moveClip(const QString& clipId, qint64 newStart, int newTrackIndex)
{
    const ClipData* dragged = findClip(clipId);
    if (!dragged)
        return tr("The clip no longer exists.");
    const QList<ClipData> members = movementSet(clipId);
    for (const ClipData& m : members)
        if (trackLockedForClip(m))
            return tr("The track is locked.");

    const int ownTrack = trackIndexOf(dragged->trackId);
    if (newTrackIndex < 0 || newTrackIndex >= m_tracks.size())
        newTrackIndex = ownTrack;
    // Callers pass a position returned by clampMove(); anything that would overlap or leave the timeline is refused.
    const QVariantMap valid = clampMove(clipId, newStart, newTrackIndex, false, 0);
    const qint64 start = valid.value("start").toLongLong();
    const int track = valid.value("trackIndex").toInt();
    if (start != newStart)
        return tr("There is no room for the clip at this position.");
    if (track != newTrackIndex)
        return tr("The clip cannot be moved to this track.");

    const qint64 delta = start - dragged->startFrame;
    QList<ClipData> moved;
    bool changed = false;
    for (ClipData m : members) {
        const bool isDragged = m.id == dragged->id && members.size() == group(clipId).size();
        const QString newTrackId = isDragged ? m_tracks.at(track).id : m.trackId;
        changed = changed || delta != 0 || newTrackId != m.trackId;
        m.startFrame += delta;
        m.trackId = newTrackId;
        moved.append(m);
    }
    if (!changed)
        return {};
    push(tr("Move clip"), members, moved);
    return {};
}

qint64 TimelineModel::clampTrimStart(const QString& clipId, qint64 desired, bool snap, int toleranceFrames) const
{
    const ClipData* c = findClip(clipId);
    if (!c)
        return desired;
    const QList<ClipData> members = group(clipId);
    QSet<QString> ids;
    for (const ClipData& m : members)
        ids.insert(m.id);
    if (snap)
        desired = snapTo(desired, snapPoints(ids), toleranceFrames);

    qint64 low = -kUnbounded;
    qint64 high = kUnbounded;
    for (const ClipData& m : members) {
        const qint64 room = m.startFrame - neighbourEndBefore(m, ids);
        const qint64 sourceRoom = isUnbounded(m) ? kUnbounded : m.sourceInFrame;
        low = std::max(low, -std::min(room, sourceRoom));
        high = std::min(high, m.durationFrames - 1);
    }
    const qint64 delta = std::clamp(desired - c->startFrame, low, std::max(low, high));
    return c->startFrame + delta;
}

qint64 TimelineModel::clampTrimEnd(const QString& clipId, qint64 desired, bool snap, int toleranceFrames) const
{
    const ClipData* c = findClip(clipId);
    if (!c)
        return desired;
    const QList<ClipData> members = group(clipId);
    QSet<QString> ids;
    for (const ClipData& m : members)
        ids.insert(m.id);
    if (snap)
        desired = snapTo(desired, snapPoints(ids), toleranceFrames);

    qint64 low = -kUnbounded;
    qint64 high = kUnbounded;
    for (const ClipData& m : members) {
        low = std::max(low, -(m.durationFrames - 1));
        const qint64 room = neighbourStartAfter(m, ids) == kUnbounded ? kUnbounded : neighbourStartAfter(m, ids) - m.endFrame();
        const qint64 mediaRoom = mediaFramesFor(m) == kUnbounded ? kUnbounded : mediaFramesFor(m) - (m.sourceInFrame + m.durationFrames);
        high = std::min(high, std::min(room, mediaRoom));
    }
    const qint64 delta = std::clamp(desired - c->endFrame(), low, std::max(low, high));
    return c->endFrame() + delta;
}

QString TimelineModel::trimStart(const QString& clipId, qint64 newStart)
{
    const ClipData* c = findClip(clipId);
    if (!c)
        return tr("The clip no longer exists.");
    const QList<ClipData> members = group(clipId);
    for (const ClipData& m : members)
        if (trackLockedForClip(m))
            return tr("The track is locked.");
    const qint64 delta = clampTrimStart(clipId, newStart, false, 0) - c->startFrame;
    if (delta == 0)
        return {};
    QList<ClipData> trimmed;
    for (ClipData m : members) {
        m.startFrame += delta;
        m.durationFrames -= delta;
        m.sourceInFrame += delta;
        trimmed.append(m);
    }
    push(tr("Trim clip"), members, trimmed);
    return {};
}

QString TimelineModel::trimEnd(const QString& clipId, qint64 newEnd)
{
    const ClipData* c = findClip(clipId);
    if (!c)
        return tr("The clip no longer exists.");
    const QList<ClipData> members = group(clipId);
    for (const ClipData& m : members)
        if (trackLockedForClip(m))
            return tr("The track is locked.");
    const qint64 delta = clampTrimEnd(clipId, newEnd, false, 0) - c->endFrame();
    if (delta == 0)
        return {};
    QList<ClipData> trimmed;
    for (ClipData m : members) {
        m.durationFrames += delta;
        trimmed.append(m);
    }
    push(tr("Trim clip"), members, trimmed);
    return {};
}

QString TimelineModel::splitAt(qint64 frame)
{
    QList<ClipData> remove;
    QList<ClipData> add;
    QHash<QString, QString> newLinks;
    bool lockedHit = false;
    // First the selection; when nothing selected is under the frame, every clip under the frame.
    for (int pass = 0; pass < 2 && add.isEmpty(); ++pass) {
    const QList<ClipData> targets = (pass == 0 && !m_selected.isEmpty()) ? groupOfAll(selectedIds()) : m_clips;
    for (const ClipData& t : targets) {
        if (frame <= t.startFrame || frame >= t.endFrame())
            continue;
        if (trackLockedForClip(t)) {
            lockedHit = true;
            continue;
        }
        ClipData left = t;
        left.durationFrames = frame - t.startFrame;
        ClipData right = t;
        right.id = newClipId();
        right.startFrame = frame;
        right.durationFrames = t.endFrame() - frame;
        right.sourceInFrame = t.sourceInFrame + (frame - t.startFrame);
        if (!t.linkId.isEmpty()) {
            if (!newLinks.contains(t.linkId))
                newLinks.insert(t.linkId, newLinkId());
            right.linkId = newLinks.value(t.linkId);
        }
        remove.append(t);
        add.append(left);
        add.append(right);
    }
    }
    if (add.isEmpty())
        return lockedHit ? tr("The track is locked.") : tr("Place the playhead over a clip to split it.");
    push(tr("Split clip"), remove, add);
    return {};
}

QString TimelineModel::deleteSelected()
{
    if (m_selected.isEmpty())
        return tr("Select a clip first.");
    const QList<ClipData> targets = groupOfAll(selectedIds());
    for (const ClipData& t : targets)
        if (trackLockedForClip(t))
            return tr("The track is locked.");
    push(tr("Delete clip"), targets, {});
    return {};
}

QString TimelineModel::rippleDeleteSelected()
{
    if (m_selected.isEmpty())
        return tr("Select a clip first.");
    const QList<ClipData> targets = groupOfAll(selectedIds());
    QSet<QString> ids;
    QSet<QString> tracks;
    for (const ClipData& t : targets) {
        if (trackLockedForClip(t))
            return tr("The track is locked.");
        ids.insert(t.id);
        tracks.insert(t.trackId);
    }
    QList<ClipData> remove = targets;
    QList<ClipData> add;
    for (const QString& trackId : tracks) {
        QList<Interval> gaps;
        for (const ClipData& t : targets)
            if (t.trackId == trackId)
                gaps.append({t.startFrame, t.endFrame()});
        std::sort(gaps.begin(), gaps.end(), [](const Interval& a, const Interval& b) { return a.lo < b.lo; });
        for (const ClipData& c : m_clips) {
            if (c.trackId != trackId || ids.contains(c.id))
                continue;
            qint64 shift = 0;
            for (const Interval& g : gaps)
                if (g.hi <= c.startFrame)
                    shift += g.hi - g.lo;
            if (shift == 0)
                continue;
            ClipData moved = c;
            moved.startFrame -= shift;
            remove.append(c);
            add.append(moved);
        }
    }
    push(tr("Ripple delete"), remove, add);
    return {};
}

} // namespace dv
