#pragma once

#include "FFmpeg.h"

#include <QImage>
#include <QSize>
#include <QString>

namespace dv {

// Random-access video decoder. frameAt() returns the frame that is on screen at a given media time:
// the last frame whose timestamp is <= that time. Timestamps are used (not frame counters), so
// variable-frame-rate sources stay in sync. One instance must only be used from one thread.
class VideoReader {
public:
    VideoReader() = default;

    bool open(const QString& path, QString* error = nullptr);
    bool isOpen() const { return m_fmt != nullptr; }
    bool isStill() const { return m_still; }
    QSize displaySize() const { return m_displaySize; }

    // Frame shown at `timeUs`, scaled to `size` (display orientation, rotation metadata applied).
    // Returns a null image when decoding fails or the media has no frame.
    QImage frameAt(int64_t timeUs, QSize size, QString* error = nullptr);

    int64_t lastFrameTimeUs() const { return m_curUs; }

private:
    bool decodeNext(ff::FramePtr& frame, int64_t& us);
    bool seekTo(int64_t timeUs);
    QImage convert(const AVFrame* frame, QSize size);

    ff::FormatPtr m_fmt;
    ff::CodecPtr m_dec;
    ff::PacketPtr m_packet = ff::makePacket();
    ff::SwsPtr m_sws;
    int m_stream = -1;
    int m_rotation = 0;
    bool m_still = false;
    bool m_eof = false;
    bool m_flushed = false;
    QSize m_displaySize;

    ff::FramePtr m_cur;
    ff::FramePtr m_pending;
    int64_t m_curUs = 0;
    int64_t m_pendingUs = 0;
    int64_t m_lastUs = 0;
    uint64_t m_curSerial = 0;

    QImage m_cache;
    uint64_t m_cacheSerial = ~0ull;
    QSize m_cacheSize;
};

} // namespace dv
