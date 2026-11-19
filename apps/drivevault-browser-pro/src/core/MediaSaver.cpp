#include "MediaSaver.h"

#include "MediaPolicy.h"
#include "UrlUtil.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QUrl>

namespace dv::br::media {

namespace {
constexpr qint64 kMaxManifestBytes = 2 * 1024 * 1024;

QNetworkRequest makeRequest(const QUrl& url, const QString& pageUrl, const QString& userAgent)
{
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(60000);
    if (!pageUrl.isEmpty())
        req.setRawHeader("Referer", pageUrl.toUtf8());
    if (!userAgent.isEmpty())
        req.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
    req.setRawHeader("Accept", "*/*");
    return req;
}
} // namespace

MediaSaver::MediaSaver(QObject* parent) : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

MediaSaver::~MediaSaver()
{
    for (Job& j : m_jobs) {
        if (j.reply) {
            j.reply->disconnect(this);
            j.reply->abort();
        }
        if (j.proc) {
            j.proc->disconnect(this);
            j.proc->kill();
            j.proc->waitForFinished(2000);
        }
        if (j.file) {
            j.file->close();
            QFile::remove(j.part);
        }
    }
}

QString MediaSaver::findFfmpeg(const QString& appDir)
{
    const QString env = qEnvironmentVariable("DV_FFMPEG");
    if (!env.isEmpty() && QFileInfo::exists(env))
        return env;
    for (const QString& c : {appDir + QStringLiteral("/ffmpeg/ffmpeg.exe"), appDir + QStringLiteral("/ffmpeg.exe"),
                             appDir + QStringLiteral("/../../../../.tools/ffmpeg/bin/ffmpeg.exe"), appDir + QStringLiteral("/../../../../../.tools/ffmpeg/bin/ffmpeg.exe")})
        if (QFileInfo::exists(c))
            return QFileInfo(c).canonicalFilePath();
    return {};
}

QString MediaSaver::ffmpegPath() const
{
    return m_ffmpeg.isEmpty() ? findFfmpeg(QCoreApplication::applicationDirPath()) : m_ffmpeg;
}

int MediaSaver::start(const Request& r)
{
    const QUrl url(r.url);
    if (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")) {
        m_lastError = QStringLiteral("only http and https addresses can be saved");
        return 0;
    }
    if (r.folder.isEmpty() || !QDir().mkpath(r.folder)) {
        m_lastError = QStringLiteral("the folder could not be made");
        return 0;
    }
    Job job;
    job.id = m_next++;
    job.req = r;
    job.isStream = r.kind == QLatin1String("hls") || r.kind == QLatin1String("dash");
    const QString name = uniqueFileName(r.folder, mediaFileName(r.url, r.kind));
    job.path = QDir(r.folder).filePath(name);
    job.part = job.path + QStringLiteral(".part");
    if (!job.isStream) {
        job.file = new QFile(job.part, this);
        if (!job.file->open(QIODevice::WriteOnly)) {
            m_lastError = QStringLiteral("the file could not be created");
            delete job.file;
            return 0;
        }
    } else {
        QFile touch(job.part);                                   // reserves the name
        if (!touch.open(QIODevice::WriteOnly)) {
            m_lastError = QStringLiteral("the file could not be created");
            return 0;
        }
    }
    const int id = job.id;
    m_jobs.insert(id, job);
    emit started(id, name, job.path);
    if (m_jobs[id].isStream)
        startStream(m_jobs[id]);
    else
        startFile(m_jobs[id]);
    return id;
}

void MediaSaver::startFile(Job& job)
{
    const int id = job.id;
    QNetworkReply* reply = m_net->get(makeRequest(QUrl(job.req.url), job.req.pageUrl, job.req.userAgent));
    job.reply = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, id, reply] {
        auto it = m_jobs.find(id);
        if (it == m_jobs.end())
            return;
        Job& j = it.value();
        if (!j.checkedType) {
            j.checkedType = true;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
            if ((status != 0 && status != 200 && status != 206) || type.startsWith(QLatin1String("text/")) || type.startsWith(QLatin1String("application/json"))) {
                j.errText = status != 200 && status != 206 ? QByteArray("HTTP ") + QByteArray::number(status) : QByteArray("the server did not send a media file");
                reply->abort();
                return;
            }
            j.total = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
            if (QFileInfo(j.path).suffix().isEmpty()) {            // no extension in the address: take it from the content type
                const QString better = QDir(j.req.folder).filePath(uniqueFileName(j.req.folder, mediaFileName(j.req.url, j.req.kind, type)));
                j.path = better;
            }
        }
        const QByteArray chunk = reply->readAll();
        if (j.file && j.file->write(chunk) != chunk.size()) {
            j.errText = "the disk is full or not writable";
            reply->abort();
            return;
        }
        j.received += chunk.size();
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, id](qint64 got, qint64 total) {
        const auto it = m_jobs.constFind(id);
        if (it != m_jobs.constEnd())
            emit progress(id, got, total > 0 ? total : it->total, QStringLiteral("bytes"));
    });
    connect(reply, &QNetworkReply::finished, this, [this, id, reply] {
        reply->deleteLater();
        auto it = m_jobs.find(id);
        if (it == m_jobs.end())
            return;
        Job& j = it.value();
        j.reply = nullptr;
        if (j.cancelled) {
            finish(id, false, QStringLiteral("cancelled"));
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (!j.errText.isEmpty()) {
            finish(id, false, QString::fromUtf8(j.errText));
        } else if (reply->error() != QNetworkReply::NoError) {
            finish(id, false, status != 0 && status != 200 && status != 206 ? QStringLiteral("HTTP %1").arg(status) : reply->errorString());
        } else if (j.received == 0) {
            finish(id, false, QStringLiteral("the server sent nothing"));
        } else {
            finish(id, true, QString());
        }
    });
}

