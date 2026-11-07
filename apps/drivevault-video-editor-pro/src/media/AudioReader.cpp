#include "AudioReader.h"

#include <algorithm>
#include <cstring>

namespace dv {

bool AudioReader::open(const QString& path, QString* error)
{
    m_fmt = ff::openInput(path, error);
    if (!m_fmt)
        return false;
    m_stream = av_find_best_stream(m_fmt.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_stream < 0) {
        if (error)
            *error = QStringLiteral("No audio stream");
        m_fmt.reset();
        return false;
    }
    m_dec = ff::openDecoder(m_fmt->streams[m_stream], error, 1);
    if (!m_dec) {
        m_fmt.reset();
        return false;
    }
    return true;
}

bool AudioReader::seekTo(int64_t sample)
{
    AVStream* st = m_fmt->streams[m_stream];
    const int64_t startTs = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
    // Land a little early so the decoder can warm up; read() discards the excess.
    const int64_t us = std::max<int64_t>(0, av_rescale(sample, 1000000, m_rate) - 200000);
    const int64_t ts = av_rescale_q(us, AVRational{1, 1000000}, st->time_base) + startTs;
    av_seek_frame(m_fmt.get(), m_stream, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_dec.get());
    m_swr.reset();
    m_buf.clear();
    m_haveStart = false;
    m_eof = false;
    m_flushed = false;
    m_ended = false;
    return true;
}

bool AudioReader::decodeMore()
{
    if (m_ended)
        return false;
    AVStream* st = m_fmt->streams[m_stream];
    const int64_t startTs = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
    ff::FramePtr frame = ff::makeFrame();

    for (;;) {
        int rc = avcodec_receive_frame(m_dec.get(), frame.get());
        if (rc == 0) {
            if (!m_swr) {
                AVChannelLayout out = AV_CHANNEL_LAYOUT_STEREO;
                SwrContext* raw = nullptr;
                if (swr_alloc_set_opts2(&raw, &out, AV_SAMPLE_FMT_FLT, m_rate, &frame->ch_layout,
                                        static_cast<AVSampleFormat>(frame->format), frame->sample_rate, 0, nullptr) < 0
                    || swr_init(raw) < 0) {
                    swr_free(&raw);
                    return false;
                }
                m_swr.reset(raw);
            }
            if (!m_haveStart) {
                int64_t ts = frame->best_effort_timestamp;
                if (ts == AV_NOPTS_VALUE)
                    ts = frame->pts;
                const int64_t us = ts == AV_NOPTS_VALUE ? 0 : av_rescale_q(ts - startTs, st->time_base, AVRational{1, 1000000});
                m_bufStart = av_rescale(std::max<int64_t>(0, us), m_rate, 1000000);
                m_haveStart = true;
            }
            const int maxOut = swr_get_out_samples(m_swr.get(), frame->nb_samples);
            const size_t old = m_buf.size();
            m_buf.resize(old + size_t(maxOut) * 2);
            uint8_t* outPtr = reinterpret_cast<uint8_t*>(m_buf.data() + old);
            const int got = swr_convert(m_swr.get(), &outPtr, maxOut, const_cast<const uint8_t**>(frame->extended_data),
                                        frame->nb_samples);
            m_buf.resize(old + size_t(std::max(0, got)) * 2);
            av_frame_unref(frame.get());
            return true;
        }
        if (rc == AVERROR_EOF) {
            m_ended = true;
            return false;
        }
        if (rc != AVERROR(EAGAIN))
            return false;

        if (m_eof) {
            if (!m_flushed) {
                avcodec_send_packet(m_dec.get(), nullptr);
                m_flushed = true;
                continue;
            }
            m_ended = true;
            return false;
        }
        rc = av_read_frame(m_fmt.get(), m_packet.get());
        if (rc < 0) {
            m_eof = true;
            continue;
        }
        if (m_packet->stream_index == m_stream)
            avcodec_send_packet(m_dec.get(), m_packet.get());
        av_packet_unref(m_packet.get());
    }
}

bool AudioReader::read(int64_t startSample, int frames, float* out)
{
    std::memset(out, 0, size_t(frames) * 2 * sizeof(float));
    if (!isOpen() || frames <= 0)
        return false;
    if (startSample < 0) {                 // before the media starts: leading silence
        const int lead = int(std::min<int64_t>(-startSample, frames));
        if (lead >= frames)
            return true;
        return read(0, frames - lead, out + size_t(lead) * 2);
    }

    const int64_t bufEnd = m_bufStart + int64_t(m_buf.size() / 2);
    const bool contiguous = m_haveStart && startSample >= m_bufStart && startSample <= bufEnd + m_rate / 2;
    if (!contiguous)
        seekTo(startSample);

    int64_t need = startSample + frames;
    while (!m_haveStart || m_bufStart + int64_t(m_buf.size() / 2) < need) {
        if (!decodeMore())
            break;
        // After a seek the first frame may start before the wanted position: drop what is not needed.
        if (m_haveStart && m_bufStart < startSample - int64_t(m_rate)) {
            const int64_t drop = startSample - int64_t(m_rate) - m_bufStart;
            const size_t dropFrames = size_t(std::min<int64_t>(drop, int64_t(m_buf.size() / 2)));
            m_buf.erase(m_buf.begin(), m_buf.begin() + std::ptrdiff_t(dropFrames * 2));
            m_bufStart += int64_t(dropFrames);
        }
    }

    const int64_t avail = m_bufStart + int64_t(m_buf.size() / 2);
    for (int i = 0; i < frames; ++i) {
        const int64_t s = startSample + i;
        if (s < m_bufStart || s >= avail)
            continue;
        const size_t idx = size_t(s - m_bufStart) * 2;
        out[size_t(i) * 2] = m_buf[idx];
        out[size_t(i) * 2 + 1] = m_buf[idx + 1];
    }

    // Keep one second of history for small backwards steps, drop the rest.
    const int64_t keepFrom = startSample + frames - m_rate;
    if (keepFrom > m_bufStart) {
        const size_t dropFrames = size_t(std::min<int64_t>(keepFrom - m_bufStart, int64_t(m_buf.size() / 2)));
        m_buf.erase(m_buf.begin(), m_buf.begin() + std::ptrdiff_t(dropFrames * 2));
        m_bufStart += int64_t(dropFrames);
    }
    return true;
}

} // namespace dv
