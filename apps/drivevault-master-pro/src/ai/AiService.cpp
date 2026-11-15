#include "AiService.h"

#include "Hub.h"

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

} // namespace

Effort Effort::of(const QString& id)
{
    Effort e;
    e.id = id;
    if (id == QLatin1String("low")) {                          // quick: no reasoning, short answers, few tool rounds
        e.thinking = false;
        e.maxTokens = 1024;
        e.maxSteps = 6;
        e.historyChars = 8000;
    } else if (id == QLatin1String("high")) {                  // deep: the model reasons first (about 3 times slower), longer answers, more tool rounds
        e.thinking = true;
        e.maxTokens = 4096;
        e.maxSteps = 20;
        e.historyChars = 9000;                                 // the reasoning needs room in the 8192 token window
    } else {
        e.id = QStringLiteral("medium");
        e.thinking = false;
        e.maxTokens = 3000;
        e.maxSteps = 12;
        e.historyChars = 14000;
    }
    return e;
}

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

QString AiChannel::notReady() const
{
    if (!m_service->engineFound())
        return QStringLiteral("noEngine");
    const QString model = m_service->localModel();
    if (model.isEmpty() || !QFileInfo::exists(model))
        return QStringLiteral("noModel");
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
                                   {"errorDetail", m.errorDetail}, {"activity", m.activity}, {"approval", m.approval}});
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

    if (isAgent() || m_service->webEnabled())
        sendTools();                                               // tools: the folder (agent) and / or the web
    else
        sendChat();
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

QString AiChannel::systemPrompt() const
{
    if (isAgent())
        return {};
    return QStringLiteral("You are the assistant inside DriveVault Master Pro, the hub of the DriveVault suite of Windows programs. Answer clearly and briefly, in the "
                          "language of the user's question (Thai or English). You can only talk: you cannot open, change or install anything on this computer, and you "
                          "must not claim that you did. If you do not know something, say so.\n\nThe programs of the suite:\n")
           + m_service->suiteSummary();
}

