#include "ModelDownloader.h"


#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QStorageInfo>
#include <QUrl>

#include <algorithm>

namespace dm::ai {

ModelDownloader::ModelDownloader(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

ModelDownloader::~ModelDownloader()
{
    m_cancelFlag = true;
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
    if (m_file) {
        m_file->close();
        delete m_file;
    }
    if (m_worker.joinable())
        m_worker.join();
}

void ModelDownloader::setCode(const QString& code, const QString& message)
{
    m_code = code;
    m_message = message;
    emit changed();
}

QString ModelDownloader::downloadFile(const QString& url, const QString& folder, const QString& name, qint64 size, const QString& sha256)
{
    if (m_busy)
        return tr("A download is already running.");
    m_url = url;
    m_folder = QDir::cleanPath(folder);
    m_name = name;
    m_size = size;
    m_sha = sha256;
    if (!QDir().mkpath(m_folder + QStringLiteral("/.part")))
        return tr("The folder %1 cannot be created.").arg(QDir::toNativeSeparators(m_folder));
    const QFileInfo target(m_folder + QLatin1Char('/') + m_name);
    if (target.exists() && target.size() == m_size) {
        m_progress = 1;
        setCode(QStringLiteral("installed"));
        emit finished(true, m_message);
        return {};
    }
    const QFileInfo part(m_folder + QStringLiteral("/.part/") + m_name + QStringLiteral(".part"));
    const qint64 have = part.exists() && part.size() <= m_size ? part.size() : 0;
    const qint64 free = QStorageInfo(m_folder).bytesAvailable();
    const qint64 needed = m_size - have + 256LL * 1024 * 1024;
    if (free >= 0 && free < needed)
        return tr("There is not enough free disk space: %1 MB needed, %2 MB free.").arg(needed / 1048576).arg(free / 1048576);
    m_cancelled = false;
    m_cancelFlag = false;
    m_busy = true;
    m_progress = 0;
    setCode(QStringLiteral("starting"));
    startRequest();
    return {};
}

void ModelDownloader::cancel()
{
    if (!m_busy)
        return;
    m_cancelled = true;
    m_cancelFlag = true;
    if (m_reply)
        m_reply->abort();
}

void ModelDownloader::startRequest()
{
    const QUrl url(m_url);
    const bool loopback = url.scheme() == QLatin1String("http") && (url.host() == QLatin1String("127.0.0.1") || url.host() == QLatin1String("localhost"));
    if (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("file") && !loopback) {
        fail(tr("Only https downloads are allowed."));
        return;
    }
    const QString part = m_folder + QStringLiteral("/.part/") + m_name + QStringLiteral(".part");
    const QFileInfo pi(part);
    m_have = pi.exists() ? pi.size() : 0;
    if (m_have > m_size) {
        QFile::remove(part);
        m_have = 0;
    }
    m_file = new QFile(part);
    if (!m_file->open(QIODevice::WriteOnly | QIODevice::Append)) {
        const QString why = m_file->errorString();
        delete m_file;
        m_file = nullptr;
        fail(tr("The file %1 cannot be written: %2").arg(QDir::toNativeSeparators(part), why));
        return;
    }
    m_rangeChecked = false;
    m_progress = m_size > 0 ? double(m_have) / double(m_size) : 0;
    emit changed();
    if (m_have == m_size) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
        verify();
        return;
    }
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    if (m_have > 0)
        req.setRawHeader("Range", "bytes=" + QByteArray::number(m_have) + "-");
    setCode(QStringLiteral("downloading"));
    m_reply = m_net->get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, &ModelDownloader::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &ModelDownloader::onReplyFinished);
}

void ModelDownloader::onReadyRead()
{
    if (!m_reply || !m_file)
        return;
    if (!m_rangeChecked) {
        m_rangeChecked = true;                       // a server that ignores Range sends 200 and the whole file again
        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (m_have > 0 && status != 206) {
            m_file->resize(0);
            m_have = 0;
        }
    }
    const QByteArray data = m_reply->readAll();
    if (m_file->write(data) != data.size()) {
        const QString why = m_file->errorString();
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
        m_file->close();
        delete m_file;
        m_file = nullptr;
        fail(tr("Writing the file failed (is the disk full?): %1").arg(why));
        return;
    }
    m_have += data.size();
    m_progress = m_size > 0 ? std::clamp(double(m_have) / double(m_size), 0.0, 1.0) : 0;
    emit changed();
}

void ModelDownloader::onReplyFinished()
{
    QNetworkReply* reply = m_reply;
    if (!reply)
        return;
    onReadyRead();
    if (!m_reply)
        return;
    m_reply = nullptr;
    reply->deleteLater();
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    if (m_cancelled) {
        m_busy = false;
        setCode(QStringLiteral("cancelled"));
        emit finished(false, m_message);
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        fail(tr("The download failed: %1. What was downloaded is kept; press Download to continue.").arg(reply->errorString()));
        return;
    }
    if (m_have != m_size) {
        fail(tr("The download ended early (%1 of %2 MB). Press Download to continue.").arg(m_have / 1048576).arg(m_size / 1048576));
        return;
    }
    verify();
}

void ModelDownloader::verify()
{
    setCode(QStringLiteral("checking"));
    if (m_worker.joinable())
        m_worker.join();
    const QString part = m_folder + QStringLiteral("/.part/") + m_name + QStringLiteral(".part");
    const QString target = m_folder + QLatin1Char('/') + m_name;
    const QString expected = m_sha, name = m_name;
    QPointer<ModelDownloader> self(this);
    std::atomic_bool* cancelFlag = &m_cancelFlag;
    m_worker = std::thread([self, part, target, expected, name, cancelFlag] {
        bool ok = true;
        QString error;
        const QString sha = sha256Of(part, cancelFlag);
        if (cancelFlag->load()) {
            ok = false;
        } else if (sha != expected) {
            QFile::remove(part);
            ok = false;
            error = QCoreApplication::translate("dm::ai::ModelDownloader", "The file is damaged or is not the expected model (checksum). It was deleted; press Download to try again.");
        } else {
            QFile::remove(target);
            ok = QFile::rename(part, target);
            if (!ok)
                error = QCoreApplication::translate("dm::ai::ModelDownloader", "The model could not be moved into place.");
        }
        QObject* object = self.data();
        if (!object)
            return;
        QMetaObject::invokeMethod(object, [self, ok, error] {
            if (!self)
                return;
            if (self->m_cancelled) {
                self->m_busy = false;
                self->setCode(QStringLiteral("cancelled"));
                emit self->finished(false, self->m_message);
            } else if (!ok) {
                self->fail(error);
            } else {
                self->m_busy = false;
                self->m_progress = 1;
                QDir(self->m_folder).rmdir(QStringLiteral(".part"));
                self->setCode(QStringLiteral("installed"));
                emit self->finished(true, self->m_message);
            }
        }, Qt::QueuedConnection);
    });
}

void ModelDownloader::fail(const QString& message)
{
    m_busy = false;
    setCode(QStringLiteral("failed"), message);
    emit finished(false, message);
}

QString ModelDownloader::sha256Of(const QString& path, const std::atomic_bool* cancel)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buf(4 * 1024 * 1024, Qt::Uninitialized);
    for (;;) {
        if (cancel && cancel->load())
            return {};
        const qint64 n = f.read(buf.data(), buf.size());
        if (n < 0)
            return {};
        if (n == 0)
            break;
        hash.addData(QByteArrayView(buf.constData(), n));
    }
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace dm::ai
