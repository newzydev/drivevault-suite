#include "Compositor.h"

#include "core/Logger.h"

#include <QFont>
#include <QFontMetricsF>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace dv {

int Compositor::clipAt(const RenderSnapshot& snapshot, int trackIndex, int64_t frame)
{
    for (int i = 0; i < snapshot.clips.size(); ++i) {
        const SnapshotClip& c = snapshot.clips.at(i);
        if (c.trackIndex == trackIndex && frame >= c.clip.startFrame && frame < c.clip.endFrame())
            return i;
    }
    return -1;
}

quint64 Compositor::cacheKey(int64_t frame, QSize size)
{
    return (quint64(frame) << 26) | (quint64(size.width() & 0x1fff) << 13) | quint64(size.height() & 0x1fff);
}

void Compositor::setCacheBudget(qint64 bytes)
{
    m_cacheBudget = std::max<qint64>(0, bytes);
    if (m_cacheBudget == 0) {
        m_cache.clear();
        m_cacheBytes = 0;
    }
}

void Compositor::setUseProxy(bool use)
{
    if (use == m_useProxy)
        return;
    m_useProxy = use;
    m_cache.clear();             // the frames were made from other pictures
    m_cacheBytes = 0;
}

bool Compositor::isCached(const RenderSnapshot& snapshot, int64_t frame, QSize size) const
{
    return m_cacheBudget > 0 && snapshot.revision != 0 && snapshot.revision == m_cacheRevision && m_cache.contains(cacheKey(frame, size));
}

QImage Compositor::render(const RenderSnapshot& snapshot, int64_t frame, QSize size)
{
    if (m_cacheBudget <= 0 || snapshot.revision == 0)
        return renderUncached(snapshot, frame, size);
    if (snapshot.revision != m_cacheRevision) {
        m_cache.clear();
        m_cacheBytes = 0;
        m_cacheRevision = snapshot.revision;
    }
    const quint64 key = cacheKey(frame, size);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        ++m_hits;
        it->used = ++m_tick;
        return it->image;
    }
    ++m_misses;
    const QImage image = renderUncached(snapshot, frame, size);
    const qint64 bytes = image.sizeInBytes();
    if (bytes > 0 && bytes <= m_cacheBudget) {
        while (m_cacheBytes + bytes > m_cacheBudget && !m_cache.isEmpty()) {
            auto oldest = std::min_element(m_cache.begin(), m_cache.end(),
                                           [](const CachedFrame& a, const CachedFrame& b) { return a.used < b.used; });
            m_cacheBytes -= oldest->image.sizeInBytes();
            m_cache.erase(oldest);
        }
        m_cache.insert(key, CachedFrame{image, ++m_tick});
        m_cacheBytes += bytes;
    }
    return image;
}

VideoReader* Compositor::readerFor(const QString& path, int trackIndex)
{
    const QString key = QString::number(trackIndex) + QLatin1Char('|') + path;
    if (m_failed.contains(key))
        return nullptr;
    auto it = m_readers.find(key);
    if (it == m_readers.end()) {
        auto reader = std::make_shared<VideoReader>();
        QString error;
        if (!reader->open(path, &error)) {
            qCWarning(lcProject) << "Cannot decode" << path << ":" << error;
            m_failed.insert(key);
            return nullptr;
        }
        // Keep the number of open decoders bounded: drop the least recently used one.
        while (m_readers.size() >= 12) {
            auto oldest = std::min_element(m_readers.begin(), m_readers.end(),
                                           [](const Entry& a, const Entry& b) { return a.used < b.used; });
            m_readers.erase(oldest);
        }
        it = m_readers.insert(key, Entry{std::move(reader), 0});
    }
    it->used = ++m_tick;
    return it->reader.get();
}

