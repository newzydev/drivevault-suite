#include "MediaProbe.h"

#include "FFmpeg.h"

#include <QFileInfo>
#include <QSet>

namespace dv {

namespace {
// Rotation (degrees, multiple of 90) from the stream's display matrix, 0 when absent.
int streamRotation(const AVStream* stream)
{
    const AVPacketSideData* sd = av_packet_side_data_get(stream->codecpar->coded_side_data,
                                                         stream->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
    if (!sd || sd->size < int(9 * sizeof(int32_t)))
        return 0;
    const double angle = av_display_rotation_get(reinterpret_cast<const int32_t*>(sd->data));
    if (std::isnan(angle))
        return 0;
    int deg = int(std::lround(-angle)) % 360;   // clockwise degrees needed to display upright
    if (deg < 0)
        deg += 360;
    return (deg / 90 * 90) % 360;
}
} // namespace

bool MediaProbe::looksLikeImage(const QString& path)
{
    static const QSet<QString> kImages = {"png", "jpg", "jpeg", "webp", "bmp", "tif", "tiff", "gif"};
    return kImages.contains(QFileInfo(path).suffix().toLower());
}

QString MediaProbe::engineDescription()
{
    return QStringLiteral("FFmpeg %1 (libavcodec %2.%3.%4)")
        .arg(QString::fromUtf8(av_version_info()))
        .arg(LIBAVCODEC_VERSION_MAJOR).arg(LIBAVCODEC_VERSION_MINOR).arg(LIBAVCODEC_VERSION_MICRO);
}

bool MediaProbe::probe(const QString& path, MediaInfo* out, QString* error)
{
    auto fail = [&](const QString& message) {
        if (error)
            *error = message;
        return false;
    };

    const QFileInfo file(path);
    if (!file.exists())
        return fail(tr("The file was not found."));
    if (!file.isFile() || !file.isReadable())
        return fail(tr("The file cannot be read."));
    if (file.size() == 0)
        return fail(tr("The file is empty."));

    QString ffError;
    ff::FormatPtr fmt = ff::openInput(path, &ffError);
    if (!fmt)
        return fail(tr("Unsupported or damaged file (%1).").arg(ffError));

    MediaInfo info;
    info.fileSize = file.size();
    info.modifiedMs = file.lastModified().toMSecsSinceEpoch();

    const int videoIndex = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const int audioIndex = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    if (videoIndex >= 0) {
        AVStream* vs = fmt->streams[videoIndex];
        // Cover art in audio files is exposed as a video stream; ignore it.
        const bool attachedPic = (vs->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0;
        if (!attachedPic) {
            const AVCodecParameters* par = vs->codecpar;
            info.hasVideo = true;
            info.width = par->width;
            info.height = par->height;
            if (streamRotation(vs) % 180 != 0)
                std::swap(info.width, info.height);
            info.videoCodec = QString::fromUtf8(avcodec_get_name(par->codec_id));
            const AVRational avg = vs->avg_frame_rate;
            const AVRational real = vs->r_frame_rate;
            if (avg.num > 0 && avg.den > 0) {
                info.fpsNum = avg.num;
                info.fpsDen = avg.den;
            } else if (real.num > 0 && real.den > 0) {
                info.fpsNum = real.num;
                info.fpsDen = real.den;
            }
            // Variable frame rate: average and nominal rates disagree noticeably.
            if (avg.num > 0 && real.num > 0)
                info.variableFrameRate = std::abs(av_q2d(avg) - av_q2d(real)) > 0.01 * av_q2d(real);
        }
    }
    if (audioIndex >= 0) {
        const AVCodecParameters* par = fmt->streams[audioIndex]->codecpar;
        info.hasAudio = true;
        info.audioRate = par->sample_rate;
        info.audioChannels = par->ch_layout.nb_channels;
        info.audioCodec = QString::fromUtf8(avcodec_get_name(par->codec_id));
    }

    if (!info.hasVideo && !info.hasAudio)
        return fail(tr("The file contains no video or audio the editor can use."));
    if (info.hasVideo && (info.width <= 0 || info.height <= 0))
        return fail(tr("The video has no valid dimensions."));

    // Duration: container first, then the longest stream.
    int64_t duration = fmt->duration > 0 ? av_rescale(fmt->duration, 1000000, AV_TIME_BASE) : 0;
    if (duration <= 0) {
        for (unsigned i = 0; i < fmt->nb_streams; ++i) {
            const AVStream* s = fmt->streams[i];
            if (s->duration > 0)
                duration = std::max(duration, av_rescale_q(s->duration, s->time_base, AVRational{1, 1000000}));
        }
    }

    const bool still = info.hasVideo && !info.hasAudio && (looksLikeImage(path) || (duration <= 0 && info.fpsNum == 0));
    if (still) {
        info.kind = QStringLiteral("image");
        info.durationUs = 0;
        info.fpsNum = 0;
        info.fpsDen = 1;
    } else if (info.hasVideo) {
        info.kind = QStringLiteral("video");
        info.durationUs = duration;
    } else {
        info.kind = QStringLiteral("audio");
        info.durationUs = duration;
    }
    if (!still && info.durationUs <= 0)
        return fail(tr("The duration of the file could not be determined."));

    *out = info;
    return true;
}

} // namespace dv
