#include "LlamaServer.h"

#include "ProcessUtil.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QThread>
#include <QTimer>

namespace dm::ai {

namespace {
QStringList g_installDirs;
}

void LlamaServer::setExtraEngineDirs(const QStringList& installDirs) { g_installDirs = installDirs; }

QString LlamaServer::serverExecutable()
{
    QStringList dirs;
    const QString env = qEnvironmentVariable("DM_LLAMA_DIR");
    if (!env.isEmpty())
        dirs << env;
    dirs << QCoreApplication::applicationDirPath() + QStringLiteral("/ai/llama");
    for (const QString& install : std::as_const(g_installDirs))
        dirs << install + QStringLiteral("/ai/llama");
    QDir d(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        dirs << d.filePath(QStringLiteral(".tools/llm/llama-vulkan"));
        if (!d.cdUp())
            break;
    }
    for (const QString& dir : std::as_const(dirs)) {
        const QString exe = QDir(dir).filePath(QStringLiteral("llama-server.exe"));
        if (QFileInfo::exists(exe))
            return QDir::cleanPath(exe);
    }
    return {};
}

LlamaServer::LlamaServer(QNetworkAccessManager* net, QObject* parent)
    : QObject(parent)
    , m_net(net)
    , m_poll(new QTimer(this))
    , m_idle(new QTimer(this))
{
    m_poll->setInterval(500);
    connect(m_poll, &QTimer::timeout, this, &LlamaServer::pollHealth);
    m_idle->setSingleShot(true);
    m_idle->setInterval(15 * 60 * 1000);
    connect(m_idle, &QTimer::timeout, this, &LlamaServer::stop);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &LlamaServer::stop);
}

LlamaServer::~LlamaServer() { stop(); }

void LlamaServer::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

void LlamaServer::useExternalServer(const QString& baseUrl, const QString& apiKey)
{
    stop();
    m_external = true;
    m_baseUrl = baseUrl;
    m_apiKey = apiKey;
    m_error.clear();
    setState(State::Ready);
}

void LlamaServer::fail(const QString& error, const QString& detail)
{
    m_poll->stop();
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_error = error;
    m_detail = detail;
    m_state = State::Failed;
    emit stateChanged();
}

void LlamaServer::start(const QString& modelPath)
{
    if (m_external) {
        touch();
        return;
    }
    if ((m_state == State::Ready || m_state == State::Starting) && m_model == modelPath) {
        touch();
        return;
    }
    if (m_state == State::Ready || m_state == State::Starting)
        stop();                                          // another model: start again
    m_error.clear();
    m_detail.clear();
    m_model = modelPath;
    if (serverExecutable().isEmpty()) {
        fail(QStringLiteral("noEngine"), {});
        return;
    }
    if (modelPath.isEmpty() || !QFileInfo::exists(modelPath)) {
        fail(QStringLiteral("noModel"), {});
        return;
    }
    setState(State::Starting);
    launch(true);
}

void LlamaServer::launch(bool gpu)
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
        m_process->deleteLater();
        m_process = nullptr;
    }
    quint16 port = 0;
    {
        QTcpServer probe;
        if (probe.listen(QHostAddress::LocalHost, 0))
            port = probe.serverPort();
        probe.close();
    }
    if (port == 0) {
        fail(QStringLiteral("startFailed"), QStringLiteral("no free port"));
        return;
    }
    QByteArray key(16, 0);
    for (char& c : key)
        c = char(QRandomGenerator::system()->bounded(256));
    m_apiKey = QString::fromLatin1(key.toHex());
    m_baseUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);
    m_gpu = gpu;
    m_waited = 0;
    m_log.clear();
    m_probing = false;

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setProgram(serverExecutable());
    m_process->setWorkingDirectory(QFileInfo(serverExecutable()).absolutePath());
    m_process->setArguments({QStringLiteral("-m"), m_model, QStringLiteral("--host"), QStringLiteral("127.0.0.1"), QStringLiteral("--port"),
                             QString::number(port), QStringLiteral("-c"), QStringLiteral("8192"), QStringLiteral("-ngl"),
                             gpu ? QStringLiteral("99") : QStringLiteral("0"), QStringLiteral("--no-webui"), QStringLiteral("--parallel"),
                             QStringLiteral("1"), QStringLiteral("--api-key"), m_apiKey, QStringLiteral("-t"),
                             QString::number(std::max(2, QThread::idealThreadCount() / 2))});
    hideConsole(m_process);
    connect(m_process, &QProcess::readyRead, this, [this] {
        m_log += m_process->readAll();
        if (m_log.size() > 8192)
            m_log = m_log.right(4096);
    });
    connect(m_process, &QProcess::finished, this, [this] {
        if (m_state != State::Starting)
            return;
        const QString detail = QString::fromUtf8(m_log.right(600)).trimmed();
        if (m_gpu)
            launch(false);                               // the graphics card could not be used: try the processor
        else
            fail(QStringLiteral("startFailed"), detail);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && m_state == State::Starting)
            fail(QStringLiteral("startFailed"), m_process ? m_process->errorString() : QString());
    });
    m_process->start();
    if (m_process->waitForStarted(5000))
        killWithThisProgram(m_process);
    m_poll->start();
}

void LlamaServer::pollHealth()
{
    if (m_state != State::Starting) {
        m_poll->stop();
        return;
    }
    m_waited += m_poll->interval();
    if (m_waited > 240000) {
        if (m_gpu)
            launch(false);
        else
            fail(QStringLiteral("startFailed"), QStringLiteral("the model did not load in time"));
        return;
    }
    if (m_probing)
        return;
    m_probing = true;
    QNetworkRequest request{QUrl(m_baseUrl + QStringLiteral("/health"))};
    request.setTransferTimeout(2000);
    QNetworkReply* reply = m_net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_probing = false;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (m_state == State::Starting && status == 200) {
            m_poll->stop();
            setState(State::Ready);
            touch();
        }
    });
}

void LlamaServer::stop()
{
    m_poll->stop();
    m_idle->stop();
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(3000);
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (!m_external)
        m_baseUrl.clear();
    if (m_state != State::Stopped && !m_external)
        setState(State::Stopped);
}

void LlamaServer::touch()
{
    if (!m_external)
        m_idle->start();
}

} // namespace dm::ai
