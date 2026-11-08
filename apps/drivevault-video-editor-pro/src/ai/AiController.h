#pragma once

#include "ai/AiTypes.h"
#include "ai/ModelManager.h"
#include "ai/Tracker.h"

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>

class QThread;

namespace dv {

class ProjectManager;
class SettingsManager;

// QML facing AI services ("Ai"): captions (whisper.cpp), silence detection, smart cut suggestions, audio enhancement,
// object tracking, model manager and the command assistant. Heavy work runs on a worker thread with progress and cancel;
// nothing edits the project until the user confirms (apply*), and every edit is one undo step.
class AiController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(QString task READ task NOTIFY busyChanged FINAL)               // "" | silence | cuts | enhance | transcribe | track
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged FINAL)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged FINAL)
    Q_PROPERTY(QString whisperStatus READ whisperStatus NOTIFY statusChanged FINAL)    // available | modelRequired | engineMissing
    Q_PROPERTY(QVariantList silences READ silences NOTIFY resultsChanged FINAL)       // [{start, end, startTimecode, seconds}]
    Q_PROPERTY(QVariantList suggestions READ suggestions NOTIFY resultsChanged FINAL) // [{frame, kind, label, timecode}]
    Q_PROPERTY(QVariantList captions READ captions NOTIFY resultsChanged FINAL)       // [{start, end, text, timecode}]
    Q_PROPERTY(QVariantMap tracking READ tracking NOTIFY resultsChanged FINAL)        // {frames, confidence, clipId} or {}
    Q_PROPERTY(QObject* models READ models CONSTANT FINAL)
    Q_PROPERTY(bool enabled READ enabled NOTIFY statusChanged FINAL)
    // Region of the picture to track, shown as a rectangle over the monitor: {x, y, w, h} in 0..1, empty = hidden.
    Q_PROPERTY(QVariantMap trackRegion READ trackRegion WRITE setTrackRegion NOTIFY trackRegionChanged FINAL)

public:
    AiController(ProjectManager* project, SettingsManager* settings, QObject* parent = nullptr);
    ~AiController() override;

    bool busy() const { return m_busy; }
    QString task() const { return m_task; }
    double progress() const { return m_progress; }
    QString message() const { return m_message; }
    QString whisperStatus() const;
    bool enabled() const;
    QVariantList silences() const { return m_silences; }
    QVariantList suggestions() const { return m_suggestions; }
    QVariantList captions() const { return m_captions; }
    QVariantMap tracking() const { return m_tracking; }
    ModelManager* models() { return &m_models; }
    QVariantMap trackRegion() const { return m_trackRegion; }
    void setTrackRegion(const QVariantMap& r) { if (r != m_trackRegion) { m_trackRegion = r; emit trackRegionChanged(); } }

    // ---- analysis (each returns "" when started, otherwise why not) ------------------------------------------
    Q_INVOKABLE QString detectSilence(double thresholdDb, int minSilenceMs);
    Q_INVOKABLE QString suggestCuts(double sensitivity, double silenceThresholdDb, int minSilenceMs);
    Q_INVOKABLE QString transcribe(const QString& language);           // "th" | "en" | "auto" | "" (setting)
    Q_INVOKABLE QString enhanceSelected(double strength, bool highPass, bool normalize);
    Q_INVOKABLE QString startTracking(double x, double y, double w, double h);   // region of the selected clip at the playhead, 0..1
    Q_INVOKABLE void cancel();

    // ---- applying results (the user confirmed) ----------------------------------------------------------------
    Q_INVOKABLE QString applySilenceCuts();
    Q_INVOKABLE void clearSilences();
    Q_INVOKABLE QString applySuggestion(int index);                    // splits the sequence at that frame
    Q_INVOKABLE QString applyAllSuggestions();
    Q_INVOKABLE void clearSuggestions();
    Q_INVOKABLE QString applyCaptions();                               // adds an editable subtitle track entry per phrase
    Q_INVOKABLE void clearCaptions();
    Q_INVOKABLE QString applyTracking(const QString& mode, const QString& targetClipId);   // follow | stabilize
    Q_INVOKABLE void clearTracking();

    // ---- assistant ------------------------------------------------------------------------------------------------
    Q_INVOKABLE QVariantMap parseAssistant(const QString& text) const;   // {valid, type, summary}
    // "Edit by prompt": a Thai / English sentence is turned into editing steps (place the clips, split, text, effects, transitions, filters, export).
    // A deterministic keyword interpreter - not a language model. planEdit only describes; applyEditPrompt carries the steps out as ONE undo step
    // (the export step is left to the caller: {exportRequested, exportPreset}).
    Q_INVOKABLE QVariantMap planEdit(const QString& text) const;   // {valid, steps: [text], unknown: [text], exportRequested, exportPreset}
    Q_INVOKABLE QString applyEditPrompt(const QString& text);       // "" when done, otherwise the first problem
    Q_INVOKABLE QString runAssistant(const QString& text);                // "" when started / done, otherwise a message

    // Tests and shutdown.
    void waitForIdle(int timeoutMs = 120000);
    void setWhisperExecutableForTests(const QString& path) { m_whisperOverride = path; }

signals:
    void busyChanged();
    void progressChanged();
    void messageChanged();
    void statusChanged();
    void resultsChanged();
    void trackRegionChanged();
    void finished(const QString& task, bool ok);

private:
    using Work = std::function<void(const ai::ProgressFn&, const std::atomic_bool&)>;
    QString begin(const QString& task, const QString& message, Work work, std::function<void(bool ok)> done);
    void setMessage(const QString& m);
    QString whisperPath() const;
    QVariantMap rangeMap(const ai::FrameRange& r) const;

    ProjectManager* m_project;
    SettingsManager* m_settings;
    ModelManager m_models;
    QThread* m_thread = nullptr;
    std::atomic_bool m_cancel{false};
    bool m_busy = false;
    QString m_task;
    double m_progress = 0;
    QString m_message;
    QString m_whisperOverride;
    QVariantList m_silences;
    QVariantList m_suggestions;
    QVariantList m_captions;
    QVariantMap m_tracking;
    QVariantMap m_trackRegion;
    // results kept in native form for applying
    ai::TrackResult m_trackResult;
    QString m_trackClip;
    qint64 m_trackStartFrame = 0;
    QString m_enhancedPath;
};

} // namespace dv
