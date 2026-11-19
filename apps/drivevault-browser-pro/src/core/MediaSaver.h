#pragma once

#include <QHash>
#include <QObject>
#include <QString>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

namespace dv::br::media {

// Saves the video / audio of a page into a folder - only when the user asked for it (a button). No WebEngine here.
//   file   a plain media file: downloaded by this program (redirects followed, written as <name>.part, renamed when complete)
//   hls    an HLS playlist, dash: a DASH manifest: the playlist is read first (DRM is refused, see MediaPolicy), then FFmpeg copies the pieces into an .mp4 (no re-encoding)
// The server is sent the page's address as Referer and the browser's user agent; the browser's cookies are NOT sent (a video behind a log-in cannot be saved).
class MediaSaver : public QObject {
    Q_OBJECT
public:
    struct Request {
        QString url, kind, pageUrl, userAgent, folder;
    };
    explicit MediaSaver(QObject* parent = nullptr);
    ~MediaSaver() override;

    void setFfmpegPath(const QString& path) { m_ffmpeg = path; }
    QString ffmpegPath() const;                                  // the one set, else found (see findFfmpeg), else ""
    static QString findFfmpeg(const QString& appDir);            // $DV_FFMPEG, <app>\ffmpeg\ffmpeg.exe, <app>\..\ffmpeg\bin, the suite's .tools\ffmpeg\bin (development)

    int start(const Request& r);                                  // the job id (> 0), or 0 when the request is refused (lastError says why)
    void cancel(int id);
    QString lastError() const { return m_lastError; }

signals:
    void started(int id, const QString& name, const QString& path);
    void progress(int id, qint64 received, qint64 total, const QString& unit);      // unit: "bytes" or "ms" (a stream: how much of the film was copied); total 0 = unknown
    void finished(int id, bool ok, const QString& error, const QString& path);

private:
    struct Job {
        int id = 0;
        Request req;
        QString path, part;
        QNetworkReply* reply = nullptr;
        QFile* file = nullptr;
        QProcess* proc = nullptr;
        qint64 received = 0, total = 0;
        double durationSec = 0;
        QByteArray manifest;
        QByteArray errText;
        bool cancelled = false, checkedType = false, isStream = false;
    };
    void startFile(Job& job);
    void startStream(Job& job);
    void runFfmpeg(int id);
    void finish(int id, bool ok, const QString& error);
    QString moveIntoPlace(Job& job);

    QNetworkAccessManager* m_net;
    QHash<int, Job> m_jobs;
    int m_next = 1;
    QString m_ffmpeg, m_lastError;
};

} // namespace dv::br::media
