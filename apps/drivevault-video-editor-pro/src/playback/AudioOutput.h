#pragma once

#include <QString>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

namespace dv {

// Audio device output (Windows WinMM waveOut, 16-bit stereo). A feeder thread pulls float audio from
// `fill(startSample, frames, out)` and the device position is the playback clock for A/V sync.
class AudioOutput {
public:
    using FillFunction = std::function<void(int64_t startSample, int frames, float* out)>;

    AudioOutput();
    ~AudioOutput();

    // Names of the output devices; the first entry is always "default".
    static QStringList deviceNames();

    // Starts playing at timeline sample `startSample`. `device` is a name from deviceNames() or "default".
    bool start(const QString& device, int sampleRate, int latencyMs, int64_t startSample, FillFunction fill, QString* error = nullptr);
    void stop();
    bool isRunning() const { return m_running.load(); }

    // Timeline sample that is audible right now (start sample + samples the device has played).
    int64_t currentSample() const;
    void setVolume(float volume) { m_volume.store(volume); }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::atomic_bool m_running{false};
    std::atomic<float> m_volume{1.0f};
    int64_t m_startSample = 0;
};

} // namespace dv
