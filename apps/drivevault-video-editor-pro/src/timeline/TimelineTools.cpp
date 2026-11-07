// Parameters / keyframes, titles and subtitles, markers, clipboard, grouping and the ripple / roll / slip /
// slide tools of TimelineModel.

#include "TimelineModel.h"

#include "core/FileUtils.h"

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>
#include <limits>

namespace dv {

namespace {
constexpr qint64 kBig = std::numeric_limits<qint64>::max() / 4;

QString toLocal(const QString& pathOrUrl)
{
    return pathOrUrl.startsWith(QLatin1String("file:"), Qt::CaseInsensitive) ? QUrl(pathOrUrl).toLocalFile() : pathOrUrl;
}

bool validTransition(const QString& direction, const QString& type)
{
    static const QStringList in = {"dissolve", "dip", "fade", "wipe", "slide", "zoom"};
    static const QStringList out = {"fade", "dip"};
    return type.isEmpty() || (direction == QLatin1String("in") ? in : out).contains(type);
}

QString srtTime(qint64 ms)
{
    return QStringLiteral("%1:%2:%3,%4").arg(ms / 3600000, 2, 10, QLatin1Char('0')).arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0')).arg(ms % 1000, 3, 10, QLatin1Char('0'));
}
} // namespace

// ---- helpers --------------------------------------------------------------------------------------------

QList<ClipData> TimelineModel::movementSet(const QString& clipId) const
{
    const QList<ClipData> g = group(clipId);
    if (g.isEmpty() || !m_selected.contains(clipId) || m_selected.size() <= g.size())
        return g;
    return groupOfAll(selectedIds());
}

const ClipData* TimelineModel::adjacentAfter(const ClipData& c) const
{
    for (const ClipData& o : m_clips)
        if (o.id != c.id && o.trackId == c.trackId && o.startFrame == c.endFrame())
            return &o;
    return nullptr;
}

bool TimelineModel::trackUsed(int trackIndex) const
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size())
        return false;
    const QString id = m_tracks.at(trackIndex).id;
    return std::any_of(m_clips.begin(), m_clips.end(), [&](const ClipData& c) { return c.trackId == id; });
}

QString TimelineModel::editParams(const QString& clipId, const QString& text, const QString& mergeKey,
                                  const std::function<bool(ClipData&)>& mutate)
{
    const ClipData* c = findClip(clipId);
    if (!c)
        return tr("The clip no longer exists.");
    ClipData next = *c;
    if (!mutate(next) || next == *c)
        return {};
    push(text, {*c}, {next}, mergeKey);
    return {};
}

// ---- parameters / keyframes ------------------------------------------------------------------------------

QVariantMap TimelineModel::paramInfo(const QString& key) const
{
    const params::Def* d = params::find(key);
    if (!d)
        return {};
    return {{"min", d->min}, {"max", d->max}, {"def", d->def}, {"group", QString::fromLatin1(d->group)}};
}

double TimelineModel::paramValue(const QString& clipId, const QString& key, qint64 frame) const
{
    const ClipData* c = findClip(clipId);
    return c ? c->param(key, frame - c->startFrame) : params::defaultOf(key);
}

bool TimelineModel::paramAnimated(const QString& clipId, const QString& key) const
{
    const ClipData* c = findClip(clipId);
    return c && c->params.contains(key) && c->params.value(key).animated();
}

bool TimelineModel::keyframeAt(const QString& clipId, const QString& key, qint64 frame) const
{
    const ClipData* c = findClip(clipId);
    if (!c || !c->params.contains(key))
        return false;
    const qint64 rel = frame - c->startFrame;
    const auto& keys = c->params.value(key).keys;
    return std::any_of(keys.begin(), keys.end(), [&](const Keyframe& k) { return k.frame == rel; });
}

QVariantList TimelineModel::keyframes(const QString& clipId, const QString& key) const
{
    QVariantList list;
    if (const ClipData* c = findClip(clipId); c && c->params.contains(key))
        for (const Keyframe& k : c->params.value(key).keys)
            list.append(QVariantMap{{"frame", qlonglong(k.frame)}, {"value", k.value}, {"interp", k.interp}});
    return list;
}

