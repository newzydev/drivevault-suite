#include "OpenThaiInstaller.h"

#include "ai/OpenThaiAsr.h"
#include "core/AppPaths.h"
#include "core/Logger.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStorageInfo>
#include <QUrl>

#include <algorithm>

namespace dv {

namespace {

quint32 readLe32(const QByteArray& b, int at) { return quint32(quint8(b[at])) | quint32(quint8(b[at + 1])) << 8 | quint32(quint8(b[at + 2])) << 16 | quint32(quint8(b[at + 3])) << 24; }
quint16 readLe16(const QByteArray& b, int at) { return quint16(quint8(b[at])) | quint16(quint8(b[at + 1])) << 8; }
} // namespace

OpenThaiInstaller::OpenThaiInstaller(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

OpenThaiInstaller::~OpenThaiInstaller()
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

QString OpenThaiInstaller::sizeText() const { return tr("about 2.6 GB"); }

// One pinned revision of huggingface.co/iapp/OpenThai2.0-ASR-Streaming-ONNX (fp32 export): sizes and SHA-256 are fixed here, so a changed or damaged
// file is refused instead of being used.
QList<OpenThaiInstaller::Item> OpenThaiInstaller::defaultItems()
{
    const QString base = QStringLiteral("https://huggingface.co/iapp/OpenThai2.0-ASR-Streaming-ONNX/resolve/9f142a43e4d35a422c1c7dac40772ef707387ebc/");
    QList<Item> items;
    items.append({QStringLiteral("LICENSE"), base + QStringLiteral("LICENSE"), 2619,
                  QStringLiteral("116493ed1369502b5e29cc559a8fa009a371bae926e769038ebb09885b091221"), {}});
    items.append({QStringLiteral("openthai2.0-asr-streaming-fp32.asr.zip"), base + QStringLiteral("openthai2.0-asr-streaming-fp32.asr.zip"), 149365631,
                  QStringLiteral("d4161d14e1c61c779ae01cf02356a99d2752f7931ce2d04be8098c57270d463f"),
                  {{QStringLiteral("encoder.onnx"), 41941207}, {QStringLiteral("decoder_joint.onnx"), 107330778}, {QStringLiteral("tokens.txt"), 93160}}});
    items.append({QStringLiteral("encoder.onnx.data"), base + QStringLiteral("encoder.onnx.data"), 2454405120,
                  QStringLiteral("a139201333ab89d5e965bc12abb1c534aa285d82e40051c2dda605365c74f18b"), {}});
    return items;
}

QString OpenThaiInstaller::partPath(const Item& item) const
{
    return QDir(m_folder).filePath(QStringLiteral(".part/") + item.name + QStringLiteral(".part"));
}

bool OpenThaiInstaller::itemPresent(const Item& item) const
{
    if (!item.unpack.isEmpty()) {
        for (auto it = item.unpack.cbegin(); it != item.unpack.cend(); ++it) {
            const QFileInfo fi(QDir(m_folder).filePath(it.key()));
            if (!fi.exists() || fi.size() != it.value())
                return false;
        }
        return true;
    }
    const QFileInfo fi(QDir(m_folder).filePath(item.name));
    return fi.exists() && fi.size() == item.size;
}

void OpenThaiInstaller::setMessage(const QString& m)
{
    m_message = m;
    emit changed();
}

void OpenThaiInstaller::updateProgress()
{
    m_progress = m_totalBytes > 0 ? std::clamp(double(m_doneBytes + m_have) / double(m_totalBytes), 0.0, 1.0) : 1.0;
    emit changed();
}

QString OpenThaiInstaller::download()
{
    return downloadItems(defaultItems(), paths::dataDir() + QStringLiteral("/models/openthai-asr"));
}

QString OpenThaiInstaller::downloadItems(const QList<Item>& items, const QString& folder)
{
    if (m_busy)
        return tr("A download is already running.");
    m_items = items;
    m_folder = QDir::cleanPath(folder);
    if (!QDir().mkpath(m_folder + QStringLiteral("/.part")))
        return tr("The folder %1 cannot be created.").arg(QDir::toNativeSeparators(m_folder));

    qint64 total = 0, onDisk = 0;
    for (const Item& item : std::as_const(m_items)) {
        if (itemPresent(item))
            continue;
        total += item.size;
        const QFileInfo part(partPath(item));
        if (part.exists() && part.size() <= item.size)
            onDisk += part.size();
    }
    const qint64 needed = total - onDisk + 256LL * 1024 * 1024;
    const qint64 free = QStorageInfo(m_folder).bytesAvailable();
    if (total > 0 && free >= 0 && free < needed)
        return tr("There is not enough free disk space: %1 MB needed, %2 MB free.").arg(needed / 1048576).arg(free / 1048576);

    m_totalBytes = total;
    m_doneBytes = 0;
    m_have = 0;
    m_index = -1;
    m_cancelled = false;
    m_cancelFlag = false;
    m_busy = true;
    m_progress = 0;
    setMessage(tr("Starting…"));
    startNext();
    return {};
}

void OpenThaiInstaller::cancel()
{
    if (!m_busy)
        return;
    m_cancelled = true;
    m_cancelFlag = true;
    if (m_reply)
        m_reply->abort();     // onReplyFinished() ends the job; the part file stays for a later continue
}

void OpenThaiInstaller::startNext()
{
    for (++m_index; m_index < m_items.size() && itemPresent(m_items.at(m_index)); ++m_index) {
    }
    if (m_index >= m_items.size()) {
        succeed();
        return;
    }
    startItem();
}

void OpenThaiInstaller::startItem()
{
    const Item& item = m_items.at(m_index);
    const QUrl url(item.url);
    const bool loopback = url.scheme() == QLatin1String("http") && (url.host() == QLatin1String("127.0.0.1") || url.host() == QLatin1String("localhost"));
    if (url.scheme() != QLatin1String("https") && url.scheme() != QLatin1String("file") && !loopback) {      // plain http only for this computer (tests)
        fail(tr("Only https downloads are allowed."));
        return;
    }
    const QString part = partPath(item);
    QFileInfo pi(part);
    m_have = pi.exists() ? pi.size() : 0;
    if (m_have > item.size) {
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
    m_restarted = false;
    updateProgress();
    if (m_have == item.size) {          // a complete part from an earlier run
        m_file->close();
        delete m_file;
        m_file = nullptr;
        verifyAndPlace();
        return;
    }
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    if (m_have > 0)
        req.setRawHeader("Range", "bytes=" + QByteArray::number(m_have) + "-");
    setMessage(tr("Downloading %1…").arg(item.name));
    m_reply = m_net->get(req);
    connect(m_reply, &QNetworkReply::readyRead, this, &OpenThaiInstaller::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &OpenThaiInstaller::onReplyFinished);
}

void OpenThaiInstaller::onReadyRead()
{
    if (!m_reply || !m_file)
        return;
    if (!m_restarted) {
        m_restarted = true;             // the first data decides: a server that ignores Range sends 200 and the whole file again
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
    updateProgress();
}

void OpenThaiInstaller::onReplyFinished()
{
    QNetworkReply* reply = m_reply;
    if (!reply)
        return;
    onReadyRead();                      // whatever is left
    if (!m_reply)
        return;                         // a write error ended the job
    m_reply = nullptr;
    reply->deleteLater();
    if (m_file) {
        m_file->close();
        delete m_file;
        m_file = nullptr;
    }
    if (m_cancelled) {
        m_busy = false;
        setMessage(tr("Cancelled. What was downloaded is kept; press Download to continue."));
        emit finished(false, m_message);
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        fail(tr("The download failed: %1. What was downloaded is kept; press Download to continue.").arg(reply->errorString()));
        return;
    }
    if (m_have != m_items.at(m_index).size) {
        fail(tr("The download ended early (%1 of %2 MB). Press Download to continue.").arg(m_have / 1048576).arg(m_items.at(m_index).size / 1048576));
        return;
    }
    verifyAndPlace();
}

// Checksum, then move the file into place (or unpack the zip), on a worker thread.
void OpenThaiInstaller::verifyAndPlace()
{
    const Item item = m_items.at(m_index);
    const QString part = partPath(item), folder = m_folder;
    setMessage(tr("Checking %1…").arg(item.name));
    if (m_worker.joinable())
        m_worker.join();
    QPointer<OpenThaiInstaller> self(this);
    std::atomic_bool* cancelFlag = &m_cancelFlag;
    m_worker = std::thread([self, item, part, folder, cancelFlag] {
        bool ok = true;
        QString error;
        const QString sha = sha256Of(part, cancelFlag);
        if (cancelFlag->load()) {
            ok = false;
        } else if (sha != item.sha256) {
            QFile::remove(part);
            ok = false;
            error = QCoreApplication::translate("dv::OpenThaiInstaller", "%1 is damaged or is not the expected file (checksum). It was deleted; press Download to try again.").arg(item.name);
        } else if (!item.unpack.isEmpty()) {
            ok = extractStoredZip(part, folder, item.unpack, &error);
            if (ok)
                QFile::remove(part);
        } else {
            const QString target = QDir(folder).filePath(item.name);
            QFile::remove(target);
            ok = QFile::rename(part, target);
            if (!ok)
                error = QCoreApplication::translate("dv::OpenThaiInstaller", "%1 could not be moved into place.").arg(item.name);
        }
        QObject* target = self.data();
        if (!target)
            return;
        QMetaObject::invokeMethod(target, [self, ok, error, size = item.size] {
            if (!self)
                return;
            if (self->m_cancelled) {
                self->m_busy = false;
                self->setMessage(QCoreApplication::translate("dv::OpenThaiInstaller", "Cancelled. What was downloaded is kept; press Download to continue."));
                emit self->finished(false, self->m_message);
            } else if (!ok) {
                self->fail(error);
            } else {
                self->m_doneBytes += size;
                self->m_have = 0;
                self->updateProgress();
                self->startNext();
            }
        }, Qt::QueuedConnection);
    });
}

void OpenThaiInstaller::fail(const QString& message)
{
    qCWarning(lcApp) << "OpenThai model download:" << message;
    m_busy = false;
    setMessage(message);
    emit finished(false, message);
}

void OpenThaiInstaller::succeed()
{
    if (!ai::openThaiModelComplete(m_folder) && m_totalBytes > 0 && QFileInfo::exists(QDir(m_folder).filePath(QStringLiteral("tokens.txt")))) {
        fail(tr("The model files are incomplete."));
        return;
    }
    QDir(m_folder).rmdir(QStringLiteral(".part"));        // only when it is empty
    m_busy = false;
    m_progress = 1.0;
    setMessage(tr("The OpenThai speech model is installed."));
    emit finished(true, m_message);
}

QString OpenThaiInstaller::sha256Of(const QString& path, const std::atomic_bool* cancel)
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

// A zip that stores its files without compression (the OpenThai .asr.zip does): read the central directory, copy the wanted files out.
bool OpenThaiInstaller::extractStoredZip(const QString& zipPath, const QString& outDir, const QMap<QString, qint64>& wanted, QString* error)
{
    QFile f(zipPath);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QCoreApplication::translate("dv::OpenThaiInstaller", "The archive cannot be read.");
        return false;
    }
    const qint64 size = f.size();
    const qint64 tailLen = std::min<qint64>(size, 22 + 65535);
    f.seek(size - tailLen);
    const QByteArray tail = f.read(tailLen);
    int eocd = -1;
    for (int i = int(tail.size()) - 22; i >= 0; --i) {
        if (readLe32(tail, i) == 0x06054b50u) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        *error = QCoreApplication::translate("dv::OpenThaiInstaller", "The archive is not a zip file.");
        return false;
    }
    const quint32 cdSize = readLe32(tail, eocd + 12), cdOffset = readLe32(tail, eocd + 16);
    if (cdSize == 0xFFFFFFFFu || cdOffset == 0xFFFFFFFFu || qint64(cdOffset) + cdSize > size) {
        *error = QCoreApplication::translate("dv::OpenThaiInstaller", "The archive layout is not supported.");
        return false;
    }
    f.seek(cdOffset);
    const QByteArray cd = f.read(cdSize);
    QMap<QString, qint64> done;
    for (int pos = 0; pos + 46 <= cd.size() && readLe32(cd, pos) == 0x02014b50u;) {
        const quint16 method = readLe16(cd, pos + 10);
        const quint32 csize = readLe32(cd, pos + 20), usize = readLe32(cd, pos + 24);
        const quint16 nameLen = readLe16(cd, pos + 28), extraLen = readLe16(cd, pos + 30), commentLen = readLe16(cd, pos + 32);
        const quint32 localOffset = readLe32(cd, pos + 42);
        const QString name = QString::fromUtf8(cd.mid(pos + 46, nameLen));
        pos += 46 + nameLen + extraLen + commentLen;
        if (!wanted.contains(name))                          // only the listed plain file names; nothing with a path can leave the folder
            continue;
        if (method != 0 || csize != usize || qint64(usize) != wanted.value(name)) {
            *error = QCoreApplication::translate("dv::OpenThaiInstaller", "%1 in the archive is not stored as expected.").arg(name);
            return false;
        }
        f.seek(localOffset);
        const QByteArray local = f.read(30);
        if (local.size() < 30 || readLe32(local, 0) != 0x04034b50u) {
            *error = QCoreApplication::translate("dv::OpenThaiInstaller", "The archive is damaged.");
            return false;
        }
        f.seek(qint64(localOffset) + 30 + readLe16(local, 26) + readLe16(local, 28));
        const QString tmp = QDir(outDir).filePath(name + QStringLiteral(".tmp"));
        QFile out(tmp);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            *error = QCoreApplication::translate("dv::OpenThaiInstaller", "%1 cannot be written.").arg(name);
            return false;
        }
        qint64 left = usize;
        QByteArray buf(4 * 1024 * 1024, Qt::Uninitialized);
        while (left > 0) {
            const qint64 n = f.read(buf.data(), std::min<qint64>(left, buf.size()));
            if (n <= 0 || out.write(buf.constData(), n) != n) {
                out.close();
                QFile::remove(tmp);
                *error = QCoreApplication::translate("dv::OpenThaiInstaller", "%1 could not be unpacked (disk full?).").arg(name);
                return false;
            }
            left -= n;
        }
        out.close();
        const QString target = QDir(outDir).filePath(name);
        QFile::remove(target);
        if (!QFile::rename(tmp, target)) {
            QFile::remove(tmp);
            *error = QCoreApplication::translate("dv::OpenThaiInstaller", "%1 could not be moved into place.").arg(name);
            return false;
        }
        done.insert(name, usize);
    }
    for (auto it = wanted.cbegin(); it != wanted.cend(); ++it) {
        if (!done.contains(it.key())) {
            *error = QCoreApplication::translate("dv::OpenThaiInstaller", "%1 is missing in the archive.").arg(it.key());
            return false;
        }
    }
    return true;
}

} // namespace dv
