#pragma once

#include "core/RenderSnapshot.h"
#include "playback/AudioMixer.h"
#include "playback/AudioOutput.h"
#include "playback/Compositor.h"

#include <QElapsedTimer>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QThread>
#include <QTimer>
#include <QWaitCondition>

#include <atomic>

namespace dv {

class ProjectManager;
class SettingsManager;

// Background renderer: composites the frame that the UI asks for. Only the newest request is
// rendered; requests that were replaced before they started count as dropped frames.
class RenderWorker : public QThread {
    Q_OBJECT
public:
    explicit RenderWorker(QObject* parent = nullptr) : QThread(parent) {}
    ~RenderWorker() override;

    void request(SnapshotPtr snapshot, qint64 frame, QSize size, quint64 serial);
    void shutdown();
    quint64 dropped() const { return m_dropped.load(); }
    // Frame cache / proxies / read-ahead (all preview-only); changes take effect with the next request.
    void setCacheBudget(qint64 bytes) { m_cacheBudget = bytes; }
    void setUseProxy(bool use) { m_useProxy = use; }
    void setPrefetch(int frames) { m_prefetch = frames; }
    quint64 cacheHits() const { return m_hits.load(); }
    quint64 cacheMisses() const { return m_misses.load(); }
    quint64 prefetched() const { return m_prefetched.load(); }

signals:
    void rendered(const QImage& image, qint64 frame, quint64 serial, double milliseconds);

protected:
    void run() override;

private:
    struct Request {
        SnapshotPtr snapshot;
        qint64 frame = 0;
        QSize size;
        quint64 serial = 0;
        bool valid = false;
    };
    QMutex m_mutex;
    QWaitCondition m_cond;
    Request m_pending;
    bool m_quit = false;
    std::atomic<quint64> m_dropped{0};
    std::atomic<qint64> m_cacheBudget{0};
    std::atomic_bool m_useProxy{false};
    std::atomic<int> m_prefetch{0};
    std::atomic<quint64> m_hits{0};
    std::atomic<quint64> m_misses{0};
    std::atomic<quint64> m_prefetched{0};
};

// Transport + preview: play / pause / seek, audio-clocked A/V sync, frame delivery to the monitors.
class PlaybackEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged FINAL)
    Q_PROPERTY(double playbackFps READ playbackFps NOTIFY statsChanged FINAL)
    Q_PROPERTY(int droppedFrames READ droppedFrames NOTIFY statsChanged FINAL)
    Q_PROPERTY(double renderMs READ renderMs NOTIFY statsChanged FINAL)
    Q_PROPERTY(double levelL READ levelL NOTIFY levelsChanged FINAL)   // master meter, 0..1
    Q_PROPERTY(double levelR READ levelR NOTIFY levelsChanged FINAL)
    Q_PROPERTY(QString audioBackend READ audioBackend NOTIFY playingChanged FINAL)
    Q_PROPERTY(double cacheHitRate READ cacheHitRate NOTIFY statsChanged FINAL)         // 0..1 of preview frames served from the cache
    Q_PROPERTY(bool hardwareDecoding READ hardwareDecoding NOTIFY statsChanged FINAL)   // frames really came from the GPU decoder
    Q_PROPERTY(bool usingProxies READ usingProxies NOTIFY statsChanged FINAL)           // the preview reads proxy files
    Q_PROPERTY(QString engineDescription READ engineDescription CONSTANT FINAL)
    Q_PROPERTY(QStringList audioDevices READ audioDevices CONSTANT FINAL)

public:
    PlaybackEngine(ProjectManager* project, SettingsManager* settings, QObject* parent = nullptr);
    ~PlaybackEngine() override;

    bool playing() const { return m_playing; }
    double playbackFps() const { return m_fps; }
    int droppedFrames() const { return int(m_worker.dropped()); }
    double renderMs() const { return m_renderMs; }
    QString audioBackend() const { return m_audioBackend; }
    double cacheHitRate() const;
    bool hardwareDecoding() const;
    bool usingProxies() const { return m_usingProxies; }
    double levelL() const { return m_levelL; }
    double levelR() const { return m_levelR; }
    QString engineDescription() const;
    QStringList audioDevices() const { return AudioOutput::deviceNames(); }
    QImage lastImage() const { return m_lastImage; }

    // [{id, label, available}] hardware decoders that really opened on this machine (probed once).
    Q_INVOKABLE QVariantList hardwareDecoders() const;
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void goToStart();
    Q_INVOKABLE void goToEnd();
    // Renders the frame under the playhead again (after edits, quality changes ...).
    Q_INVOKABLE void refresh();

signals:
    void playingChanged();
    void statsChanged();
    void levelsChanged();
    void frameReady(const QImage& image, qint64 frame);
    void reachedEnd();

private slots:
    void onTick();
    void onRendered(const QImage& image, qint64 frame, quint64 serial, double milliseconds);

private:
    QSize previewSize() const;
    void requestFrame(qint64 frame);
    void onSnapshotChanged();
    void onPlayheadChanged();
    void applyVolume();

    ProjectManager* m_project;
    SettingsManager* m_settings;
    RenderWorker m_worker;
    AudioOutput m_audio;
    AudioMixer m_mixer;
    QMutex m_mixerMutex;
    QTimer m_timer;
    SnapshotPtr m_snapshot;
    QImage m_lastImage;
    bool m_usingProxies = false;
    quint64 m_hwMark = 0;
    quint64 m_hwNow = 0;
    void applyPerformanceSettings();

    bool m_playing = false;
    bool m_internalPlayhead = false;
    bool m_audioClock = false;
    QString m_audioBackend = QStringLiteral("none");
    qint64 m_startFrame = 0;
    qint64 m_startSample = 0;
    QElapsedTimer m_clock;
    quint64 m_serial = 0;
    qint64 m_lastRequested = -1;

    // statistics
    QElapsedTimer m_statTimer;
    int m_statFrames = 0;
    double m_fps = 0;
    double m_renderMs = 0;

    // master meter
    std::atomic<float> m_peakL{0.f};
    std::atomic<float> m_peakR{0.f};
    double m_levelL = 0;
    double m_levelR = 0;
    int m_tickCount = 0;
};

} // namespace dv
