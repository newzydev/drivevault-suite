#include "AudioOutput.h"

#include "core/Logger.h"

#include <algorithm>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace dv {

#ifdef _WIN32

struct AudioOutput::Impl {
    HWAVEOUT device = nullptr;
    HANDLE event = nullptr;
    std::vector<std::vector<int16_t>> buffers;
    std::vector<WAVEHDR> headers;
    int bufferFrames = 0;
    int sampleRate = 48000;
    std::thread thread;
    std::atomic_bool stopRequested{false};
    AudioOutput::FillFunction fill;
    std::vector<float> temp;
    int64_t nextSample = 0;
};

namespace {
UINT deviceIdForName(const QString& name)
{
    if (name.isEmpty() || name == QLatin1String("default"))
        return WAVE_MAPPER;
    const UINT count = waveOutGetNumDevs();
    for (UINT i = 0; i < count; ++i) {
        WAVEOUTCAPSW caps{};
        if (waveOutGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR && QString::fromWCharArray(caps.szPname) == name)
            return i;
    }
    return WAVE_MAPPER;
}
} // namespace

QStringList AudioOutput::deviceNames()
{
    QStringList names{QStringLiteral("default")};
    const UINT count = waveOutGetNumDevs();
    for (UINT i = 0; i < count; ++i) {
        WAVEOUTCAPSW caps{};
        if (waveOutGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            names << QString::fromWCharArray(caps.szPname);
    }
    return names;
}

AudioOutput::AudioOutput() = default;
AudioOutput::~AudioOutput() { stop(); }

bool AudioOutput::start(const QString& deviceName, int sampleRate, int latencyMs, int64_t startSample, FillFunction fill,
                        QString* error)
{
    stop();
    // Automated tests set DV_NO_AUDIO=1 so that nothing is played aloud; the engine then uses its timer clock.
    if (qEnvironmentVariableIntValue("DV_NO_AUDIO") != 0) {
        if (error)
            *error = QStringLiteral("Audio output is switched off (DV_NO_AUDIO)");
        return false;
    }
    auto impl = std::make_unique<Impl>();
    impl->sampleRate = sampleRate;
    impl->fill = std::move(fill);
    impl->nextSample = startSample;

    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = DWORD(sampleRate);
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = DWORD(sampleRate) * 4;

    impl->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    const MMRESULT rc = waveOutOpen(&impl->device, deviceIdForName(deviceName), &fmt, reinterpret_cast<DWORD_PTR>(impl->event), 0,
                                    CALLBACK_EVENT);
    if (rc != MMSYSERR_NOERROR) {
        if (error)
            *error = QStringLiteral("waveOutOpen failed (%1)").arg(int(rc));
        CloseHandle(impl->event);
        return false;
    }

    constexpr int kBuffers = 4;
    impl->bufferFrames = std::max(240, sampleRate * std::clamp(latencyMs, 10, 500) / 1000 / kBuffers);
    impl->buffers.assign(kBuffers, std::vector<int16_t>(size_t(impl->bufferFrames) * 2));
    impl->headers.assign(kBuffers, WAVEHDR{});
    impl->temp.resize(size_t(impl->bufferFrames) * 2);

    m_startSample = startSample;
    m_impl = std::move(impl);
    Impl* p = m_impl.get();

    auto fillBuffer = [this, p](int i) {
        p->fill(p->nextSample, p->bufferFrames, p->temp.data());
        p->nextSample += p->bufferFrames;
        const float vol = m_volume.load();
        int16_t* dst = p->buffers[size_t(i)].data();
        for (int s = 0; s < p->bufferFrames * 2; ++s)
            dst[s] = int16_t(std::clamp(p->temp[size_t(s)] * vol, -1.f, 1.f) * 32767.f);
        WAVEHDR& h = p->headers[size_t(i)];
        h.lpData = reinterpret_cast<LPSTR>(dst);
        h.dwBufferLength = DWORD(p->bufferFrames) * 4;
        h.dwFlags &= ~DWORD(WHDR_DONE);
        waveOutWrite(p->device, &h, sizeof(WAVEHDR));
    };

    for (int i = 0; i < kBuffers; ++i) {
        waveOutPrepareHeader(p->device, &p->headers[size_t(i)], sizeof(WAVEHDR));
        fillBuffer(i);
    }
    m_running = true;
    p->thread = std::thread([p, fillBuffer, this] {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
        while (!p->stopRequested.load()) {
            WaitForSingleObject(p->event, 20);
            for (int i = 0; i < int(p->headers.size()); ++i)
                if ((p->headers[size_t(i)].dwFlags & WHDR_DONE) && !p->stopRequested.load())
                    fillBuffer(i);
        }
        Q_UNUSED(this)
    });
    return true;
}

void AudioOutput::stop()
{
    if (!m_impl)
        return;
    m_impl->stopRequested = true;
    SetEvent(m_impl->event);
    if (m_impl->thread.joinable())
        m_impl->thread.join();
    waveOutReset(m_impl->device);
    for (WAVEHDR& h : m_impl->headers)
        waveOutUnprepareHeader(m_impl->device, &h, sizeof(WAVEHDR));
    waveOutClose(m_impl->device);
    CloseHandle(m_impl->event);
    m_impl.reset();
    m_running = false;
}

int64_t AudioOutput::currentSample() const
{
    if (!m_impl)
        return m_startSample;
    MMTIME t{};
    t.wType = TIME_SAMPLES;
    if (waveOutGetPosition(m_impl->device, &t, sizeof(t)) != MMSYSERR_NOERROR || t.wType != TIME_SAMPLES)
        return m_startSample;
    return m_startSample + int64_t(t.u.sample);
}

#else  // non-Windows: no audio device support yet

struct AudioOutput::Impl {};
AudioOutput::AudioOutput() = default;
AudioOutput::~AudioOutput() = default;
QStringList AudioOutput::deviceNames() { return {QStringLiteral("default")}; }
bool AudioOutput::start(const QString&, int, int, int64_t, FillFunction, QString* error)
{
    if (error)
        *error = QStringLiteral("Audio output is not implemented on this platform");
    return false;
}
void AudioOutput::stop() {}
int64_t AudioOutput::currentSample() const { return m_startSample; }

#endif

} // namespace dv