QString TimelineModel::setParam(const QString& clipId, const QString& key, double value, qint64 frame)
{
    if (!params::find(key))
        return tr("Unknown parameter.");
    value = params::clamp(key, value);
    return editParams(clipId, tr("Change %1").arg(key), clipId + QLatin1Char('|') + key, [&](ClipData& c) {
        ParamTrack track = c.params.value(key, ParamTrack{params::defaultOf(key), {}});
        if (track.animated()) {
            const qint64 rel = std::clamp<qint64>(frame - c.startFrame, 0, std::max<qint64>(0, c.durationFrames - 1));
            bool found = false;
            for (Keyframe& k : track.keys)
                if (k.frame == rel) {
                    k.value = value;
                    found = true;
                }
            if (!found) {
                track.keys.append({rel, value, QStringLiteral("linear")});
                std::sort(track.keys.begin(), track.keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
            }
        } else {
            track.value = value;
        }
        if (!track.animated() && qFuzzyCompare(track.value + 1000.0, params::defaultOf(key) + 1000.0))
            c.params.remove(key);        // back to the default: nothing to store
        else
            c.params.insert(key, track);
        return true;
    });
}

QString TimelineModel::toggleKeyframe(const QString& clipId, const QString& key, qint64 frame)
{
    if (!params::find(key))
        return tr("Unknown parameter.");
    return editParams(clipId, tr("Keyframe"), {}, [&](ClipData& c) {
        ParamTrack track = c.params.value(key, ParamTrack{params::defaultOf(key), {}});
        const qint64 rel = std::clamp<qint64>(frame - c.startFrame, 0, std::max<qint64>(0, c.durationFrames - 1));
        const double current = track.at(rel);
        const auto it = std::find_if(track.keys.begin(), track.keys.end(), [&](const Keyframe& k) { return k.frame == rel; });
        if (it != track.keys.end()) {
            track.keys.erase(it);
            if (track.keys.isEmpty())
                track.value = current;   // the last keyframe becomes the constant value
        } else {
            track.keys.append({rel, current, QStringLiteral("linear")});
            std::sort(track.keys.begin(), track.keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
        }
        if (!track.animated() && qFuzzyCompare(track.value + 1000.0, params::defaultOf(key) + 1000.0))
            c.params.remove(key);
        else
            c.params.insert(key, track);
        return true;
    });
}

QString TimelineModel::setKeyInterpolation(const QString& clipId, const QString& key, qint64 frame, const QString& interp)
{
    if (interp != QLatin1String("linear") && interp != QLatin1String("ease") && interp != QLatin1String("hold"))
        return tr("Unknown interpolation.");
    return editParams(clipId, tr("Keyframe"), {}, [&](ClipData& c) {
        if (!c.params.contains(key))
            return false;
        ParamTrack& track = c.params[key];
        const qint64 rel = frame - c.startFrame;
        for (Keyframe& k : track.keys)
            if (k.frame == rel)
                k.interp = interp;
        return true;
    });
}

QString TimelineModel::clearKeyframes(const QString& clipId, const QString& key)
{
    return editParams(clipId, tr("Remove keyframes"), {}, [&](ClipData& c) {
        if (!c.params.contains(key) || !c.params.value(key).animated())
            return false;
        ParamTrack track = c.params.value(key);
        track.value = track.keys.first().value;
        track.keys.clear();
        if (qFuzzyCompare(track.value + 1000.0, params::defaultOf(key) + 1000.0))
            c.params.remove(key);
        else
            c.params.insert(key, track);
        return true;
    });
}

QString TimelineModel::resetParams(const QString& clipId, const QString& group)
{
    return editParams(clipId, tr("Reset"), {}, [&](ClipData& c) {
        for (const params::Def& d : params::all())
            if (group == QLatin1String(d.group))
                c.params.remove(QString::fromLatin1(d.key));
        if (group == QLatin1String("color")) {
            c.lutPath.clear();
            c.curves.clear();
        }
        if (group == QLatin1String("audio")) {
            c.fadeInFrames = 0;
            c.fadeOutFrames = 0;
        }
        return true;
    });
}

bool TimelineModel::paramsModified(const QString& clipId, const QString& group) const
{
    const ClipData* c = findClip(clipId);
    if (!c)
        return false;
    for (auto it = c->params.cbegin(); it != c->params.cend(); ++it) {
        const params::Def* d = params::find(it.key());
        if (d && group == QLatin1String(d->group)
            && (it->animated() || !qFuzzyCompare(it->value + 1000.0, d->def + 1000.0)))
            return true;
    }
    if (group == QLatin1String("color") && (!c->lutPath.isEmpty() || !c->curves.isEmpty()))
        return true;
    if (group == QLatin1String("audio") && (c->fadeInFrames > 0 || c->fadeOutFrames > 0))
        return true;
    return false;
}

QString TimelineModel::setCurve(const QString& clipId, const QString& channel, const QVariantList& points)
{
    if (channel != QLatin1String("master") && channel != QLatin1String("r") && channel != QLatin1String("g") && channel != QLatin1String("b"))
        return tr("Unknown curve.");
    QList<QPointF> pts;
    for (const QVariant& v : points) {
        const QVariantList p = v.toList();
        if (p.size() == 2)
            pts.append(QPointF(std::clamp(p[0].toDouble(), 0.0, 1.0), std::clamp(p[1].toDouble(), 0.0, 1.0)));
    }
    std::sort(pts.begin(), pts.end(), [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
    const bool identity = pts.size() < 2 || (pts.size() == 2 && pts[0] == QPointF(0, 0) && pts[1] == QPointF(1, 1));
    return editParams(clipId, tr("Curves"), clipId + QStringLiteral("|curve|") + channel, [&](ClipData& c) {
        if (identity)
            c.curves.remove(channel);
        else
            c.curves.insert(channel, pts);
        return true;
    });
}

QVariantList TimelineModel::curve(const QString& clipId, const QString& channel) const
{
    QVariantList list;
    if (const ClipData* c = findClip(clipId); c && c->curves.contains(channel))
        for (const QPointF& p : c->curves.value(channel))
            list.append(QVariant(QVariantList{p.x(), p.y()}));   // QVariant(...): append(QList) would flatten the pair
    return list;
}

QString TimelineModel::setLut(const QString& clipId, const QString& path)
{
    const QString local = toLocal(path);
    if (!local.isEmpty() && !QFileInfo::exists(local))
        return tr("The file was not found.");
    return editParams(clipId, tr("Colour look"), {}, [&](ClipData& c) {
        c.lutPath = local;
        return true;
    });
}

QString TimelineModel::setFades(const QString& clipId, qint64 fadeInFrames, qint64 fadeOutFrames)
{
    return editParams(clipId, tr("Audio fades"), clipId + QStringLiteral("|fades"), [&](ClipData& c) {
        c.fadeInFrames = std::clamp<qint64>(fadeInFrames, 0, c.durationFrames);
        c.fadeOutFrames = std::clamp<qint64>(fadeOutFrames, 0, c.durationFrames - c.fadeInFrames);
        return true;
    });
}

QString TimelineModel::setTransition(const QString& clipId, const QString& direction, const QString& type, qint64 frames)
{
    if ((direction != QLatin1String("in") && direction != QLatin1String("out")) || !validTransition(direction, type))
        return tr("This transition is not available here.");
    return editParams(clipId, tr("Transition"), clipId + QStringLiteral("|trans|") + direction, [&](ClipData& c) {
        TransitionSpec spec;
        if (!type.isEmpty()) {
            spec.type = type;
            spec.frames = std::clamp<qint64>(frames, 1, c.durationFrames);
        }
        (direction == QLatin1String("in") ? c.transIn : c.transOut) = spec;
        return true;
    });
}

// ---- titles and subtitles -------------------------------------------------------------------------------

QString TimelineModel::addGeneratedClip(const ClipData& clip, int trackIndex, qint64 startFrame)
{
    if (trackIndex < 0 || trackIndex >= m_tracks.size())
        return tr("There is no suitable track.");
    if (m_tracks.at(trackIndex).locked)
        return tr("The track is locked.");
    ClipData c = clip;
    c.id = newClipId();
    c.trackId = m_tracks.at(trackIndex).id;
    c.startFrame = firstFreeStart(trackIndex, startFrame, c.durationFrames);
    push(tr("Add title"), {}, {c});
    clearSelection();
    select(c.id);
    return {};
}

QString TimelineModel::addTitle(int trackIndex, qint64 startFrame, const QString& preset)
{
    if (trackIndex < 0 || trackKind(trackIndex) != QLatin1String("video") || trackLocked(trackIndex)) {
        trackIndex = -1;
        for (int i = 0; i < m_tracks.size(); ++i)      // top-most unlocked video track
            if (m_tracks.at(i).kind == QLatin1String("video") && !m_tracks.at(i).locked) {
                trackIndex = i;
                break;
            }
    }
    ClipData c;
    c.type = QStringLiteral("title");
    c.durationFrames = std::max<qint64>(1, qRound64(5.0 * m_rate.toDouble()));
    TitleData& t = c.title;
    if (preset == QLatin1String("lowerThird")) {
        t.text = tr("Lower third");
        t.size = 52;
        t.x = 0.08;
        t.y = 0.84;
        t.align = QStringLiteral("left");
        t.shadow = true;
    } else if (preset == QLatin1String("caption")) {
        t.text = tr("Caption");
        t.size = 44;
        t.x = 0.5;
        t.y = 0.9;
        t.strokeWidth = 3;
    } else {
        t.text = tr("Title");
        t.size = 96;
    }
    return addGeneratedClip(c, trackIndex, startFrame);
}

QVariantMap TimelineModel::titleInfo(const QString& clipId) const
{
    const ClipData* c = findClip(clipId);
    if (!c || !c->isTitle())
        return {};
    const TitleData& t = c->title;
    return {{"text", t.text}, {"font", t.font}, {"size", t.size}, {"color", t.color}, {"strokeColor", t.strokeColor},
            {"strokeWidth", t.strokeWidth}, {"shadow", t.shadow}, {"bold", t.bold}, {"align", t.align}, {"x", t.x}, {"y", t.y}};
}

QString TimelineModel::setTitle(const QString& clipId, const QVariantMap& f)
{
    const QString key = f.size() == 1 ? clipId + QStringLiteral("|title|") + f.firstKey() : QString();
    return editParams(clipId, tr("Edit title"), key, [&](ClipData& c) {
        if (!c.isTitle())
            return false;
        TitleData& t = c.title;
        if (f.contains("text")) t.text = f.value("text").toString().left(2000);
        if (f.contains("font")) t.font = f.value("font").toString();
        if (f.contains("size")) t.size = std::clamp(f.value("size").toInt(), 6, 600);
        if (f.contains("color") && QColor(f.value("color").toString()).isValid()) t.color = f.value("color").toString();
        if (f.contains("strokeColor") && QColor(f.value("strokeColor").toString()).isValid()) t.strokeColor = f.value("strokeColor").toString();
        if (f.contains("strokeWidth")) t.strokeWidth = std::clamp(f.value("strokeWidth").toDouble(), 0.0, 60.0);
        if (f.contains("shadow")) t.shadow = f.value("shadow").toBool();
        if (f.contains("bold")) t.bold = f.value("bold").toBool();
        if (f.contains("align") && QStringList({"left", "center", "right"}).contains(f.value("align").toString()))
            t.align = f.value("align").toString();
        if (f.contains("x")) t.x = std::clamp(f.value("x").toDouble(), -1.0, 2.0);
        if (f.contains("y")) t.y = std::clamp(f.value("y").toDouble(), -1.0, 2.0);
        return true;
    });
}

int TimelineModel::subtitleCount() const
{
    int n = 0;
    for (const ClipData& c : m_clips)
        if (trackKind(trackIndexOf(c.trackId)) == QLatin1String("subtitle"))
            ++n;
    return n;
}

QString TimelineModel::addSubtitle(qint64 startFrame, qint64 durationFrames, const QString& text)
{
    int track = -1;
    for (int i = 0; i < m_tracks.size(); ++i)
        if (m_tracks.at(i).kind == QLatin1String("subtitle"))
            track = i;
    if (track < 0)
        return tr("Add a subtitle track first.");
    ClipData c;
    c.type = QStringLiteral("title");
    c.durationFrames = std::max<qint64>(1, durationFrames);
    c.title.text = text;
    c.title.size = 44;
    c.title.x = 0.5;
    c.title.y = 0.9;
    c.title.strokeWidth = 3;
    return addGeneratedClip(c, track, startFrame);
}

QString TimelineModel::importSrt(const QString& pathOrUrl)
{
    int track = -1;
    for (int i = 0; i < m_tracks.size(); ++i)
        if (m_tracks.at(i).kind == QLatin1String("subtitle"))
            track = i;
    if (track < 0)
        return tr("Add a subtitle track first.");
    if (m_tracks.at(track).locked)
        return tr("The track is locked.");

    QFile file(toLocal(pathOrUrl));
    if (!file.open(QIODevice::ReadOnly))
        return tr("Cannot open the file: %1").arg(file.errorString());
    QString text = QString::fromUtf8(file.readAll());
    if (text.startsWith(QChar(0xFEFF)))
        text.remove(0, 1);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));

    static const QRegularExpression timing(
        QStringLiteral(R"((\d+):(\d{2}):(\d{2})[,.](\d{1,3})\s*-->\s*(\d+):(\d{2}):(\d{2})[,.](\d{1,3}))"));
    auto ms = [](const QRegularExpressionMatch& m, int i) {
        QString frac = m.captured(i + 3).leftJustified(3, QLatin1Char('0'));
        return qint64(m.captured(i).toLongLong() * 3600000 + m.captured(i + 1).toLongLong() * 60000
                      + m.captured(i + 2).toLongLong() * 1000 + frac.toLongLong());
    };
    auto toFrame = [&](qint64 millis) { return (millis * m_rate.num + 500 * m_rate.den) / (1000 * m_rate.den); };

    struct Cue { qint64 start; qint64 end; QString text; };
    QList<Cue> cues;
    for (const QString& block : text.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts)) {
        const QStringList lines = block.split(QLatin1Char('\n'));
        int at = -1;
        QRegularExpressionMatch m;
        for (int i = 0; i < lines.size() && at < 0; ++i) {
            m = timing.match(lines.at(i));
            if (m.hasMatch())
                at = i;
        }
        if (at < 0)
            continue;
        const QString body = lines.mid(at + 1).join(QLatin1Char('\n')).trimmed();
        if (body.isEmpty())
            continue;
        cues.append({toFrame(ms(m, 1)), toFrame(ms(m, 5)), body});
    }
    if (cues.isEmpty())
        return tr("The file contains no subtitles.");
    std::sort(cues.begin(), cues.end(), [](const Cue& a, const Cue& b) { return a.start < b.start; });

    qint64 lastEnd = 0;
    for (const ClipData& c : m_clips)
        if (c.trackId == m_tracks.at(track).id)
            lastEnd = std::max(lastEnd, c.endFrame());
    QList<ClipData> add;
    qint64 cursor = 0;
    for (const Cue& cue : std::as_const(cues)) {
        ClipData c;
        c.id = newClipId();
        c.type = QStringLiteral("title");
        c.trackId = m_tracks.at(track).id;
        c.startFrame = std::max(cue.start, cursor);                  // SRT cues may overlap; clips may not
        c.durationFrames = std::max<qint64>(1, cue.end - c.startFrame);
        c.title.text = cue.text;
        c.title.size = 44;
        c.title.x = 0.5;
        c.title.y = 0.9;
        c.title.strokeWidth = 3;
        cursor = c.endFrame();
        add.append(c);
    }
    // Existing cues that would collide stay; the imported ones are shifted behind them.
    for (ClipData& c : add) {
        qint64 start = c.startFrame;
        for (bool moved = true; moved;) {
            moved = false;
            for (const ClipData& o : std::as_const(m_clips))
                if (o.trackId == c.trackId && start < o.endFrame() && start + c.durationFrames > o.startFrame) {
                    start = o.endFrame();
                    moved = true;
                }
        }
        c.startFrame = start;
    }
    Q_UNUSED(lastEnd)
    push(tr("Import subtitles"), {}, add);
    return {};
}

QString TimelineModel::exportSrt(const QString& pathOrUrl) const
{
    QList<ClipData> cues;
    for (const ClipData& c : m_clips)
        if (trackKind(trackIndexOf(c.trackId)) == QLatin1String("subtitle") && c.isTitle())
            cues.append(c);
    if (cues.isEmpty())
        return tr("There are no subtitles to export.");
    std::sort(cues.begin(), cues.end(), [](const ClipData& a, const ClipData& b) { return a.startFrame < b.startFrame; });

    QString out;
    int n = 1;
    for (const ClipData& c : std::as_const(cues)) {
        const qint64 s = (timecode::frameToMicroseconds(c.startFrame, m_rate) + 500) / 1000;
        const qint64 e = (timecode::frameToMicroseconds(c.endFrame(), m_rate) + 500) / 1000;
        out += QStringLiteral("%1\n%2 --> %3\n%4\n\n").arg(n++).arg(srtTime(s), srtTime(e), c.title.text);
    }
    QString error;
    if (!fileutils::writeAtomic(toLocal(pathOrUrl), out.toUtf8(), &error))
        return tr("Cannot save the file: %1").arg(error);
    return {};
}

// ---- markers / range ------------------------------------------------------------------------------------

QVariantList TimelineModel::markerList() const
{
    QVariantList list;
    for (const MarkerData& m : m_markers)
        list.append(QVariantMap{{"id", m.id}, {"frame", qlonglong(m.frame)}, {"name", m.name}, {"color", m.color}});
    return list;
}

void TimelineModel::setMarkers(const QList<MarkerData>& markers, qint64 inPoint, qint64 outPoint)
{
    m_markers = markers;
    std::sort(m_markers.begin(), m_markers.end(), [](const MarkerData& a, const MarkerData& b) { return a.frame < b.frame; });
    for (const MarkerData& m : m_markers) {
        bool ok = false;
        const qint64 n = m.id.mid(1).toLongLong(&ok);
        if (ok)
            m_idCounter = std::max(m_idCounter, n);
    }
    m_in = inPoint;
    m_out = outPoint;
    emit markersChanged();
    emit rangeChanged();
}

QString TimelineModel::addMarker(qint64 frame, const QString& name)
{
    MarkerData m;
    m.id = QStringLiteral("k%1").arg(++m_idCounter);
    m.frame = std::max<qint64>(0, frame);
    m.name = name.isEmpty() ? tr("Marker %1").arg(m_markers.size() + 1) : name;
    m_markers.append(m);
    std::sort(m_markers.begin(), m_markers.end(), [](const MarkerData& a, const MarkerData& b) { return a.frame < b.frame; });
    emit markersChanged();
    return m.id;
}

void TimelineModel::removeMarker(const QString& id)
{
    const auto it = std::find_if(m_markers.begin(), m_markers.end(), [&](const MarkerData& m) { return m.id == id; });
    if (it == m_markers.end())
        return;
    m_markers.erase(it);
    emit markersChanged();
}

void TimelineModel::renameMarker(const QString& id, const QString& name)
{
    for (MarkerData& m : m_markers)
        if (m.id == id && m.name != name) {
            m.name = name;
            emit markersChanged();
            return;
        }
}

qint64 TimelineModel::nextMarker(qint64 frame) const
{
    for (const MarkerData& m : m_markers)
        if (m.frame > frame)
            return m.frame;
    return -1;
}

qint64 TimelineModel::prevMarker(qint64 frame) const
{
    qint64 best = -1;
    for (const MarkerData& m : m_markers)
        if (m.frame < frame)
            best = m.frame;
    return best;
}

void TimelineModel::setInPoint(qint64 frame)
{
    m_in = std::max<qint64>(0, frame);
    if (m_out >= 0 && m_out <= m_in)
        m_out = -1;
    emit rangeChanged();
}

void TimelineModel::setOutPoint(qint64 frame)
{
    m_out = std::max<qint64>(1, frame);
    if (m_in >= 0 && m_in >= m_out)
        m_in = -1;
    emit rangeChanged();
}

void TimelineModel::clearRange()
{
    m_in = m_out = -1;
    emit rangeChanged();
}

// ---- grouping / clipboard ----------------------------------------------------------------------------------

QString TimelineModel::groupSelected()
{
    const QList<ClipData> sel = groupOfAll(selectedIds());
    if (sel.size() < 2)
        return tr("Select at least two clips to group them.");
    const QString link = newLinkId();
    QList<ClipData> changed;
    for (ClipData c : sel) {
        c.linkId = link;
        changed.append(c);
    }
    push(tr("Group clips"), sel, changed);
    return {};
}

QString TimelineModel::ungroupSelected()
{
    const QList<ClipData> sel = groupOfAll(selectedIds());
    QList<ClipData> remove;
    QList<ClipData> changed;
    for (ClipData c : sel) {
        if (c.linkId.isEmpty())
            continue;
        remove.append(c);
        c.linkId.clear();
        changed.append(c);
    }
    if (changed.isEmpty())
        return tr("The selected clips are not grouped.");
    push(tr("Ungroup clips"), remove, changed);
    return {};
}

QString TimelineModel::copySelected()
{
    const QList<ClipData> sel = groupOfAll(selectedIds());
    if (sel.isEmpty())
        return tr("Select a clip first.");
    m_clipboard = sel;
    emit clipboardChanged();
    return {};
}

QString TimelineModel::cutSelected()
{
    if (const QString err = copySelected(); !err.isEmpty())
        return err;
    const QString err = deleteSelected();
    if (!err.isEmpty()) {
        m_clipboard.clear();
        emit clipboardChanged();
    }
    return err;
}

QString TimelineModel::paste(qint64 frame)
{
    if (m_clipboard.isEmpty())
        return tr("There is nothing to paste.");
    qint64 base = kBig;
    for (const ClipData& c : std::as_const(m_clipboard))
        base = std::min(base, c.startFrame);
    for (const ClipData& c : std::as_const(m_clipboard)) {
        const int t = trackIndexOf(c.trackId);
        if (t < 0)
            return tr("The track of the copied clip no longer exists.");
        if (m_tracks.at(t).locked)
            return tr("The track is locked.");
        if (!c.isTitle() && !(m_media && m_media(c.mediaId)))
            return tr("The media of the copied clip is no longer in the project.");
    }

    qint64 delta = std::max<qint64>(0, frame) - base;
    for (int guard = 0; guard < 10000; ++guard) {
        bool moved = false;
        for (const ClipData& c : std::as_const(m_clipboard)) {
            const int t = trackIndexOf(c.trackId);
            const qint64 want = c.startFrame + delta;
            const qint64 free = firstFreeStart(t, want, c.durationFrames);
            if (free > want) {
                delta += free - want;
                moved = true;
            }
        }
        if (!moved)
            break;
    }

    QHash<QString, QString> links;
    QList<ClipData> add;
    for (ClipData c : std::as_const(m_clipboard)) {
        c.id = newClipId();
        c.startFrame += delta;
        if (!c.linkId.isEmpty()) {
            if (!links.contains(c.linkId))
                links.insert(c.linkId, newLinkId());
            c.linkId = links.value(c.linkId);
        }
        add.append(c);
    }
    push(tr("Paste"), {}, add);
    m_selected.clear();
    for (const ClipData& c : std::as_const(add))
        m_selected.insert(c.id);
    if (!m_clips.isEmpty())
        emit dataChanged(index(0), index(rowCount() - 1), {SelectedRole});
    emit selectionChanged();
    return {};
}

// ---- ripple / roll / slip / slide ------------------------------------------------------------------------

QString TimelineModel::rippleTrim(const QString& clipId, qint64 desired, bool endEdge)
{
    const ClipData* c = findClip(clipId);
    if (!c)
        return tr("The clip no longer exists.");
    const QList<ClipData> members = group(clipId);
    QSet<QString> ids;
    QSet<QString> tracks;
    for (const ClipData& m : members) {
        if (trackLockedForClip(m))
            return tr("The track is locked.");
        ids.insert(m.id);
        tracks.insert(m.trackId);
    }

    qint64 low = -kBig;
    qint64 high = kBig;
    for (const ClipData& m : members) {
        if (endEdge) {
            low = std::max(low, -(m.durationFrames - 1));
            if (mediaFramesFor(m) != kBig)
                high = std::min(high, mediaFramesFor(m) - (m.sourceInFrame + m.durationFrames));
        } else {
            if (!isUnbounded(m))
                low = std::max(low, -m.sourceInFrame);
            high = std::min(high, m.durationFrames - 1);
        }
    }
    const qint64 current = endEdge ? c->endFrame() : c->startFrame;
    const qint64 delta = std::clamp(desired - current, low, std::max(low, high));
    if (delta == 0)
        return {};

    QList<ClipData> remove;
    QList<ClipData> add;
    for (const ClipData& m : members) {
        ClipData n = m;
        if (endEdge) {
            n.durationFrames += delta;
        } else {
            n.sourceInFrame += delta;
            n.durationFrames -= delta;
        }
        remove.append(m);
        add.append(n);
    }
    // Everything behind the edit on the affected tracks follows (the head trim shortens the clip, so it moves by -delta).
    const qint64 shift = endEdge ? delta : -delta;
    for (const ClipData& o : m_clips) {
        if (ids.contains(o.id) || !tracks.contains(o.trackId))
            continue;
        for (const ClipData& m : members) {
            if (m.trackId == o.trackId && o.startFrame >= m.endFrame()) {
                ClipData n = o;
                n.startFrame += shift;
                remove.append(o);
                add.append(n);
                break;
            }
        }
    }
    push(tr("Ripple trim"), remove, add);
    return {};
}

QString TimelineModel::rippleTrimEnd(const QString& clipId, qint64 newEnd) { return rippleTrim(clipId, newEnd, true); }
QString TimelineModel::rippleTrimStart(const QString& clipId, qint64 newStart) { return rippleTrim(clipId, newStart, false); }

QString TimelineModel::rollEdit(const QString& clipId, qint64 newCut)
{
    const ClipData* c = findClip(clipId);
    if (!c)
        return tr("The clip no longer exists.");
    struct Pair { ClipData a; ClipData b; };
    QList<Pair> pairs;
    for (const ClipData& m : group(clipId)) {
        if (const ClipData* next = adjacentAfter(m)) {
            if (trackLockedForClip(m))
                return tr("The track is locked.");
            pairs.append({m, *next});
        }
    }
    if (pairs.isEmpty())
        return tr("There is no clip directly behind this one.");

    qint64 low = -kBig;
    qint64 high = kBig;
    for (const Pair& p : std::as_const(pairs)) {
        low = std::max(low, -(p.a.durationFrames - 1));
        high = std::min(high, p.b.durationFrames - 1);
        if (mediaFramesFor(p.a) != kBig)
            high = std::min(high, mediaFramesFor(p.a) - (p.a.sourceInFrame + p.a.durationFrames));
        if (!isUnbounded(p.b))
            low = std::max(low, -p.b.sourceInFrame);
    }
    const qint64 delta = std::clamp(newCut - c->endFrame(), low, std::max(low, high));
    if (delta == 0)
        return {};
    QList<ClipData> remove;
    QList<ClipData> add;
    for (const Pair& p : std::as_const(pairs)) {
        ClipData a = p.a;
        ClipData b = p.b;
        a.durationFrames += delta;
        b.startFrame += delta;
        b.sourceInFrame += delta;
        b.durationFrames -= delta;
        remove << p.a << p.b;
        add << a << b;
    }
    push(tr("Roll edit"), remove, add);
    return {};
}

QString TimelineModel::slip(const QString& clipId, qint64 deltaFrames)
{
    const QList<ClipData> members = group(clipId);
    if (members.isEmpty())
        return tr("The clip no longer exists.");
    qint64 low = -kBig;
    qint64 high = kBig;
    for (const ClipData& m : members) {
        if (trackLockedForClip(m))
            return tr("The track is locked.");
        if (isUnbounded(m))
            return tr("Stills and titles have no source to slip.");
        low = std::max(low, -m.sourceInFrame);
        high = std::min(high, mediaFramesFor(m) - (m.sourceInFrame + m.durationFrames));
    }
    const qint64 delta = std::clamp(deltaFrames, low, std::max(low, high));
    if (delta == 0)
        return {};
    QList<ClipData> moved;
    for (ClipData m : members) {
        m.sourceInFrame += delta;
        moved.append(m);
    }
    push(tr("Slip clip"), members, moved, clipId + QStringLiteral("|slip"));
    return {};
}

QString TimelineModel::slide(const QString& clipId, qint64 deltaFrames)
{
    const ClipData* c = findClip(clipId);
    if (!c)
        return tr("The clip no longer exists.");
    if (group(clipId).size() > 1)
        return tr("Ungroup the clip first: slide works on a single clip.");
    if (trackLockedForClip(*c))
        return tr("The track is locked.");
    const ClipData* next = adjacentAfter(*c);
    const ClipData* prev = nullptr;
    for (const ClipData& o : m_clips)
        if (o.id != c->id && o.trackId == c->trackId && o.endFrame() == c->startFrame)
            prev = &o;
    if (!prev || !next)
        return tr("Slide needs a clip directly before and after.");

    qint64 low = -(prev->durationFrames - 1);
    qint64 high = next->durationFrames - 1;
    if (mediaFramesFor(*prev) != kBig)
        high = std::min(high, mediaFramesFor(*prev) - (prev->sourceInFrame + prev->durationFrames));
    if (!isUnbounded(*next))
        low = std::max(low, -next->sourceInFrame);
    const qint64 delta = std::clamp(deltaFrames, low, std::max(low, high));
    if (delta == 0)
        return {};
    ClipData p = *prev;
    ClipData m = *c;
    ClipData n = *next;
    p.durationFrames += delta;
    m.startFrame += delta;
    n.startFrame += delta;
    n.sourceInFrame += delta;
    n.durationFrames -= delta;
    push(tr("Slide clip"), {*prev, *c, *next}, {p, m, n});
    return {};
}

} // namespace dv
