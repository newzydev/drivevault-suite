#include "AiService.h"

#include "Hub.h"
#include "ProcessUtil.h"
#include "SecretStore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

namespace dm::ai {

namespace {

QString sizeText(qint64 bytes)
{
    if (bytes >= 1000LL * 1000 * 1000)
        return QStringLiteral("%1 GB").arg(double(bytes) / 1e9, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(qint64(double(bytes) / 1e6 + 0.5));
}

QJsonObject readJson(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool writeJson(const QString& path, const QJsonObject& o)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    return f.commit();
}

QString newId() { return QString::number(QDateTime::currentMSecsSinceEpoch(), 36) + QString::number(QRandomGenerator::global()->bounded(46656), 36); }

bool loopbackUrl(const QString& url)
{
    const QString host = QUrl(url).host();
    return host == QLatin1String("127.0.0.1") || host == QLatin1String("localhost") || host == QLatin1String("::1");
}

} // namespace

// ================================================================================================================================================
// AiChannel
// ================================================================================================================================================

AiChannel::AiChannel(const QString& mode, AiService* service)
    : QObject(service)
    , m_mode(mode)
    , m_service(service)
    , m_viewTimer(new QTimer(this))
{
    m_viewTimer->setSingleShot(true);
    m_viewTimer->setInterval(60);
    connect(m_viewTimer, &QTimer::timeout, this, &AiChannel::messagesChanged);
}

AiChannel::~AiChannel()
{
    cancelRunning();
}

QStringList AiChannel::providers() const
{
    if (isAgent())
        return {QStringLiteral("claude"), QStringLiteral("codex")};
    return {QStringLiteral("local"), QStringLiteral("openai"), QStringLiteral("anthropic"), QStringLiteral("claude"), QStringLiteral("codex")};
}

QString AiChannel::notReady() const
{
    if (m_provider.isEmpty() || !providers().contains(m_provider))
        return QStringLiteral("noProvider");
    if (m_provider == QLatin1String("local")) {
        if (!m_service->engineFound())
            return QStringLiteral("noEngine");
        const QString model = m_service->localModel();
        if (model.isEmpty() || !QFileInfo::exists(model))
            return QStringLiteral("noModel");
    } else if (m_provider == QLatin1String("openai")) {
        if (m_service->openAiModel().trimmed().isEmpty())
            return QStringLiteral("noModel");
        if (m_service->openAiKey().isEmpty() && !loopbackUrl(m_service->openAiBaseUrl()))
            return QStringLiteral("noKey");
    } else if (m_provider == QLatin1String("anthropic")) {
        if (m_service->anthropicKey().isEmpty())
            return QStringLiteral("noKey");
        if (m_service->anthropicModel().trimmed().isEmpty())
            return QStringLiteral("noModel");
    } else if (m_service->cliPath(m_provider).isEmpty()) {
        return QStringLiteral("noProgram");
    }
    if (isAgent() && (m_folder.isEmpty() || !QFileInfo(m_folder).isDir()))
        return QStringLiteral("noFolder");
    return {};
}

AiChannel::Conversation* AiChannel::find(const QString& id)
{
    for (Conversation& c : m_conversations)
        if (c.id == id)
            return &c;
    return nullptr;
}

AiChannel::Conversation* AiChannel::current() { return find(m_currentId); }

QVariantList AiChannel::messages() const
{
    QVariantList out;
    for (const Conversation& c : m_conversations) {
        if (c.id != m_currentId)
            continue;
        for (const Message& m : c.messages) {
            out.append(QVariantMap{{"role", m.role}, {"text", m.text}, {"streaming", m.streaming}, {"stopped", m.stopped}, {"errorCode", m.errorCode},
                                   {"errorDetail", m.errorDetail}, {"activity", m.activity}});
        }
    }
    return out;
}

QVariantList AiChannel::conversations() const
{
    QList<const Conversation*> sorted;
    for (const Conversation& c : m_conversations)
        sorted << &c;
    std::sort(sorted.begin(), sorted.end(), [](const Conversation* a, const Conversation* b) { return a->updated > b->updated; });
    QVariantList out;
    for (const Conversation* c : sorted)
        out.append(QVariantMap{{"id", c->id}, {"title", c->title}});
    return out;
}

void AiChannel::scheduleView()
{
    if (!m_viewTimer->isActive())
        m_viewTimer->start();
}

void AiChannel::setBusy(bool busy, const QString& status)
{
    if (m_busy == busy && m_status == status)
        return;
    m_busy = busy;
    m_status = status;
    emit busyChanged();
}

AiChannel::Conversation& AiChannel::ensureConversation(const QString& firstText)
{
    if (Conversation* c = current())
        return *c;
    Conversation c;
    c.id = newId();
    QString title = firstText.simplified();
    c.title = title.size() > 48 ? title.left(47) + QStringLiteral("…") : title;
    c.updated = QDateTime::currentMSecsSinceEpoch();
    m_conversations.append(c);
    m_currentId = c.id;
    emit conversationsChanged();
    return m_conversations.last();
}

QString AiChannel::send(const QString& text)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return QStringLiteral("empty");
    if (m_busy)
        return QStringLiteral("busy");
    const QString nr = notReady();
    if (!nr.isEmpty())
        return nr;
    beginRequest(t);
    return {};
}

void AiChannel::beginRequest(const QString& userText)
{
    Conversation& c = ensureConversation(userText);
    Message user;
    user.role = QStringLiteral("user");
    user.text = userText;
    Message reply;
    reply.role = QStringLiteral("assistant");
    reply.streaming = true;
    c.messages << user << reply;
    c.updated = QDateTime::currentMSecsSinceEpoch();
    m_runId = c.id;
    ++m_generation;
    setBusy(true, QStringLiteral("starting"));
    emit messagesChanged();
    emit conversationsChanged();
    saveConversations();

    if (m_provider == QLatin1String("local")) {
        sendLocal(userText);
    } else if (m_provider == QLatin1String("openai") || m_provider == QLatin1String("anthropic")) {
        HttpChat::Request r;
        if (m_provider == QLatin1String("openai")) {
            r.kind = HttpChat::Kind::OpenAi;
            r.baseUrl = m_service->openAiBaseUrl();
            r.apiKey = m_service->openAiKey();
            r.model = m_service->openAiModel();
        } else {
            r.kind = HttpChat::Kind::Anthropic;
            r.baseUrl = AiService::anthropicBase();
            r.apiKey = m_service->anthropicKey();
            r.model = m_service->anthropicModel();
        }
        r.system = systemPrompt();
        QList<Message> past = find(m_runId)->messages;
        past.removeLast();                                         // the empty reply that is being written
        r.messages = historyFor(past, 80000);
        sendHttp(r);
    } else {
        sendCli(userText);
    }
}

QList<QPair<QString, QString>> AiChannel::historyFor(const QList<Message>& messages, int maxChars)
{
    QList<QPair<QString, QString>> picked;
    int chars = 0;
    for (int i = messages.size() - 1; i >= 0; --i) {
        const Message& m = messages.at(i);
        if (m.text.trimmed().isEmpty())
            continue;                                              // a failed or stopped reply with no text
        if (!picked.isEmpty() && chars + m.text.size() > maxChars)
            break;
        chars += m.text.size();
        picked.prepend({m.role, m.text});
    }
    // merge neighbours of the same role (a failed reply between two questions), and begin with a question
    QList<QPair<QString, QString>> out;
    for (const auto& p : std::as_const(picked)) {
        if (out.isEmpty() && p.first != QLatin1String("user"))
            continue;
        if (!out.isEmpty() && out.last().first == p.first)
            out.last().second += QStringLiteral("\n\n") + p.second;
        else
            out << p;
    }
    return out;
}

QString AiChannel::transcriptFor(const QList<Message>& messages)
{
    const auto history = historyFor(messages, 60000);
    if (history.size() <= 1)
        return history.isEmpty() ? QString() : history.first().second;
    QString out = QStringLiteral("Conversation so far:\n\n");
    for (int i = 0; i < history.size() - 1; ++i)
        out += (history.at(i).first == QLatin1String("user") ? QStringLiteral("User: ") : QStringLiteral("Assistant: ")) + history.at(i).second + QStringLiteral("\n\n");
    out += QStringLiteral("Now answer the last message of the user:\n\n") + history.last().second;
    return out;
}

QString AiChannel::systemPrompt() const
{
    if (isAgent())
        return {};
    return QStringLiteral("You are the assistant inside DriveVault Master Pro, the hub of the DriveVault suite of Windows programs. Answer clearly and briefly, in the "
                          "language of the user's question (Thai or English). You can only talk: you cannot open, change or install anything on this computer, and you "
                          "must not claim that you did. If you do not know something, say so.\n\nThe programs of the suite:\n")
           + m_service->suiteSummary();
}

void AiChannel::sendHttp(HttpChat::Request request)
{
    if (!m_http) {
        m_http = new HttpChat(m_service->net(), this);
        connect(m_http, &HttpChat::piece, this, &AiChannel::appendPiece);
        connect(m_http, &HttpChat::finished, this, &AiChannel::finish);
    }
    const QString err = m_http->start(request);
    if (!err.isEmpty())
        finish(false, err, {});
    else
        setBusy(true, QStringLiteral("starting"));
}

void AiChannel::sendLocal(const QString&)
{
    LlamaServer* server = m_service->llama();
    const QString model = m_service->localModel();
    const int gen = m_generation;
    auto go = [this, server, gen] {
        if (gen != m_generation || !m_busy)
            return;
        HttpChat::Request r;
        r.kind = HttpChat::Kind::OpenAi;
        r.baseUrl = server->baseUrl() + QStringLiteral("/v1");
        r.apiKey = server->apiKey();
        r.model = QStringLiteral("local");
        r.system = systemPrompt();
        QList<Message> past = find(m_runId)->messages;
        past.removeLast();
        r.messages = historyFor(past, 14000);                      // the 8192 token window of the local server
        r.extraBody = QJsonObject{{"chat_template_kwargs", QJsonObject{{"enable_thinking", false}}}};
        sendHttp(r);
        server->touch();
    };
    if (server->state() == LlamaServer::State::Ready && server->modelPath() == model) {
        go();
        return;
    }
    QObject::disconnect(m_waitLocal);
    m_waitLocal = connect(server, &LlamaServer::stateChanged, this, [this, server, gen, go] {
        if (gen != m_generation || !m_busy) {
            QObject::disconnect(m_waitLocal);
            return;
        }
        if (server->state() == LlamaServer::State::Ready) {
            QObject::disconnect(m_waitLocal);
            go();
        } else if (server->state() == LlamaServer::State::Failed) {
            QObject::disconnect(m_waitLocal);
            finish(false, QStringLiteral("local_") + server->error(), server->detail());
        }
    });
    server->start(model);
}

void AiChannel::sendCli(const QString& userText)
{
    Conversation* c = find(m_runId);
    CliChat::Request r;
    r.tool = m_provider == QLatin1String("claude") ? CliChat::Tool::Claude : CliChat::Tool::Codex;
    r.executable = m_service->cliPath(m_provider);
    r.allowEdits = isAgent() && m_allowEdits;
    r.workDir = isAgent() ? m_folder : m_service->dataFolder() + QStringLiteral("/ask-folder");
    QDir().mkpath(r.workDir);
    QList<Message> past = c->messages;
    past.removeLast();
    if (r.tool == CliChat::Tool::Claude) {
        r.sessionId = c->sessionId;
        r.prompt = (r.sessionId.isEmpty() && !isAgent()) ? systemPrompt() + QStringLiteral("\n\n") + userText : userText;
    } else {
        r.prompt = (isAgent() ? QString() : systemPrompt() + QStringLiteral("\n\n")) + transcriptFor(past);
    }
    if (!m_cli) {
        m_cli = new CliChat(this);
        connect(m_cli, &CliChat::piece, this, &AiChannel::appendPiece);
        connect(m_cli, &CliChat::activity, this, [this](const QString& a) {
            if (Conversation* conv = find(m_runId); conv && !conv->messages.isEmpty()) {
                conv->messages.last().activity << a;
                scheduleView();
            }
        });
        connect(m_cli, &CliChat::sessionId, this, [this](const QString& id) {
            if (Conversation* conv = find(m_runId); conv && m_provider == QLatin1String("claude"))
                conv->sessionId = id;
        });
        connect(m_cli, &CliChat::finished, this, &AiChannel::finish);
    }
    const QString err = m_cli->start(r);
    if (!err.isEmpty())
        finish(false, err, {});
}

void AiChannel::appendPiece(const QString& text)
{
    if (!m_busy)
        return;
    Conversation* c = find(m_runId);
    if (!c || c->messages.isEmpty())
        return;
    c->messages.last().text += text;
    if (m_status != QLatin1String("writing"))
        setBusy(true, QStringLiteral("writing"));
    scheduleView();
}

void AiChannel::finish(bool ok, const QString& code, const QString& detail)
{
    if (!m_busy)
        return;
    QObject::disconnect(m_waitLocal);
    if (Conversation* c = find(m_runId); c && !c->messages.isEmpty()) {
        Message& m = c->messages.last();
        m.streaming = false;
        if (code == QLatin1String("cancelled"))
            m.stopped = true;
        else if (!ok) {
            m.errorCode = code.isEmpty() ? QStringLiteral("failed") : code;
            m.errorDetail = detail;
        }
        c->updated = QDateTime::currentMSecsSinceEpoch();
    }
    setBusy(false);
    emit messagesChanged();
    emit conversationsChanged();
    saveConversations();
}

void AiChannel::cancelRunning()
{
    if (m_http && m_http->running())
        m_http->cancel();
    if (m_cli && m_cli->running())
        m_cli->cancel();
}

void AiChannel::stop()
{
    if (!m_busy)
        return;
    ++m_generation;
    const bool http = m_http && m_http->running();
    const bool cli = m_cli && m_cli->running();
    cancelRunning();
    if (!http && !cli)
        finish(false, QStringLiteral("cancelled"), {});      // still starting the model: nothing is running yet
}

void AiChannel::newConversation()
{
    m_currentId.clear();
    emit messagesChanged();
    emit conversationsChanged();
}

void AiChannel::select(const QString& id)
{
    if (!find(id) || id == m_currentId)
        return;
    m_currentId = id;
    emit messagesChanged();
    emit conversationsChanged();
}

void AiChannel::remove(const QString& id)
{
    if (m_busy && id == m_runId)
        return;
    for (int i = 0; i < m_conversations.size(); ++i) {
        if (m_conversations.at(i).id == id) {
            m_conversations.removeAt(i);
            break;
        }
    }
    if (m_currentId == id)
        m_currentId.clear();
    emit messagesChanged();
    emit conversationsChanged();
    saveConversations();
}

void AiChannel::setProvider(const QString& provider)
{
    if (m_busy || !providers().contains(provider) || provider == m_provider)
        return;
    m_provider = provider;
    emit settingsChanged();
    emit changed();
}

void AiChannel::setFolder(const QString& folder)
{
    QString f = QDir::cleanPath(folder);
    if (f.startsWith(QLatin1String("file:///")))
        f = QUrl(folder).toLocalFile();
    if (m_busy || f == m_folder)
        return;
    m_folder = f;
    emit settingsChanged();
    emit changed();
}

void AiChannel::setAllowEdits(bool allow)
{
    if (m_busy || allow == m_allowEdits)
        return;
    m_allowEdits = allow;
    emit settingsChanged();
    emit changed();
}

QVariantMap AiChannel::settingsValues() const { return {{"provider", m_provider}, {"folder", m_folder}, {"allowEdits", m_allowEdits}}; }

void AiChannel::applySettings(const QVariantMap& v)
{
    m_provider = v.value(QStringLiteral("provider")).toString();
    if (!providers().contains(m_provider))
        m_provider.clear();
    m_folder = v.value(QStringLiteral("folder")).toString();
    m_allowEdits = v.value(QStringLiteral("allowEdits")).toBool();
    emit settingsChanged();
}

void AiChannel::saveConversations()
{
    QList<const Conversation*> sorted;
    for (const Conversation& c : m_conversations)
        sorted << &c;
    std::sort(sorted.begin(), sorted.end(), [](const Conversation* a, const Conversation* b) { return a->updated > b->updated; });
    QJsonArray list;
    for (int i = 0; i < sorted.size() && i < 100; ++i) {                // the newest hundred
        const Conversation& c = *sorted.at(i);
        QJsonArray msgs;
        for (const Message& m : c.messages) {
            QJsonObject o{{"role", m.role}, {"text", m.text}};
            if (!m.activity.isEmpty())
                o["activity"] = QJsonArray::fromStringList(m.activity);
            if (!m.errorCode.isEmpty()) {
                o["errorCode"] = m.errorCode;
                o["errorDetail"] = m.errorDetail;
            }
            if (m.stopped)
                o["stopped"] = true;
            msgs.append(o);
        }
        list.append(QJsonObject{{"id", c.id}, {"title", c.title}, {"sessionId", c.sessionId}, {"updated", double(c.updated)}, {"messages", msgs}});
    }
    writeJson(m_service->dataFolder() + QStringLiteral("/conversations-") + m_mode + QStringLiteral(".json"), QJsonObject{{"conversations", list}});
}

void AiChannel::load()
{
    m_conversations.clear();
    const QJsonObject root = readJson(m_service->dataFolder() + QStringLiteral("/conversations-") + m_mode + QStringLiteral(".json"));
    for (const QJsonValue& v : root.value(QLatin1String("conversations")).toArray()) {
        const QJsonObject o = v.toObject();
        Conversation c;
        c.id = o.value(QLatin1String("id")).toString();
        c.title = o.value(QLatin1String("title")).toString();
        c.sessionId = o.value(QLatin1String("sessionId")).toString();
        c.updated = qint64(o.value(QLatin1String("updated")).toDouble());
        for (const QJsonValue& mv : o.value(QLatin1String("messages")).toArray()) {
            const QJsonObject mo = mv.toObject();
            Message m;
            m.role = mo.value(QLatin1String("role")).toString();
            m.text = mo.value(QLatin1String("text")).toString();
            for (const QJsonValue& a : mo.value(QLatin1String("activity")).toArray())
                m.activity << a.toString();
            m.errorCode = mo.value(QLatin1String("errorCode")).toString();
            m.errorDetail = mo.value(QLatin1String("errorDetail")).toString();
            m.stopped = mo.value(QLatin1String("stopped")).toBool();
            c.messages << m;
        }
        if (!c.id.isEmpty())
            m_conversations << c;
    }
    emit conversationsChanged();
    emit messagesChanged();
}

// ================================================================================================================================================
// AiService
// ================================================================================================================================================

AiService::AiService(Hub* hub, QObject* parent, bool probePrograms)
    : QObject(parent)
    , m_hub(hub)
    , m_probePrograms(probePrograms)
    , m_net(new QNetworkAccessManager(this))
    , m_llama(m_net)
    , m_downloader()
    , m_ask(nullptr)
    , m_agent(nullptr)
{
    m_dir = m_hub->dataDir() + QStringLiteral("/ai");
    QDir().mkpath(m_dir);
    m_ask = new AiChannel(QStringLiteral("ask"), this);
    m_agent = new AiChannel(QStringLiteral("agent"), this);
    auto engineDirs = [this] {
        QStringList dirs;
        for (const QVariant& p : m_hub->products()) {
            const QString dir = p.toMap().value(QStringLiteral("installDir")).toString();
            if (!dir.isEmpty())
                dirs << dir;
        }
        dirs.sort();
        if (dirs == m_installDirs)
            return;                                          // the 5 second refresh of the hub: nothing changed
        m_installDirs = dirs;
        LlamaServer::setExtraEngineDirs(dirs);
        emit modelsChanged();
        m_ask->reloadReadiness();
    };
    engineDirs();
    connect(m_hub, &Hub::productsChanged, this, engineDirs);
    connect(&m_downloader, &ModelDownloader::changed, this, &AiService::downloadChanged);
    connect(&m_downloader, &ModelDownloader::finished, this, [this](bool ok, const QString&) {
        refreshModels();
        if (ok && m_localModel.isEmpty())
            selectLocalModel(m_downloadFile);                // the first model becomes the chosen one
    });
    connect(m_ask, &AiChannel::changed, this, &AiService::saveSettings);
    connect(m_agent, &AiChannel::changed, this, &AiService::saveSettings);
    loadSettings();
    m_ask->load();
    m_agent->load();
    detectPrograms();
}

AiService::~AiService() = default;

QString AiService::modelsFolder() const { return m_hub->dataDir() + QStringLiteral("/models"); }

QStringList AiService::modelFolders() const
{
    QStringList dirs{modelsFolder(), QCoreApplication::applicationDirPath() + QStringLiteral("/ai/models")};
    for (const QVariant& p : m_hub->products()) {
        const QString dir = p.toMap().value(QStringLiteral("installDir")).toString();
        if (!dir.isEmpty())
            dirs << dir + QStringLiteral("/ai/models");
    }
    QDir d(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 6; ++i) {
        dirs << d.filePath(QStringLiteral(".tools/llm"));
        if (!d.cdUp())
            break;
    }
    return dirs;
}

QVariantList AiService::installedModels() const
{
    QVariantList out;
    for (const InstalledModel& m : ModelStore::installed(modelFolders()))
        out.append(QVariantMap{{"name", m.name}, {"path", m.path}, {"sizeText", sizeText(m.size)}});
    return out;
}

QVariantList AiService::catalog() const
{
    const auto installed = ModelStore::installed(modelFolders());
    QVariantList out;
    for (const CatalogEntry& e : ModelStore::catalog()) {
        QString path;
        for (const InstalledModel& m : installed)
            if (m.name == e.fileName && m.size == e.size)
                path = m.path;
        out.append(QVariantMap{{"id", e.id}, {"title", e.title}, {"fileName", e.fileName}, {"sizeText", sizeText(e.size)}, {"level", e.level},
                               {"installed", !path.isEmpty()}, {"path", path}});
    }
    return out;
}

void AiService::refreshModels() { emit modelsChanged(); emit settingsChanged(); }

QVariantMap AiService::openai() const { return {{"baseUrl", m_openaiBase}, {"model", m_openaiModel}, {"keySet", !m_openaiKeyEnc.isEmpty()}}; }
QVariantMap AiService::anthropic() const { return {{"model", m_anthropicModel}, {"keySet", !m_anthropicKeyEnc.isEmpty()}}; }
QString AiService::openAiKey() const { return SecretStore::unprotect(m_openaiKeyEnc); }
QString AiService::anthropicKey() const { return SecretStore::unprotect(m_anthropicKeyEnc); }

QString AiService::suiteSummary() const
{
    QStringList lines;
    for (const QVariant& v : m_hub->products()) {
        const QVariantMap p = v.toMap();
        const QString state = p.value(QStringLiteral("installed")).toBool() ? QStringLiteral("installed, version ") + p.value(QStringLiteral("version")).toString()
                              : p.value(QStringLiteral("planned")).toBool() ? QStringLiteral("coming soon") : QStringLiteral("not installed");
        lines << QStringLiteral("- %1 (%2): %3").arg(p.value(QStringLiteral("name")).toString(), state, p.value(QStringLiteral("description")).toString());
    }
    return lines.join(QLatin1Char('\n'));
}

void AiService::loadSettings()
{
    const QJsonObject o = readJson(m_dir + QStringLiteral("/settings.json"));
    m_ask->applySettings(o.value(QLatin1String("ask")).toObject().toVariantMap());
    m_agent->applySettings(o.value(QLatin1String("agent")).toObject().toVariantMap());
    m_localModel = o.value(QLatin1String("local")).toObject().value(QLatin1String("model")).toString();
    const QJsonObject oa = o.value(QLatin1String("openai")).toObject();
    if (!oa.value(QLatin1String("baseUrl")).toString().isEmpty())
        m_openaiBase = oa.value(QLatin1String("baseUrl")).toString();
    m_openaiModel = oa.value(QLatin1String("model")).toString();
    m_openaiKeyEnc = oa.value(QLatin1String("key")).toString();
    const QJsonObject an = o.value(QLatin1String("anthropic")).toObject();
    if (!an.value(QLatin1String("model")).toString().isEmpty())
        m_anthropicModel = an.value(QLatin1String("model")).toString();
    m_anthropicKeyEnc = an.value(QLatin1String("key")).toString();
}

void AiService::saveSettings()
{
    QJsonObject o;
    o["ask"] = QJsonObject::fromVariantMap(m_ask->settingsValues());
    o["agent"] = QJsonObject::fromVariantMap(m_agent->settingsValues());
    o["local"] = QJsonObject{{"model", m_localModel}};
    o["openai"] = QJsonObject{{"baseUrl", m_openaiBase}, {"model", m_openaiModel}, {"key", m_openaiKeyEnc}};
    o["anthropic"] = QJsonObject{{"model", m_anthropicModel}, {"key", m_anthropicKeyEnc}};
    writeJson(m_dir + QStringLiteral("/settings.json"), o);
}

void AiService::setOpenAi(const QString& baseUrl, const QString& model, const QString& key)
{
    m_openaiBase = baseUrl.trimmed();
    m_openaiModel = model.trimmed();
    if (!key.trimmed().isEmpty())
        m_openaiKeyEnc = SecretStore::protect(key.trimmed());
    saveSettings();
    emit settingsChanged();
    m_ask->reloadReadiness();
}

void AiService::setAnthropic(const QString& model, const QString& key)
{
    m_anthropicModel = model.trimmed();
    if (!key.trimmed().isEmpty())
        m_anthropicKeyEnc = SecretStore::protect(key.trimmed());
    saveSettings();
    emit settingsChanged();
    m_ask->reloadReadiness();
}

void AiService::clearKey(const QString& provider)
{
    if (provider == QLatin1String("openai"))
        m_openaiKeyEnc.clear();
    else if (provider == QLatin1String("anthropic"))
        m_anthropicKeyEnc.clear();
    saveSettings();
    emit settingsChanged();
    m_ask->reloadReadiness();
}

void AiService::selectLocalModel(const QString& path)
{
    if (!path.endsWith(QLatin1String(".gguf"), Qt::CaseInsensitive) || !QFileInfo::exists(path))
        return;
    m_localModel = QDir::cleanPath(path);
    saveSettings();
    emit settingsChanged();
    m_ask->reloadReadiness();
}

QString AiService::downloadCatalog(const QString& id)
{
    for (const CatalogEntry& e : ModelStore::catalog()) {
        if (e.id != id)
            continue;
        m_downloadFile = modelsFolder() + QLatin1Char('/') + e.fileName;
        m_downloadTitle = e.title;
        emit downloadChanged();
        return m_downloader.downloadFile(e.url, modelsFolder(), e.fileName, e.size, e.sha256);
    }
    return QStringLiteral("unknown");
}

void AiService::setProbe(const QVariantMap& probe)
{
    m_probe = probe;
    emit probeChanged();
}

void AiService::probeLink(const QString& link)
{
    if (m_probeReply) {
        m_probeReply->disconnect(this);
        m_probeReply->abort();
        m_probeReply->deleteLater();
        m_probeReply = nullptr;
    }
    m_probeLink = ModelStore::parseHfLink(link);
    if (!m_probeLink.ok) {
        setProbe({{"state", "failed"}, {"error", m_probeLink.error}});
        return;
    }
    setProbe({{"state", "checking"}, {"repo", m_probeLink.repo}, {"fileName", m_probeLink.fileName}});
    QNetworkRequest req{QUrl(m_probeLink.url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);        // the headers we need are on the redirect
    req.setTransferTimeout(15000);
    m_probeReply = m_net->head(req);
    connect(m_probeReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply* reply = m_probeReply;
        m_probeReply = nullptr;
        if (!reply)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QMap<QString, QString> headers;
        for (const auto& pair : reply->rawHeaderPairs())
            headers.insert(QString::fromLatin1(pair.first).toLower(), QString::fromUtf8(pair.second));
        const QNetworkReply::NetworkError netError = reply->error();
        reply->deleteLater();
        if (status == 0 && netError != QNetworkReply::NoError) {
            setProbe({{"state", "failed"}, {"error", "network"}});
            return;
        }
        m_probeInfo = ModelStore::readHfHeaders(status, headers, m_probeLink);
        if (!m_probeInfo.ok) {
            setProbe({{"state", "failed"}, {"error", m_probeInfo.error}});
            return;
        }
        setProbe({{"state", "ready"}, {"repo", m_probeLink.repo}, {"fileName", m_probeLink.fileName}, {"size", double(m_probeInfo.size)},
                  {"sizeText", sizeText(m_probeInfo.size)}, {"sha256", m_probeInfo.sha256}});
    });
}

QString AiService::downloadProbed()
{
    if (m_probe.value(QStringLiteral("state")).toString() != QLatin1String("ready"))
        return QStringLiteral("notProbed");
    m_downloadFile = modelsFolder() + QLatin1Char('/') + m_probeLink.fileName;
    m_downloadTitle = m_probeLink.fileName;
    emit downloadChanged();
    return m_downloader.downloadFile(m_probeInfo.pinnedUrl, modelsFolder(), m_probeLink.fileName, m_probeInfo.size, m_probeInfo.sha256);
}

void AiService::cancelDownload() { m_downloader.cancel(); }

void AiService::detectPrograms()
{
    QVariantMap cli;
    for (const CliChat::Tool tool : {CliChat::Tool::Claude, CliChat::Tool::Codex}) {
        const QString name = CliChat::toolName(tool);
        const QString path = CliChat::executableFor(tool);
        cli[name] = QVariantMap{{"found", !path.isEmpty()}, {"path", path}, {"version", QString()}};
        if (path.isEmpty() || !m_probePrograms)
            continue;
        auto* p = new QProcess(this);
        hideConsole(p);
        connect(p, &QProcess::finished, this, [this, p, name](int code, QProcess::ExitStatus) {
            if (code == 0) {
                QVariantMap entry = m_cli.value(name).toMap();
                entry["version"] = QString::fromUtf8(p->readAllStandardOutput()).trimmed().left(80);
                m_cli[name] = entry;
                emit cliChanged();
            }
            p->deleteLater();
        });
        connect(p, &QProcess::errorOccurred, p, &QObject::deleteLater);
        QTimer::singleShot(10000, p, [p] { if (p->state() != QProcess::NotRunning) p->kill(); });
        p->start(path, {QStringLiteral("--version")});
    }
    m_cli = cli;
    emit cliChanged();
    m_ask->reloadReadiness();
    m_agent->reloadReadiness();
}

} // namespace dm::ai
