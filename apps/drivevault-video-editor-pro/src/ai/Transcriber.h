#pragma once

#include "ai/AiTypes.h"
#include "core/RenderSnapshot.h"

#include <QList>

namespace dv::ai {

struct TranscribeRequest {
    SnapshotPtr snapshot;      // the timeline mix is transcribed, so segment times are timeline times
    QString whisperExe;
    QString model;             // path of a ggml model file
    QString language;          // "th", "en" ... or "auto"
    QString workDir;           // temporary files are created (and removed) here
    int threads = 4;
};

struct TranscribeResult {
    QList<Segment> segments;
    QString error;
    bool cancelled = false;
    bool ok() const { return error.isEmpty() && !cancelled; }
};

// Where whisper-cli.exe lives: next to the application (ai\whisper), $DV_WHISPER_DIR, or the development .tools folder.
QString whisperExecutable();
// SubRip text -> segments (pure).
QList<Segment> parseSrt(const QString& text);
// Mixes the snapshot to 16 kHz mono, runs whisper.cpp (progress from its output, cancel kills it) and parses the result.
TranscribeResult transcribeTimeline(const TranscribeRequest& request, const ProgressFn& progress, const std::atomic_bool& cancel);

} // namespace dv::ai
