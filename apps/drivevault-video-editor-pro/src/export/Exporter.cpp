#include "Exporter.h"

#include "core/FileUtils.h"
#include "core/Logger.h"
#include "media/FFmpeg.h"
#include "media/MediaProbe.h"
#include "media/VideoReader.h"
#include "playback/AudioMixer.h"
#include "playback/Compositor.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace dv {

namespace {

struct CandidateEncoder {
    const char* id;
    const char* label;
    bool hardware;
};
const CandidateEncoder kCandidates[] = {
    {"h264_nvenc", "NVIDIA NVENC (hardware)", true},
    {"h264_qsv", "Intel Quick Sync (hardware)", true},
    {"h264_amf", "AMD AMF (hardware)", true},
    {"h264_mf", "Windows Media Foundation", false},
    {"libopenh264", "OpenH264 (software)", false},
};

AVPixelFormat pickPixelFormat(const AVCodecContext* ctx, const AVCodec* codec)
{
    const void* configs = nullptr;
    int count = 0;
    if (avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &configs, &count) < 0 || !configs || count <= 0)
        return AV_PIX_FMT_YUV420P;
    const auto* fmts = static_cast<const AVPixelFormat*>(configs);
    for (AVPixelFormat want : {AV_PIX_FMT_YUV420P, AV_PIX_FMT_NV12}) {
        for (int i = 0; i < count; ++i)
            if (fmts[i] == want)
                return want;
    }
    return fmts[0];
}

// Opens one H.264 encoder with the given parameters; null (and `error`) when it is unavailable.
ff::CodecPtr openVideoEncoder(const char* name, int width, int height, const timecode::FrameRate& fps, int bitrateKbps,
                              bool globalHeader, QString* error, int gop = 0)
{
    const AVCodec* codec = avcodec_find_encoder_by_name(name);
    if (!codec) {
        if (error)
            *error = QStringLiteral("encoder not built in");
        return nullptr;
    }
    ff::CodecPtr ctx(avcodec_alloc_context3(codec));
    ctx->width = width;
    ctx->height = height;
    ctx->time_base = AVRational{int(fps.den), int(fps.num)};
    ctx->framerate = AVRational{int(fps.num), int(fps.den)};
    ctx->pix_fmt = pickPixelFormat(ctx.get(), codec);
    ctx->bit_rate = int64_t(bitrateKbps) * 1000;
    ctx->rc_max_rate = ctx->bit_rate * 3 / 2;
    ctx->rc_buffer_size = int(ctx->bit_rate);
    ctx->gop_size = gop > 0 ? gop : std::max(1, timecode::nominalFps(fps) * 2);
    ctx->max_b_frames = 0;
    ctx->colorspace = AVCOL_SPC_BT709;
    ctx->color_primaries = AVCOL_PRI_BT709;
    ctx->color_trc = AVCOL_TRC_BT709;
    ctx->color_range = AVCOL_RANGE_MPEG;
    if (globalHeader)
        ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    AVDictionary* opts = nullptr;
    const QString n = QString::fromLatin1(name);
    if (n == QLatin1String("h264_nvenc")) {
        av_dict_set(&opts, "preset", "p4", 0);
        av_dict_set(&opts, "rc", "vbr", 0);
    } else if (n == QLatin1String("libopenh264")) {
        av_dict_set(&opts, "allow_skip_frames", "0", 0);
    } else if (n == QLatin1String("h264_mf")) {
        av_dict_set(&opts, "rate_control", "cbr", 0);
        av_dict_set(&opts, "hw_encoding", "0", 0);
    }
    const int rc = avcodec_open2(ctx.get(), codec, &opts);
    av_dict_free(&opts);
    if (rc < 0) {
        if (error)
            *error = ff::errorString(rc);
        return nullptr;
    }
    return ctx;
}

// Sends one frame (or null to flush) and writes every packet that comes out.
bool encodeAndWrite(AVFormatContext* oc, AVCodecContext* enc, AVStream* stream, AVFrame* frame, QString* error)
{
    int rc = avcodec_send_frame(enc, frame);
    if (rc < 0 && rc != AVERROR_EOF) {
        if (error)
            *error = ff::errorString(rc);
        return false;
    }
    ff::PacketPtr pkt = ff::makePacket();
    for (;;) {
        rc = avcodec_receive_packet(enc, pkt.get());
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            return true;
        if (rc < 0) {
            if (error)
                *error = ff::errorString(rc);
            return false;
        }
        av_packet_rescale_ts(pkt.get(), enc->time_base, stream->time_base);
        pkt->stream_index = stream->index;
        rc = av_interleaved_write_frame(oc, pkt.get());
        if (rc < 0) {
            if (error)
                *error = ff::errorString(rc);
            return false;
        }
    }
}

