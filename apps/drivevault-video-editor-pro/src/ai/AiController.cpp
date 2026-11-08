#include "AiController.h"

#include "ai/Assistant.h"
#include "ai/AudioAnalysis.h"
#include "ai/AudioEnhancer.h"
#include "ai/EditPlanner.h"
#include "project/TrackListModel.h"
#include "ai/Transcriber.h"
#include "core/Logger.h"
#include "media/MediaLibrary.h"
#include "project/ProjectManager.h"
#include "settings/SettingsManager.h"
#include "timeline/TimelineModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QPointer>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <memory>

namespace dv {

namespace {
bool hasAudioClips(const RenderSnapshot& s)
{
    for (const SnapshotClip& c : s.clips)
        if (c.trackIndex >= 0 && c.trackIndex < s.tracks.size() && s.tracks.at(c.trackIndex).kind == QLatin1String("audio")
            && !s.tracks.at(c.trackIndex).muted && c.online && c.media.hasAudio)
            return true;
    return false;
}
} // namespace

AiController::AiController(ProjectManager* project, SettingsManager* settings, QObject* parent)
    : QObject(parent)
    , m_project(project)
    , m_settings(settings)
    , m_models(settings)
    , m_llm(settings)
{
    m_pendingMap = planToMap(ai::EditPlan(), QStringLiteral("keyword"));          // QML reads steps / valid before the first keystroke
    connect(&m_models, &ModelManager::modelsChanged, this, &AiController::statusChanged);
    connect(&m_llm, &LocalLlm::planFinished, this, [this](bool ok, const QByteArray& content, const QString& message) {
        if (m_llmText.isEmpty())
            return;                                    // the text was changed while the model worked: its answer no longer fits
        m_llmText.clear();
        if (!ok) {
            emit llmFailed(message);
            return;
        }
        setPending(ai::planFromLlmJson(content), QStringLiteral("ai"));
    });
    connect(m_settings, &SettingsManager::valueChanged, this, [this](const QString& key) {
        if (key == QLatin1String("aiEnabled") || key == QLatin1String("aiModelsLocation") || key == QLatin1String("aiModel"))
            emit statusChanged();
    });
}

AiController::~AiController()
{
    if (m_thread) {
        m_cancel = true;
        m_thread->wait(30000);
    }
}

bool AiController::enabled() const
{
    return m_settings->get(QStringLiteral("aiEnabled")).toBool();
}

QString AiController::whisperPath() const
{
    return m_whisperOverride.isEmpty() ? ai::whisperExecutable() : m_whisperOverride;
}

QString AiController::whisperStatus() const
{
    if (whisperPath().isEmpty())
        return QStringLiteral("engineMissing");
    if (m_models.selectedPath().isEmpty())
        return QStringLiteral("modelRequired");
    return QStringLiteral("available");
}

void AiController::setMessage(const QString& m)
{
    m_message = m;
    emit messageChanged();
}

QVariantMap AiController::rangeMap(const ai::FrameRange& r) const
{
    const double fps = std::max(1.0, m_project->frameRate().toDouble());
    return {{"start", qlonglong(r.start)}, {"end", qlonglong(r.end)}, {"startTimecode", m_project->timecodeFor(r.start)},
            {"endTimecode", m_project->timecodeFor(r.end)}, {"seconds", double(r.end - r.start) / fps}};
}

QString AiController::begin(const QString& task, const QString& message, Work work, std::function<void(bool)> done)
{
    if (m_busy)
        return tr("Another AI task is running. Wait for it or cancel it.");
    m_busy = true;
    m_task = task;
    m_progress = 0;
    m_cancel = false;
    setMessage(message);
    emit busyChanged();
    emit progressChanged();

    QPointer<AiController> self(this);
    auto ok = std::make_shared<bool>(true);
    m_thread = QThread::create([self, work, ok, this] {
        QElapsedTimer last;
        last.start();
        auto progress = [self, &last](double p) {
            if (!self || last.elapsed() < 80)
                return;
            last.restart();
            QMetaObject::invokeMethod(self.data(), [self, p] {
                if (self) {
                    self->m_progress = p;
                    emit self->progressChanged();
                }
            }, Qt::QueuedConnection);
        };
        try {
            work(progress, m_cancel);
        } catch (...) {
            *ok = false;
        }
        if (self)
            QMetaObject::invokeMethod(self.data(), [self, ok] {
                if (self)
                    self->m_progress = 1.0;
            }, Qt::QueuedConnection);
    });
    connect(m_thread, &QThread::finished, this, [this, done, ok, task] {
        m_thread = nullptr;
        const bool cancelled = m_cancel.load();
        m_busy = false;
        m_task.clear();
        emit busyChanged();
        emit progressChanged();
        if (cancelled)
            setMessage(tr("Cancelled."));
        else
            done(*ok);
        emit finished(task, !cancelled && *ok);
    }, Qt::QueuedConnection);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
    return {};
}

void AiController::cancel()
{
    if (m_busy)
        m_cancel = true;
}

void AiController::waitForIdle(int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (m_busy && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents();
}

// ---- silence + cuts ------------------------------------------------------------------------------------------------

QString AiController::detectSilence(double thresholdDb, int minSilenceMs)
{
    const SnapshotPtr snap = m_project->snapshot();
    if (!m_project->isOpen() || !snap || snap->isEmpty())
        return tr("The timeline is empty.");
    if (!hasAudioClips(*snap))
        return tr("The timeline has no audio to analyse.");
    ai::SilenceParams p;
    p.thresholdDb = std::clamp(thresholdDb, -80.0, -10.0);
    p.minSilenceSec = std::clamp(minSilenceMs, 100, 10000) / 1000.0;
    auto out = std::make_shared<QList<ai::FrameRange>>();
    return begin(QStringLiteral("silence"), tr("Looking for silent passages…"),
                 [snap, p, out](const ai::ProgressFn& progress, const std::atomic_bool& cancel) {
                     *out = ai::detectSilence(snap, p, progress, cancel);
                 },
                 [this, out](bool) {
                     m_silences.clear();
                     double total = 0;
                     for (const ai::FrameRange& r : std::as_const(*out)) {
                         const QVariantMap m = rangeMap(r);
                         total += m.value("seconds").toDouble();
                         m_silences.append(m);
                     }
                     setMessage(out->isEmpty() ? tr("No silent passages found with these settings.")
                                               : tr("%n silent passage(s), %1 s in total. Review them in the timeline, then apply.", "", out->size())
                                                     .arg(total, 0, 'f', 1));
                     emit resultsChanged();
                 });
}

QString AiController::applySilenceCuts()
{
    if (m_silences.isEmpty())
        return tr("There are no silent passages to remove.");
    const QString err = m_project->timeline()->removeRanges(m_silences);
    if (err.isEmpty()) {
        setMessage(tr("Silent passages removed. Use Undo to bring them back."));
        clearSilences();
    }
    return err;
}

void AiController::clearSilences()
{
    m_silences.clear();
    emit resultsChanged();
}

QString AiController::suggestCuts(double sensitivity, double silenceThresholdDb, int minSilenceMs)
{
    const SnapshotPtr snap = m_project->snapshot();
    if (!m_project->isOpen() || !snap || snap->isEmpty())
        return tr("The timeline is empty.");
    ai::SilenceParams p;
    p.thresholdDb = std::clamp(silenceThresholdDb, -80.0, -10.0);
    p.minSilenceSec = std::clamp(minSilenceMs, 100, 10000) / 1000.0;
    struct Out { QList<int64_t> scenes; QList<ai::FrameRange> silence; };
    auto out = std::make_shared<Out>();
    const bool audio = hasAudioClips(*snap);
    return begin(QStringLiteral("cuts"), tr("Analysing scenes and pauses…"),
                 [snap, p, out, sensitivity, audio](const ai::ProgressFn& progress, const std::atomic_bool& cancel) {
                     out->scenes = ai::detectSceneChanges(snap, sensitivity,
                                                          [&](double v) { if (progress) progress(audio ? v * 0.6 : v); }, cancel);
                     if (audio && !cancel.load())
                         out->silence = ai::detectSilence(snap, p, [&](double v) { if (progress) progress(0.6 + 0.4 * v); }, cancel);
                 },
                 [this, out](bool) {
                     struct S { int64_t frame; QString kind; QString label; };
                     QList<S> all;
                     for (int64_t f : std::as_const(out->scenes))
                         all.append({f, QStringLiteral("scene"), tr("Scene change")});
                     for (const ai::FrameRange& r : std::as_const(out->silence)) {
                         all.append({r.start, QStringLiteral("silence"), tr("Silence starts")});
                         all.append({r.end, QStringLiteral("silence"), tr("Silence ends")});
                     }
                     std::sort(all.begin(), all.end(), [](const S& a, const S& b) { return a.frame < b.frame; });
                     m_suggestions.clear();
                     for (const S& s : std::as_const(all))
                         m_suggestions.append(QVariantMap{{"frame", qlonglong(s.frame)}, {"kind", s.kind}, {"label", s.label},
                                                          {"timecode", m_project->timecodeFor(s.frame)}});
                     setMessage(all.isEmpty() ? tr("No cut points found with these settings.")
                                              : tr("%n suggested cut point(s). Nothing was cut yet.", "", all.size()));
                     emit resultsChanged();
                 });
}

QString AiController::applySuggestion(int index)
{
    if (index < 0 || index >= m_suggestions.size())
        return tr("There is no such suggestion.");
    TimelineModel* tl = m_project->timeline();
    tl->clearSelection();
    return tl->splitAt(m_suggestions.at(index).toMap().value("frame").toLongLong());
}

QString AiController::applyAllSuggestions()
{
    if (m_suggestions.isEmpty())
        return tr("There are no suggestions to apply.");
    TimelineModel* tl = m_project->timeline();
    tl->undoStack()->beginMacro(tr("Apply suggested cuts"));
    tl->clearSelection();
    QString firstError;
    for (const QVariant& v : std::as_const(m_suggestions)) {
        const QString err = tl->splitAt(v.toMap().value("frame").toLongLong());
        if (!err.isEmpty() && firstError.isEmpty())
            firstError = err;
    }
    tl->undoStack()->endMacro();
    clearSuggestions();
    return firstError;
}

void AiController::clearSuggestions()
{
    m_suggestions.clear();
    emit resultsChanged();
}

// ---- captions ---------------------------------------------------------------------------------------------------------

QString AiController::transcribe(const QString& languageIn)
{
    const SnapshotPtr snap = m_project->snapshot();
    if (!m_project->isOpen() || !snap || snap->isEmpty())
        return tr("The timeline is empty.");
    if (!hasAudioClips(*snap))
        return tr("The timeline has no audio to transcribe.");
    if (whisperPath().isEmpty())
        return tr("The speech engine (whisper.cpp) was not found.");
    const QString model = m_models.selectedPath();
    if (model.isEmpty())
        return tr("A speech model is required. Download or import one in the model manager.");
    ai::TranscribeRequest req;
    req.snapshot = snap;
    req.whisperExe = whisperPath();
    req.model = model;
    req.language = languageIn.isEmpty() ? QStringLiteral("auto") : languageIn;
    req.workDir = QDir::tempPath();
    req.threads = std::clamp(QThread::idealThreadCount() / 2, 1, 8);
    auto out = std::make_shared<ai::TranscribeResult>();
    return begin(QStringLiteral("transcribe"), tr("Transcribing speech…"),
                 [req, out](const ai::ProgressFn& progress, const std::atomic_bool& cancel) {
                     *out = ai::transcribeTimeline(req, progress, cancel);
                 },
                 [this, out](bool) {
                     if (!out->ok()) {
                         setMessage(out->cancelled ? tr("Cancelled.") : out->error);
                         return;
                     }
                     const double fps = std::max(1.0, m_project->frameRate().toDouble());
                     const qint64 dur = m_project->timeline()->durationFrames();
                     m_captions.clear();
                     for (const ai::Segment& s : std::as_const(out->segments)) {
                         const qint64 a = std::clamp<qint64>(std::llround(s.startSec * fps), 0, dur);
                         const qint64 b = std::clamp<qint64>(std::llround(s.endSec * fps), a + 1, std::max<qint64>(dur, a + 1));
                         m_captions.append(QVariantMap{{"start", a}, {"end", b}, {"text", s.text}, {"timecode", m_project->timecodeFor(a)}});
                     }
                     setMessage(m_captions.isEmpty()
                                    ? tr("No speech was recognised.")
                                    : tr("%n phrase(s) recognised. Automatic captions are not always accurate - check and correct the text.", "", m_captions.size()));
                     emit resultsChanged();
                 });
}

QString AiController::applyCaptions()
{
    if (m_captions.isEmpty())
        return tr("There are no captions to add.");
    if (m_project->timeline()->subtitleCount() == 0) {
        bool hasTrack = false;
        for (const TrackInfo& t : m_project->timeline()->tracks())
            hasTrack = hasTrack || t.kind == QLatin1String("subtitle");
        if (!hasTrack) {
            const QString e = m_project->addTrack(QStringLiteral("subtitle"));
            if (!e.isEmpty())
                return e;
        }
    }
    const QString err = m_project->timeline()->addSubtitles(m_captions);
    if (err.isEmpty()) {
        setMessage(tr("Captions added to the subtitle track. Select a subtitle to edit its text."));
        clearCaptions();
    }
    return err;
}

void AiController::clearCaptions()
{
    m_captions.clear();
    emit resultsChanged();
}

// ---- audio enhancement ------------------------------------------------------------------------------------------------

QString AiController::enhanceSelected(double strength, bool highPass, bool normalize)
{
    TimelineModel* tl = m_project->timeline();
    const QString clipId = tl->selectedClipOfKind(QStringLiteral("audio"));
    if (clipId.isEmpty())
        return tr("Select an audio clip on the timeline first.");
    const QVariantMap info = tl->clipInfo(clipId);
    const QVariantMap media = m_project->media()->describe(info.value("mediaId").toString());
    if (media.isEmpty() || !media.value("hasAudio").toBool())
        return tr("The selected clip has no audio.");
    const QString in = media.value("path").toString();
    if (!QFileInfo::exists(in))
        return tr("The media file is missing.");
    const QString dir = QFileInfo(m_project->filePath()).absolutePath() + QStringLiteral("/AI");
    QString out = QDir(dir).filePath(QFileInfo(in).completeBaseName() + QStringLiteral("_enhanced.wav"));
    for (int n = 2; QFileInfo::exists(out); ++n)
        out = QDir(dir).filePath(QFileInfo(in).completeBaseName() + QStringLiteral("_enhanced%1.wav").arg(n));
    ai::EnhanceParams p;
    p.reduction = std::clamp(strength, 0.0, 1.0);
    p.highPass = highPass;
    p.normalize = normalize;
    auto error = std::make_shared<QString>();
    const int rate = m_project->sampleRate();
    return begin(QStringLiteral("enhance"), tr("Enhancing audio…"),
                 [in, out, rate, p, error](const ai::ProgressFn& progress, const std::atomic_bool& cancel) {
                     *error = ai::enhanceFile(in, out, rate, p, progress, cancel);
                 },
                 [this, out, error, clipId](bool) {
                     if (!error->isEmpty()) {
                         setMessage(*error);
                         QFile::remove(out);
                         return;
                     }
                     const QString id = m_project->media()->importFile(out);
                     if (id.isEmpty()) {
                         setMessage(tr("The enhanced file could not be imported."));
                         return;
                     }
                     // wait for the import (probe) to finish, then swap the clip's media
                     auto tries = std::make_shared<int>(0);
                     auto poll = std::make_shared<std::function<void()>>();
                     QPointer<AiController> self(this);
                     *poll = [self, id, clipId, tries, poll] {
                         if (!self)
                             return;
                         if (self->m_project->media()->isReady(id)) {
                             const QString e = self->m_project->timeline()->replaceMedia(clipId, id);
                             self->setMessage(e.isEmpty() ? AiController::tr("The clip now uses the enhanced audio. Undo restores the original.") : e);
                         } else if (++*tries < 200) {
                             QTimer::singleShot(100, self.data(), *poll);
                         } else {
                             self->setMessage(AiController::tr("The enhanced file was created but could not be prepared."));
                         }
                     };
                     (*poll)();
                 });
}

// ---- tracking -------------------------------------------------------------------------------------------------------------

QString AiController::startTracking(double x, double y, double w, double h)
{
    TimelineModel* tl = m_project->timeline();
    const QString clipId = tl->selectedClipOfKind(QStringLiteral("video"));
    if (clipId.isEmpty())
        return tr("Select a video clip on the timeline first.");
    const QVariantMap info = tl->clipInfo(clipId);
    if (info.value("type").toString() == QLatin1String("title"))
        return tr("Titles cannot be tracked.");
    const qint64 start = info.value("start").toLongLong();
    const qint64 end = info.value("end").toLongLong();
    const qint64 head = m_project->playheadFrame();
    if (head < start || head >= end)
        return tr("Move the playhead onto the clip, at the frame where the subject is visible.");
    const QVariantMap media = m_project->media()->describe(info.value("mediaId").toString());
    const QString path = media.value("path").toString();
    if (!QFileInfo::exists(path))
        return tr("The media file is missing.");
    const QRectF region(std::clamp(x, 0.0, 1.0), std::clamp(y, 0.0, 1.0), std::clamp(w, 0.02, 1.0), std::clamp(h, 0.02, 1.0));
    const int frames = int(std::min<qint64>(end - head, 900));
    const double fps = m_project->frameRate().toDouble();
    const qint64 startUs = m_project->framesToMicroseconds(info.value("sourceIn").toLongLong() + (head - start));
    auto out = std::make_shared<ai::TrackResult>();
    return begin(QStringLiteral("track"), tr("Tracking…"),
                 [=](const ai::ProgressFn& progress, const std::atomic_bool& cancel) {
                     *out = ai::trackRegion(path, startUs, fps, frames, region, progress, cancel);
                 },
                 [this, out, clipId, head](bool) {
                     if (!out->error.isEmpty() || out->centers.isEmpty()) {
                         setMessage(out->error.isEmpty() ? tr("Tracking failed.") : out->error);
                         return;
                     }
                     m_trackResult = *out;
                     m_trackClip = clipId;
                     m_trackStartFrame = head;
                     double avg = 0;
                     for (float c : out->confidence)
                         avg += c;
                     avg /= out->confidence.size();
                     m_tracking = {{"clipId", clipId}, {"frames", out->centers.size()}, {"confidence", avg}, {"startFrame", head}};
                     setMessage(tr("Tracked %n frame(s). Choose how to use the motion.", "", out->centers.size()));
                     emit resultsChanged();
                 });
}

QString AiController::applyTracking(const QString& mode, const QString& targetId)
{
    if (m_trackResult.centers.isEmpty())
        return tr("There is no tracking data.");
    TimelineModel* tl = m_project->timeline();
    const bool stabilize = mode == QLatin1String("stabilize");
    const QString target = stabilize ? m_trackClip : targetId;
    if (target.isEmpty())
        return tr("Select the clip that should follow the subject.");
    const QVariantMap tinfo = tl->clipInfo(target);
    if (tinfo.isEmpty())
        return tr("The clip no longer exists.");
    if (tinfo.value("type").toString() == QLatin1String("title"))
        return tr("Titles cannot be keyframed yet. Use a video or image clip.");
    if (tinfo.value("kind").toString() != QLatin1String("video"))
        return tr("Choose a clip on a video track.");

    // How much of the canvas the tracked picture covers (aspect-fit), so a picture offset becomes a canvas offset.
    double fx = 1, fy = 1;
    if (const SnapshotPtr snap = m_project->snapshot()) {
        for (const SnapshotClip& c : snap->clips)
            if (c.clip.id == m_trackClip && c.media.width > 0 && c.media.height > 0) {
                const double s = std::min(double(snap->width) / c.media.width, double(snap->height) / c.media.height);
                fx = c.media.width * s / snap->width;
                fy = c.media.height * s / snap->height;
            }
    }
    const double sign = stabilize ? -1.0 : 1.0;
    const QPointF base = m_trackResult.centers.first();
    QVariantList kx, ky;
    const qint64 n = m_trackResult.centers.size();
    const qint64 tEnd = tinfo.value("end").toLongLong();
    for (qint64 i = 0; i < n; ++i) {
        if (i % 2 != 0 && i != n - 1)
            continue;                                           // every other frame is enough: linear interpolation in between
        const qint64 frame = m_trackStartFrame + i;
        if (frame >= tEnd)
            break;
        const QPointF d = m_trackResult.centers.at(i) - base;
        kx.append(QVariantMap{{"frame", frame}, {"value", sign * d.x() * fx}});
        ky.append(QVariantMap{{"frame", frame}, {"value", sign * d.y() * fy}});
    }
    tl->undoStack()->beginMacro(stabilize ? tr("Stabilize") : tr("Follow tracked subject"));
    QString err = tl->setKeyframes(target, QStringLiteral("posX"), kx);
    if (err.isEmpty())
        err = tl->setKeyframes(target, QStringLiteral("posY"), ky);
    tl->undoStack()->endMacro();
    if (err.isEmpty())
        setMessage(stabilize ? tr("Stabilization keyframes added to the clip (Position X / Y).")
                             : tr("Position keyframes added: the clip follows the subject."));
    return err;
}

void AiController::clearTracking()
{
    m_trackResult = {};
    m_tracking.clear();
    emit resultsChanged();
}

// ---- assistant --------------------------------------------------------------------------------------------------------------

QVariantMap AiController::parseAssistant(const QString& text) const
{
    const ai::Command c = ai::parseCommand(text);
    QString summary;
    switch (c.type) {
    case ai::Command::Transcribe: summary = tr("Transcribe the timeline audio into subtitles"); break;
    case ai::Command::DetectSilence: summary = tr("Find silent passages"); break;
    case ai::Command::SuggestCuts: summary = tr("Suggest cut points at scene changes and pauses"); break;
    case ai::Command::EnhanceAudio: summary = tr("Reduce noise and even out the audio of the selected clip"); break;
    case ai::Command::TrackObject: summary = tr("Track a region of the selected clip (choose the region in the Tracking section)"); break;
    case ai::Command::Help:
        summary = tr("I can: create subtitles, find silent passages, suggest cut points, clean up speech audio. Try: \"create Thai subtitles\".");
        break;
    default: summary = tr("I did not understand that. Try: \"create Thai subtitles\", \"find the silent parts\", \"suggest cut points\" or \"make the speech clearer\"."); break;
    }
    return {{"valid", c.valid()}, {"type", ai::Command::typeName(c.type)}, {"summary", summary}, {"language", c.language}};
}

namespace {
const ai::CatalogEntry* catalogEntry(const QList<ai::CatalogEntry>& list, const QString& id)
{
    for (const ai::CatalogEntry& e : list)
        if (e.id == id)
            return &e;
    return nullptr;
}
QString entryName(const QList<ai::CatalogEntry>& list, const QString& id)
{
    const ai::CatalogEntry* e = catalogEntry(list, id);
    return e ? QCoreApplication::translate("EditPlanner", e->label.toUtf8().constData()) : id;
}
} // namespace

QVariantMap AiController::planEdit(const QString& text) const
{
    return planToMap(ai::parseEditPrompt(text), QStringLiteral("keyword"));
}

QVariantMap AiController::planToMap(const ai::EditPlan& plan, const QString& source) const
{
    auto target = [&](int n) {
        return n == 0 ? tr("every video clip") : n < 0 ? tr("the selected clip") : tr("clip %1").arg(n);
    };
    QVariantList steps;
    bool exportRequested = false;
    QString exportPreset;
    for (const ai::EditStep& s : plan.steps) {
        switch (s.kind) {
        case ai::EditStep::PlaceAll: steps << tr("Place every imported clip on the timeline, one after another"); break;
        case ai::EditStep::SplitAt: steps << tr("Split the timeline at %1 s").arg(QString::number(s.seconds, 'f', 1)); break;
        case ai::EditStep::SplitEvery: steps << tr("Split the timeline every %1 s").arg(QString::number(s.every, 'f', 1)); break;
        case ai::EditStep::AddText: {
            const QString style = entryName(ai::textStyleCatalog(), s.id);
            const QString at = QString::number(std::max(0.0, s.seconds), 'f', 1);
            steps << (s.text.isEmpty() ? tr("Add a text clip (style: %1) at %2 s").arg(style, at)
                                       : tr("Add the text \"%1\" (style: %2) at %3 s").arg(s.text, style, at));
            break;
        }
        case ai::EditStep::Look: steps << tr("Filter \"%1\" on %2").arg(entryName(ai::lookCatalog(), s.id), target(s.clipNumber)); break;
        case ai::EditStep::Effect: steps << tr("Effect \"%1\" on %2").arg(entryName(ai::effectCatalog(), s.id), target(s.clipNumber)); break;
        case ai::EditStep::Transition:
            steps << tr("Transition \"%1\" at the %2 of %3").arg(entryName(ai::transitionCatalog(), s.id),
                                                               s.direction == QLatin1String("out") ? tr("end") : tr("start"), target(s.clipNumber));
            break;
        case ai::EditStep::Export:
            exportRequested = true;
            exportPreset = s.preset;
            steps << (s.preset.isEmpty() ? tr("Export the video with the current export settings") : tr("Export the video (%1)").arg(s.preset));
            break;
        }
    }
    QVariantList unknown;
    for (const QString& u : plan.unknown)
        unknown << u;
    return {{"valid", !plan.steps.isEmpty()}, {"steps", steps}, {"unknown", unknown}, {"exportRequested", exportRequested}, {"exportPreset", exportPreset}, {"source", source}};
}

QString AiController::applyEditPrompt(const QString& text)
{
    return executePlan(ai::parseEditPrompt(text));
}

void AiController::setPending(const ai::EditPlan& plan, const QString& source)
{
    m_pendingPlan = plan;
    m_pendingMap = planToMap(plan, source);
    emit pendingPlanChanged();
}

void AiController::previewEdit(const QString& text)
{
    m_llmText.clear();                       // a typed change makes an answer of the model that is still coming obsolete
    setPending(ai::parseEditPrompt(text), QStringLiteral("keyword"));
}

QString AiController::understandWithLlm(const QString& text)
{
    if (text.trimmed().isEmpty())
        return tr("Write what to do first.");
    const QString problem = m_llm.requestPlan(text);
    if (problem.isEmpty())
        m_llmText = text;
    return problem;
}

QString AiController::applyPendingPlan()
{
    return executePlan(m_pendingPlan);
}

QString AiController::executePlan(const ai::EditPlan& plan)
{
    if (plan.steps.isEmpty())
        return tr("I did not understand that. Try: \"place all clips, add the filter black and white, add the slide transition, add the text Hello at 3 seconds\".");
    TimelineModel* tl = m_project->timeline();
    const double fps = std::max(1, m_project->nominalFps());
    QString firstError;
    auto note = [&](const QString& error) {
        if (!error.isEmpty() && firstError.isEmpty())
            firstError = error;
    };
    // the video clips (not titles, not subtitles) in timeline order; n = 0 all, n > 0 the n-th, n < 0 the selected one
    auto targets = [&](int n) {
        QList<ClipData> list;
        for (const ClipData& c : tl->clips()) {
            if (c.isTitle())
                continue;
            for (const TrackInfo& t : m_project->tracks()->tracks())
                if (t.id == c.trackId && t.kind == QLatin1String("video"))
                    list << c;
        }
        std::sort(list.begin(), list.end(), [](const ClipData& a, const ClipData& b) { return a.startFrame < b.startFrame; });
        if (n > 0) {
            QList<ClipData> one;
            if (n <= list.size())
                one << list.at(n - 1);
            return one;
        }
        if (n < 0) {
            QList<ClipData> picked;
            for (const ClipData& c : list)
                if (tl->isSelected(c.id))
                    picked << c;
            return picked;
        }
        return list;
    };

    tl->undoStack()->beginMacro(tr("Edit by prompt"));
    for (const ai::EditStep& s : plan.steps) {
        switch (s.kind) {
        case ai::EditStep::PlaceAll:
            note(m_project->placeAllMedia());
            break;
        case ai::EditStep::SplitAt:
            tl->clearSelection();
            note(tl->splitAt(qRound64(s.seconds * fps)));
            break;
        case ai::EditStep::SplitEvery: {
            tl->clearSelection();
            const qint64 step = std::max<qint64>(1, qRound64(s.every * fps));
            int count = 0;
            for (qint64 f = step; f < tl->durationFrames() && count < 2000; f += step, ++count)
                note(tl->splitAt(f));
            break;
        }
        case ai::EditStep::AddText: {
            tl->clearSelection();
            note(tl->addTitle(-1, s.seconds < 0 ? 0 : qRound64(s.seconds * fps), s.id));
            const QStringList picked = tl->selectedIds();
            if (!s.text.isEmpty() && !picked.isEmpty())
                note(tl->setTitle(picked.first(), {{"text", s.text}}));
            break;
        }
        case ai::EditStep::Look:
        case ai::EditStep::Effect: {
            const bool look = s.kind == ai::EditStep::Look;
            const ai::CatalogEntry* e = catalogEntry(look ? ai::lookCatalog() : ai::effectCatalog(), s.id);
            const QList<ClipData> list = targets(s.clipNumber);
            if (!e || list.isEmpty()) {
                note(tr("There is no video clip to apply it to."));
                break;
            }
            for (const ClipData& c : list) {
                if (!e->resetGroup.isEmpty())
                    note(tl->resetParams(c.id, e->resetGroup));
                for (const auto& kv : e->values) {
                    double value = kv.second;
                    if (look) {                       // a fraction of the way from the default to the end of the range
                        const QVariantMap info = tl->paramInfo(kv.first);
                        const double def = info.value("def").toDouble(), lo = info.value("min").toDouble(), hi = info.value("max").toDouble();
                        value = kv.second >= 0 ? def + kv.second * (hi - def) : def + kv.second * (def - lo);
                    }
                    note(tl->setParam(c.id, kv.first, value, c.startFrame));
                }
            }
            break;
        }
        case ai::EditStep::Transition: {
            const QList<ClipData> list = targets(s.clipNumber);
            if (list.isEmpty()) {
                note(tr("There is no video clip to apply it to."));
                break;
            }
            for (const ClipData& c : list)
                note(tl->setTransition(c.id, s.direction, s.id == QLatin1String("none") ? QString() : s.id,
                                       s.id == QLatin1String("none") ? 0 : qRound64(fps / 2.0)));
            break;
        }
        case ai::EditStep::Export:
            break;                                   // the caller starts the export after the edits
        }
    }
    tl->undoStack()->endMacro();
    return firstError;
}

QString AiController::runAssistant(const QString& text)
{
    const ai::Command c = ai::parseCommand(text);
    switch (c.type) {
    case ai::Command::Transcribe: return transcribe(c.language);
    case ai::Command::DetectSilence: return detectSilence(-35.0, 500);
    case ai::Command::SuggestCuts: return suggestCuts(0.5, -35.0, 500);
    case ai::Command::EnhanceAudio: return enhanceSelected(0.7, true, true);
    case ai::Command::TrackObject: return tr("Choose the region to track in the Tracking section first.");
    case ai::Command::Help: return {};
    default: return tr("I did not understand that request.");
    }
}

} // namespace dv
