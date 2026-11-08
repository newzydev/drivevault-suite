#pragma once

#include <QImage>
#include <QQuickItem>
#include <QQuickPaintedItem>

namespace dv {

class PlaybackEngine;
class MediaLibrary;
class FrameThumbs;

// QML item that shows the frames delivered by the PlaybackEngine (GPU texture, no copies in QML).
class FrameView : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* engine READ engine WRITE setEngine NOTIFY engineChanged FINAL)
public:
    explicit FrameView(QQuickItem* parent = nullptr);

    QObject* engine() const;
    void setEngine(QObject* engine);
    bool hasImage() const { return !m_image.isNull(); }

signals:
    void engineChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;

private:
    void setImage(const QImage& image);
    PlaybackEngine* m_engine = nullptr;
    QImage m_image;
    bool m_dirty = false;
};

// Draws the audio peak envelope of a clip (the part of the media between sourceStartUs and +spanUs).
class WaveformItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject* library READ library WRITE setLibrary NOTIFY changed FINAL)
    Q_PROPERTY(QString mediaId READ mediaId WRITE setMediaId NOTIFY changed FINAL)
    Q_PROPERTY(double sourceStartUs READ sourceStartUs WRITE setSourceStartUs NOTIFY changed FINAL)
    Q_PROPERTY(double spanUs READ spanUs WRITE setSpanUs NOTIFY changed FINAL)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed FINAL)
public:
    explicit WaveformItem(QQuickItem* parent = nullptr);

    QObject* library() const;
    void setLibrary(QObject* library);
    QString mediaId() const { return m_mediaId; }
    void setMediaId(const QString& id);
    double sourceStartUs() const { return m_start; }
    void setSourceStartUs(double v);
    double spanUs() const { return m_span; }
    void setSpanUs(double v);
    QColor color() const { return m_color; }
    void setColor(const QColor& c);

    void paint(QPainter* painter) override;

signals:
    void changed();

private:
    MediaLibrary* m_library = nullptr;
    QString m_mediaId;
    double m_start = 0;
    double m_span = 0;
    QColor m_color = QColor(80, 200, 150);
};

// The pictures of a video clip on the timeline: one frame per tile, taken at the time that the tile stands for (so zooming in shows the real frames).
// The item only covers the part of the clip that is on screen (tileOrigin = where it starts inside the clip, in pixels); the frames come from
// FrameThumbs and are decoded in the background, the still thumbnail of the media is shown for tiles that are not there yet.
class FrameStripItem : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject* library READ library WRITE setLibrary NOTIFY changed FINAL)
    Q_PROPERTY(QString mediaId READ mediaId WRITE setMediaId NOTIFY changed FINAL)
    Q_PROPERTY(double clipSourceUs READ clipSourceUs WRITE setClipSourceUs NOTIFY changed FINAL)   // media time at the left edge of the clip
    Q_PROPERTY(double usPerPixel READ usPerPixel WRITE setUsPerPixel NOTIFY changed FINAL)
    Q_PROPERTY(double tileOrigin READ tileOrigin WRITE setTileOrigin NOTIFY changed FINAL)
    Q_PROPERTY(double frameUs READ frameUs WRITE setFrameUs NOTIFY changed FINAL)                  // length of a frame: times are rounded down to whole frames
public:
    explicit FrameStripItem(QQuickItem* parent = nullptr);

    QObject* library() const;
    void setLibrary(QObject* library);
    QString mediaId() const { return m_mediaId; }
    void setMediaId(const QString& id);
    double clipSourceUs() const { return m_clipSourceUs; }
    void setClipSourceUs(double v);
    double usPerPixel() const { return m_usPerPixel; }
    void setUsPerPixel(double v);
    double tileOrigin() const { return m_tileOrigin; }
    void setTileOrigin(double v);
    double frameUs() const { return m_frameUs; }
    void setFrameUs(double v);

    void paint(QPainter* painter) override;

signals:
    void changed();

private:
    void refreshMedia();
    MediaLibrary* m_library = nullptr;
    QString m_mediaId;
    QString m_path;
    double m_aspect = 16.0 / 9.0;
    QImage m_still;
    double m_clipSourceUs = 0;
    double m_usPerPixel = 1000;
    double m_tileOrigin = 0;
    double m_frameUs = 0;
};

} // namespace dv
