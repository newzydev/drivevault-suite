#include "FrameThumbs.h"

#include "VideoReader.h"
#include "core/Logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QMetaObject>

#include <algorithm>
#include <list>
#include <memory>

namespace dv {

namespace {
constexpr qint64 kBudgetBytes = 96LL * 1024 * 1024;     // pictures kept in memory
constexpr qint64 kWantedMs = 1500;                       // a picture nobody asked for during this time is not decoded any more
constexpr qint64 kCurrentMs = 300;                       // asked for during this time: part of what is on screen now
constexpr size_t kReaders = 4;                           // open decoders kept for the next frames of the same files

QString keyFor(const QString& path, qint64 bucket, int height)
{
    return path + QLatin1Char('|') + QString::number(bucket) + QLatin1Char('|') + QString::number(height);
}
} // namespace

namespace {
FrameThumbs* g_instance = nullptr;
}

FrameThumbs& FrameThumbs::instance()
{
    if (!g_instance) {
        g_instance = new FrameThumbs;
        qAddPostRoutine([] { delete g_instance; g_instance = nullptr; });      // stops the worker before the libraries are unloaded
    }
    return *g_instance;
}

qint64 FrameThumbs::nowMs() { return QDateTime::currentMSecsSinceEpoch(); }

FrameThumbs::FrameThumbs()
{
    m_notify.setSingleShot(true);
    m_notify.setInterval(40);
    connect(&m_notify, &QTimer::timeout, this, [this] {
        {
            QMutexLocker lock(&m_mutex);
            m_notifyPending = false;
        }
        emit ready();
    });
    m_thread = std::thread([this] { run(); });
}

FrameThumbs::~FrameThumbs()
{
    {
        QMutexLocker lock(&m_mutex);
        m_stop = true;
    }
    m_wake.wakeAll();
    if (m_thread.joinable())
        m_thread.join();
}

QImage FrameThumbs::get(const QString& path, qint64 timeUs, int height)
{
    if (path.isEmpty() || height < 4)
        return {};
    const qint64 bucket = std::max<qint64>(0, timeUs) / 1000;      // a millisecond is finer than a frame
    const QString key = keyFor(path, bucket, height);
    QMutexLocker lock(&m_mutex);
    if (const auto it = m_cache.constFind(key); it != m_cache.constEnd())
        return it.value();
    if (m_failed.contains(path) || m_failed.contains(key))
        return {};
    auto job = m_jobs.find(key);
    if (job == m_jobs.end()) {
        job = m_jobs.insert(key, Job{key, path, bucket * 1000, height, nowMs()});
        m_wake.wakeOne();
    } else {
        job->wantedMs = nowMs();
    }
    return {};
}

void FrameThumbs::clear()
{
    QMutexLocker lock(&m_mutex);
    m_cache.clear();
    m_order.clear();
    m_bytes = 0;
    m_jobs.clear();
    m_failed.clear();
}

qint64 FrameThumbs::cachedBytes() const
{
    QMutexLocker lock(&m_mutex);
    return m_bytes;
}

int FrameThumbs::cachedCount() const
{
    QMutexLocker lock(&m_mutex);
    return int(m_cache.size());
}

void FrameThumbs::run()
{
    std::list<std::pair<QString, std::unique_ptr<VideoReader>>> readers;     // most recently used first
    while (true) {
        Job job;
        {
            QMutexLocker lock(&m_mutex);
            while (true) {
                if (m_stop)
                    return;
                const qint64 now = nowMs();
                // forget what is not wanted any more
                for (auto it = m_jobs.begin(); it != m_jobs.end();)
                    it = now - it->wantedMs > kWantedMs ? m_jobs.erase(it) : std::next(it);
                if (!m_jobs.isEmpty())
                    break;
                m_wake.wait(&m_mutex, 500);
            }
            // what is on screen now, earliest time first (neighbouring frames are decoded in one pass); anything else after that
            const qint64 now = nowMs();
            const Job* best = nullptr;
            bool bestCurrent = false;
            for (auto it = m_jobs.constBegin(); it != m_jobs.constEnd(); ++it) {
                const bool current = now - it->wantedMs <= kCurrentMs;
                if (!best || (current && !bestCurrent) || (current == bestCurrent && (it->path < best->path || (it->path == best->path && it->timeUs < best->timeUs)))) {
                    best = &it.value();
                    bestCurrent = current;
                }
            }
            job = *best;
            m_jobs.remove(job.key);
        }

        // decode without holding the lock
        VideoReader* reader = nullptr;
        for (auto it = readers.begin(); it != readers.end(); ++it)
            if (it->first == job.path) {
                readers.splice(readers.begin(), readers, it);
                reader = readers.front().second.get();
                break;
            }
        bool failed = false;
        if (!reader) {
            auto fresh = std::make_unique<VideoReader>();
            if (fresh->open(job.path)) {
                readers.emplace_front(job.path, std::move(fresh));
                reader = readers.front().second.get();
                if (readers.size() > kReaders)
                    readers.pop_back();
            } else {
                failed = true;
            }
        }
        QImage image;
        if (reader) {
            const QSize display = reader->displaySize();
            const double aspect = display.height() > 0 ? double(display.width()) / double(display.height()) : 16.0 / 9.0;
            const QSize size(std::max(2, int(std::lround(job.height * aspect))), job.height);
            image = reader->frameAt(job.timeUs, size);
            if (image.isNull()) {
                QMutexLocker lock(&m_mutex);
                m_failed.insert(job.key);                    // this frame cannot be decoded: do not ask again
            }
        }

        bool notify = false;
        {
            QMutexLocker lock(&m_mutex);
            if (failed)
                m_failed.insert(job.path);
            if (!image.isNull()) {
                m_cache.insert(job.key, image);
                m_order.append(job.key);
                m_bytes += image.sizeInBytes();
                while (m_bytes > kBudgetBytes && !m_order.isEmpty()) {
                    const QString oldest = m_order.takeFirst();
                    if (const auto it = m_cache.find(oldest); it != m_cache.end()) {
                        m_bytes -= it->sizeInBytes();
                        m_cache.erase(it);
                    }
                }
                if (!m_notifyPending) {
                    m_notifyPending = true;
                    notify = true;
                }
            }
        }
        if (notify)
            QMetaObject::invokeMethod(&m_notify, [this] { m_notify.start(); }, Qt::QueuedConnection);
    }
}

} // namespace dv
