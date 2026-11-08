#include "LocalLlm.h"

#include "ai/EditPlanner.h"
#include "core/AppPaths.h"
#include "core/Logger.h"
#include "settings/SettingsManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkRequest>
#include <QStorageInfo>
#include <QTcpServer>
#include <QThread>
#include <QUrl>

namespace dv {

namespace {
// Qwen3.5-4B (Alibaba, Apache 2.0), the Q4_K_M quantisation made by Unsloth.
constexpr const char* kModelFile = "Qwen3.5-4B-Q4_K_M.gguf";
constexpr const char* kModelUrl = "https://huggingface.co/unsloth/Qwen3.5-4B-GGUF/resolve/main/Qwen3.5-4B-Q4_K_M.gguf";
constexpr qint64 kModelBytes = 2740937888;
constexpr const char* kModelSha256 = "00fe7986ff5f6b463e62455821146049db6f9313603938a70800d1fb69ef11a4";
constexpr int kIdleMinutes = 10;

QString devToolsDir(const QString& relative)
{
    QDir d(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        const QString dev = d.filePath(relative);
        if (QFileInfo(dev).exists())
            return dev;
        if (!d.cdUp())
            break;
    }
    return {};
}
} // namespace

LocalLlm::LocalLlm(SettingsManager* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    m_idle.setSingleShot(true);
    m_idle.setInterval(kIdleMinutes * 60 * 1000);
    connect(&m_idle, &QTimer::timeout, this, [this] { if (!busy()) stopServer(); });
    m_poll.setInterval(400);
    connect(&m_poll, &QTimer::timeout, this, [this] { pollHealth(); });
}

LocalLlm::~LocalLlm()
{
    if (m_reply)
        m_reply->abort();
    stopServer();
}

bool LocalLlm::enabled() const
{
    return m_settings->get(QStringLiteral("aiUseLanguageModel")).toBool();
}

void LocalLlm::setEnabled(bool on)
{
    if (on == enabled())
        return;
    m_settings->set(QStringLiteral("aiUseLanguageModel"), on);
    if (!on)
        stopServer();
    emit changed();
}

QString LocalLlm::modelName() const { return QStringLiteral("Qwen3.5 4B"); }
int LocalLlm::modelSizeMb() const { return int(kModelBytes / (1024 * 1024)); }

QString LocalLlm::modelFolder() const
{
    QString dir = m_settings->get(QStringLiteral("aiModelsLocation")).toString();
    if (dir.isEmpty())
        dir = paths::dataDir() + QStringLiteral("/models");
    return QDir::cleanPath(dir);
}

QString LocalLlm::serverExecutable()
{
    QStringList candidates;
    const QString env = qEnvironmentVariable("DV_LLAMA_DIR");
    if (!env.isEmpty())
        candidates << QDir(env).filePath(QStringLiteral("llama-server.exe"));
    candidates << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ai/llama/llama-server.exe"));
    if (const QString dev = devToolsDir(QStringLiteral(".tools/llm/llama/llama-server.exe")); !dev.isEmpty())
        candidates << dev;
    for (const QString& c : candidates)
        if (QFileInfo(c).isFile())
            return c;
    return {};
}

QString LocalLlm::modelPath() const
{
    QStringList candidates{QDir(modelFolder()).filePath(QString::fromLatin1(kModelFile))};
    const QString env = qEnvironmentVariable("DV_LLM_MODEL");
    if (!env.isEmpty())
        candidates << env;
    if (const QString dev = devToolsDir(QStringLiteral(".tools/llm/") + QString::fromLatin1(kModelFile)); !dev.isEmpty())
        candidates << dev;
    for (const QString& c : candidates)
        if (QFileInfo(c).isFile() && QFileInfo(c).size() > 100 * 1024 * 1024)
            return c;
    return {};
}

void LocalLlm::setState(const QString& state)
{
    if (state == m_state)
        return;
    m_state = state;
    emit changed();
}

// ---- download -----------------------------------------------------------------------------------------------------------------------------

QString LocalLlm::download()
{
    if (installed())
        return tr("The language model is already installed.");
    return downloadFrom(QString::fromLatin1(kModelUrl), kModelBytes, QString::fromLatin1(kModelSha256));
}

QString LocalLlm::downloadFrom(const QString& url, qint64 expectedSize, const QString& expectedSha256)
{
    if (m_reply)
        return tr("A download is already running.");
    if (!paths::ensureDir(modelFolder()))
        return tr("Cannot create the model folder.");
    const QStorageInfo storage(modelFolder());
    if (storage.isValid() && storage.bytesAvailable() < expectedSize + 512LL * 1024 * 1024)
        return tr("There is not enough free disk space for the model (%1 MB needed).").arg(expectedSize / (1024 * 1024) + 512);
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    m_target = QDir(modelFolder()).filePath(QString::fromLatin1(kModelFile));
    m_file = new QFile(m_target + QStringLiteral(".part"), this);
    if (!m_file->open(QIODevice::WriteOnly)) {
        delete m_file;
        m_file = nullptr;
        return tr("Cannot create the model file.");
    }
    m_hash = new QCryptographicHash(QCryptographicHash::Sha256);
    m_expectedSize = expectedSize;
    m_expectedSha = expectedSha256.toLower();
    QNetworkRequest req{QUrl(url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_net->get(req);
    m_progress = 0;
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        if (!m_file || !m_reply)
            return;
        const QByteArray chunk = m_reply->readAll();
        m_hash->addData(chunk);
        m_file->write(chunk);
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
        finishDownload(ok, ok ? QString() : r->errorString());
    });
    emit downloadChanged();
    return {};
}

void LocalLlm::finishDownload(bool ok, const QString& message)
{
    QNetworkReply* r = m_reply;
    m_reply = nullptr;
    if (m_file) {
        if (r) {
            const QByteArray rest = r->readAll();
            m_hash->addData(rest);
            m_file->write(rest);
        }
        m_file->close();
    }
    const QString part = m_file ? m_file->fileName() : QString();
    const qint64 got = part.isEmpty() ? 0 : QFileInfo(part).size();
    const QString sha = m_hash ? QString::fromLatin1(m_hash->result().toHex()) : QString();
    delete m_file;
    m_file = nullptr;
    delete m_hash;
    m_hash = nullptr;
    if (r)
        r->deleteLater();
    QString result = message;
    if (ok && got != m_expectedSize) {
        ok = false;
        result = tr("The downloaded file has the wrong size (%1 of %2 bytes).").arg(got).arg(m_expectedSize);
    } else if (ok && sha != m_expectedSha) {
        ok = false;
        result = tr("The downloaded file is damaged (the SHA-256 check failed). It was deleted.");
    }
    if (ok) {
        QFile::remove(m_target);
        if (!QFile::rename(part, m_target)) {
            ok = false;
            result = tr("Cannot save the model file.");
        }
    }
    if (!ok)
        QFile::remove(part);
    m_progress = ok ? 1.0 : 0.0;
    qCInfo(lcProject) << "Language model download" << (ok ? "finished" : "failed") << result;
    emit downloadChanged();
    emit changed();
    emit downloadFinished(ok, result);
}

void LocalLlm::cancelDownload()
{
    if (m_reply)
        m_reply->abort();
}

QString LocalLlm::remove()
{
    const QString path = modelPath();
    if (path.isEmpty())
        return tr("The language model is not installed.");
    if (QFileInfo(path).absolutePath() != QFileInfo(modelFolder() + QStringLiteral("/x")).absolutePath())
        return tr("The model is outside the model folder and is not deleted from here.");
    stopServer();
    if (!QFile::remove(path))
        return tr("Cannot delete the model file.");
    emit changed();
    return {};
}

// ---- server + request ---------------------------------------------------------------------------------------------------------------------

QString LocalLlm::requestPlan(const QString& text)
{
    if (busy())
        return tr("The language model is busy.");
    if (!enabled())
        return tr("The language model is switched off.");
    if (!engineAvailable())
        return tr("The language model engine (llama-server) is missing.");
    if (!installed())
        return tr("The language model is not installed.");
    m_pendingText = text;
    m_idle.stop();
    if (m_proc && m_proc->state() == QProcess::Running && m_ready) {
        setState(QStringLiteral("thinking"));
        sendRequest();
    } else {
        startServer();
    }
    return {};
}

void LocalLlm::startServer()
{
    stopServer();
    // a free port on the loopback interface only
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) {
        failRequest(tr("Cannot find a free local port."));
        return;
    }
    m_port = probe.serverPort();
    probe.close();

    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_proc, &QProcess::readyRead, m_proc, [p = m_proc] { p->readAll(); });      // never let the pipe fill up
    connect(m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        qCInfo(lcProject) << "llama-server exited" << code;
        m_ready = false;
        m_poll.stop();
        if (busy())
            failRequest(tr("The language model stopped unexpectedly."));
    });
    const QString exe = serverExecutable();
    m_proc->setWorkingDirectory(QFileInfo(exe).absolutePath());
    const int threads = std::max(2, QThread::idealThreadCount() / 2);
    // one slot, a small context (the prompt is about 1500 tokens), loopback only, no web page; the model path is never taken from user text
    m_proc->start(exe, {QStringLiteral("-m"), modelPath(), QStringLiteral("--host"), QStringLiteral("127.0.0.1"), QStringLiteral("--port"), QString::number(m_port),
                        QStringLiteral("-c"), QStringLiteral("4096"), QStringLiteral("-np"), QStringLiteral("1"), QStringLiteral("-t"), QString::number(threads),
                        QStringLiteral("--jinja"), QStringLiteral("--no-webui")});
    if (!m_proc->waitForStarted(5000)) {
        failRequest(tr("The language model could not be started."));
        return;
    }
    m_ready = false;
    m_startedAt = QDateTime::currentMSecsSinceEpoch();
    setState(QStringLiteral("starting"));
    m_poll.start();
}

