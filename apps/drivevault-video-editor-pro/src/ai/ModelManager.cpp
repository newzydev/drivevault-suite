#include "ModelManager.h"

#include "core/AppPaths.h"
#include "core/Logger.h"
#include "settings/SettingsManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace dv {

namespace {
struct CatalogEntry {
    const char* id;
    const char* name;
    const char* description;
    int sizeMb;
};
const CatalogEntry kCatalog[] = {
    {"tiny", QT_TRANSLATE_NOOP("ModelManager", "Whisper tiny"), QT_TRANSLATE_NOOP("ModelManager", "Fastest, lowest accuracy (multilingual)"), 75},
    {"base", QT_TRANSLATE_NOOP("ModelManager", "Whisper base"), QT_TRANSLATE_NOOP("ModelManager", "Good balance for English, usable for Thai (multilingual)"), 142},
    {"small", QT_TRANSLATE_NOOP("ModelManager", "Whisper small"), QT_TRANSLATE_NOOP("ModelManager", "Better Thai accuracy, slower (multilingual)"), 466},
};
QString fileNameFor(const QString& id) { return QStringLiteral("ggml-%1.bin").arg(id); }
QString urlFor(const QString& id)
{
    return QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-%1.bin").arg(id);
}
} // namespace

ModelManager::ModelManager(SettingsManager* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
}

QString ModelManager::directory() const
{
    QString dir = m_settings->get(QStringLiteral("aiModelsLocation")).toString();
    if (dir.isEmpty())
        dir = paths::dataDir() + QStringLiteral("/models");
    return QDir::cleanPath(dir);
}

QStringList ModelManager::searchDirs() const
{
    QStringList dirs{directory()};
    const QString env = qEnvironmentVariable("DV_WHISPER_MODELS");
    if (!env.isEmpty())
        dirs << env;
    const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/ai/models");     // installed with the program (ggml-base.bin)
    if (QFileInfo(bundled).isDir())
        dirs << bundled;
    QDir d(QCoreApplication::applicationDirPath());     // development tree: .tools\whisper\models
    for (int i = 0; i < 6; ++i) {
        const QString dev = d.filePath(QStringLiteral(".tools/whisper/models"));
        if (QFileInfo(dev).isDir())
            dirs << dev;
        if (!d.cdUp())
            break;
    }
    return dirs;
}

QString ModelManager::pathFor(const QString& id) const
{
    if (id.isEmpty() || id.contains(QLatin1Char('/')) || id.contains(QLatin1Char('\\')))
        return {};
    for (const QString& dir : searchDirs()) {
        const QString p = QDir(dir).filePath(fileNameFor(id));
        if (QFileInfo(p).isFile() && QFileInfo(p).size() > 1024)
            return p;
    }
    return {};
}

QString ModelManager::selected() const
{
    const QString id = m_settings->get(QStringLiteral("aiModel")).toString();
    return pathFor(id).isEmpty() ? QString() : id;
}

QString ModelManager::selectedPath() const
{
    if (const QString id = selected(); !id.isEmpty())
        return pathFor(id);
    for (const QVariant& v : models())
        if (v.toMap().value("installed").toBool())
            return pathFor(v.toMap().value("id").toString());
    return {};
}

QVariantList ModelManager::models() const
{
    QVariantList list;
    QStringList ids;
    for (const CatalogEntry& c : kCatalog)
        ids << QString::fromLatin1(c.id);
    // models the user imported (ggml-<id>.bin) that are not in the catalog
    for (const QString& dir : searchDirs())
        for (const QFileInfo& fi : QDir(dir).entryInfoList({QStringLiteral("ggml-*.bin")}, QDir::Files)) {
            const QString id = fi.completeBaseName().mid(5);
            if (!id.isEmpty() && !ids.contains(id))
                ids << id;
        }
    const QString sel = selected();
    for (const QString& id : std::as_const(ids)) {
        QVariantMap m{{"id", id}};
        const CatalogEntry* entry = nullptr;
        for (const CatalogEntry& c : kCatalog)
            if (id == QLatin1String(c.id))
                entry = &c;
        const QString path = pathFor(id);
        m["name"] = entry ? QCoreApplication::translate("ModelManager", entry->name) : id;
        m["description"] = entry ? QCoreApplication::translate("ModelManager", entry->description) : tr("Imported model");
        m["sizeMb"] = path.isEmpty() ? (entry ? entry->sizeMb : 0) : int(QFileInfo(path).size() / (1024 * 1024));
        m["installed"] = !path.isEmpty();
        m["selected"] = id == sel;
        m["removable"] = !path.isEmpty() && QFileInfo(path).absolutePath() == QFileInfo(directory() + "/x").absolutePath();
        m["downloadable"] = entry != nullptr;
        list.append(m);
    }
    return list;
}

