#include "PlaybackEngine.h"

#include "core/Logger.h"
#include "media/HardwareDecode.h"
#include "media/MediaProbe.h"
#include "project/ProjectManager.h"
#include "settings/SettingsManager.h"

#include <QElapsedTimer>
#include <QMutexLocker>

#include <algorithm>

namespace dv {

// ---- RenderWorker ---------------------------------------------------------------------------------

RenderWorker::~RenderWorker()
{
    shutdown();
}

void RenderWorker::shutdown()
{
    {
        QMutexLocker lock(&m_mutex);
        m_quit = true;
        m_cond.wakeAll();
    }
    if (isRunning())
        wait(5000);
}

void RenderWorker::request(SnapshotPtr snapshot, qint64 frame, QSize size, quint64 serial)
{
    QMutexLocker lock(&m_mutex);
    if (m_pending.valid)
        ++m_dropped;               // the previous request never got rendered
    m_pending = {std::move(snapshot), frame, size, serial, true};
    m_cond.wakeOne();
}

void RenderWorker::run()
{
    Compositor compositor;
    SnapshotPtr lastSnapshot;
    quint64 hits = 0, misses = 0;
    for (;;) {
        Request req;
        {
            QMutexLocker lock(&m_mutex);
            while (!m_pending.valid && !m_quit)
                m_cond.wait(&m_mutex);
            if (m_quit)
                return;
            req = std::move(m_pending);
            m_pending = {};
        }
        if (req.snapshot != lastSnapshot) {
            if (req.snapshot)
                compositor.prune(*req.snapshot);
            else
                compositor.reset();
            lastSnapshot = req.snapshot;
        }
        compositor.setCacheBudget(m_cacheBudget.load());
        compositor.setUseProxy(m_useProxy.load());
        QElapsedTimer timer;
        timer.start();
        QImage image;
        if (req.snapshot)
            image = compositor.render(*req.snapshot, req.frame, req.size);
        emit rendered(image, req.frame, req.serial, double(timer.nsecsElapsed()) / 1e6);
        m_hits += compositor.cacheHits() - hits;
        m_misses += compositor.cacheMisses() - misses;
        hits = compositor.cacheHits();
        misses = compositor.cacheMisses();

        // Read ahead while playing: the frames after this one are usually the next requests. Stops as soon as a newer request waits.
        if (req.snapshot && m_prefetch.load() > 0 && m_cacheBudget.load() > 0) {
            for (int i = 1; i <= m_prefetch.load(); ++i) {
                {
                    QMutexLocker lock(&m_mutex);
                    if (m_pending.valid || m_quit)
                        break;
                }
                const int64_t f = req.frame + i;
                if (f >= req.snapshot->durationFrames || compositor.isCached(*req.snapshot, f, req.size))
                    continue;
                compositor.render(*req.snapshot, f, req.size);
                ++m_prefetched;
            }
            // the read-ahead renders are not counted as the viewer's misses
            hits = compositor.cacheHits();
            misses = compositor.cacheMisses();
        }
    }
}

// ---- PlaybackEngine -----------------------------------------------------------------------------------

PlaybackEngine::PlaybackEngine(ProjectManager* project, SettingsManager* settings, QObject* parent)
    : QObject(parent)
    , m_project(project)
    , m_settings(settings)
{
    qRegisterMetaType<QImage>("QImage");
    connect(&m_worker, &RenderWorker::rendered, this, &PlaybackEngine::onRendered, Qt::QueuedConnection);
    m_worker.start();

    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setInterval(8);
    connect(&m_timer, &QTimer::timeout, this, &PlaybackEngine::onTick);

    connect(m_project, &ProjectManager::snapshotChanged, this, &PlaybackEngine::onSnapshotChanged);
    connect(m_project, &ProjectManager::playheadChanged, this, &PlaybackEngine::onPlayheadChanged);
    connect(m_settings, &SettingsManager::valueChanged, this, [this](const QString& key) {
        if (key == QLatin1String("previewQuality"))
            refresh();
        else if (key == QLatin1String("previewVolume"))
            applyVolume();
        else if (key == QLatin1String("hardwareAcceleration") || key == QLatin1String("useProxies") || key == QLatin1String("memoryLimitMb"))
            applyPerformanceSettings();
    });
    applyVolume();
    applyPerformanceSettings();
    m_snapshot = m_project->snapshot();
}

PlaybackEngine::~PlaybackEngine()
{
    m_timer.stop();
    m_audio.stop();
    m_worker.shutdown();
}

QString PlaybackEngine::engineDescription() const
{
    return MediaProbe::engineDescription();
}

void PlaybackEngine::applyPerformanceSettings()
{
    hw::setEnabled(m_settings->get(QStringLiteral("hardwareAcceleration")).toBool());
    // the frame cache gets a quarter of the memory limit, at most 1 GB
    const qint64 limit = qint64(m_settings->get(QStringLiteral("memoryLimitMb")).toInt()) * 1024 * 1024;
    m_worker.setCacheBudget(std::min<qint64>(limit / 4, 1024ll * 1024 * 1024));
    m_worker.setUseProxy(m_settings->get(QStringLiteral("useProxies")).toBool());
    m_worker.setPrefetch(6);
    refresh();
}

QVariantList PlaybackEngine::hardwareDecoders() const
{
    QVariantList list;
    for (const hw::DecoderInfo& d : hw::detectDecoders())
        list.append(QVariantMap{{"id", d.id}, {"label", d.label}, {"available", d.available}});
    return list;
}

double PlaybackEngine::cacheHitRate() const
{
    const quint64 h = m_worker.cacheHits(), m = m_worker.cacheMisses();
    return h + m > 0 ? double(h) / double(h + m) : 0.0;
}

bool PlaybackEngine::hardwareDecoding() const
{
    return m_hwNow > m_hwMark;      // GPU-decoded frames appeared since the last statistics window
}

void PlaybackEngine::applyVolume()
{
    m_audio.setVolume(float(m_settings->get(QStringLiteral("previewVolume")).toInt()) / 100.f);
}

QSize PlaybackEngine::previewSize() const
{
    const QString quality = m_settings->get(QStringLiteral("previewQuality")).toString();
    const double factor = quality == QLatin1String("half") ? 0.5 : quality == QLatin1String("quarter") ? 0.25 : 1.0;
    const int w = std::max(64, int(m_project->width() * factor) & ~1);
    const int h = std::max(36, int(m_project->height() * factor) & ~1);
    return QSize(w, h);
}

void PlaybackEngine::requestFrame(qint64 frame)
{
    if (!m_project->isOpen()) {
        emit frameReady(QImage(), frame);
        return;
    }
    m_lastRequested = frame;
    m_worker.request(m_snapshot, frame, previewSize(), m_serial);
}

void PlaybackEngine::refresh()
{
    requestFrame(m_project->playheadFrame());
}

void PlaybackEngine::onSnapshotChanged()
{
    m_snapshot = m_project->snapshot();
    {
        QMutexLocker lock(&m_mixerMutex);
        m_mixer.setSnapshot(m_snapshot);
    }
    if (!m_project->isOpen() && m_playing)
        pause();
    if (!m_playing)
        refresh();
}

void PlaybackEngine::onPlayheadChanged()
{
    if (m_internalPlayhead)
        return;
    if (m_playing) {
        // The user moved the playhead during playback: continue from the new position.
        pause();
        play();
        return;
    }
    refresh();
}

void PlaybackEngine::play()
{
    if (m_playing || !m_project->isOpen())
        return;
    const qint64 duration = m_snapshot ? m_snapshot->durationFrames : 0;
    if (duration <= 0)
        return;
    if (m_project->playheadFrame() >= duration) {
        m_internalPlayhead = true;
        m_project->setPlayheadFrame(0);
        m_internalPlayhead = false;
    }

    m_startFrame = m_project->playheadFrame();
    const auto rate = m_project->frameRate();
    const int sampleRate = m_project->sampleRate();
    m_startSample = timecode::frameToSample(m_startFrame, rate, sampleRate);
    {
        QMutexLocker lock(&m_mixerMutex);
        m_mixer.setSnapshot(m_snapshot);
    }

    QString error;
    const bool ok = m_audio.start(m_settings->get(QStringLiteral("audioOutputDevice")).toString(), sampleRate,
                                  m_settings->get(QStringLiteral("audioLatencyMs")).toInt(), m_startSample,
                                  [this](int64_t start, int frames, float* out) {
                                      QMutexLocker lock(&m_mixerMutex);
                                      AudioMixer::Peaks pk;
                                      m_mixer.render(start, frames, out, &pk);
                                      if (pk.left > m_peakL.load())
                                          m_peakL.store(pk.left);
                                      if (pk.right > m_peakR.load())
                                          m_peakR.store(pk.right);
                                  },
                                  &error);
    m_audioClock = ok;
    m_audioBackend = ok ? QStringLiteral("WinMM") : QStringLiteral("timer");
    if (!ok) {
        if (qEnvironmentVariableIntValue("DV_NO_AUDIO") != 0)
            qCInfo(lcProject) << "Audio output is switched off (DV_NO_AUDIO), using the system timer as clock";
        else
            qCWarning(lcProject) << "No audio output, using the system timer as clock:" << error;
    }
    m_clock.start();
    m_statTimer.start();
    m_statFrames = 0;
    m_playing = true;
    m_timer.start();
    emit playingChanged();
}

void PlaybackEngine::pause()
{
    if (!m_playing)
        return;
    m_timer.stop();
    m_audio.stop();
    m_playing = false;
    m_audioBackend = QStringLiteral("none");
    m_peakL = m_peakR = 0.f;
    m_levelL = m_levelR = 0;
    emit levelsChanged();
    emit playingChanged();
    refresh();
}

void PlaybackEngine::togglePlay()
{
    if (m_playing)
        pause();
    else
        play();
}

void PlaybackEngine::goToStart()
{
    m_project->setPlayheadFrame(0);
}

void PlaybackEngine::goToEnd()
{
    const qint64 duration = m_snapshot ? m_snapshot->durationFrames : 0;
    m_project->setPlayheadFrame(duration);
}

void PlaybackEngine::onTick()
{
    if (!m_playing)
        return;
    if (++m_tickCount % 4 == 0) {          // ~30 Hz meter update with a quick fall-back
        m_levelL = std::max(double(m_peakL.exchange(0.f)), m_levelL * 0.82);
        m_levelR = std::max(double(m_peakR.exchange(0.f)), m_levelR * 0.82);
        emit levelsChanged();
    }
    const auto rate = m_project->frameRate();
    qint64 frame;
    if (m_audioClock) {
        const qint64 sample = m_audio.currentSample();
        frame = timecode::sampleToFrame(sample, rate, m_project->sampleRate());
    } else {
        const qint64 us = m_clock.nsecsElapsed() / 1000;
        frame = m_startFrame + (us * rate.num) / (rate.den * 1000000);
    }
    frame = std::max(frame, m_startFrame);

    const qint64 duration = m_snapshot ? m_snapshot->durationFrames : 0;
    if (frame >= duration) {
        m_internalPlayhead = true;
        m_project->setPlayheadFrame(duration);
        m_internalPlayhead = false;
        pause();
        emit reachedEnd();
        return;
    }
    if (frame != m_project->playheadFrame()) {
        m_internalPlayhead = true;
        m_project->setPlayheadFrame(frame);
        m_internalPlayhead = false;
        requestFrame(frame);
    }
}

void PlaybackEngine::onRendered(const QImage& image, qint64 frame, quint64 serial, double milliseconds)
{
    Q_UNUSED(serial)
    // Late results of an older request are still shown unless a newer frame was already requested.
    if (m_playing && frame < m_lastRequested - 1 && m_lastRequested >= 0 && frame != m_project->playheadFrame())
        return;
    m_lastImage = image;
    m_renderMs = m_renderMs * 0.8 + milliseconds * 0.2;
    ++m_statFrames;
    if (m_statTimer.isValid() && m_statTimer.elapsed() >= 500) {
        m_fps = double(m_statFrames) * 1000.0 / double(m_statTimer.elapsed());
        m_hwMark = m_hwNow;
        m_hwNow = hw::hardwareFrames();
        bool proxies = false;
        if (m_snapshot && m_settings->get(QStringLiteral("useProxies")).toBool())
            for (const SnapshotClip& c : m_snapshot->clips)
                proxies = proxies || !c.proxyPath.isEmpty();
        m_usingProxies = proxies;
        m_statFrames = 0;
        m_statTimer.restart();
        emit statsChanged();
    }
    emit frameReady(image, frame);
}

} // namespace dv
