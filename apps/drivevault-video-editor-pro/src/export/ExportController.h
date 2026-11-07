#pragma once

#include "export/Exporter.h"

#include <QElapsedTimer>
#include <QObject>
#include <QSize>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>

class QThread;

namespace dv {

class ProjectManager;

// QML facing export job: validates the request, runs Exporter on a worker thread, reports
// progress / ETA, supports cancel and exposes the validation of the finished file.
class ExportController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged FINAL)         // idle | running | done | failed | cancelled
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged FINAL)  // 0..1
    Q_PROPERTY(qint64 framesDone READ framesDone NOTIFY progressChanged FINAL)
    Q_PROPERTY(qint64 totalFrames READ totalFrames NOTIFY progressChanged FINAL)
    Q_PROPERTY(double throughputFps READ throughputFps NOTIFY progressChanged FINAL)
    Q_PROPERTY(QString etaText READ etaText NOTIFY progressChanged FINAL)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString encoderUsed READ encoderUsed NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantMap result READ result NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantList encoders READ encoders NOTIFY encodersChanged FINAL)
    Q_PROPERTY(bool detectingEncoders READ detectingEncoders NOTIFY encodersChanged FINAL)
    Q_PROPERTY(bool hasAudio READ hasAudio NOTIFY hasAudioChanged FINAL)   // the sequence has audible audio
    // "all" exports the whole sequence, "inout" only the part between the In and Out marks.
    Q_PROPERTY(QString range READ range WRITE setRange NOTIFY rangeChanged FINAL)

public:
    explicit ExportController(ProjectManager* project, QObject* parent = nullptr);
    ~ExportController() override;

    QString state() const { return m_state; }
    double progress() const { return m_total > 0 ? double(m_done) / double(m_total) : 0.0; }
    qint64 framesDone() const { return m_done; }
    qint64 totalFrames() const { return m_total; }
    double throughputFps() const { return m_fps; }
    QString etaText() const { return m_eta; }
    QString message() const { return m_message; }
    QString encoderUsed() const { return m_encoder; }
    QString outputPath() const { return m_output; }
    QVariantMap result() const { return m_result; }
    QVariantList encoders() const { return m_encoders; }
    bool detectingEncoders() const { return m_detecting; }
    bool hasAudio() const { return m_hasAudio; }
    QString range() const { return m_range; }
    void setRange(const QString& range);

    // preset: 720p | 1080p | 1440p | 4k | custom. The project's aspect ratio is kept (portrait projects use the
    // rotated box). Returns the even output size.
    Q_INVOKABLE QSize outputSize(const QString& preset, int customWidth, int customHeight) const;
    Q_INVOKABLE QString outputFilePath(const QString& name, const QString& folderOrUrl, const QString& container) const;
    Q_INVOKABLE bool fileExists(const QString& path) const;
    Q_INVOKABLE QString estimatedSize(const QString& preset, int customWidth, int customHeight, int quality, int audioKbps,
                                      const QString& container) const;
    Q_INVOKABLE void detectEncoders();
    // Frames that the current range exports (start inclusive, end exclusive); {0, total} for the whole sequence.
    Q_INVOKABLE QVariantList rangeFrames() const;
    // The built-in export presets (id, label, description, format, preset, quality, audioKbps).
    Q_INVOKABLE QVariantList builtinPresets() const;
    // Renders the frame under `frame` to a PNG at the sequence size. Returns "" or the error.
    Q_INVOKABLE QString exportFrame(const QString& name, const QString& folderOrUrl, qint64 frame);

    // Returns "" when the job started, otherwise the reason it did not.
    Q_INVOKABLE QString start(const QString& name, const QString& folderOrUrl, const QString& container, const QString& preset,
                              int customWidth, int customHeight, int quality, int audioKbps, const QString& encoderId);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void reset();
    // Blocks until a running job has finished (tests / shutdown).
    void waitForFinished(int timeoutMs = 120000);

signals:
    void stateChanged();
    void progressChanged();
    void encodersChanged();
    void hasAudioChanged();
    void rangeChanged();
    void finished(bool ok);

private:
    void setState(const QString& state, const QString& message = {});
    void onFinished(const ExportResult& result);

    ProjectManager* m_project;
    QString m_state = QStringLiteral("idle");
    QString m_message;
    QString m_encoder;
    QString m_output;
    QString m_eta;
    QVariantMap m_result;
    QVariantList m_encoders;
    bool m_detecting = false;
    bool m_hasAudio = false;
    QString m_range = QStringLiteral("all");
    qint64 m_done = 0;
    qint64 m_total = 0;
    double m_fps = 0;
    QElapsedTimer m_clock;
    qint64 m_lastEmitMs = 0;
    std::atomic_bool m_cancel{false};
    QThread* m_thread = nullptr;
};

} // namespace dv
