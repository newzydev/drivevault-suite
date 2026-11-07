#pragma once

#include "ai/AiTypes.h"

#include <vector>

namespace dv::ai {

struct EnhanceParams {
    double reduction = 0.7;        // 0..1 noise reduction strength
    bool highPass = true;          // remove rumble below 80 Hz
    bool normalize = true;         // bring the speech level to targetRmsDb
    double targetRmsDb = -20.0;
};

// Spectral-subtraction noise reduction of one channel (in place): the noise spectrum is learned from the quietest
// tenth of the signal, then removed with a smoothed gain. Classical DSP - no neural network.
void reduceNoise(std::vector<float>& samples, int sampleRate, double reduction);

// Decodes `inPath`, enhances it (noise reduction, high-pass, level) and writes a 16-bit stereo WAV.
// Returns "" on success, "cancelled", or an error message.
QString enhanceFile(const QString& inPath, const QString& outPath, int sampleRate, const EnhanceParams& params,
                    const ProgressFn& progress, const std::atomic_bool& cancel);

} // namespace dv::ai
