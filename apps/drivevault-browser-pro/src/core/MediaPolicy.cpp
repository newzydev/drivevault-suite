#include "MediaPolicy.h"

#include "UrlUtil.h"

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace dv::br::media {

namespace {

QString pathOf(const QString& url)
{
    return QUrl(url).path(QUrl::FullyDecoded).toLower();
}

bool looksLikeStreamPiece(const QString& path)
{
    static const QRegularExpression piece(QStringLiteral(R"((^|[/_.-])(init|seg|segment|chunk|frag|fragment)[-_.]?\d*\.(mp4|m4s|webm)$)"));
    return piece.match(path).hasMatch();
}

} // namespace

QString classifyMediaUrl(const QString& url, bool fromMediaElement)
{
    const QUrl u(url);
    if (u.scheme() != QLatin1String("http") && u.scheme() != QLatin1String("https"))
        return {};
    const QString path = pathOf(url);
    if (path.endsWith(QLatin1String(".m3u8")))
        return QStringLiteral("hls");
    if (path.endsWith(QLatin1String(".mpd")))
        return QStringLiteral("dash");
    for (const char* piece : {".ts", ".m4s", ".cmfv", ".cmfa", ".cmft", ".vtt", ".webvtt", ".srt", ".key"})
        if (path.endsWith(QLatin1String(piece)))
            return {};                                        // a piece of a stream, a subtitle or a key
    static const QSet<QString> files = {QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("webm"), QStringLiteral("mov"), QStringLiteral("mkv"), QStringLiteral("ogv"),
                                        QStringLiteral("mp3"), QStringLiteral("m4a"), QStringLiteral("ogg"), QStringLiteral("oga"), QStringLiteral("opus"), QStringLiteral("wav"),
                                        QStringLiteral("flac"), QStringLiteral("aac")};
    const QString ext = QFileInfo(path).suffix();
    if (files.contains(ext)) {
        if (looksLikeStreamPiece(path))
            return {};
        // a request for one part of a file made by a player (byte range in the address) is not "the file"
        const QString query = u.query().toLower();
        if (query.contains(QLatin1String("range=")) || query.contains(QLatin1String("bytestart=")) || query.contains(QLatin1String("byterange=")))
            return {};
        return QStringLiteral("file");
    }
    return fromMediaElement && ext.isEmpty() && !path.isEmpty() && path != QLatin1String("/") ? QStringLiteral("file") : QString();
}

bool hostIsExcluded(const QString& host)
{
    static const QStringList excluded = {
        // video platforms whose terms forbid saving
        QStringLiteral("youtube.com"), QStringLiteral("youtu.be"), QStringLiteral("googlevideo.com"), QStringLiteral("ytimg.com"), QStringLiteral("youtube-nocookie.com"),
        // subscription streaming (DRM and terms)
        QStringLiteral("netflix.com"), QStringLiteral("nflxvideo.net"), QStringLiteral("nflximg.net"), QStringLiteral("disneyplus.com"), QStringLiteral("disney-plus.net"),
        QStringLiteral("hulu.com"), QStringLiteral("hulustream.com"), QStringLiteral("primevideo.com"), QStringLiteral("aiv-cdn.net"), QStringLiteral("max.com"), QStringLiteral("hbomax.com"),
        QStringLiteral("peacocktv.com"), QStringLiteral("paramountplus.com"), QStringLiteral("tv.apple.com"), QStringLiteral("viu.com"), QStringLiteral("iq.com"), QStringLiteral("iqiyi.com"),
        // music streaming
        QStringLiteral("spotify.com"), QStringLiteral("scdn.co"), QStringLiteral("music.apple.com"), QStringLiteral("deezer.com"), QStringLiteral("tidal.com"),
    };
    const QString h = host.toLower();
    for (const QString& d : excluded)
        if (h == d || h.endsWith(QLatin1Char('.') + d))
            return true;
    return false;
}

QString mediaFileName(const QString& url, const QString& kind, const QString& contentType)
{
    QString name = QFileInfo(QUrl(url).path(QUrl::FullyDecoded)).fileName();
    if (kind == QLatin1String("hls") || kind == QLatin1String("dash")) {
        name = QFileInfo(name).completeBaseName();
        if (name.isEmpty() || name.toLower() == QLatin1String("index") || name.toLower() == QLatin1String("master") || name.toLower() == QLatin1String("playlist"))
            name = QUrl(url).host().replace(QLatin1Char('.'), QLatin1Char('-')) + QStringLiteral("-video");
        return dv::br::safeFileName(name + QStringLiteral(".mp4"));
    }
    if (name.isEmpty())
        name = QUrl(url).host().replace(QLatin1Char('.'), QLatin1Char('-')) + QStringLiteral("-media");
    if (QFileInfo(name).suffix().isEmpty() && !contentType.trimmed().isEmpty()) {
        const QString ct = contentType.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
        static const QHash<QString, QString> byType = {
            {QStringLiteral("video/mp4"), QStringLiteral("mp4")}, {QStringLiteral("video/webm"), QStringLiteral("webm")}, {QStringLiteral("video/ogg"), QStringLiteral("ogv")},
            {QStringLiteral("video/quicktime"), QStringLiteral("mov")}, {QStringLiteral("audio/mpeg"), QStringLiteral("mp3")}, {QStringLiteral("audio/mp4"), QStringLiteral("m4a")},
            {QStringLiteral("audio/ogg"), QStringLiteral("ogg")}, {QStringLiteral("audio/wav"), QStringLiteral("wav")}, {QStringLiteral("audio/webm"), QStringLiteral("weba")},
            {QStringLiteral("audio/aac"), QStringLiteral("aac")}, {QStringLiteral("audio/flac"), QStringLiteral("flac")}};
        name += QLatin1Char('.') + byType.value(ct, QStringLiteral("bin"));
    }
    return dv::br::safeFileName(name);
}

bool hlsDownloadable(const QByteArray& playlist, QString* reason)
{
    const QString text = QString::fromUtf8(playlist);
    if (!text.trimmed().startsWith(QLatin1String("#EXTM3U"))) {
        *reason = QStringLiteral("this is not an HLS playlist");
        return false;
    }
    static const QRegularExpression key(QStringLiteral(R"(^#EXT-X-(?:SESSION-)?KEY:(.*)$)"), QRegularExpression::MultilineOption);
    auto it = key.globalMatch(text);
    while (it.hasNext()) {
        const QString attrs = it.next().captured(1);
        const QString up = attrs.toUpper();
        if (up.contains(QLatin1String("METHOD=NONE")))
            continue;
        if (up.contains(QLatin1String("SAMPLE-AES")) || up.contains(QLatin1String("KEYFORMAT=\"COM.APPLE")) || up.contains(QLatin1String("SKD://")) || up.contains(QLatin1String("WIDEVINE"))
            || up.contains(QLatin1String("PLAYREADY")) || up.contains(QLatin1String("URN:UUID")) || up.contains(QLatin1String("DATA:"))) {
            *reason = QStringLiteral("the stream is protected by DRM (a key system): it is never saved");
            return false;
        }
        if (up.contains(QLatin1String("KEYFORMAT=")) && !up.contains(QLatin1String("KEYFORMAT=\"IDENTITY\""))) {
            *reason = QStringLiteral("the stream uses a key format that is not the plain one: it is never saved");
            return false;
        }
    }
    return true;
}

bool dashDownloadable(const QByteArray& manifest, QString* reason)
{
    const QString text = QString::fromUtf8(manifest);
    if (!text.contains(QLatin1String("<MPD"), Qt::CaseSensitive)) {
        *reason = QStringLiteral("this is not a DASH manifest");
        return false;
    }
    if (text.contains(QLatin1String("<ContentProtection"), Qt::CaseInsensitive)) {
        *reason = QStringLiteral("the stream is protected by DRM (ContentProtection): it is never saved");
        return false;
    }
    return true;
}

double hlsDurationSeconds(const QByteArray& playlist)
{
    const QString text = QString::fromUtf8(playlist);
    if (text.contains(QLatin1String("#EXT-X-STREAM-INF")) || !text.contains(QLatin1String("#EXT-X-ENDLIST")))
        return 0;                                            // a master playlist, or a live stream
    static const QRegularExpression inf(QStringLiteral(R"(#EXTINF:([0-9.]+))"));
    double sum = 0;
    auto it = inf.globalMatch(text);
    while (it.hasNext())
        sum += it.next().captured(1).toDouble();
    return sum;
}

} // namespace dv::br::media
