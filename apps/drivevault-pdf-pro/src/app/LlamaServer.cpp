#include "LlamaServer.h"

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

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace dp::assistant {

namespace {

QString g_dataDir;

#ifdef Q_OS_WIN
// A job that kills its processes when the last handle closes (when this program ends, however it ends).
HANDLE killOnCloseJob()
{
    static HANDLE job = [] {
        HANDLE h = CreateJobObjectW(nullptr, nullptr);
        if (h) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(h, JobObjectExtendedLimitInformation, &info, sizeof(info));
        }
        return h;
    }();
    return job;
}
#endif

} // namespace

QString LlamaServer::modelFileName() { return QStringLiteral("Qwen3.5-4B-Q4_K_M.gguf"); }

void LlamaServer::setDataDir(const QString& dir) { g_dataDir = dir; }

QString LlamaServer::downloadFolder() { return g_dataDir + QStringLiteral("/models"); }

QString LlamaServer::serverExecutable()
{
    QStringList dirs;
    const QString env = qEnvironmentVariable("DP_LLAMA_DIR");
    if (!env.isEmpty())
        dirs << env;
    dirs << QCoreApplication::applicationDirPath() + QStringLiteral("/ai/llama");
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

QString LlamaServer::modelPath()
{
    const QString env = qEnvironmentVariable("DP_LLM_MODEL");
    if (!env.isEmpty() && QFileInfo::exists(env))
        return QDir::cleanPath(env);
    QStringList files;
    files << QCoreApplication::applicationDirPath() + QStringLiteral("/ai/models/") + modelFileName();
    if (!g_dataDir.isEmpty())
        files << g_dataDir + QStringLiteral("/models/") + modelFileName();
    QDir d(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        files << d.filePath(QStringLiteral(".tools/llm/") + modelFileName());
        if (!d.cdUp())
            break;
    }
    for (const QString& file : std::as_const(files)) {
        if (QFileInfo::exists(file) && QFileInfo(file).size() > 1000000000LL)
            return QDir::cleanPath(file);
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

LlamaServer::~LlamaServer()
{
    stop();
}

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

void LlamaServer::start()
{
    if (m_external || m_state == State::Ready || m_state == State::Starting) {
        touch();
        return;
    }
    m_error.clear();
    m_detail.clear();
    if (serverExecutable().isEmpty()) {
        fail(QStringLiteral("noEngine"), {});
        return;
    }
    if (modelPath().isEmpty()) {
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
    // a free port (the server needs a moment to take it, so another program could be quicker: then the start fails and is reported)
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
    m_process->setArguments({QStringLiteral("-m"), modelPath(), QStringLiteral("--host"), QStringLiteral("127.0.0.1"), QStringLiteral("--port"),
                             QString::number(port), QStringLiteral("-c"), QStringLiteral("8192"), QStringLiteral("-ngl"),
                             gpu ? QStringLiteral("99") : QStringLiteral("0"), QStringLiteral("--no-webui"), QStringLiteral("--parallel"),
                             QStringLiteral("1"), QStringLiteral("--api-key"), m_apiKey, QStringLiteral("-t"),
                             QString::number(std::max(2, QThread::idealThreadCount() / 2))});
#ifdef Q_OS_WIN
    m_process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    connect(m_process, &QProcess::readyRead, this, [this] {
        m_log += m_process->readAll();
        if (m_log.size() > 8192)
            m_log = m_log.right(4096);
    });
    connect(m_process, &QProcess::finished, this, [this] {
        if (m_state != State::Starting)
            return;                                  // stopped on purpose, or ready and ended later (reported on the next request)
        const QString detail = QString::fromUtf8(m_log.right(600)).trimmed();
        if (m_gpu)
            launch(false);                           // the graphics card could not be used: try the processor
        else
            fail(QStringLiteral("startFailed"), detail);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart && m_state == State::Starting)
            fail(QStringLiteral("startFailed"), m_process ? m_process->errorString() : QString());
    });
    m_process->start();
#ifdef Q_OS_WIN
    if (m_process->waitForStarted(5000)) {
        if (HANDLE job = killOnCloseJob()) {
            if (HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(m_process->processId()))) {
                AssignProcessToJobObject(job, h);
                CloseHandle(h);
            }
        }
    }
#endif
    m_poll->start();
}

void LlamaServer::pollHealth()
{
    if (m_state != State::Starting) {
        m_poll->stop();
        return;
    }
    m_waited += m_poll->interval();
    if (m_waited > 240000) {                         // four minutes without an answer
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

} // namespace dp::assistant
