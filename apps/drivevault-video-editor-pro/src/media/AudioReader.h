#pragma once

#include "FFmpeg.h"

#include <QString>
#include <vector>

namespace dv {

// Random-access audio decoder producing interleaved 32-bit float stereo at a fixed sample rate.
// Sample indices are in the *output* domain (e.g. 48 kHz). One instance = one thread.
class AudioReader {
public:
    explicit AudioReader(int outRate = 48000) : m_rate(outRate) {}

    bool open(const QString& path, QString* error = nullptr);
    bool isOpen() const { return m_fmt != nullptr; }
    int rate() const { return m_rate; }

    // Fills `frames` stereo frames (frames*2 floats) starting at output sample `startSample`.
    // Anything past the end of the media (or before its start) is silence. Returns false on I/O failure.
    bool read(int64_t startSample, int frames, float* out);
    // True once the decoder reached the end of the stream and `sample` is past the last decoded sample.
    bool atEnd(int64_t sample) const { return m_ended && sample >= m_bufStart + int64_t(m_buf.size() / 2); }

private:
    bool seekTo(int64_t sample);
    bool decodeMore();   // appends decoded + resampled audio to m_buf; false at end of stream

    ff::FormatPtr m_fmt;
    ff::CodecPtr m_dec;
    ff::SwrPtr m_swr;
    ff::PacketPtr m_packet = ff::makePacket();
    int m_stream = -1;
    int m_rate;
    bool m_eof = false;
    bool m_flushed = false;
    bool m_ended = false;

    std::vector<float> m_buf;   // interleaved stereo
    int64_t m_bufStart = 0;     // output sample index of m_buf[0]
    bool m_haveStart = false;
};

} // namespace dv