void MediaSaver::startStream(Job& job)
{
    const int id = job.id;
    QNetworkReply* reply = m_net->get(makeRequest(QUrl(job.req.url), job.req.pageUrl, job.req.userAgent));
    job.reply = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, id, reply] {
        auto it = m_jobs.find(id);
        if (it == m_jobs.end())
            return;
        it->manifest += reply->readAll();
        if (it->manifest.size() > kMaxManifestBytes) {
            it->errText = "the playlist is too big";
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, id, reply] {
        reply->deleteLater();
        auto it = m_jobs.find(id);
        if (it == m_jobs.end())
            return;
        Job& j = it.value();
        j.reply = nullptr;
        if (j.cancelled) {
            finish(id, false, QStringLiteral("cancelled"));
            return;
        }
        if (!j.errText.isEmpty()) {
            finish(id, false, QString::fromUtf8(j.errText));
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            finish(id, false, status != 0 && status != 200 ? QStringLiteral("HTTP %1").arg(status) : reply->errorString());
            return;
        }
        QString reason;
        const bool hls = j.req.kind == QLatin1String("hls");
        if (!(hls ? hlsDownloadable(j.manifest, &reason) : dashDownloadable(j.manifest, &reason))) {
            finish(id, false, reason);                              // DRM or not a playlist: FFmpeg is never started
            return;
        }
        if (hls)
            j.durationSec = hlsDurationSeconds(j.manifest);
        runFfmpeg(id);
    });
}

