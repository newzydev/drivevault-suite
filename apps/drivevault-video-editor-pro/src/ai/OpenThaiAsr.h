#pragma once

#include "ai/AiTypes.h"

#include <QList>
#include <QString>

#include <atomic>
#include <vector>

namespace dv::ai {

// OpenThai 2.0 ASR Streaming (iApp, OpenMDW-1.1): a Thai speech recogniser (FastConformer encoder + RNN-T decoder) exported to ONNX and run with
// ONNX Runtime on the CPU. The export used here takes a whole utterance, so long audio is cut at pauses and every piece is decoded on its own.
// Everything runs on this computer.

struct OpenThaiResult {
    QList<Segment> segments;
    QString error;
    bool cancelled = false;
    bool ok() const { return error.isEmpty() && !cancelled; }
};

// True when this build contains the ONNX Runtime engine (the program still builds and runs without it).
bool openThaiCompiledIn();
// Folder that holds encoder.onnx (+ .data), decoder_joint.onnx and tokens.txt: $DV_OPENTHAI_DIR, <app>\ai\openthai, <data>\models\openthai-asr or the
// development .tools\openthai-asr. Empty when the model is not installed.
QString openThaiModelDir();
// Does `dir` hold a complete model? (fp32 or int8 files)
bool openThaiModelComplete(const QString& dir);

// 128 log-mel bands (25 ms Hann window, 10 ms hop, 512-point FFT, pre-emphasis 0.97, Slaney mel scale, no normalisation), laid out [band][frame].
std::vector<float> openThaiLogMel(const std::vector<float>& pcm16k, int* frames);
// Pause based pieces: [startSample, endSample) of speech, each at most maxSeconds long (pure; used by the engine and the tests).
std::vector<std::pair<size_t, size_t>> openThaiSplit(const std::vector<float>& pcm16k, double maxSeconds = 24.0);

// pcm16k: mono, 16 kHz, -1..1. language: "th" (prompt 32) or "mix" (Thai with English, prompt 63). Progress 0..1 (called from the worker thread).
OpenThaiResult openThaiTranscribe(const std::vector<float>& pcm16k, const QString& modelDir, const QString& language, const ProgressFn& progress,
                                  const std::atomic_bool& cancel);

} // namespace dv::ai
