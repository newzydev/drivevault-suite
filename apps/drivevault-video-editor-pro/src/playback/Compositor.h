#pragma once

#include "core/RenderSnapshot.h"
#include "media/VideoReader.h"
#include "playback/Effects.h"

#include <QHash>
#include <QImage>
#include <QPainter>
#include <QSet>

#include <memory>

namespace dv {

// Renders one frame of the timeline: black background, then the video tracks from the lowest upwards (each clip
// with its transform, opacity, effects, colour grade and in / out transition), then the subtitle track. The preview
// and the exporter both use this class, so what is previewed is what is exported. One instance = one thread
// (it owns the decoders).
class Compositor {
public:
    // `size` is the output canvas (the project's aspect ratio, possibly scaled for the preview).
    QImage render(const RenderSnapshot& snapshot, int64_t frame, QSize size);

    // Preview only: keep rendered frames (least recently used first out) up to `bytes`; 0 switches the cache off. Frames are
    // only reused for the same snapshot revision, size and frame, so an edit can never show stale pictures.
    void setCacheBudget(qint64 bytes);
    // Preview only: decode the small proxy file of a clip when it has one.
    void setUseProxy(bool use);
    bool isCached(const RenderSnapshot& snapshot, int64_t frame, QSize size) const;
    quint64 cacheHits() const { return m_hits; }
    quint64 cacheMisses() const { return m_misses; }
    qint64 cacheBytes() const { return m_cacheBytes; }

    // Drops decoders for media that the snapshot no longer uses.
    void prune(const RenderSnapshot& snapshot);
    void reset() { m_readers.clear(); m_failed.clear(); m_titles.clear(); m_cache.clear(); m_cacheBytes = 0; m_cacheRevision = 0; }

    // Index of the clip showing on the given track at `frame`, or -1.
    static int clipAt(const RenderSnapshot& snapshot, int trackIndex, int64_t frame);
    // The text of a title rasterised on a transparent canvas (also used by tests).
    static QImage renderTitle(const TitleData& title, QSize canvas);

private:
    struct Entry {
        std::shared_ptr<VideoReader> reader;
        uint64_t used = 0;
    };
    struct Drawing {            // how a clip is drawn at this frame
        double alpha = 1.0;
        double scaleMul = 1.0;
        double offsetX = 0;     // in canvas widths
        double offsetY = 0;     // in canvas heights
        double rotationAdd = 0; // degrees (spin)
        // wipe: the part of the canvas that is revealed, as fractions of its width / height
        double clipLeft = 0.0;
        double clipRight = 1.0;
        double clipTop = 0.0;
        double clipBottom = 1.0;
        double flash = 0.0;     // 0 .. 1: a white flash over the canvas after the clip is drawn
    };

    static void applyTransition(Drawing& how, const QString& type, double t);
    QImage renderUncached(const RenderSnapshot& snapshot, int64_t frame, QSize size);
    static quint64 cacheKey(int64_t frame, QSize size);
    VideoReader* readerFor(const QString& path, int trackIndex);
    // The picture of a clip at timeline frame `sourceFrame` (may lie outside the clip for transitions).
    QImage layerImage(const RenderSnapshot& snapshot, const SnapshotClip& clip, int64_t clipFrame, int64_t sourceFrame, QSize canvas);
    void drawClip(QPainter& painter, const RenderSnapshot& snapshot, const SnapshotClip& clip, int64_t clipFrame,
                  int64_t sourceFrame, QSize canvas, const Drawing& how);
    void drawTrackClip(QPainter& painter, const RenderSnapshot& snapshot, int clipIndex, int64_t frame, QSize canvas);

    QHash<QString, Entry> m_readers;   // key: track index + path, so a split clip keeps its decoder
    QSet<QString> m_failed;
    QHash<QString, std::shared_ptr<const Lut3D>> m_luts;
    struct TitleCache { QString key; QImage image; };
    QHash<QString, TitleCache> m_titles;
    uint64_t m_tick = 0;
    struct CachedFrame { QImage image; uint64_t used = 0; };
    QHash<quint64, CachedFrame> m_cache;
    uint64_t m_cacheRevision = 0;
    qint64 m_cacheBudget = 0;
    qint64 m_cacheBytes = 0;
    bool m_useProxy = false;
    quint64 m_hits = 0;
    quint64 m_misses = 0;
};

} // namespace dv