QString ModelManager::select(const QString& id)
{
    if (pathFor(id).isEmpty())
        return tr("This model is not installed.");
    m_settings->set(QStringLiteral("aiModel"), id);
    emit modelsChanged();
    return {};
}

QString ModelManager::importModel(const QString& pathOrUrl)
{
    const QString src = pathOrUrl.startsWith(QLatin1String("file:")) ? QUrl(pathOrUrl).toLocalFile() : pathOrUrl;
    const QFileInfo fi(src);
    if (!fi.isFile())
        return tr("The file was not found.");
    if (fi.size() < 1024 * 1024)
        return tr("This does not look like a speech model (the file is too small).");
    QFile probe(src);
    if (!probe.open(QIODevice::ReadOnly) || probe.read(4) != QByteArray("lmgg"))     // ggml magic, little endian
        return tr("This is not a ggml speech model file.");
    probe.close();
    QString name = fi.fileName();
    if (!name.startsWith(QLatin1String("ggml-")) || !name.endsWith(QLatin1String(".bin")))
        name = QStringLiteral("ggml-%1.bin").arg(fi.completeBaseName().remove(QLatin1String("ggml-")));
    if (!paths::ensureDir(directory()))
        return tr("Cannot create the model folder.");
    const QString dst = QDir(directory()).filePath(name);
    if (QFileInfo(dst) == fi)
        return {};
    QFile::remove(dst);
    if (!QFile::copy(src, dst))
        return tr("Cannot copy the model file.");
    emit modelsChanged();
    return {};
}

QString ModelManager::remove(const QString& id)
{
    const QString path = pathFor(id);
    if (path.isEmpty())
        return tr("This model is not installed.");
    if (QFileInfo(path).absolutePath() != QFileInfo(directory() + "/x").absolutePath())
        return tr("This model is outside the model folder and is not deleted from here.");
    if (!QFile::remove(path))
        return tr("Cannot delete the model file.");
    if (m_settings->get(QStringLiteral("aiModel")).toString() == id)
        m_settings->set(QStringLiteral("aiModel"), QString());
    emit modelsChanged();
    return {};
}

QString ModelManager::download(const QString& id)
{
    for (const CatalogEntry& c : kCatalog)
        if (id == QLatin1String(c.id))
            return downloadFrom(urlFor(id), fileNameFor(id));
    return tr("Unknown model.");
}

QString ModelManager::downloadFrom(const QString& url, const QString& fileName)
{
    if (m_reply)
        return tr("A download is already running.");
    if (!paths::ensureDir(directory()))
        return tr("Cannot create the model folder.");
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    m_target = QDir(directory()).filePath(fileName);
    m_file = new QFile(m_target + QStringLiteral(".part"), this);
    if (!m_file->open(QIODevice::WriteOnly)) {
        delete m_file;
        m_file = nullptr;
        return tr("Cannot create the model file.");
    }
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_net->get(req);
    m_progress = 0;
    m_downloadName = fileName;
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if (m_file && m_reply)
            m_file->write(m_reply->readAll());
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        m_progress = total > 0 ? double(got) / double(total) : 0;
        emit downloadChanged();
    });
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply* r = m_reply;
        if (!r)
            return;
        const bool ok = r->error() == QNetworkReply::NoError;
        const QString err = r->errorString();
        finishDownload(ok, ok ? QString() : err);
    });
    emit downloadChanged();
    return {};
}

void ModelManager::finishDownload(bool ok, const QString& message)
{
    QNetworkReply* r = m_reply;
    m_reply = nullptr;
    if (m_file) {
        if (r)
            m_file->write(r->readAll());
        m_file->close();
    }
    const QString part = m_file ? m_file->fileName() : QString();
    delete m_file;
    m_file = nullptr;
    if (r)
        r->deleteLater();
    QString result = message;
    if (ok) {
        if (QFileInfo(part).size() < 1024 * 1024) {
            ok = false;
            result = tr("The downloaded file is too small to be a model.");
        } else {
            QFile::remove(m_target);
            if (!QFile::rename(part, m_target)) {
                ok = false;
                result = tr("Cannot save the model file.");
            }
        }
    }
    if (!ok)
        QFile::remove(part);
    m_progress = ok ? 1.0 : 0.0;
    qCInfo(lcProject) << "Model download" << m_downloadName << (ok ? "finished" : "failed") << result;
    emit downloadChanged();
    emit modelsChanged();
    emit downloadFinished(ok, result);
}

void ModelManager::cancelDownload()
{
    if (m_reply)
        m_reply->abort();     // finished() fires with OperationCanceledError and cleans up
}

} // namespace dv
