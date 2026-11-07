#include "VideoReader.h"

#include "HardwareDecode.h"

#include <QTransform>

namespace dv {

namespace {
int rotationOf(const AVStream* stream)
{
    const AVPacketSideData* sd = av_packet_side_data_get(stream->codecpar->coded_side_data,
                                                         stream->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
    if (!sd || sd->size < int(9 * sizeof(int32_t)))
        return 0;
    const double angle = av_display_rotation_get(reinterpret_cast<const int32_t*>(sd->data));
    if (std::isnan(angle))
        return 0;
    int deg = int(std::lround(-angle)) % 360;
    if (deg < 0)
        deg += 360;
    return (deg / 90 * 90) % 360;
}
} // namespace

bool VideoReader::open(const QString& path, QString* error)
{
    m_fmt = ff::openInput(path, error);
    if (!m_fmt)
        return false;
    m_stream = av_find_best_stream(m_fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_stream < 0) {
        if (error)
            *error = QStringLiteral("No video stream");
        m_fmt.reset();
        return false;
    }
    AVStream* st = m_fmt->streams[m_stream];
    m_dec = nullptr;
    // The GPU pays off for large pictures only: for small ones the copy back to system memory costs more than software decoding.
    if (hw::enabled() && st->codecpar->width >= 3000 && st->codecpar->codec_id != AV_CODEC_ID_PNG && st->codecpar->codec_id != AV_CODEC_ID_MJPEG)
        if (AVBufferRef* device = hw::sharedDevice())
            m_dec = ff::openDecoder(st, nullptr, 0, device);        // falls back below when the codec or the driver refuses
    if (!m_dec)
        m_dec = ff::openDecoder(st, error);
    if (!m_dec) {
        m_fmt.reset();
        return false;
    }
    m_rotation = rotationOf(st);
    m_displaySize = QSize(m_dec->width, m_dec->height);
    if (m_rotation % 180 != 0)
        m_displaySize.transpose();
    // Still pictures are demuxed by image2 / *_pipe demuxers; they deliver exactly one frame.
    const QString demuxer = QString::fromUtf8(m_fmt->iformat->name);
    m_still = demuxer.contains(QLatin1String("image2")) || demuxer.endsWith(QLatin1String("_pipe"));
    return true;
}

bool VideoReader::decodeNext(ff::FramePtr& out, int64_t& us)
{
    AVStream* st = m_fmt->streams[m_stream];
    const int64_t startTs = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
    ff::FramePtr frame = ff::makeFrame();
    for (;;) {
        int rc = avcodec_receive_frame(m_dec.get(), frame.get());
        if (rc == 0) {
            const bool onGpu = frame->format == AV_PIX_FMT_D3D11;
            if (onGpu) {                                   // bring the picture to system memory (NV12)
                ff::FramePtr sw = ff::makeFrame();
                if (av_hwframe_transfer_data(sw.get(), frame.get(), 0) < 0)
                    return false;
                av_frame_copy_props(sw.get(), frame.get());
                frame = std::move(sw);
            }
            hw::countFrame(onGpu);
            int64_t ts = frame->best_effort_timestamp;
            if (ts == AV_NOPTS_VALUE)
                ts = frame->pts;
            if (ts == AV_NOPTS_VALUE)
                us = m_lastUs + 33333;
            else
                us = av_rescale_q(ts - startTs, st->time_base, AVRational{1, 1000000});
            if (us < 0 && m_still)
                us = 0;
            m_lastUs = us;
            out = std::move(frame);
            return true;
        }
        if (rc == AVERROR_EOF)
            return false;
        if (rc != AVERROR(EAGAIN))
            return false;

        if (m_eof) {
            if (!m_flushed) {
                avcodec_send_packet(m_dec.get(), nullptr);
                m_flushed = true;
                continue;
            }
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

bool VideoReader::seekTo(int64_t timeUs)
{
    AVStream* st = m_fmt->streams[m_stream];
    const int64_t startTs = st->start_time != AV_NOPTS_VALUE ? st->start_time : 0;
    const int64_t ts = av_rescale_q(std::max<int64_t>(0, timeUs), AVRational{1, 1000000}, st->time_base) + startTs;
    const int rc = av_seek_frame(m_fmt.get(), m_stream, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_dec.get());
    m_cur.reset();
    m_pending.reset();
    m_eof = false;
    m_flushed = false;
    return rc >= 0;
}

QImage VideoReader::convert(const AVFrame* frame, QSize size)
{
    const QSize conv = (m_rotation % 180 != 0) ? QSize(size.height(), size.width()) : size;
    const AVPixelFormat srcFmt = static_cast<AVPixelFormat>(frame->format);
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(srcFmt);
    const bool alpha = desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA);

    QImage image(conv, alpha ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    SwsContext* ctx = sws_getCachedContext(m_sws.release(), frame->width, frame->height, srcFmt, conv.width(), conv.height(),
                                           AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    m_sws.reset(ctx);
    if (!ctx)
        return {};

    // Colour matrix: honour the stream's tag, otherwise BT.709 for HD and BT.601 for SD.
    int cs = SWS_CS_DEFAULT;
    switch (frame->colorspace) {
    case AVCOL_SPC_BT709: cs = SWS_CS_ITU709; break;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M: cs = SWS_CS_ITU601; break;
    case AVCOL_SPC_BT2020_NCL: cs = SWS_CS_BT2020; break;
    default: cs = frame->height >= 720 ? SWS_CS_ITU709 : SWS_CS_ITU601; break;
    }
    const bool fullRange = frame->color_range == AVCOL_RANGE_JPEG || srcFmt == AV_PIX_FMT_YUVJ420P
                           || srcFmt == AV_PIX_FMT_YUVJ422P || srcFmt == AV_PIX_FMT_YUVJ444P;
    if (desc && !(desc->flags & AV_PIX_FMT_FLAG_RGB))
        sws_setColorspaceDetails(ctx, sws_getCoefficients(cs), fullRange ? 1 : 0, sws_getCoefficients(SWS_CS_DEFAULT), 1, 0,
                                 1 << 16, 1 << 16);

    uint8_t* dst[4] = {image.bits(), nullptr, nullptr, nullptr};
    int dstStride[4] = {int(image.bytesPerLine()), 0, 0, 0};
    sws_scale(ctx, frame->data, frame->linesize, 0, frame->height, dst, dstStride);

    if (m_rotation != 0)
        image = image.transformed(QTransform().rotate(m_rotation), Qt::FastTransformation);
    return image;
}

QImage VideoReader::frameAt(int64_t timeUs, QSize size, QString* error)
{
    if (!isOpen() || size.isEmpty()) {
        if (error)
            *error = QStringLiteral("Reader is not open");
        return {};
    }

    if (m_still) {
        if (!m_cur) {
            int64_t us = 0;
            if (!decodeNext(m_cur, us)) {
                if (error)
                    *error = QStringLiteral("Cannot decode the image");
                return {};
            }
            ++m_curSerial;
        }
    } else {
        const int64_t horizon = m_pending ? m_pendingUs : m_curUs;
        const bool needSeek = !m_cur || timeUs < m_curUs || timeUs - horizon > 1500000;
        if (needSeek)
            seekTo(timeUs);

        for (;;) {
            if (!m_pending) {
                int64_t us = 0;
                if (!decodeNext(m_pending, us))
                    break;
                m_pendingUs = us;
            }
            if (m_pendingUs <= timeUs) {
                m_cur = std::move(m_pending);
                m_curUs = m_pendingUs;
                ++m_curSerial;
            } else {
                break;
            }
        }
        if (!m_cur && m_pending) {         // time is before the first frame: show the first frame
            m_cur = std::move(m_pending);
            m_curUs = m_pendingUs;
            ++m_curSerial;
        }
        if (!m_cur) {
            if (error)
                *error = QStringLiteral("No frame at this position");
            return {};
        }
    }

    if (m_cacheSerial == m_curSerial && m_cacheSize == size && !m_cache.isNull())
        return m_cache;
    m_cache = convert(m_cur.get(), size);
    m_cacheSerial = m_curSerial;
    m_cacheSize = size;
    return m_cache;
}

} // namespace dv
