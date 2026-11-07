#pragma once

// Thin RAII helpers around the FFmpeg C API. FFmpeg headers are only included from the media /
// export modules - no other module (and no QML) touches FFmpeg directly.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/display.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <QString>

#include <memory>

namespace dv::ff {

// FFmpeg prints demuxer diagnostics for damaged files to stderr; errors are reported through return values.
inline const bool kQuietLogging = (av_log_set_level(AV_LOG_FATAL), true);

inline QString errorString(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

struct FormatDeleter { void operator()(AVFormatContext* p) const { avformat_close_input(&p); } };
struct CodecDeleter { void operator()(AVCodecContext* p) const { avcodec_free_context(&p); } };
struct FrameDeleter { void operator()(AVFrame* p) const { av_frame_free(&p); } };
struct PacketDeleter { void operator()(AVPacket* p) const { av_packet_free(&p); } };
struct SwsDeleter { void operator()(SwsContext* p) const { sws_freeContext(p); } };
struct SwrDeleter { void operator()(SwrContext* p) const { swr_free(&p); } };

using FormatPtr = std::unique_ptr<AVFormatContext, FormatDeleter>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using SwsPtr = std::unique_ptr<SwsContext, SwsDeleter>;
using SwrPtr = std::unique_ptr<SwrContext, SwrDeleter>;

inline FramePtr makeFrame() { return FramePtr(av_frame_alloc()); }
inline PacketPtr makePacket() { return PacketPtr(av_packet_alloc()); }

// Native path -> UTF-8 as expected by avformat_open_input (handles Thai paths on Windows).
inline QByteArray pathBytes(const QString& path) { return path.toUtf8(); }

// Opens a file and finds its streams. Returns null and fills `error` on failure.
inline FormatPtr openInput(const QString& path, QString* error)
{
    AVFormatContext* raw = nullptr;
    int rc = avformat_open_input(&raw, pathBytes(path).constData(), nullptr, nullptr);
    if (rc < 0) {
        if (error)
            *error = errorString(rc);
        return nullptr;
    }
    FormatPtr ctx(raw);
    rc = avformat_find_stream_info(ctx.get(), nullptr);
    if (rc < 0) {
        if (error)
            *error = errorString(rc);
        return nullptr;
    }
    return ctx;
}

// Opens a decoder for `stream`. Returns null and fills `error` on failure.
// Picks the Direct3D 11 surface format when the decoder offers it, otherwise the usual software format.
inline AVPixelFormat pickHardwareFormat(AVCodecContext* ctx, const AVPixelFormat* formats)
{
    for (const AVPixelFormat* p = formats; *p != AV_PIX_FMT_NONE; ++p)
        if (*p == AV_PIX_FMT_D3D11)
            return *p;
    return avcodec_default_get_format(ctx, formats);
}

// `hwDevice`: optional hardware device (not owned); the decoder then uses it for the codecs it supports.
inline CodecPtr openDecoder(AVStream* stream, QString* error, int threads = 0, AVBufferRef* hwDevice = nullptr)
{
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) {
        if (error)
            *error = QStringLiteral("No decoder for codec %1").arg(QString::fromUtf8(avcodec_get_name(stream->codecpar->codec_id)));
        return nullptr;
    }
    CodecPtr ctx(avcodec_alloc_context3(codec));
    avcodec_parameters_to_context(ctx.get(), stream->codecpar);
    ctx->pkt_timebase = stream->time_base;
    ctx->thread_count = threads;   // 0 = auto
    ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    if (hwDevice) {
        ctx->hw_device_ctx = av_buffer_ref(hwDevice);
        ctx->get_format = pickHardwareFormat;
        ctx->extra_hw_frames = 4;
    }
    const int rc = avcodec_open2(ctx.get(), codec, nullptr);
    if (rc < 0) {
        if (error)
            *error = errorString(rc);
        return nullptr;
    }
    return ctx;
}

} // namespace dv::ff