QString formatNameFor(const QString& container)
{
    if (container == QLatin1String("mov"))
        return QStringLiteral("mov");
    if (container == QLatin1String("mkv"))
        return QStringLiteral("matroska");
    return QStringLiteral("mp4");
}

} // namespace

// ---- helpers -------------------------------------------------------------------------------------------------

bool Exporter::hasAudibleAudio(const RenderSnapshot& snapshot)
{
    for (const SnapshotClip& c : snapshot.clips) {
        if (c.trackIndex < 0 || c.trackIndex >= snapshot.tracks.size())
            continue;
        const TrackInfo& t = snapshot.tracks.at(c.trackIndex);
        if (t.kind == QLatin1String("audio") && !t.muted && c.online && c.media.hasAudio)
            return true;
    }
    return false;
}

QStringList Exporter::offlineMedia(const RenderSnapshot& snapshot)
{
    QStringList names;
    for (const SnapshotClip& c : snapshot.clips)
        if (!c.online) {
            const QString name = c.mediaPath.isEmpty() ? c.clip.mediaId : QFileInfo(c.mediaPath).fileName();
            if (!names.contains(name))
                names << name;
        }
    return names;
}

int Exporter::defaultBitrateKbps(int width, int height, double fps, int quality)
{
    // bits per pixel per frame: 0.04 (lowest) .. 0.20 (highest quality) for H.264
    const double bpp = 0.04 + 0.16 * std::clamp(quality, 0, 100) / 100.0;
    return std::max(500, int(width * height * fps * bpp / 1000.0));
}

QList<EncoderInfo> Exporter::availableEncoders()
{
    static QMutex mutex;
    static bool probed = false;
    static QList<EncoderInfo> result;
    QMutexLocker lock(&mutex);
    if (!probed) {
        for (const CandidateEncoder& c : kCandidates) {
            QString error;
            if (openVideoEncoder(c.id, 640, 360, {30, 1}, 2000, false, &error))
                result.append({QString::fromLatin1(c.id), QString::fromUtf8(c.label), c.hardware});
            else
                qCInfo(lcProject) << "Encoder" << c.id << "not available:" << error;
        }
        probed = true;
    }
    return result;
}

// ---- validation ------------------------------------------------------------------------------------------------

ExportValidation Exporter::validate(const QString& path, const RenderSnapshot& snapshot, const ExportSettings& settings)
{
    ExportValidation v;
    const int64_t rangeStart = std::clamp<int64_t>(settings.startFrame, 0, snapshot.durationFrames);
    const int64_t rangeEnd = settings.endFrame < 0 ? snapshot.durationFrames
                                                   : std::clamp<int64_t>(settings.endFrame, rangeStart, snapshot.durationFrames);
    v.expectedDurationUs = timecode::frameToMicroseconds(rangeEnd - rangeStart, snapshot.fps);
    const bool expectAudio = hasAudibleAudio(snapshot);

    MediaInfo info;
    QString error;
    if (!MediaProbe::probe(path, &info, &error)) {
        v.problems << QStringLiteral("The output file cannot be read back: %1").arg(error);
        return v;
    }
    v.fileSize = info.fileSize;
    v.width = info.width;
    v.height = info.height;
    v.fps = info.fpsDen > 0 ? double(info.fpsNum) / double(info.fpsDen) : 0;
    v.durationUs = info.durationUs;
    v.hasAudio = info.hasAudio;
    v.audioRate = info.audioRate;
    v.videoCodec = info.videoCodec;
    v.audioCodec = info.audioCodec;

    if (settings.audioOnly) {
        if (info.hasVideo)
            v.problems << QStringLiteral("The audio export contains a video stream.");
    } else if (!info.hasVideo) {
        v.problems << QStringLiteral("The output has no video stream.");
    }
    if (!settings.audioOnly && (info.width != settings.width || info.height != settings.height))
        v.problems << QStringLiteral("Resolution is %1x%2, expected %3x%4.").arg(info.width).arg(info.height).arg(settings.width).arg(settings.height);
    if (!settings.audioOnly && std::abs(v.fps - snapshot.fps.toDouble()) > 0.05)
        v.problems << QStringLiteral("Frame rate is %1, expected %2.").arg(v.fps).arg(snapshot.fps.toDouble());
    const qint64 frameUs = qint64(1e6 / std::max(1.0, snapshot.fps.toDouble()));
    const qint64 tolerance = std::max<qint64>(2 * frameUs, 60000);
    if (std::abs(info.durationUs - v.expectedDurationUs) > tolerance)
        v.problems << QStringLiteral("Duration is %1 ms, expected %2 ms.").arg(info.durationUs / 1000).arg(v.expectedDurationUs / 1000);
    if (expectAudio && !info.hasAudio)
        v.problems << QStringLiteral("The sequence has audio but the output does not.");
    if (!expectAudio && info.hasAudio)
        v.problems << QStringLiteral("The output has audio although the sequence is silent.");
    if (info.hasAudio && info.audioRate != snapshot.sampleRate)
        v.problems << QStringLiteral("Audio sample rate is %1, expected %2.").arg(info.audioRate).arg(snapshot.sampleRate);

    // The picture must be decodable: read the first and the last frame back.
    VideoReader reader;
    if (settings.audioOnly) {
        // nothing to decode on the picture side
    } else if (!reader.open(path, &error)) {
        v.problems << QStringLiteral("The output video cannot be decoded: %1").arg(error);
    } else {
        const QSize probeSize(std::min(info.width, 320) & ~1, std::min(info.height, 180) & ~1);
        if (reader.frameAt(0, probeSize).isNull())
            v.problems << QStringLiteral("The first frame of the output cannot be decoded.");
        else if (reader.frameAt(std::max<qint64>(0, v.expectedDurationUs - 2 * frameUs), probeSize).isNull())
            v.problems << QStringLiteral("The last frame of the output cannot be decoded.");
    }
    v.ok = v.problems.isEmpty();
    return v;
}