void LocalLlm::pollHealth()
{
    if (m_state != QLatin1String("starting") || !m_proc)
        return;
    if (QDateTime::currentMSecsSinceEpoch() - m_startedAt > 180000) {
        failRequest(tr("The language model did not start in time."));
        return;
    }
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    QNetworkReply* r = m_net->get(QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/health").arg(m_port))));
    r->setProperty("health", true);
    connect(r, &QNetworkReply::finished, this, [this, r] {
        const bool ok = r->error() == QNetworkReply::NoError && r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        r->deleteLater();
        if (ok && m_state == QLatin1String("starting")) {
            m_poll.stop();
            m_ready = true;
            setState(QStringLiteral("thinking"));
            sendRequest();
        }
    });
}

void LocalLlm::sendRequest()
{
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    const QJsonObject body{
        {"messages", QJsonArray{QJsonObject{{"role", "system"}, {"content", ai::llmSystemPrompt()}}, QJsonObject{{"role", "user"}, {"content", m_pendingText}}}},
        {"temperature", 0},
        {"max_tokens", 300},
        {"response_format", QJsonObject{{"type", "json_schema"}, {"json_schema", QJsonObject{{"name", "plan"}, {"strict", true}, {"schema", ai::llmSchema()}}}}},
        {"chat_template_kwargs", QJsonObject{{"enable_thinking", false}}},
    };
    QNetworkRequest req{QUrl(QStringLiteral("http://127.0.0.1:%1/v1/chat/completions").arg(m_port))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(180000);
    m_request = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_request, &QNetworkReply::finished, this, [this] {
        QNetworkReply* r = m_request;
        m_request = nullptr;
        if (!r)
            return;
        const QByteArray data = r->readAll();
        const bool ok = r->error() == QNetworkReply::NoError;
        const QString err = r->errorString();
        r->deleteLater();
        if (!ok) {
            failRequest(tr("The language model did not answer: %1").arg(err));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        const QString content = doc.object().value(QStringLiteral("choices")).toArray().first().toObject().value(QStringLiteral("message")).toObject()
                                    .value(QStringLiteral("content")).toString();
        m_pendingText.clear();
        setState(QStringLiteral("idle"));
        m_idle.start();
        if (content.isEmpty())
            emit planFinished(false, {}, tr("The language model gave an empty answer."));
        else
            emit planFinished(true, content.toUtf8(), {});
    });
}

void LocalLlm::failRequest(const QString& message)
{
    m_poll.stop();
    if (m_request) {
        QNetworkReply* r = m_request;
        m_request = nullptr;
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
    const bool wasBusy = busy();
    m_pendingText.clear();
    if (m_proc) {
        QProcess* p = m_proc;
        m_proc = nullptr;
        m_ready = false;
        p->disconnect(this);
        p->kill();
        p->waitForFinished(3000);
        p->deleteLater();
    }
    setState(QStringLiteral("idle"));
    if (wasBusy)
        emit planFinished(false, {}, message);
}

void LocalLlm::stopServer()
{
    m_poll.stop();
    m_idle.stop();
    if (m_request) {
        QNetworkReply* r = m_request;
        m_request = nullptr;
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
    if (m_proc) {
        QProcess* p = m_proc;
        m_proc = nullptr;
        m_ready = false;
        p->disconnect(this);
        p->terminate();
        if (!p->waitForFinished(3000)) {
            p->kill();
            p->waitForFinished(3000);
        }
        p->deleteLater();
    }
    if (m_state != QLatin1String("idle")) {
        m_pendingText.clear();
        setState(QStringLiteral("idle"));
    }
}

void LocalLlm::waitForIdle(int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (busy() && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(10);
    }
}

} // namespace dv