void Compositor::prune(const RenderSnapshot& snapshot)
{
    QSet<QString> alive;
    QSet<QString> titles;
    for (const SnapshotClip& c : snapshot.clips) {
        alive.insert(QString::number(c.trackIndex) + QLatin1Char('|') + c.mediaPath);
        if (!c.proxyPath.isEmpty())
            alive.insert(QString::number(c.trackIndex) + QLatin1Char('|') + c.proxyPath);
        if (c.clip.isTitle())
            titles.insert(c.clip.id);
    }
    for (auto it = m_readers.begin(); it != m_readers.end();)
        it = alive.contains(it.key()) ? ++it : m_readers.erase(it);
    for (auto it = m_failed.begin(); it != m_failed.end();)
        it = alive.contains(*it) ? ++it : m_failed.erase(it);
    for (auto it = m_titles.begin(); it != m_titles.end();)
        it = titles.contains(it.key()) ? ++it : m_titles.erase(it);
}

// ---- titles ------------------------------------------------------------------------------------------------------

QImage Compositor::renderTitle(const TitleData& t, QSize canvas)
{
    QImage img(canvas, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    if (t.text.trimmed().isEmpty())
        return img;

    const double k = canvas.height() / 1080.0;
    QFont font(t.font);
    font.setFamilies({t.font, QStringLiteral("Noto Sans Thai"), QStringLiteral("Leelawadee UI"), QStringLiteral("Segoe UI")});
    font.setPixelSize(std::max(6, int(std::lround(t.size * k))));
    font.setBold(t.bold);

    const double W = canvas.width();
    const double H = canvas.height();
    const double maxW = std::min(0.9 * W, t.align == QLatin1String("left") ? W - t.x * W : t.align == QLatin1String("right") ? t.x * W : 0.9 * W);
    int flags = Qt::TextWordWrap | Qt::AlignVCenter;
    flags |= t.align == QLatin1String("left") ? Qt::AlignLeft : t.align == QLatin1String("right") ? Qt::AlignRight : Qt::AlignHCenter;

    const QFontMetricsF fm(font);
    const QRectF bounds = fm.boundingRect(QRectF(0, 0, std::max(10.0, maxW), H), flags, t.text);
    const double boxW = std::max(10.0, maxW);
    const double boxH = bounds.height() + fm.height() * 0.2;
    double left = t.x * W - boxW / 2;
    if (t.align == QLatin1String("left"))
        left = t.x * W;
    else if (t.align == QLatin1String("right"))
        left = t.x * W - boxW;
    const QRectF box(left, t.y * H - boxH / 2, boxW, boxH);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.setFont(font);

    if (t.shadow) {
        const double o = std::max(2.0, t.size * k * 0.05);
        p.setPen(QColor(0, 0, 0, 170));
        p.drawText(box.translated(o, o), flags, t.text);
    }
    if (t.strokeWidth > 0) {
        const double sw = t.strokeWidth * k;
        p.setPen(QColor(t.strokeColor));
        for (int i = 0; i < 16; ++i) {
            const double a = i * M_PI / 8.0;
            p.drawText(box.translated(std::cos(a) * sw, std::sin(a) * sw), flags, t.text);
        }
    }
    p.setPen(QColor(t.color));
    p.drawText(box, flags, t.text);
    return img;
}

// ---- layers ---------------------------------------------------------------------------------------------------------

QImage Compositor::layerImage(const RenderSnapshot& snapshot, const SnapshotClip& sc, int64_t clipFrame, int64_t sourceFrame, QSize canvas)
{
    QImage image;
    if (sc.clip.isTitle()) {
        const QString key = QStringLiteral("%1|%2x%3|%4|%5|%6|%7|%8|%9|%10|%11|%12").arg(sc.clip.title.text, QString::number(canvas.width()),
            QString::number(canvas.height()), sc.clip.title.font, QString::number(sc.clip.title.size), sc.clip.title.color,
            sc.clip.title.strokeColor, QString::number(sc.clip.title.strokeWidth), QString::number(sc.clip.title.shadow),
            QString::number(sc.clip.title.bold), sc.clip.title.align, QString::number(sc.clip.title.x * 1000 + sc.clip.title.y));
        TitleCache& cache = m_titles[sc.clip.id];
        if (cache.key != key || cache.image.isNull()) {
            cache.image = renderTitle(sc.clip.title, canvas);
            cache.key = key;
        }
        image = cache.image;
    } else {
        if (!sc.online || !sc.media.hasVideo)
            return {};
        const QString& file = (m_useProxy && !sc.proxyPath.isEmpty()) ? sc.proxyPath : sc.mediaPath;
        VideoReader* reader = readerFor(file, sc.trackIndex);
        if (!reader)
            return {};
        const int64_t src = std::max<int64_t>(0, sc.clip.sourceInFrame + (sourceFrame - sc.clip.startFrame));
        const int64_t sourceUs = timecode::frameToMicroseconds(src, snapshot.fps);
        QSize fit = reader->displaySize().scaled(canvas, Qt::KeepAspectRatio);
        fit = QSize(std::max(2, fit.width()), std::max(2, fit.height()));
        image = reader->frameAt(sourceUs, fit);
    }
    if (image.isNull())
        return {};
    const EffectSettings fx = EffectSettings::fromClip(sc.clip, clipFrame, m_luts);
    if (!fx.isIdentity()) {
        image.detach();
        applyEffects(image, fx, canvas.height() / 1080.0);
    }
    return image;
}

void Compositor::drawClip(QPainter& p, const RenderSnapshot& snapshot, const SnapshotClip& sc, int64_t clipFrame,
                          int64_t sourceFrame, QSize canvas, const Drawing& how)
{
    const QImage image = layerImage(snapshot, sc, clipFrame, sourceFrame, canvas);
    if (image.isNull())
        return;
    const double opacity = std::clamp(sc.clip.param("opacity", clipFrame) * how.alpha, 0.0, 1.0);
    if (opacity <= 0.0)
        return;

    const double posX = sc.clip.param("posX", clipFrame);
    const double posY = sc.clip.param("posY", clipFrame);
    const double scale = sc.clip.param("scale", clipFrame) * how.scaleMul;
    const double rotation = sc.clip.param("rotation", clipFrame);
    const bool plain = posX == 0 && posY == 0 && scale == 1.0 && rotation == 0 && how.rotationAdd == 0 && how.offsetX == 0 && how.offsetY == 0;

    p.save();
    if (how.clipLeft > 0.0 || how.clipRight < 1.0 || how.clipTop > 0.0 || how.clipBottom < 1.0)
        p.setClipRect(QRectF(canvas.width() * how.clipLeft, canvas.height() * how.clipTop,
                             canvas.width() * (how.clipRight - how.clipLeft), canvas.height() * (how.clipBottom - how.clipTop)));
    p.setOpacity(opacity);
    if (plain) {
        p.drawImage(QPointF((canvas.width() - image.width()) / 2.0, (canvas.height() - image.height()) / 2.0), image);
    } else {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QTransform t;
        t.translate(canvas.width() / 2.0 + (posX + how.offsetX) * canvas.width(), canvas.height() / 2.0 + (posY + how.offsetY) * canvas.height());
        t.rotate(rotation + how.rotationAdd);
        t.scale(scale, scale);
        t.translate(-image.width() / 2.0, -image.height() / 2.0);
        p.setTransform(t);
        p.drawImage(0, 0, image);
    }
    p.restore();
    if (how.flash > 0.0) {
        p.save();
        p.setOpacity(std::clamp(how.flash, 0.0, 1.0));
        p.fillRect(QRectF(0, 0, canvas.width(), canvas.height()), Qt::white);
        p.restore();
    }
}

// How a clip looks `t` of the way through a transition (0 = hidden, 1 = fully shown).
void Compositor::applyTransition(Drawing& how, const QString& type, double t)
{
    const double inv = 1.0 - t;
    if (type == QLatin1String("dissolve") || type == QLatin1String("fade"))
        how.alpha *= t;
    else if (type == QLatin1String("dip"))
        how.alpha *= std::max(0.0, 2.0 * t - 1.0);
    else if (type == QLatin1String("wipe"))
        how.clipRight = std::max(0.001, t);
    else if (type == QLatin1String("wipeLeft"))
        how.clipLeft = std::min(0.999, inv);
    else if (type == QLatin1String("wipeDown"))
        how.clipBottom = std::max(0.001, t);
    else if (type == QLatin1String("wipeUp"))
        how.clipTop = std::min(0.999, inv);
    else if (type == QLatin1String("slide"))
        how.offsetX = inv;
    else if (type == QLatin1String("slideLeft"))
        how.offsetX = -inv;
    else if (type == QLatin1String("slideUp"))
        how.offsetY = inv;
    else if (type == QLatin1String("slideDown"))
        how.offsetY = -inv;
    else if (type == QLatin1String("zoom")) {
        how.scaleMul = 0.3 + 0.7 * t;
        how.alpha *= t;
    } else if (type == QLatin1String("zoomOut")) {
        how.scaleMul = 1.0 + 0.8 * inv;
        how.alpha *= t;
    } else if (type == QLatin1String("spin")) {
        how.scaleMul = 0.3 + 0.7 * t;
        how.rotationAdd = -180.0 * inv;
        how.alpha *= t;
    } else if (type == QLatin1String("flash")) {
        how.alpha *= t;
        how.flash = 1.0 - std::abs(2.0 * t - 1.0);
    }
}

void Compositor::drawTrackClip(QPainter& p, const RenderSnapshot& snapshot, int idx, int64_t frame, QSize canvas)
{
    const SnapshotClip& sc = snapshot.clips.at(idx);
    const ClipData& c = sc.clip;
    const int64_t cf = frame - c.startFrame;
    Drawing how;

    // ---- out transition: the clip fades away over its last frames ----
    if (c.transOut.active() && cf >= c.durationFrames - c.transOut.frames) {
        const double q = double(c.durationFrames - cf) / double(c.transOut.frames);   // 1 .. 0
        applyTransition(how, c.transOut.type, q);      // an out transition is the in transition played backwards
    }

    // ---- in transition: blend with the clip that ends where this one starts ----
    if (c.transIn.active() && cf < c.transIn.frames) {
        const double t = double(cf) / double(c.transIn.frames);                        // 0 .. 1
        const QString type = c.transIn.type;

        const SnapshotClip* previous = nullptr;
        for (const SnapshotClip& o : snapshot.clips)
            if (o.trackIndex == sc.trackIndex && o.clip.id != c.id && o.clip.endFrame() == c.startFrame)
                previous = &o;
        if (previous && type != QLatin1String("fade")) {
            Drawing back;
            if (type == QLatin1String("dip"))
                back.alpha = std::max(0.0, 1.0 - 2.0 * t);
            // The outgoing clip carries on past its end (its next source frames, or a hold on the last one).
            drawClip(p, snapshot, *previous, previous->clip.durationFrames - 1, frame, canvas, back);
        }
        applyTransition(how, type, t);
    }
    drawClip(p, snapshot, sc, cf, frame, canvas, how);
}

QImage Compositor::renderUncached(const RenderSnapshot& snapshot, int64_t frame, QSize size)
{
    QImage canvas(size, QImage::Format_RGB32);
    canvas.fill(Qt::black);
    if (snapshot.clips.isEmpty())
        return canvas;

    QPainter painter(&canvas);
    // Lowest video track first (the last entries of the track list), the top track is drawn last.
    for (int track = int(snapshot.tracks.size()) - 1; track >= 0; --track) {
        const TrackInfo& info = snapshot.tracks.at(track);
        if (info.kind != QLatin1String("video") || !info.visible)
            continue;
        const int idx = clipAt(snapshot, track, frame);
        if (idx >= 0)
            drawTrackClip(painter, snapshot, idx, frame, size);
    }
    // Subtitles are burnt in on top of everything.
    for (int track = int(snapshot.tracks.size()) - 1; track >= 0; --track) {
        const TrackInfo& info = snapshot.tracks.at(track);
        if (info.kind != QLatin1String("subtitle") || !info.visible)
            continue;
        const int idx = clipAt(snapshot, track, frame);
        if (idx >= 0)
            drawTrackClip(painter, snapshot, idx, frame, size);
    }
    painter.end();
    return canvas;
}

} // namespace dv
