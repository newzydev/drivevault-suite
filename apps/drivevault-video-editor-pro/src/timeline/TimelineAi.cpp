// Edits that the AI helpers need: cutting ranges out of the whole sequence, swapping the media of a clip,
// writing many keyframes at once and adding a batch of subtitles. Each is one undo step.
#include "TimelineModel.h"

#include <QHash>

#include <algorithm>

namespace dv {

namespace {

// Re-bases the keyframes of a clip part that now starts `offset` frames later in its own time.
void rebaseParams(ClipData& c, int64_t offset)
{
    for (auto it = c.params.begin(); it != c.params.end(); ++it) {
        ParamTrack& t = it.value();
        if (!t.animated())
            continue;
        const double atStart = t.at(offset);
        QList<Keyframe> keys;
        keys.append({0, atStart, QStringLiteral("linear")});
        for (const Keyframe& k : std::as_const(t.keys))
            if (k.frame > offset)
                keys.append({k.frame - offset, k.value, k.interp});
        t.keys = keys;
    }
}

} // namespace

QString TimelineModel::removeRanges(const QVariantList& ranges)
{
    struct R { int64_t a, b; };
    QList<R> list;
    for (const QVariant& v : ranges) {
        const QVariantMap m = v.toMap();
        const int64_t a = m.value("start").toLongLong();
        const int64_t b = m.value("end").toLongLong();
        if (b > a && a >= 0)
            list.append({a, b});
    }
    if (list.isEmpty())
        return tr("There is nothing to cut.");
    std::sort(list.begin(), list.end(), [](const R& x, const R& y) { return x.a > y.a; });   // later ranges first: earlier frames stay valid
    for (const ClipData& c : std::as_const(m_clips))
        if (trackLockedForClip(c))
            return tr("Unlock all tracks first: a cut removes time from every track.");

    QList<ClipData> work = m_clips;
    QHash<QString, QString> newLinks;
    for (const R& r : std::as_const(list)) {
        const int64_t d = r.b - r.a;
        QList<ClipData> next;
        for (const ClipData& c : std::as_const(work)) {
            const int64_t s = c.startFrame, e = c.endFrame();
            if (e <= r.a) {
                next.append(c);
            } else if (s >= r.b) {
                ClipData m = c;
                m.startFrame -= d;
                next.append(m);
            } else if (s >= r.a && e <= r.b) {
                continue;                                   // inside the cut
            } else if (s < r.a && e > r.b) {                // the cut is inside the clip: two parts
                ClipData left = c;
                left.durationFrames = r.a - s;
                left.transOut = {};
                left.fadeOutFrames = 0;
                ClipData right = c;
                right.id = newClipId();
                right.startFrame = r.a;
                right.sourceInFrame = c.sourceInFrame + (r.b - s);
                right.durationFrames = e - r.b;
                right.transIn = {};
                right.fadeInFrames = 0;
                if (!c.linkId.isEmpty()) {
                    if (!newLinks.contains(c.linkId))
                        newLinks.insert(c.linkId, newLinkId());
                    right.linkId = newLinks.value(c.linkId);
                }
                rebaseParams(right, r.b - s);
                next.append(left);
                next.append(right);
            } else if (s < r.a) {                           // the cut eats the end
                ClipData m = c;
                m.durationFrames = r.a - s;
                m.transOut = {};
                m.fadeOutFrames = 0;
                next.append(m);
            } else {                                        // the cut eats the start; the rest moves up to the cut
                ClipData m = c;
                m.startFrame = r.a;
                m.sourceInFrame = c.sourceInFrame + (r.b - s);
                m.durationFrames = e - r.b;
                m.transIn = {};
                m.fadeInFrames = 0;
                rebaseParams(m, r.b - s);
                next.append(m);
            }
        }
        work = next;
    }

    // One command: remove every clip version that changed, add the new ones.
    QList<ClipData> remove, add;
    QHash<QString, const ClipData*> after;
    for (const ClipData& c : std::as_const(work))
        after.insert(c.id, &c);
    for (const ClipData& c : std::as_const(m_clips)) {
        const auto it = after.constFind(c.id);
        if (it == after.constEnd() || !(*it.value() == c))
            remove.append(c);
    }
    QSet<QString> before;
    for (const ClipData& c : std::as_const(m_clips))
        before.insert(c.id);
    for (const ClipData& c : std::as_const(work)) {
        const ClipData* old = findClip(c.id);
        if (!before.contains(c.id) || !old || !(*old == c))
            add.append(c);
    }
    if (remove.isEmpty() && add.isEmpty())
        return tr("There is nothing to cut.");
    push(tr("Cut silent passages"), remove, add);
    return {};
}

QString TimelineModel::replaceMedia(const QString& clipId, const QString& mediaId)
{
    if (mediaId.isEmpty() || !m_media || !m_media(mediaId))
        return tr("The media is not in the project.");
    return editParams(clipId, tr("Replace media"), {}, [&](ClipData& c) {
        if (c.isTitle())
            return false;
        c.mediaId = mediaId;
        return true;
    });
}

QString TimelineModel::setKeyframes(const QString& clipId, const QString& key, const QVariantList& keys)
{
    if (!params::find(key))
        return tr("Unknown parameter.");
    return editParams(clipId, tr("Keyframes"), {}, [&](ClipData& c) {
        ParamTrack track;
        for (const QVariant& v : keys) {
            const QVariantMap m = v.toMap();
            const int64_t rel = std::clamp<int64_t>(m.value("frame").toLongLong() - c.startFrame, 0, std::max<int64_t>(0, c.durationFrames - 1));
            track.keys.append({rel, params::clamp(key, m.value("value").toDouble()), QStringLiteral("linear")});
        }
        std::sort(track.keys.begin(), track.keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
        if (track.keys.isEmpty()) {
            c.params.remove(key);
        } else {
            track.value = track.keys.first().value;
            c.params.insert(key, track);
        }
        return true;
    });
}

QString TimelineModel::addSubtitles(const QVariantList& items)
{
    if (items.isEmpty())
        return tr("There are no subtitles to add.");
    QString firstError;
    int added = 0;
    m_undo.beginMacro(tr("Add subtitles"));
    for (const QVariant& v : items) {
        const QVariantMap m = v.toMap();
        const qint64 start = m.value("start").toLongLong();
        const qint64 dur = std::max<qint64>(1, m.value("end").toLongLong() - start);
        const QString err = addSubtitle(start, dur, m.value("text").toString());
        if (err.isEmpty())
            ++added;
        else if (firstError.isEmpty())
            firstError = err;
    }
    m_undo.endMacro();
    if (added == 0)
        return firstError;
    return {};
}

} // namespace dv