void AiChannel::withLocalServer(const std::function<void()>& run)
{
    LlamaServer* server = m_service->llama();
    const QString model = m_service->localModel();
    const int gen = m_generation;
    auto go = [this, gen, run] {
        if (gen != m_generation || !m_busy)
            return;
        run();
    };
    if (server->state() == LlamaServer::State::Ready && (server->isExternal() || server->modelPath() == model)) {
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

// A plain streamed chat (Q&A with the web switched off).
void AiChannel::sendChat()
{
    withLocalServer([this] {
        LlamaServer* server = m_service->llama();
        const Effort effort = Effort::of(m_effort);
        HttpChat::Request r;
        r.baseUrl = server->baseUrl() + QStringLiteral("/v1");
        r.apiKey = server->apiKey();
        r.model = QStringLiteral("local");
        r.system = systemPrompt();
        QList<Message> past = find(m_runId)->messages;
        past.removeLast();                                         // the empty reply that is being written
        r.messages = historyFor(past, effort.historyChars);
        r.extraBody = QJsonObject{{"chat_template_kwargs", QJsonObject{{"enable_thinking", effort.thinking}}}, {"max_tokens", effort.maxTokens}};
        if (!m_http) {
            m_http = new HttpChat(m_service->net(), this);
            connect(m_http, &HttpChat::piece, this, &AiChannel::appendPiece);
            connect(m_http, &HttpChat::thinking, this, [this] { setStatus(QStringLiteral("thinking")); });
            connect(m_http, &HttpChat::finished, this, &AiChannel::finish);
        }
        const QString err = m_http->start(r);
        if (!err.isEmpty())
            finish(false, err, {});
        else
            setStatus(QStringLiteral("starting"));
        server->touch();
    });
}

// The agent loop: tools are the folder (agent page) and / or the web.
void AiChannel::sendTools()
{
    withLocalServer([this] {
        LlamaServer* server = m_service->llama();
        const Effort effort = Effort::of(m_effort);
        LocalAgent::Config c;
        c.baseUrl = server->baseUrl() + QStringLiteral("/v1");
        c.apiKey = server->apiKey();
        c.folder = isAgent() ? m_folder : QString();
        c.allowEdits = isAgent() && canWrite();
        c.approveWrites = isAgent() && m_permission == QLatin1String("ask");
        c.web = m_service->webEnabled();
        c.thinking = effort.thinking;
        c.maxTokens = effort.maxTokens;
        c.maxSteps = effort.maxSteps;
        const QString web = c.web ? QStringLiteral("\n\nYou can look things up on the Internet with web_search and fetch_page. Search when the answer needs current or unknown "
                                                   "facts (not for what you already know), open a page to read it, and say which pages you used (title and address).")
                                  : QString();
        if (isAgent()) {
            c.system = QStringLiteral("You are an assistant that works inside the folder the user chose, with tools: list_files, read_file, search_text")
                       + (canWrite() ? QStringLiteral(" and write_file. ") : QStringLiteral(". You can only read; you cannot change anything. "))
                       + (m_permission == QLatin1String("plan") ? QStringLiteral("The user wants a PLAN: look at what you need, then answer with a short numbered list of the steps you would take and the files you would change. Do not change anything and do not claim that you did. ") : QString())
                       + (m_permission == QLatin1String("ask") ? QStringLiteral("Every file change is shown to the user, who may decline it. ") : QString())
                       + QStringLiteral("Look at the files before you answer or change anything, and never invent what a file contains. Make only the changes the user asked "
                                        "for, then say briefly what you did. You cannot run programs. Answer in the language of the user's message (Thai or English).")
                       + web;
        } else {
            c.system = systemPrompt() + web;
        }
        QList<Message> past = find(m_runId)->messages;
        past.removeLast();
        c.history = historyFor(past, effort.historyChars);
        if (!m_localAgent) {
            m_localAgent = new LocalAgent(m_service->net(), this);
            connect(m_localAgent, &LocalAgent::piece, this, &AiChannel::appendPiece);
            connect(m_localAgent, &LocalAgent::activity, this, &AiChannel::appendActivity);
            connect(m_localAgent, &LocalAgent::finished, this, &AiChannel::finish);
            connect(m_localAgent, &LocalAgent::approvalNeeded, this, [this](const QString& path, bool replaces, qint64 bytes, const QString& preview) {
                if (Conversation* conv = find(m_runId); conv && !conv->messages.isEmpty()) {
                    conv->messages.last().approval = QVariantMap{{"path", path}, {"replaces", replaces}, {"bytes", double(bytes)}, {"preview", preview}};
                    setBusy(true, QStringLiteral("waiting"));
                    emit messagesChanged();
                }
            });
        }
        const QString err = m_localAgent->start(c);
        if (!err.isEmpty())
            finish(false, err, {});
        else
            setStatus(effort.thinking ? QStringLiteral("thinking") : QStringLiteral("starting"));
        server->touch();
    });
}

void AiChannel::appendActivity(const QString& text)
{
    if (Conversation* conv = find(m_runId); conv && !conv->messages.isEmpty()) {
        conv->messages.last().activity << text;
        scheduleView();
    }
}

void AiChannel::setStatus(const QString& status) { setBusy(true, status); }
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
        m.approval.clear();
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
    if (m_localAgent && m_localAgent->running())
        m_localAgent->cancel();
}

void AiChannel::stop()
{
    if (!m_busy)
        return;
    ++m_generation;
    const bool http = m_http && m_http->running();
    const bool agent = m_localAgent && m_localAgent->running();
    cancelRunning();
    if (!http && !agent)
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

void AiChannel::setEffort(const QString& effort)
{
    const QString e = Effort::of(effort).id;                   // an unknown name becomes "medium"
    if (m_busy || e == m_effort)
        return;
    m_effort = e;
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

void AiChannel::setPermission(const QString& permission)
{
    if (m_busy || !permissions().contains(permission) || permission == m_permission)
        return;
    m_permission = permission;
    emit settingsChanged();
    emit changed();
}

void AiChannel::answerApproval(bool allow)
{
    if (!m_localAgent || !m_localAgent->waitingForApproval())
        return;
    if (Conversation* conv = find(m_runId); conv && !conv->messages.isEmpty())
        conv->messages.last().approval.clear();
    setBusy(true, QStringLiteral("working"));
    emit messagesChanged();
    m_localAgent->answerApproval(allow);
}
QVariantMap AiChannel::settingsValues() const { return {{"folder", m_folder}, {"permission", m_permission}, {"effort", m_effort}}; }

void AiChannel::applySettings(const QVariantMap& v)
{
    m_effort = Effort::of(v.value(QStringLiteral("effort")).toString()).id;
    m_folder = v.value(QStringLiteral("folder")).toString();
    m_permission = v.value(QStringLiteral("permission")).toString();
    if (!permissions().contains(m_permission))
        m_permission = v.value(QStringLiteral("allowEdits")).toBool() ? QStringLiteral("edit") : QStringLiteral("ask");     // an older settings file: the old switch
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
        list.append(QJsonObject{{"id", c.id}, {"title", c.title}, {"updated", double(c.updated)}, {"messages", msgs}});
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

AiService::AiService(Hub* hub, QObject* parent)
    : QObject(parent)
    , m_hub(hub)
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
        m_agent->reloadReadiness();
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
    pickModelIfNone();
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

void AiService::pickModelIfNone()
{
    if (!m_localModel.isEmpty() && QFileInfo::exists(m_localModel))
        return;
    const auto models = ModelStore::installed(modelFolders());
    if (models.isEmpty())
        return;
    QString best = models.first().path;
    const QString preferred = ModelStore::catalog().at(2).fileName;      // Qwen3.5 4B: the tested default
    for (const InstalledModel& m : models)
        if (m.name == preferred)
            best = m.path;
    m_localModel = best;
    saveSettings();
}

void AiService::refreshModels()
{
    pickModelIfNone();
    emit modelsChanged();
    emit settingsChanged();
}

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

QString AiService::localModelName() const { return m_localModel.isEmpty() ? QString() : QFileInfo(m_localModel).fileName(); }

void AiService::loadSettings()
{
    const QJsonObject o = readJson(m_dir + QStringLiteral("/settings.json"));
    m_ask->applySettings(o.value(QLatin1String("ask")).toObject().toVariantMap());
    m_agent->applySettings(o.value(QLatin1String("agent")).toObject().toVariantMap());
    m_localModel = o.value(QLatin1String("local")).toObject().value(QLatin1String("model")).toString();
    m_web = o.value(QLatin1String("web")).toBool(true);
}

void AiService::saveSettings()
{
    QJsonObject o;
    o["ask"] = QJsonObject::fromVariantMap(m_ask->settingsValues());
    o["agent"] = QJsonObject::fromVariantMap(m_agent->settingsValues());
    o["local"] = QJsonObject{{"model", m_localModel}};
    o["web"] = m_web;


    writeJson(m_dir + QStringLiteral("/settings.json"), o);
}

void AiService::setWebEnabled(bool on)
{
    if (on == m_web)
        return;
    m_web = on;
    saveSettings();
    emit settingsChanged();
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

} // namespace dm::ai