// ---- proxy -------------------------------------------------------------------------------------------------------

QString Exporter::buildProxy(const QString& src, const QString& dst, int maxWidth, const Progress& progress, const std::atomic_bool& cancel)
{
    MediaInfo info;
    QString error;
    if (!MediaProbe::probe(src, &info, &error))
        return error;
    if (!info.hasVideo || info.fpsNum <= 0 || info.fpsDen <= 0)
        return QStringLiteral("The media has no video.");
    if (info.variableFrameRate)
        return QStringLiteral("Variable frame rate media cannot get a proxy (the timing would drift).");
    const timecode::FrameRate fps{info.fpsNum, info.fpsDen};
    VideoReader reader;
    if (!reader.open(src, &error))
        return error;
    const QSize disp = reader.displaySize();
    int w = std::min(std::max(16, maxWidth & ~1), disp.width() & ~1);
    int h = std::max(16, int(std::lround(double(w) * disp.height() / std::max(1, disp.width()))) & ~1);
    w = std::max(16, w);
    const int64_t total = std::max<int64_t>(1, int64_t(std::llround(double(info.durationUs) / 1e6 * fps.toDouble())));

    QDir().mkpath(QFileInfo(dst).absolutePath());
    const QString temp = dst + QStringLiteral(".dvpart");
    QFile::remove(temp);
    AVFormatContext* rawOc = nullptr;
    if (avformat_alloc_output_context2(&rawOc, nullptr, "mp4", nullptr) < 0 || !rawOc)
        return QStringLiteral("The output format is not available.");
    struct OcCloser {
        AVFormatContext* oc;
        ~OcCloser()
        {
            if (oc->pb)
                avio_closep(&oc->pb);
            avformat_free_context(oc);
        }
    } closer{rawOc};
    AVFormatContext* oc = rawOc;
    const bool globalHeader = (oc->oformat->flags & AVFMT_GLOBALHEADER) != 0;

    ff::CodecPtr venc;
    QStringList order;
    for (const EncoderInfo& e : availableEncoders())
        order << e.id;
    if (order.isEmpty())
        for (const CandidateEncoder& c : kCandidates)
            order << QString::fromLatin1(c.id);
    const int bitrate = std::max(800, int(w * h * fps.toDouble() * 0.12 / 1000.0));
    for (const QString& name : std::as_const(order)) {
        venc = openVideoEncoder(name.toLatin1().constData(), w, h, fps, bitrate, globalHeader, nullptr, 6);
        if (venc)
            break;
    }
    if (!venc)
        return QStringLiteral("No H.264 encoder could be opened.");
    AVStream* vstream = avformat_new_stream(oc, nullptr);
    avcodec_parameters_from_context(vstream->codecpar, venc.get());
    vstream->time_base = venc->time_base;
    vstream->avg_frame_rate = venc->framerate;
    if (int rc = avio_open(&oc->pb, temp.toUtf8().constData(), AVIO_FLAG_WRITE); rc < 0)
        return QStringLiteral("Cannot create the proxy file: %1").arg(ff::errorString(rc));
    if (int rc = avformat_write_header(oc, nullptr); rc < 0) {
        avio_closep(&oc->pb);
        QFile::remove(temp);
        return QStringLiteral("Cannot write the proxy header: %1").arg(ff::errorString(rc));
    }
    ff::FramePtr yuv = ff::makeFrame();
    yuv->format = venc->pix_fmt;
    yuv->width = w;
    yuv->height = h;
    av_frame_get_buffer(yuv.get(), 0);
    ff::SwsPtr sws(sws_getContext(w, h, AV_PIX_FMT_BGRA, w, h, venc->pix_fmt, SWS_BILINEAR, nullptr, nullptr, nullptr));
    if (!sws) {
        avio_closep(&oc->pb);
        QFile::remove(temp);
        return QStringLiteral("Cannot create the colour converter.");
    }
    sws_setColorspaceDetails(sws.get(), sws_getCoefficients(SWS_CS_DEFAULT), 1, sws_getCoefficients(SWS_CS_ITU709), 0, 0, 1 << 16, 1 << 16);

    auto fail = [&](const QString& message) {
        if (oc->pb)
            avio_closep(&oc->pb);
        QFile::remove(temp);
        return message;
    };
    const QSize size(w, h);
    for (int64_t f = 0; f < total; ++f) {
        if (cancel.load())
            return fail(QStringLiteral("cancelled"));
        // +1 ms keeps the lookup inside the frame even with rounding of the source timestamps
        const QImage image = reader.frameAt(timecode::frameToMicroseconds(f, fps) + 1000, size);
        if (image.isNull() || image.size() != size)
            return fail(QStringLiteral("Decoding frame %1 failed.").arg(f));
        const QImage bgra = image.format() == QImage::Format_RGB32 || image.format() == QImage::Format_ARGB32 ? image : image.convertToFormat(QImage::Format_ARGB32);
        av_frame_make_writable(yuv.get());
        const uint8_t* srcData[4] = {bgra.constBits(), nullptr, nullptr, nullptr};
        const int srcStride[4] = {int(bgra.bytesPerLine()), 0, 0, 0};
        sws_scale(sws.get(), srcData, srcStride, 0, h, yuv->data, yuv->linesize);
        yuv->pts = f;
        yuv->colorspace = AVCOL_SPC_BT709;
        yuv->color_range = AVCOL_RANGE_MPEG;
        if (!encodeAndWrite(oc, venc.get(), vstream, yuv.get(), &error))
            return fail(QStringLiteral("Proxy encoding failed: %1").arg(error));
        if (progress)
            progress(f + 1, total);
    }
    if (!encodeAndWrite(oc, venc.get(), vstream, nullptr, &error))
        return fail(QStringLiteral("Proxy encoding failed: %1").arg(error));
    if (int rc = av_write_trailer(oc); rc < 0)
        return fail(QStringLiteral("Cannot finish the proxy: %1").arg(ff::errorString(rc)));
    avio_closep(&oc->pb);
    QFile::remove(dst);
    if (!QFile::rename(temp, dst)) {
        QFile::remove(temp);
        return QStringLiteral("Cannot move the proxy into place.");
    }
    return {};
}

