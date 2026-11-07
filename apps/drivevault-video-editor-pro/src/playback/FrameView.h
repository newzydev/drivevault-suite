#pragma once

#include <QImage>
#include <QQuickItem>
#include <QQuickPaintedItem>

namespace dv {

class PlaybackEngine;
class MediaLibrary;

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

} // namespace dv
