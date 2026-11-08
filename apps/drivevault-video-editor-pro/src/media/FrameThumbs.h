#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QWaitCondition>

#include <atomic>
#include <thread>

namespace dv {

// Frames of a video at given times for the pictures on the timeline clips (small, scaled to a height). get() returns at once: the picture when it
// is cached, otherwise a null image and the frame is decoded by one worker thread (the ones that are still being asked for first, in time order,
// so that a run of neighbouring frames is decoded in one pass). ready() is emitted (on the GUI thread, a few times per second at most) when new
// frames arrived. GUI thread only.
class FrameThumbs : public QObject {
    Q_OBJECT
public:
    static FrameThumbs& instance();

    QImage get(const QString& path, qint64 timeUs, int height);
    void clear();
    qint64 cachedBytes() const;
    int cachedCount() const;

signals:
    void ready();

private:
    FrameThumbs();
    ~FrameThumbs() override;

    struct Job {
        QString key;
        QString path;
        qint64 timeUs = 0;
        int height = 0;
        qint64 wantedMs = 0;
    };
    void run();
    static qint64 nowMs();

    mutable QMutex m_mutex;
    QWaitCondition m_wake;
    QHash<QString, QImage> m_cache;           // key -> picture
    QList<QString> m_order;                   // insertion order, oldest first (eviction)
    qint64 m_bytes = 0;
    QHash<QString, Job> m_jobs;               // wanted and not decoded yet
    QSet<QString> m_failed;                   // paths that cannot be decoded
    bool m_stop = false;
    bool m_notifyPending = false;
    QTimer m_notify;
    std::thread m_thread;
};

} // namespace dv