// ---- single frame ------------------------------------------------------------------------------------------------

QString Exporter::exportFrame(const RenderSnapshot& snapshot, int64_t frame, QSize size, const QString& path)
{
    if (snapshot.durationFrames <= 0)
        return QStringLiteral("The timeline is empty.");
    if (size.width() < 16 || size.height() < 16)
        return QStringLiteral("The image size is invalid.");
    Compositor compositor;
    const QImage image = compositor.render(snapshot, std::clamp<int64_t>(frame, 0, snapshot.durationFrames - 1), size);
    if (image.isNull())
        return QStringLiteral("Rendering the frame failed.");
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return QStringLiteral("Cannot create the output folder.");
    const QString temp = path + QStringLiteral(".dvpart");
    QFile::remove(temp);
    if (!image.save(temp, "PNG"))
        return QStringLiteral("Cannot write the image.");
    QFile::remove(path);
    if (!QFile::rename(temp, path)) {
        QFile::remove(temp);
        return QStringLiteral("Cannot move the finished image to %1.").arg(QDir::toNativeSeparators(path));
    }
    return {};
}

// ---- audio only --------------------------------------------------------------------------------------------------

ExportResult Exporter::runAudioOnly(SnapshotPtr snapshot, const ExportSettings& settings, const Progress& progress,
                                    const std::atomic_bool& cancel)
{
    ExportResult result;
    result.outputPath = settings.outputPath;
    auto fail = [&](const QString& message) {
        result.ok = false;
        result.error = message;
        return result;
    };
    const RenderSnapshot& snap = *snapshot;
    if (!hasAudibleAudio(snap))
        return fail(QStringLiteral("The sequence has no audible audio to export."));
    const int64_t rangeStart = std::clamp<int64_t>(settings.startFrame, 0, snap.durationFrames);
    const int64_t rangeEnd = settings.endFrame < 0 ? snap.durationFrames
                                                   : std::clamp<int64_t>(settings.endFrame, rangeStart, snap.durationFrames);
    const int64_t totalFrames = rangeEnd - rangeStart;
    if (totalFrames <= 0)
        return fail(QStringLiteral("The export range is empty."));
    const int64_t sampleBase = timecode::frameToSample(rangeStart, snap.fps, snap.sampleRate);
    const int64_t totalSamples = timecode::frameToSample(totalFrames, snap.fps, snap.sampleRate);

    QElapsedTimer wall;
    wall.start();
    if (!QDir().mkpath(QFileInfo(settings.outputPath).absolutePath()))
        return fail(QStringLiteral("Cannot create the output folder."));
    const QString tempPath = settings.outputPath + QStringLiteral(".dvpart");
    QFile::remove(tempPath);

    AVFormatContext* oc = nullptr;
    if (avformat_alloc_output_context2(&oc, nullptr, "ipod", nullptr) < 0 || !oc)
        return fail(QStringLiteral("The output format is not available."));
    struct OcCloser {
        AVFormatContext* oc;
        ~OcCloser()
        {
            if (oc->pb)
                avio_closep(&oc->pb);
            avformat_free_context(oc);
        }
    } closer{oc};

    const AVCodec* acodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!acodec)
        return fail(QStringLiteral("The AAC encoder is not available."));
    ff::CodecPtr aenc(avcodec_alloc_context3(acodec));
    aenc->sample_rate = snap.sampleRate;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    av_channel_layout_copy(&aenc->ch_layout, &stereo);
    aenc->sample_fmt = AV_SAMPLE_FMT_FLTP;
    aenc->bit_rate = int64_t(settings.audioBitrateKbps) * 1000;
    aenc->time_base = AVRational{1, snap.sampleRate};
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        aenc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (const int rc = avcodec_open2(aenc.get(), acodec, nullptr); rc < 0)
        return fail(QStringLiteral("Cannot open the AAC encoder: %1").arg(ff::errorString(rc)));
    AVStream* astream = avformat_new_stream(oc, nullptr);
    avcodec_parameters_from_context(astream->codecpar, aenc.get());
    astream->time_base = aenc->time_base;

    if (int rc = avio_open(&oc->pb, tempPath.toUtf8().constData(), AVIO_FLAG_WRITE); rc < 0)
        return fail(QStringLiteral("Cannot create the output file: %1").arg(ff::errorString(rc)));
    if (int rc = avformat_write_header(oc, nullptr); rc < 0) {
        avio_closep(&oc->pb);
        QFile::remove(tempPath);
        return fail(QStringLiteral("Cannot write the file header: %1").arg(ff::errorString(rc)));
    }

    AudioMixer mixer;
    mixer.setSnapshot(snapshot);
    constexpr int kChunk = 1024;
    std::vector<float> interleaved(size_t(kChunk) * 2);
    ff::FramePtr aframe = ff::makeFrame();
    aframe->format = AV_SAMPLE_FMT_FLTP;
    aframe->nb_samples = kChunk;
    aframe->sample_rate = snap.sampleRate;
    av_channel_layout_copy(&aframe->ch_layout, &aenc->ch_layout);
    av_frame_get_buffer(aframe.get(), 0);

    auto abort = [&](const QString& message, bool cancelled) {
        if (oc->pb)
            avio_closep(&oc->pb);
        QFile::remove(tempPath);
        result.ok = false;
        result.cancelled = cancelled;
        result.error = message;
        return result;
    };

    QString error;
    int64_t written = 0;
    int64_t lastReport = -1;
    while (written < totalSamples) {
        if (cancel.load())
            return abort(QStringLiteral("Export cancelled."), true);
        mixer.render(sampleBase + written, kChunk, interleaved.data());
        av_frame_make_writable(aframe.get());
        auto* left = reinterpret_cast<float*>(aframe->extended_data[0]);
        auto* right = reinterpret_cast<float*>(aframe->extended_data[1]);
        for (int i = 0; i < kChunk; ++i) {
            left[i] = interleaved[size_t(i) * 2];
            right[i] = interleaved[size_t(i) * 2 + 1];
        }
        aframe->pts = written;
        if (!encodeAndWrite(oc, aenc.get(), astream, aframe.get(), &error))
            return abort(QStringLiteral("Audio encoding failed: %1").arg(error), false);
        written += kChunk;
        const int64_t framesDone = std::min(totalFrames, written * totalFrames / std::max<int64_t>(1, totalSamples));
        if (progress && framesDone != lastReport) {
            progress(framesDone, totalFrames);
            lastReport = framesDone;
        }
    }
    if (!encodeAndWrite(oc, aenc.get(), astream, nullptr, &error))
        return abort(QStringLiteral("Audio encoding failed: %1").arg(error), false);
    if (int rc = av_write_trailer(oc); rc < 0)
        return abort(QStringLiteral("Cannot finish the file: %1").arg(ff::errorString(rc)), false);
    avio_closep(&oc->pb);

    result.encoder = QStringLiteral("aac");
    result.validation = validate(tempPath, snap, settings);
    result.frames = totalFrames;
    result.seconds = double(wall.elapsed()) / 1000.0;
    result.throughputFps = result.seconds > 0 ? double(totalFrames) / result.seconds : 0;
    if (!result.validation.ok)
        return abort(QStringLiteral("The exported file did not pass validation: %1").arg(result.validation.problems.join(QStringLiteral(" "))), false);
    QFile::remove(settings.outputPath);
    if (!QFile::rename(tempPath, settings.outputPath))
        return abort(QStringLiteral("Cannot move the finished file to %1.").arg(QDir::toNativeSeparators(settings.outputPath)), false);
    result.ok = true;
    return result;
}

