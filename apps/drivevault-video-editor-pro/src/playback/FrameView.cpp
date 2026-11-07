#include "FrameView.h"

#include "media/MediaLibrary.h"
#include "playback/PlaybackEngine.h"

#include <QPainter>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>

#include <algorithm>

namespace dv {

FrameView::FrameView(QQuickItem* parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, true);
}

QObject* FrameView::engine() const
{
    return m_engine;
}

void FrameView::setEngine(QObject* engine)
{
    auto* e = qobject_cast<PlaybackEngine*>(engine);
    if (e == m_engine)
        return;
    if (m_engine)
        disconnect(m_engine, nullptr, this, nullptr);
    m_engine = e;
    if (m_engine) {
        connect(m_engine, &PlaybackEngine::frameReady, this, [this](const QImage& image) { setImage(image); });
        setImage(m_engine->lastImage());
    }
    emit engineChanged();
}

void FrameView::setImage(const QImage& image)
{
    m_image = image;
    m_dirty = true;
    update();
}

QSGNode* FrameView::updatePaintNode(QSGNode* old, UpdatePaintNodeData*)
{
    auto* node = static_cast<QSGSimpleTextureNode*>(old);
    if (m_image.isNull()) {
        delete node;
        return nullptr;
    }
    if (!node) {
        node = new QSGSimpleTextureNode;
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
        m_dirty = true;
    }
    if (m_dirty && window()) {
        node->setTexture(window()->createTextureFromImage(m_image));
        m_dirty = false;
    }
    node->setRect(boundingRect());
    return node;
}

// ------------------------------------------------------------------------------------------------

WaveformItem::WaveformItem(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(false);
}

QObject* WaveformItem::library() const { return m_library; }

void WaveformItem::setLibrary(QObject* library)
{
    auto* lib = qobject_cast<MediaLibrary*>(library);
    if (lib == m_library)
        return;
    if (m_library)
        disconnect(m_library, nullptr, this, nullptr);
    m_library = lib;
    if (m_library)
        connect(m_library, &MediaLibrary::libraryChanged, this, [this] { update(); });   // peaks arrive asynchronously
    emit changed();
    update();
}

void WaveformItem::setMediaId(const QString& id) { if (id != m_mediaId) { m_mediaId = id; emit changed(); update(); } }
void WaveformItem::setSourceStartUs(double v) { if (v != m_start) { m_start = v; emit changed(); update(); } }
void WaveformItem::setSpanUs(double v) { if (v != m_span) { m_span = v; emit changed(); update(); } }
void WaveformItem::setColor(const QColor& c) { if (c != m_color) { m_color = c; emit changed(); update(); } }

void WaveformItem::paint(QPainter* painter)
{
    if (!m_library || m_mediaId.isEmpty() || m_span <= 0 || width() < 1)
        return;
    const auto peaks = m_library->peaks(m_mediaId);
    if (!peaks || peaks->empty())
        return;

    const int w = int(std::ceil(width()));
    const double mid = height() / 2.0;
    const double perPeakUs = 1e6 / mediacache::kPeaksPerSecond;
    painter->setPen(QPen(m_color, 1));
    for (int x = 0; x < w; ++x) {
        const double t0 = m_start + m_span * x / width();
        const double t1 = m_start + m_span * (x + 1) / width();
        const int i0 = std::clamp(int(t0 / perPeakUs), 0, int(peaks->size()) - 1);
        const int i1 = std::clamp(int(t1 / perPeakUs), i0, int(peaks->size()) - 1);
        int peak = 0;
        for (int i = i0; i <= i1; ++i)
            peak = std::max<int>(peak, (*peaks)[size_t(i)]);
        // Gentle gamma so quiet passages stay visible.
        const double amp = std::pow(peak / 255.0, 0.6) * (height() / 2.0 - 1.0);
        painter->drawLine(QPointF(x + 0.5, mid - amp), QPointF(x + 0.5, mid + amp));
    }
}

} // namespace dv