void MediaSaver::runFfmpeg(int id)
{
    Job& j = m_jobs[id];
    const QString ffmpeg = ffmpegPath();
    if (ffmpeg.isEmpty()) {
        finish(id, false, QStringLiteral("FFmpeg was not found (needed to save a stream)"));
        return;
    }
    auto* proc = new QProcess(this);
    j.proc = proc;
    QStringList args = {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-nostdin"), QStringLiteral("-y"),
                        QStringLiteral("-protocol_whitelist"), QStringLiteral("http,https,tcp,tls,crypto"),
                        QStringLiteral("-rw_timeout"), QStringLiteral("30000000")};
    if (!j.req.userAgent.isEmpty())
        args << QStringLiteral("-user_agent") << j.req.userAgent;
    if (!j.req.pageUrl.isEmpty())
        args << QStringLiteral("-headers") << QStringLiteral("Referer: %1\r\n").arg(j.req.pageUrl);
    args << QStringLiteral("-i") << j.req.url << QStringLiteral("-c") << QStringLiteral("copy") << QStringLiteral("-f") << QStringLiteral("mp4")
         << QStringLiteral("-progress") << QStringLiteral("pipe:1") << QStringLiteral("-nostats") << j.part;
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, id, proc] {
        const auto it = m_jobs.constFind(id);
        if (it == m_jobs.constEnd())
            return;
        const QList<QByteArray> lines = proc->readAllStandardOutput().split('\n');
        for (const QByteArray& line : lines)
            if (line.startsWith("out_time_us=")) {
                const qint64 ms = line.mid(12).trimmed().toLongLong() / 1000;
                if (ms > 0)
                    emit progress(id, ms, qint64(it->durationSec * 1000.0), QStringLiteral("ms"));
            }
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this, id, proc] {
        const auto it = m_jobs.find(id);
        if (it != m_jobs.end()) {
            it->errText += proc->readAllStandardError();
            if (it->errText.size() > 4000)
                it->errText = it->errText.right(4000);
        }
    });
    connect(proc, &QProcess::errorOccurred, this, [this, id](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            finish(id, false, QStringLiteral("FFmpeg could not be started"));
    });
    connect(proc, &QProcess::finished, this, [this, id](int code, QProcess::ExitStatus) {
        const auto it = m_jobs.constFind(id);
        if (it == m_jobs.constEnd())
            return;
        if (it->cancelled)
            finish(id, false, QStringLiteral("cancelled"));
        else if (code == 0 && QFileInfo(it->part).size() > 0)
            finish(id, true, QString());
        else
            finish(id, false, QString::fromUtf8(it->errText).trimmed().section(QLatin1Char('\n'), -1).left(300).isEmpty() ? QStringLiteral("FFmpeg failed") : QString::fromUtf8(it->errText).trimmed().section(QLatin1Char('\n'), -1).left(300));
    });
    proc->start(ffmpeg, args);
}

QString MediaSaver::moveIntoPlace(Job& job)
{
    QString target = job.path;
    if (QFileInfo::exists(target))                                   // somebody made a file of that name meanwhile
        target = QDir(job.req.folder).filePath(uniqueFileName(job.req.folder, QFileInfo(job.path).fileName()));
    return QFile::rename(job.part, target) ? target : QString();
}

void MediaSaver::finish(int id, bool ok, const QString& error)
{
    auto it = m_jobs.find(id);
    if (it == m_jobs.end())
        return;
    Job job = it.value();
    m_jobs.erase(it);
    if (job.file) {
        job.file->close();
        job.file->deleteLater();
    }
    if (job.proc) {
        job.proc->disconnect(this);
        if (job.proc->state() != QProcess::NotRunning) {
            job.proc->kill();
            job.proc->waitForFinished(2000);
        }
        job.proc->deleteLater();
    }
    if (job.reply) {
        job.reply->disconnect(this);
        job.reply->abort();
        job.reply->deleteLater();
    }
    QString path = job.path;
    if (ok) {
        path = moveIntoPlace(job);
        if (path.isEmpty()) {
            ok = false;
            QFile::remove(job.part);
            emit finished(id, false, QStringLiteral("the file could not be completed"), job.path);
            return;
        }
    } else {
        QFile::remove(job.part);
    }
    emit finished(id, ok, error, path);
}

void MediaSaver::cancel(int id)
{
    auto it = m_jobs.find(id);
    if (it == m_jobs.end())
        return;
    it->cancelled = true;
    if (it->reply)
        it->reply->abort();
    else if (it->proc)
        it->proc->kill();
    else
        finish(id, false, QStringLiteral("cancelled"));
}

} // namespace dv::br::media