// ---- export ------------------------------------------------------------------------------------------------------

ExportResult Exporter::run(SnapshotPtr snapshot, const ExportSettings& settingsIn, const Progress& progress,
                           const std::atomic_bool& cancel)
{
    ExportResult result;
    result.outputPath = settingsIn.outputPath;
    auto fail = [&](const QString& message) {
        result.ok = false;
        result.error = message;
        return result;
    };
    if (!snapshot || snapshot->clips.isEmpty() || snapshot->durationFrames <= 0)
        return fail(QStringLiteral("The timeline is empty."));
    if (const QStringList offline = offlineMedia(*snapshot); !offline.isEmpty())
        return fail(QStringLiteral("Media files are missing: %1").arg(offline.join(QStringLiteral(", "))));

    ExportSettings settings = settingsIn;
    settings.width = std::max(16, settings.width & ~1);
    settings.height = std::max(16, settings.height & ~1);
    const RenderSnapshot& snap = *snapshot;
    if (settings.audioOnly)
        return runAudioOnly(snapshot, settings, progress, cancel);
    const int64_t rangeStart = std::clamp<int64_t>(settings.startFrame, 0, snap.durationFrames);
    const int64_t rangeEnd = settings.endFrame < 0 ? snap.durationFrames
                                                   : std::clamp<int64_t>(settings.endFrame, rangeStart, snap.durationFrames);
    const int64_t totalFrames = rangeEnd - rangeStart;
    if (totalFrames <= 0)
        return fail(QStringLiteral("The export range is empty."));
    const int64_t sampleBase = timecode::frameToSample(rangeStart, snap.fps, snap.sampleRate);
    const bool withAudio = hasAudibleAudio(snap);
    const int bitrate = settings.videoBitrateKbps > 0
                            ? settings.videoBitrateKbps
                            : defaultBitrateKbps(settings.width, settings.height, snap.fps.toDouble(), settings.quality);

    QElapsedTimer wall;
    wall.start();

    if (!QDir().mkpath(QFileInfo(settings.outputPath).absolutePath()))
        return fail(QStringLiteral("Cannot create the output folder."));
    const QString tempPath = settings.outputPath + QStringLiteral(".dvpart");
    QFile::remove(tempPath);

    AVFormatContext* rawOc = nullptr;
    const QByteArray fmtName = formatNameFor(settings.container).toLatin1();
    if (avformat_alloc_output_context2(&rawOc, nullptr, fmtName.constData(), nullptr) < 0 || !rawOc)
        return fail(QStringLiteral("The output format is not available."));
    struct OcCloser {
        AVFormatContext* oc;
        ~OcCloser()
        {
            if (oc) {
                if (oc->pb)
                    avio_closep(&oc->pb);
                avformat_free_context(oc);
            }
        }
    } closer{rawOc};
    AVFormatContext* oc = rawOc;
    const bool globalHeader = (oc->oformat->flags & AVFMT_GLOBALHEADER) != 0;

    // ---- video encoder: the requested one, or the first that opens in Auto mode ------------------------------
    ff::CodecPtr venc;
    QStringList triedErrors;
    QStringList order;
    if (settings.encoder != QLatin1String("auto")) {
        order << settings.encoder;
    } else {
        for (const EncoderInfo& e : availableEncoders())
            order << e.id;
        if (order.isEmpty())
            for (const CandidateEncoder& c : kCandidates)
                order << QString::fromLatin1(c.id);
    }
    for (const QString& name : std::as_const(order)) {
        QString error;
        venc = openVideoEncoder(name.toLatin1().constData(), settings.width, settings.height, snap.fps, bitrate, globalHeader, &error);
        if (venc) {
            result.encoder = name;
            break;
        }
        triedErrors << QStringLiteral("%1: %2").arg(name, error);
    }
    if (!venc)
        return fail(QStringLiteral("No H.264 encoder could be opened (%1).").arg(triedErrors.join(QStringLiteral("; "))));

    AVStream* vstream = avformat_new_stream(oc, nullptr);
    avcodec_parameters_from_context(vstream->codecpar, venc.get());
    vstream->time_base = venc->time_base;
    vstream->avg_frame_rate = venc->framerate;

    // ---- audio encoder ------------------------------------------------------------------------------------------
    ff::CodecPtr aenc;
    AVStream* astream = nullptr;
    if (withAudio) {
        const AVCodec* acodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!acodec)
            return fail(QStringLiteral("The AAC encoder is not available."));
        aenc.reset(avcodec_alloc_context3(acodec));
        aenc->sample_rate = snap.sampleRate;
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        av_channel_layout_copy(&aenc->ch_layout, &stereo);
        aenc->sample_fmt = AV_SAMPLE_FMT_FLTP;
        aenc->bit_rate = int64_t(settings.audioBitrateKbps) * 1000;
        aenc->time_base = AVRational{1, snap.sampleRate};
        if (globalHeader)
            aenc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        const int rc = avcodec_open2(aenc.get(), acodec, nullptr);
        if (rc < 0)
            return fail(QStringLiteral("Cannot open the AAC encoder: %1").arg(ff::errorString(rc)));
        astream = avformat_new_stream(oc, nullptr);
        avcodec_parameters_from_context(astream->codecpar, aenc.get());
        astream->time_base = aenc->time_base;
    }

    if (int rc = avio_open(&oc->pb, tempPath.toUtf8().constData(), AVIO_FLAG_WRITE); rc < 0)
        return fail(QStringLiteral("Cannot create the output file: %1").arg(ff::errorString(rc)));
    if (int rc = avformat_write_header(oc, nullptr); rc < 0) {
        avio_closep(&oc->pb);
        QFile::remove(tempPath);
        return fail(QStringLiteral("Cannot write the file header: %1").arg(ff::errorString(rc)));
    }

    // ---- render loop --------------------------------------------------------------------------------------------
    Compositor compositor;
    AudioMixer mixer;
    mixer.setSnapshot(snapshot);

    ff::FramePtr yuv = ff::makeFrame();
    yuv->format = venc->pix_fmt;
    yuv->width = settings.width;
    yuv->height = settings.height;
    av_frame_get_buffer(yuv.get(), 0);

    ff::SwsPtr sws(sws_getContext(settings.width, settings.height, AV_PIX_FMT_BGRA, settings.width, settings.height, venc->pix_fmt,
                                  SWS_BICUBIC, nullptr, nullptr, nullptr));
    if (!sws) {
        avio_closep(&oc->pb);
        QFile::remove(tempPath);
        return fail(QStringLiteral("Cannot create the colour converter."));
    }
    // RGB (full range) -> BT.709 limited range YUV, matching the tags written to the stream.
    sws_setColorspaceDetails(sws.get(), sws_getCoefficients(SWS_CS_DEFAULT), 1, sws_getCoefficients(SWS_CS_ITU709), 0, 0, 1 << 16, 1 << 16);

    constexpr int kAudioChunk = 1024;
    std::vector<float> interleaved(size_t(kAudioChunk) * 2);
    ff::FramePtr aframe;
    if (withAudio) {
        aframe = ff::makeFrame();
        aframe->format = AV_SAMPLE_FMT_FLTP;
        aframe->nb_samples = kAudioChunk;
        aframe->sample_rate = snap.sampleRate;
        av_channel_layout_copy(&aframe->ch_layout, &aenc->ch_layout);
        av_frame_get_buffer(aframe.get(), 0);
    }
    int64_t audioWritten = 0;
    const int64_t totalSamples = timecode::frameToSample(totalFrames, snap.fps, snap.sampleRate);

    auto writeAudioUpTo = [&](int64_t untilSample, QString* error) {
        while (withAudio && audioWritten < untilSample) {
            mixer.render(sampleBase + audioWritten, kAudioChunk, interleaved.data());
            av_frame_make_writable(aframe.get());
            auto* left = reinterpret_cast<float*>(aframe->extended_data[0]);
            auto* right = reinterpret_cast<float*>(aframe->extended_data[1]);
            for (int i = 0; i < kAudioChunk; ++i) {
                left[i] = interleaved[size_t(i) * 2];
                right[i] = interleaved[size_t(i) * 2 + 1];
            }
            aframe->pts = audioWritten;
            if (!encodeAndWrite(oc, aenc.get(), astream, aframe.get(), error))
                return false;
            audioWritten += kAudioChunk;
        }
        return true;
    };

    auto abort = [&](const QString& message, bool cancelled) {
        if (oc->pb)
            avio_closep(&oc->pb);   // the file must be closed before it can be deleted on Windows
        QFile::remove(tempPath);
        result.ok = false;
        result.cancelled = cancelled;
        result.error = message;
        return result;
    };

    const QSize outSize(settings.width, settings.height);
    for (int64_t f = 0; f < totalFrames; ++f) {
        if (cancel.load())
            return abort(QStringLiteral("Export cancelled."), true);

        const QImage image = compositor.render(snap, rangeStart + f, outSize);
        if (image.isNull() || image.size() != outSize)
            return abort(QStringLiteral("Rendering frame %1 failed.").arg(f), false);

        av_frame_make_writable(yuv.get());
        const uint8_t* srcData[4] = {image.constBits(), nullptr, nullptr, nullptr};
        const int srcStride[4] = {int(image.bytesPerLine()), 0, 0, 0};
        sws_scale(sws.get(), srcData, srcStride, 0, settings.height, yuv->data, yuv->linesize);
        yuv->pts = f;
        yuv->colorspace = AVCOL_SPC_BT709;
        yuv->color_range = AVCOL_RANGE_MPEG;

        QString error;
        if (!encodeAndWrite(oc, venc.get(), vstream, yuv.get(), &error))
            return abort(QStringLiteral("Video encoding failed: %1").arg(error), false);
        if (!writeAudioUpTo(timecode::frameToSample(f + 1, snap.fps, snap.sampleRate), &error))
            return abort(QStringLiteral("Audio encoding failed: %1").arg(error), false);

        if (progress)
            progress(f + 1, totalFrames);
    }

    QString error;
    if (!writeAudioUpTo(totalSamples, &error))
        return abort(QStringLiteral("Audio encoding failed: %1").arg(error), false);
    if (!encodeAndWrite(oc, venc.get(), vstream, nullptr, &error))
        return abort(QStringLiteral("Video encoding failed: %1").arg(error), false);
    if (withAudio && !encodeAndWrite(oc, aenc.get(), astream, nullptr, &error))
        return abort(QStringLiteral("Audio encoding failed: %1").arg(error), false);
    if (int rc = av_write_trailer(oc); rc < 0)
        return abort(QStringLiteral("Cannot finish the file: %1").arg(ff::errorString(rc)), false);
    avio_closep(&oc->pb);

    // Validate the temporary file before it replaces anything.
    ExportSettings check = settings;
    result.validation = validate(tempPath, snap, check);
    result.frames = totalFrames;
    result.seconds = double(wall.elapsed()) / 1000.0;
    result.throughputFps = result.seconds > 0 ? double(totalFrames) / result.seconds : 0;
    if (!result.validation.ok)
        return abort(QStringLiteral("The exported file did not pass validation: %1").arg(result.validation.problems.join(QStringLiteral(" "))), false);

    QFile::remove(settings.outputPath);
    if (!QFile::rename(tempPath, settings.outputPath))
        return abort(QStringLiteral("Cannot move the finished file to %1.").arg(QDir::toNativeSeparators(settings.outputPath)), false);

    result.ok = true;
    return result;
}

} // namespace dv
