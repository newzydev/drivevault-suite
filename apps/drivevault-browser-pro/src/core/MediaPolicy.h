#pragma once

#include <QString>

// Which requests of a page are offered as "save this video", and what may be saved. No WebEngine here.
//   * only plain files (mp4, webm, mp3 ...) and HLS / DASH playlists of a page are listed; pieces of a stream (.ts, .m4s ...) are not
//   * subscription streaming services and video platforms whose terms forbid saving are never listed (the owner's decision 9.4 of the plan: YouTube and the like are OUT)
//   * DRM is never touched: a playlist that says it is encrypted with a key system (SAMPLE-AES, FairPlay, ContentProtection) is refused
namespace dv::br::media {

// "file", "hls", "dash" or "" (not a media request). `fromMediaElement`: the engine said the request came from a <video> / <audio> element.
QString classifyMediaUrl(const QString& url, bool fromMediaElement);

// true for the hosts that are never offered (video platforms, subscription streaming, music services)
bool hostIsExcluded(const QString& host);

// A good file name for the download: the last part of the address (made safe), an extension from the content type when it has none.
QString mediaFileName(const QString& url, const QString& kind, const QString& contentType = QString());

// Playlists: may this one be saved? On "no", `reason` says why (a short English text for the log / the status line).
bool hlsDownloadable(const QByteArray& playlist, QString* reason);
bool dashDownloadable(const QByteArray& manifest, QString* reason);
// the length in seconds of a media playlist (sum of #EXTINF), 0 when unknown (master playlist, live)
double hlsDurationSeconds(const QByteArray& playlist);

} // namespace dv::br::media
