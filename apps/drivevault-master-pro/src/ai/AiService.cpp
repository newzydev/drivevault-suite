#include "AiService.h"

#include "Hub.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
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
    // The server window is LlamaServer::kContextTokens (32768). maxSteps is only a safety net against a model that never stops (the agent also notices a model that
    // repeats itself): a real job ends when the model says it is done, the user presses Stop, or the model is stuck.
    if (id == QLatin1String("low")) {                          // quick: no reasoning, short answers
        e.thinking = false;
        e.maxTokens = 2048;
        e.maxSteps = 60;
        e.historyChars = 16000;
    } else if (id == QLatin1String("high")) {                  // deep: the model reasons first (about 3 times slower), long answers
        e.thinking = true;
        e.maxTokens = 12288;                                   // the reasoning counts too
        e.maxSteps = 1000;
        e.historyChars = 40000;
    } else {
        e.id = QStringLiteral("medium");
        e.thinking = false;
        e.maxTokens = 8192;                                    // a whole page of a few hundred lines fits in one answer
        e.maxSteps = 400;
        e.historyChars = 40000;
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
    m_model = new MessageModel(this);
    connect(this, &AiChannel::messagesChanged, this, [this] { m_model->sync(m_currentId, messages()); });
    // The model server stops itself after 15 quiet minutes (LlamaServer). A job can run longer than that - a slow model writes for many minutes and the user may
    // take their time to answer a card - so a running request keeps it alive. (It was stopped in the middle of a 15 minute job: "Connection closed".)
    m_keepAlive = new QTimer(this);
    m_keepAlive->setInterval(60 * 1000);
    connect(m_keepAlive, &QTimer::timeout, this, [this] { m_service->llama()->touch(); });
    if (isAgent()) {                                               // the commands of the agent are background tasks that the page lists
        m_pool = new CommandPool(this);
        connect(m_pool, &CommandPool::changed, this, &AiChannel::tasksChanged);
        connect(service, &AiService::downloadChanged, this, &AiChannel::tasksChanged);
    }
}

AiChannel::~AiChannel()
{
    cancelRunning();
}

// ---- background tasks -------------------------------------------------------------------------------------------------------------------
QVariantList AiChannel::tasks() const
{
    QVariantList out;
    if (m_compacting) {
        qint64 started = 0;
        for (const Conversation& c : m_conversations)
            if (c.id == m_runId && !c.messages.isEmpty())
                started = c.messages.last().startedMs;
        out << QVariantMap{{"id", "compact"}, {"kind", "compact"}, {"title", QString()}, {"state", "running"}, {"startedMs", double(started)}, {"endedMs", 0.0}, {"note", QString()},
                           {"background", true}, {"progress", -1.0}};
    }
    if (isAgent() && m_service->downloading())
        out << QVariantMap{{"id", "download"}, {"kind", "download"}, {"title", m_service->downloadTitle()}, {"state", "running"}, {"startedMs", 0.0}, {"endedMs", 0.0},
                           {"note", QString()}, {"background", true}, {"progress", m_service->downloadProgress()}};
    if (m_pool)
        for (const QVariant& v : m_pool->list()) {
            QVariantMap t = v.toMap();
            t["kind"] = QStringLiteral("command");
            t["id"] = QStringLiteral("cmd:%1").arg(t.value("id").toInt());
            t["progress"] = -1.0;
            out << t;
        }
    return out;
}

bool AiChannel::tasksRunning() const
{
    return m_compacting || (isAgent() && m_service->downloading()) || (m_pool && m_pool->runningCount() > 0);
}

void AiChannel::startDiagnosticTasks()
{
    if (!m_pool)
        return;
    const QString folder = QDir::tempPath();
    m_pool->start(folder, QStringLiteral("echo built"), 0, false, [](const QString&, bool) {});
    m_pool->start(folder, QStringLiteral("dir /b & echo done"), 0, false, [](const QString&, bool) {});
    m_pool->start(folder, QStringLiteral("ping -n 30 127.0.0.1"), 0, true, [](const QString&, bool) {});
    m_pool->start(folder, QStringLiteral("npm run dev"), 0, true, [](const QString&, bool) {});      // not installed: it ends with an error, which the list shows too
}

void AiChannel::stopTask(const QString& id)
{
    if (id == QLatin1String("compact")) {
        if (m_compacting)
            stop();
    } else if (id == QLatin1String("download")) {
        m_service->cancelDownload();
    } else if (id.startsWith(QLatin1String("cmd:")) && m_pool) {
        m_pool->stop(id.mid(4).toInt());
    }
}

// The folder dialog gives a file: address; the agent needs a path. (QDir::cleanPath would first squeeze "file:///" to "file:/", so convert before cleaning.)
static QString localFolder(const QString& folder)
{
    QString f = folder.trimmed();
    if (f.startsWith(QLatin1String("file:"), Qt::CaseInsensitive))   // file:///C:/x and the squeezed file:/C:/x that 1.12.0 saved
        f = QUrl(f).toLocalFile();
    return f.isEmpty() ? f : QDir::cleanPath(f);
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
                                   {"errorDetail", m.errorDetail}, {"activity", m.activity}, {"approval", m.approval},
                                   {"startedMs", double(m.startedMs)}, {"durationMs", double(m.durationMs)}, {"steps", m.steps}, {"stepList", m.stepList},
                                   {"live", m.live}, {"compact", m.compact}, {"summary", m.summary}, {"tokensBefore", m.tokensBefore}, {"tokensAfter", m.tokensAfter}});
        }
    }
    return out;
}

// The conversations for the sidebar, newest first. On the agent page they come GROUPED by working folder: the groups are ordered by their newest
// conversation, inside a group newest first, and every entry says its `folder` (the full path, "" when it has none) and `group` (the name to show).
QVariantList AiChannel::conversations() const
{
    QList<const Conversation*> sorted;
    for (const Conversation& c : m_conversations)
        sorted << &c;
    std::sort(sorted.begin(), sorted.end(), [](const Conversation* a, const Conversation* b) { return a->updated > b->updated; });
    if (isAgent()) {
        QStringList order;                                         // the folders, the one with the newest conversation first (lower case: Windows paths)
        for (const Conversation* c : std::as_const(sorted))
            if (!order.contains(c->folder.toLower()))
                order << c->folder.toLower();
        std::stable_sort(sorted.begin(), sorted.end(), [&order](const Conversation* a, const Conversation* b) {
            return order.indexOf(a->folder.toLower()) < order.indexOf(b->folder.toLower());
        });
    }
    QVariantList out;
    for (const Conversation* c : std::as_const(sorted)) {
        QString name = c->folder.isEmpty() ? QString() : QDir(c->folder).dirName();
        if (name.isEmpty() && !c->folder.isEmpty())
            name = c->folder;                                      // a drive root
        out.append(QVariantMap{{"id", c->id}, {"title", c->title}, {"folder", c->folder}, {"group", name}});
    }
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
    const bool was = m_busy;
    m_busy = busy;
    m_status = status;
    if (busy && !was) {
        m_service->llama()->touch();
        m_keepAlive->start();
    } else if (!busy && was) {
        m_keepAlive->stop();
        m_service->llama()->touch();                               // the quiet time counts from the end of the work
    }
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
    c.folder = isAgent() ? m_folder : QString();                   // the sidebar puts it in the group of this folder
    m_conversations.append(c);
    m_currentId = c.id;
    emit conversationsChanged();
    return m_conversations.last();
}

QString AiChannel::send(const QString& text)
{
    QString t = text.trimmed();
    if (t.isEmpty())
        return QStringLiteral("empty");
    if (m_busy)
        return QStringLiteral("busy");
    if (t.startsWith(QLatin1Char('/'))) {
        QString prompt;
        if (slashCommand(t, &prompt)) {
            if (prompt.isEmpty())
                return {};                                         // done here: nothing goes to the model
            t = prompt;
        }
    }
    const QString nr = notReady();
    if (!nr.isEmpty())
        return nr;
    if (needsCompaction()) {                                       // like Claude Code: a long history is compacted first, then the message goes on
        startCompaction({}, true, t, {});
        return {};
    }
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
    reply.startedMs = QDateTime::currentMSecsSinceEpoch();
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
        r.system = systemPrompt() + summaryNote();
        QList<Message> past = find(m_runId)->messages;
        past.removeLast();                                         // the empty reply that is being written
        r.messages = historyFor(afterLastCompact(past), effort.historyChars);          // only what came after the last compaction (its summary is in the system prompt)
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
        c.allowCommands = isAgent() && canWrite();                 // ask and edit: a command always waits for the user
        c.planMode = isAgent() && m_permission == QLatin1String("plan");
        c.web = m_service->webEnabled();
        c.thinking = effort.thinking;
        c.maxTokens = effort.maxTokens;
        c.maxSteps = effort.maxSteps;
        c.contextTokens = server->contextTokens();                // the window the running server really has (it falls back to the small one when memory is short)
        if (server->isExternal())                                 // a server that was not started here (the tests): its window is unknown, assume the small one
            c.contextTokens = LlamaServer::kSmallContextTokens;
        const QString web = c.web ? QStringLiteral("\n\nYou can look things up on the Internet with web_search and fetch_page. Search when the answer needs current or unknown "
                                                   "facts (not for what you already know), open a page to read it, and say which pages you used (title and address).")
                                  : QString();
        if (isAgent()) {
            QString tools = QStringLiteral("list_files, read_file, search_text, glob_files, todo_write");
            if (c.allowEdits)
                tools += QStringLiteral(", edit_file, write_file, append_file");
            if (c.allowCommands)
                tools += QStringLiteral(", run_command");
            if (c.planMode)
                tools += QStringLiteral(", exit_plan_mode");
            QString modeText;
            if (m_permission == QLatin1String("read"))
                modeText = QStringLiteral("You can only read; you cannot change anything or run anything. ");
            else if (c.planMode)
                modeText = QStringLiteral("PLAN MODE: look at what you need with the read tools, then call exit_plan_mode with a short numbered plan (the steps and the files you will change). "
                                          "You cannot change anything or run anything until the user approves the plan. For a plain question just answer. ");
            else if (m_permission == QLatin1String("ask"))
                modeText = QStringLiteral("Every file change and every command is shown to the user, who may decline it. ");
            else
                modeText = QStringLiteral("File changes are applied at once (the old version is kept in .drivevault-backup); every command still waits for the user's OK. ");
            c.system = QStringLiteral("You are a careful coding and file assistant that works inside the folder the user chose, with tools: ") + tools + QStringLiteral(". ") + modeText
                       + QStringLiteral("How you work: (1) A small request (a question, one small change) you simply do. (2) A bigger job: first look around (list_files, glob_files, "
                                        "read the files that matter), then write a task list with todo_write, then do the items one by one and set each to completed when it is done. "
                                        "(3) Never invent what a file contains: read it first. To change an existing file prefer edit_file (a small exact replacement); use write_file for "
                                        "a new file. (4) After you change something, check it: read the file back")
                       + (c.allowCommands ? QStringLiteral(" or run the build or test") : QString())
                       + QStringLiteral(". (5) Do not stop and do not ask the user questions unless you are really blocked: go on until the whole job is done, then give a short summary of "
                                        "what you did and what you checked. (6) A tool error is information: read it, fix the cause and take another way; never repeat a call that failed. "
                                        "(7) Your answer has a length limit and a longer one is cut off and lost: keep every write_file and append_file under about 100 lines and build a big file in "
                                        "parts: the first part with write_file, then each next part with append_file (each starts exactly where the file ends); edit_file changes what is "
                                        "already in a file. A long file is read in parts (start_line). "
                                        "(8) To save room, old tool results in this conversation may be shortened: read the file again when you need its text. ")
                       + (c.allowCommands ? QStringLiteral("run_command runs on the user's PC with the user's permissions: use it only for what the job needs (build, test, git status, dir) "
                                                           "and never to delete, install or reinstall anything that was not asked for. ") : QString())
                       + QStringLiteral("Make only the changes the user asked for. Answer in the language of the user's message (Thai or English).")
                       + web;
            QString notesFile;
            const QString notes = projectNotes(&notesFile);
            if (!notes.isEmpty()) {
                c.system += QStringLiteral("\n\nProject instructions from %1 in this folder (follow them):\n").arg(notesFile) + notes;
                appendActivity(QStringLiteral("Project instructions: ") + notesFile);
                onStep(0, QStringLiteral("note"), QStringLiteral("Project instructions: ") + notesFile, QStringLiteral("done"), {});
            }
        } else {
            c.system = systemPrompt() + web;
        }
        QList<Message> past = find(m_runId)->messages;
        past.removeLast();
        c.history = historyFor(afterLastCompact(past), effort.historyChars);
        c.system += summaryNote();                                 // a compacted conversation: what happened before, in the model's own summary
        if (!m_localAgent) {
            m_localAgent = new LocalAgent(m_service->net(), this);
            if (m_pool)
                m_localAgent->setPool(m_pool);                                // its commands are the tasks of the page
            connect(m_localAgent, &LocalAgent::piece, this, &AiChannel::appendPiece);
            connect(m_localAgent, &LocalAgent::activity, this, &AiChannel::appendActivity);
            connect(m_localAgent, &LocalAgent::stepEvent, this, &AiChannel::onStep);
            connect(m_localAgent, &LocalAgent::serverLost, this, [this] {          // the model server stopped: start it again, then the agent asks again
                withLocalServer([this] {
                    if (m_localAgent)
                        m_localAgent->resume();
                });
            });
            connect(m_localAgent, &LocalAgent::generating, this, [this](const QString& phase, const QString& tool, const QString& target, int chars) {   // the live line
                Conversation* conv = find(m_runId);
                if (!conv || conv->messages.isEmpty())
                    return;
                Message& m = conv->messages.last();
                if (phase.isEmpty())
                    m.live.clear();
                else
                    m.live = QVariantMap{{"phase", phase}, {"tool", tool}, {"target", target}, {"chars", chars}};
                scheduleView();
            });
            connect(m_localAgent, &LocalAgent::usage, this, [this](int prompt, int completion) {          // what the server counted: the context meter of the page
                if (Conversation* conv = find(m_runId)) {
                    conv->usedTokens = prompt + completion;
                    emit contextChanged();
                }
            });
            connect(m_localAgent, &LocalAgent::finished, this, &AiChannel::finish);
            auto ask = [this](const QVariantMap& approval) {
                if (Conversation* conv = find(m_runId); conv && !conv->messages.isEmpty()) {
                    conv->messages.last().approval = approval;
                    setBusy(true, QStringLiteral("waiting"));
                    emit messagesChanged();
                }
            };
            connect(m_localAgent, &LocalAgent::approvalNeeded, this, [ask](const QString& path, bool replaces, qint64 bytes, const QString& preview, const QString& operation) {
                ask(QVariantMap{{"kind", "change"}, {"path", path}, {"replaces", replaces}, {"bytes", double(bytes)}, {"preview", preview}, {"op", operation},
                                {"diff", operation == QLatin1String("edit")}});
            });
            connect(m_localAgent, &LocalAgent::commandApprovalNeeded, this, [ask](const QString& command) { ask(QVariantMap{{"kind", "command"}, {"command", command}}); });
            connect(m_localAgent, &LocalAgent::planApprovalNeeded, this, [ask](const QString& plan) { ask(QVariantMap{{"kind", "plan"}, {"plan", plan}}); });
            connect(m_localAgent, &LocalAgent::planApproved, this, [this] {          // the work starts: the page goes to "ask" (every change and command is shown)
                m_permission = QStringLiteral("ask");
                emit settingsChanged();
                emit changed();
            });
            connect(m_localAgent, &LocalAgent::todosChanged, this, [this](const QVariantList& todos) {
                if (Conversation* conv = find(m_runId))                  // the plan belongs to the conversation (it is saved with it)
                    conv->todos = todos;
                emit todosChanged();
                saveConversations();
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
        Message& m = conv->messages.last();
        m.activity << text;
        ++m.steps;
        while (m.activity.size() > 300)                           // a long job: the newest 300 lines are kept
            m.activity.removeFirst();
        scheduleView();
    }
}

// A line of the step list: a new step is added, a known one (same number) changes its state / result. The list is rebuilt for the page at most every 60 ms.
void AiChannel::onStep(int no, const QString& kind, const QString& target, const QString& state, const QString& note)
{
    Conversation* conv = find(m_runId);
    if (!conv || conv->messages.isEmpty())
        return;
    Message& m = conv->messages.last();
    const QVariantMap entry{{"no", no}, {"kind", kind}, {"target", target}, {"state", state}, {"note", note}};
    bool found = false;
    if (no > 0) {
        for (int i = int(m.stepList.size()) - 1; i >= 0 && i >= int(m.stepList.size()) - 6; --i) {          // the step that changes is one of the newest
            if (m.stepList.at(i).toMap().value("no").toInt() == no) {
                m.stepList[i] = entry;
                found = true;
                break;
            }
        }
    }
    if (!found) {
        m.stepList << entry;
        while (m.stepList.size() > 400)
            m.stepList.removeFirst();
    }
    if (state == QLatin1String("done") && (kind == QLatin1String("edit") || kind == QLatin1String("write") || kind == QLatin1String("command")))
        scheduleGit();                                             // a file or a command may have changed the folder: the list of changes follows
    scheduleView();
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
    if (m_compacting) {                                            // the model could not be started for the summary
        finishCompaction(ok, detail, code);
        return;
    }
    QObject::disconnect(m_waitLocal);
    if (Conversation* c = find(m_runId); c && !c->messages.isEmpty()) {
        Message& m = c->messages.last();
        m.streaming = false;
        m.approval.clear();
        m.live.clear();
        if (m.startedMs > 0)
            m.durationMs = qMax<qint64>(1, QDateTime::currentMSecsSinceEpoch() - m.startedMs);
        for (int i = 0; i < m.stepList.size(); ++i) {              // a step that never ended (stopped, an error): it is not "running" for ever
            QVariantMap s = m.stepList.at(i).toMap();
            if (s.value("state").toString() == QLatin1String("running") || s.value("state").toString() == QLatin1String("waiting")) {
                s["state"] = QStringLiteral("stopped");
                m.stepList[i] = s;
            }
        }
        if (code == QLatin1String("cancelled"))
            m.stopped = true;
        else if (!ok) {
            m.errorCode = code.isEmpty() ? QStringLiteral("failed") : code;
            m.errorDetail = detail.size() > 600 ? detail.left(600) + QStringLiteral("…") : detail;      // a server may repeat a whole file in its message
        }
        c->updated = QDateTime::currentMSecsSinceEpoch();
    }
    setBusy(false);
    emit messagesChanged();
    emit conversationsChanged();
    emit contextChanged();
    saveConversations();
    scheduleGit();                                                 // the answer may have changed files
    if (m_localAgent && m_localAgent->running())                   // (e.g. the model server could not be started again: the agent waits no more)
        m_localAgent->cancel();
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
    if (m_compacting) {                                            // a summary is being written: stop it (the message that waited is not sent)
        m_pendingText.clear();
        ++m_generation;
        if (m_compactor && m_compactor->running())
            m_compactor->cancel();
        else
            finishCompaction(false, {}, QStringLiteral("cancelled"));
        return;
    }
    ++m_generation;
    const bool http = m_http && m_http->running();
    const bool agent = m_localAgent && m_localAgent->running();
    cancelRunning();
    if (!http && !agent)
        finish(false, QStringLiteral("cancelled"), {});      // still starting the model: nothing is running yet
}

QVariantList AiChannel::todos() const
{
    for (const Conversation& c : m_conversations)
        if (c.id == m_currentId)
            return c.todos;
    return {};
}

void AiChannel::clearTodos() { emit todosChanged(); }          // another conversation is shown: its own plan (or none)

// ---- slash commands ----------------------------------------------------------------------------------------------------------------------
void AiChannel::localExchange(const QString& userText, const QString& reply)
{
    Conversation& c = ensureConversation(userText);
    Message user;
    user.role = QStringLiteral("user");
    user.text = userText;
    Message answer;
    answer.role = QStringLiteral("assistant");
    answer.text = reply;
    c.messages << user << answer;
    c.updated = QDateTime::currentMSecsSinceEpoch();
    emit messagesChanged();
    emit conversationsChanged();
    saveConversations();
}

bool AiChannel::slashCommand(const QString& text, QString* prompt)
{
    prompt->clear();
    const QStringList parts = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    const QString cmd = parts.value(0).toLower();
    const QString arg = parts.value(1).toLower();
    Hub* hub = m_service->hub();
    auto tr = [hub](const QString& en, const QString& th) { return hub->tr2(en, th); };
    const bool agent = isAgent();

    if (cmd == QLatin1String("/help")) {
        QString en = QStringLiteral("Commands:\n- /help  this list\n- /clear (or /new)  start a new conversation\n- /compact [wish]  the AI summarizes the conversation so far and goes on with the summary (also automatic when the history gets long)\n- /effort low|medium|high  how much the model works\n- /web on|off  let the AI search the web");
        QString th = QString::fromUtf8("คำสั่ง:\n- /help  รายการนี้\n- /clear (หรือ /new)  เริ่มบทสนทนาใหม่\n- /compact [ข้อความ]  ให้ AI สรุปบทสนทนาที่ผ่านมาแล้วทำต่อด้วยสรุปนั้น (ทำเองอัตโนมัติเมื่อประวัติยาว)\n- /effort low|medium|high  ระดับความพยายามของโมเดล\n- /web on|off  ให้ AI ค้นเว็บหรือไม่");
        if (agent) {
            en += QStringLiteral("\n- /mode read|plan|ask|edit  how the agent may work (Shift+Tab changes it)\n- /todos  show the task list\n- /init  write a CLAUDE.md for this folder\n\nEsc stops the work.");
            th += QString::fromUtf8("\n- /mode read|plan|ask|edit  เอเจนต์ทำอะไรได้บ้าง (กด Shift+Tab เพื่อสลับ)\n- /todos  แสดงรายการงาน\n- /init  สร้างไฟล์ CLAUDE.md ของโฟลเดอร์นี้\n\nกด Esc เพื่อหยุดงาน");
        }
        localExchange(text, tr(en, th));
        return true;
    }
    if (cmd == QLatin1String("/clear") || cmd == QLatin1String("/new")) {
        newConversation();
        return true;
    }
    if (cmd == QLatin1String("/effort")) {
        if (!Effort::ids().contains(arg)) {
            localExchange(text, tr(QStringLiteral("Effort now: %1. Use /effort low, medium or high.").arg(m_effort),
                                   QString::fromUtf8("ระดับตอนนี้: %1 ใช้ /effort low, medium หรือ high").arg(m_effort)));
            return true;
        }
        setEffort(arg);
        localExchange(text, tr(QStringLiteral("Effort: %1.").arg(m_effort), QString::fromUtf8("ตั้งระดับเป็น %1 แล้ว").arg(m_effort)));
        return true;
    }
    if (cmd == QLatin1String("/web")) {
        if (arg != QLatin1String("on") && arg != QLatin1String("off")) {
            localExchange(text, tr(QStringLiteral("Web is %1. Use /web on or /web off.").arg(m_service->webEnabled() ? QStringLiteral("on") : QStringLiteral("off")),
                                   QString::fromUtf8("เว็บ%1อยู่ ใช้ /web on หรือ /web off").arg(m_service->webEnabled() ? QString::fromUtf8("เปิด") : QString::fromUtf8("ปิด"))));
            return true;
        }
        m_service->setWebEnabled(arg == QLatin1String("on"));
        localExchange(text, tr(QStringLiteral("Web: %1.").arg(arg), QString::fromUtf8("เว็บ: %1").arg(arg == QLatin1String("on") ? QString::fromUtf8("เปิด") : QString::fromUtf8("ปิด"))));
        return true;
    }
    if (cmd == QLatin1String("/compact")) {
        const QString focus = text.mid(8).trimmed();
        Conversation* c = current();
        if (!c || afterLastCompact(c->messages).size() < 2) {
            localExchange(text, tr(QStringLiteral("There is nothing to compact yet."), QString::fromUtf8("ยังไม่มีอะไรให้บีบอัด")));
            return true;
        }
        if (!notReady().isEmpty()) {
            localExchange(text, tr(QStringLiteral("The AI is not ready, so the conversation cannot be compacted now (see the note above the message box)."),
                                   QString::fromUtf8("AI ยังไม่พร้อม จึงบีบอัดตอนนี้ไม่ได้ (ดูข้อความเหนือช่องพิมพ์)")));
            return true;
        }
        startCompaction(focus, false, {}, text);
        return true;
    }
    if (!agent)
        return false;
    if (cmd == QLatin1String("/mode")) {
        QString want = arg;
        if (want == QLatin1String("default"))
            want = QStringLiteral("ask");
        else if (want == QLatin1String("accept") || want == QLatin1String("acceptedits"))
            want = QStringLiteral("edit");
        else if (want == QLatin1String("readonly"))
            want = QStringLiteral("read");
        if (!permissions().contains(want)) {
            localExchange(text, tr(QStringLiteral("Mode now: %1. Use /mode read, plan, ask or edit.").arg(m_permission),
                                   QString::fromUtf8("โหมดตอนนี้: %1 ใช้ /mode read, plan, ask หรือ edit").arg(m_permission)));
            return true;
        }
        setPermission(want);
        localExchange(text, tr(QStringLiteral("Mode: %1.").arg(m_permission), QString::fromUtf8("เปลี่ยนโหมดเป็น %1 แล้ว").arg(m_permission)));
        return true;
    }
    if (cmd == QLatin1String("/todos")) {
        QStringList lines;
        for (const QVariant& v : todos()) {
            const QVariantMap t = v.toMap();
            const QString s = t.value("status").toString();
            lines << QStringLiteral("%1 %2").arg(s == QLatin1String("completed") ? QStringLiteral("[x]") : s == QLatin1String("in_progress") ? QStringLiteral("[>]") : QStringLiteral("[ ]"),
                                                 t.value("content").toString());
        }
        localExchange(text, lines.isEmpty() ? tr(QStringLiteral("There is no task list yet."), QString::fromUtf8("ยังไม่มีรายการงาน")) : lines.join(QLatin1Char('\n')));
        return true;
    }
    if (cmd == QLatin1String("/init")) {
        *prompt = QStringLiteral("Look at the files in this folder (list them, read the main ones) and write a file CLAUDE.md in this folder for future work here: what the project is, "
                                 "how it is built, run and tested, the folder layout, and the rules that someone working here must follow. Keep it short and only write what you saw in the files.");
        return true;
    }
    return false;
}

// CLAUDE.md (or AGENTS.md) of the working folder: the project's own instructions for the agent
QString AiChannel::projectNotes(QString* fileName) const
{
    if (m_folder.isEmpty())
        return {};
    for (const QString& name : {QStringLiteral("CLAUDE.md"), QStringLiteral("AGENTS.md")}) {
        QFile f(QDir(m_folder).filePath(name));
        if (!QFileInfo(f).isFile() || !f.open(QIODevice::ReadOnly))
            continue;
        const QString text = QString::fromUtf8(f.read(6000)).trimmed();
        if (text.isEmpty())
            continue;
        *fileName = name;
        return f.size() > 6000 ? text + QStringLiteral("\n... (cut)") : text;
    }
    return {};
}

void AiChannel::newConversation()
{
    m_currentId.clear();
    clearTodos();
    emit contextChanged();
    emit messagesChanged();
    emit conversationsChanged();
}

void AiChannel::select(const QString& id)
{
    if (!find(id) || id == m_currentId)
        return;
    m_currentId = id;
    // the conversation belongs to a folder: work goes on there (and git shows that folder)
    if (isAgent() && !m_busy) {
        const QString folder = find(id)->folder;
        if (!folder.isEmpty() && QFileInfo(folder).isDir() && folder.compare(m_folder, Qt::CaseInsensitive) != 0) {
            m_folder = folder;
            emit settingsChanged();
            emit changed();
            scheduleGit();
        }
    }
    clearTodos();
    emit contextChanged();
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
    const QString f = localFolder(folder);
    if (m_busy || f == m_folder)
        return;
    m_folder = f;
    // another folder is another piece of work: a conversation that already has messages stays in its own group and the next message starts a new one
    if (Conversation* c = current(); c && !c->messages.isEmpty() && c->folder.compare(f, Qt::CaseInsensitive) != 0) {
        m_currentId.clear();
        emit messagesChanged();
        emit conversationsChanged();
        emit todosChanged();
        emit contextChanged();
    }
    emit settingsChanged();
    emit changed();
    scheduleGit();
}

// the "+" of a group in the sidebar
void AiChannel::newConversationIn(const QString& folder)
{
    if (m_busy)
        return;
    if (!folder.isEmpty())
        setFolder(folder);
    newConversation();
}

// ---- git of the working folder (agent page) ----------------------------------------------------------------------------------------------
void AiChannel::refreshGit()
{
    if (!isAgent())
        return;
    if (!m_git) {
        m_git = new GitInfo(this);
        connect(m_git, &GitInfo::changed, this, &AiChannel::gitChanged);
    }
    m_git->refresh(m_folder);
}

void AiChannel::scheduleGit()
{
    if (!isAgent())
        return;
    if (!m_gitTimer) {
        m_gitTimer = new QTimer(this);
        m_gitTimer->setSingleShot(true);
        m_gitTimer->setInterval(500);
        connect(m_gitTimer, &QTimer::timeout, this, &AiChannel::refreshGit);
    }
    m_gitTimer->start();
}

// How much of the model window the open conversation uses: the number the server reported for the last request when there is one, else an estimate
// from the text (about 2.5 characters to a token).
QVariantMap AiChannel::context() const
{
    int window = m_service->llama()->isExternal() ? LlamaServer::kSmallContextTokens : m_service->llama()->contextTokens();
    int used = 0, history = 0;
    bool exact = false, canCompact = false;
    for (const Conversation& c : m_conversations) {
        if (c.id != m_currentId)
            continue;
        const QList<Message> active = afterLastCompact(c.messages);
        qint64 chars = 0;                                          // the text that the next request carries from this conversation: the summary and what came after it
        for (const Message& m : active)
            chars += m.text.size();
        const int before = int(c.messages.size()) - int(active.size()) - 1;              // the card of the last compaction (when there is one)
        if (before >= 0 && c.messages.at(before).compact)
            chars += c.messages.at(before).summary.size();
        history = int(chars * 2 / 5);
        canCompact = !m_busy && active.size() >= 2;
        if (c.usedTokens > 0) {
            used = c.usedTokens;
            exact = true;
        } else {
            used = history;
        }
    }
    const int percent = window > 0 ? qMin(100, int(qint64(used) * 100 / window)) : 0;
    const int autoAt = int(Effort::of(m_effort).historyChars * 3 / 4 * 2 / 5);          // the history size (tokens) at which the next message compacts first
    return QVariantMap{{"used", used}, {"window", window}, {"percent", percent}, {"exact", exact}, {"history", history}, {"autoAt", autoAt}, {"canCompact", canCompact}};
}

// ---- compacting the conversation (like Claude Code's /compact and its automatic compaction) ---------------------------------------------------
// The messages the next request carries: those after the last FINISHED compaction card.
QList<AiChannel::Message> AiChannel::afterLastCompact(const QList<Message>& messages)
{
    for (int i = int(messages.size()) - 1; i >= 0; --i)
        if (messages.at(i).compact && !messages.at(i).summary.isEmpty())
            return messages.mid(i + 1);
    return messages;
}

QString AiChannel::summaryNote() const
{
    for (const Conversation& c : m_conversations) {
        if (c.id != m_runId)
            continue;
        for (int i = int(c.messages.size()) - 1; i >= 0; --i)
            if (c.messages.at(i).compact && !c.messages.at(i).summary.isEmpty())
                return QStringLiteral("\n\nThe conversation so far was compacted to save room. This is the summary of what happened before the messages below; rely on it:\n")
                       + c.messages.at(i).summary;
    }
    return {};
}

// the history that would go along has reached three quarters of what is kept (Effort::historyChars): older messages would be cut off in a moment, so they are
// summarized first
bool AiChannel::needsCompaction() const
{
    for (const Conversation& c : m_conversations) {
        if (c.id != m_currentId)
            continue;
        const QList<Message> active = afterLastCompact(c.messages);
        qint64 chars = 0;
        int count = 0;
        for (const Message& m : active) {
            if (m.text.trimmed().isEmpty())
                continue;
            chars += m.text.size();
            ++count;
        }
        return count >= 4 && chars >= qint64(Effort::of(m_effort).historyChars) * 3 / 4;
    }
    return false;
}

// What the model summarizes: an earlier summary (if any), then every message with the steps the AI took and how the answer ended, and the plan.
QString AiChannel::transcriptFor(const Conversation& conversation) const
{
    QString out;
    int start = 0;
    for (int i = int(conversation.messages.size()) - 1; i >= 0; --i)
        if (conversation.messages.at(i).compact && !conversation.messages.at(i).summary.isEmpty()) {
            out += QStringLiteral("[Summary of the conversation before this part]\n") + conversation.messages.at(i).summary + QStringLiteral("\n\n");
            start = i + 1;
            break;
        }
    for (int i = start; i < conversation.messages.size(); ++i) {
        const Message& m = conversation.messages.at(i);
        if (m.compact || (m.text.trimmed().isEmpty() && m.stepList.isEmpty()))
            continue;
        if (m.role == QLatin1String("user")) {
            out += QStringLiteral("User: ") + m.text.trimmed() + QStringLiteral("\n\n");
            continue;
        }
        QStringList steps;
        for (const QVariant& v : m.stepList) {
            const QVariantMap s = v.toMap();
            if (s.value("kind").toString() == QLatin1String("note"))
                continue;
            steps << QStringLiteral("%1 %2 (%3)").arg(s.value("kind").toString(), s.value("target").toString().left(100), s.value("state").toString());
            if (steps.size() >= 60)
                break;
        }
        out += QStringLiteral("Assistant: ") + m.text.trimmed();
        if (!steps.isEmpty())
            out += QStringLiteral("\n[Steps taken: ") + steps.join(QStringLiteral("; ")) + QLatin1Char(']');
        if (!m.errorCode.isEmpty())
            out += QStringLiteral("\n[This answer ended with an error: ") + m.errorCode + QLatin1Char(']');
        out += QStringLiteral("\n\n");
    }
    if (!conversation.todos.isEmpty()) {
        QStringList plan;
        for (const QVariant& v : conversation.todos) {
            const QString status = v.toMap().value("status").toString();
            plan << QStringLiteral("[%1] %2").arg(status == QLatin1String("completed") ? QStringLiteral("x") : status == QLatin1String("in_progress") ? QStringLiteral(">") : QStringLiteral(" "),
                                                  v.toMap().value("content").toString());
        }
        out += QStringLiteral("Plan of the assistant now: ") + plan.join(QStringLiteral("; ")) + QStringLiteral("\n");
    }
    // it must fit in the window together with the answer: a longer text keeps its beginning and (more of) its end
    const int window = m_service->llama()->isExternal() ? LlamaServer::kSmallContextTokens : m_service->llama()->contextTokens();
    const int budget = qMax(6000, (window - Compactor::kSummaryTokens - 1500) * 5 / 2);
    if (out.size() > budget) {
        const int head = budget / 5;
        out = out.left(head) + QStringLiteral("\n... (the middle was left out because it is too long) ...\n") + out.right(budget - head);
    }
    return out;
}

QString AiChannel::compactNow(const QString& focus)
{
    if (m_busy)
        return QStringLiteral("busy");
    const QString nr = notReady();
    if (!nr.isEmpty())
        return nr;
    Conversation* c = current();
    if (!c || afterLastCompact(c->messages).size() < 2)
        return QStringLiteral("nothing");
    startCompaction(focus, false, {}, {});
    return {};
}

void AiChannel::startCompaction(const QString& focus, bool automatic, const QString& pendingText, const QString& userText)
{
    Q_UNUSED(automatic)
    Conversation& c = ensureConversation(userText.isEmpty() ? QStringLiteral("…") : userText);
    const QString transcript = transcriptFor(c);                    // before the card and the "/compact" message are added
    m_compactBefore = context().value("history").toInt();
    if (!userText.isEmpty()) {
        Message user;
        user.role = QStringLiteral("user");
        user.text = userText;
        c.messages << user;
    }
    Message card;
    card.role = QStringLiteral("assistant");
    card.compact = true;
    card.streaming = true;
    card.startedMs = QDateTime::currentMSecsSinceEpoch();
    c.messages << card;
    c.updated = QDateTime::currentMSecsSinceEpoch();
    m_runId = c.id;
    ++m_generation;
    m_compacting = true;
    m_pendingText = pendingText;
    setBusy(true, QStringLiteral("compacting"));
    emit tasksChanged();
    emit messagesChanged();
    emit conversationsChanged();
    saveConversations();
    withLocalServer([this, focus, transcript] {
        LlamaServer* server = m_service->llama();
        if (!m_compactor) {
            m_compactor = new Compactor(m_service->net(), this);
            connect(m_compactor, &Compactor::finished, this, &AiChannel::finishCompaction);
        }
        const QString err = m_compactor->start(server->baseUrl() + QStringLiteral("/v1"), server->apiKey(), transcript, focus, Compactor::kSummaryTokens);
        if (!err.isEmpty())
            finishCompaction(false, {}, err);
        server->touch();
    });
}

void AiChannel::finishCompaction(bool ok, const QString& summary, const QString& error)
{
    if (!m_compacting)
        return;
    m_compacting = false;
    QObject::disconnect(m_waitLocal);
    if (Conversation* c = find(m_runId); c && !c->messages.isEmpty() && c->messages.last().compact) {
        Message& card = c->messages.last();
        card.streaming = false;
        card.durationMs = qMax<qint64>(1, QDateTime::currentMSecsSinceEpoch() - card.startedMs);
        if (ok) {
            card.summary = summary;
            card.tokensBefore = m_compactBefore;
            card.tokensAfter = int(summary.size() * 2 / 5);
            c->usedTokens = 0;                                      // what the server counted is out of date now
        } else if (error == QLatin1String("cancelled")) {
            card.stopped = true;
        } else {
            card.errorCode = error.isEmpty() ? QStringLiteral("failed") : error;
            card.errorDetail = summary.left(600);
        }
        c->updated = QDateTime::currentMSecsSinceEpoch();
    }
    setBusy(false);
    emit tasksChanged();
    emit messagesChanged();
    emit conversationsChanged();
    emit contextChanged();
    saveConversations();
    const QString pending = m_pendingText;
    m_pendingText.clear();
    if (!pending.isEmpty())                                         // an automatic compaction: the message that waited goes on (also when the summary failed)
        beginRequest(pending);
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
    m_folder = localFolder(v.value(QStringLiteral("folder")).toString());      // 1.12.0 saved the dialog's file: address
    m_permission = v.value(QStringLiteral("permission")).toString();
    if (!permissions().contains(m_permission))
        m_permission = v.value(QStringLiteral("allowEdits")).toBool() ? QStringLiteral("edit") : QStringLiteral("ask");     // an older settings file: the old switch
    emit settingsChanged();
    scheduleGit();
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
            if (m.durationMs > 0) {
                o["durationMs"] = double(m.durationMs);
                o["steps"] = m.steps;
            }
            if (m.compact) {
                o["compact"] = true;
                o["summary"] = m.summary;
                o["tokensBefore"] = m.tokensBefore;
                o["tokensAfter"] = m.tokensAfter;
            }
            if (!m.stepList.isEmpty()) {
                QJsonArray sl;
                for (const QVariant& v : m.stepList) {
                    const QVariantMap s = v.toMap();
                    sl.append(QJsonObject{{"kind", s.value("kind").toString()}, {"target", s.value("target").toString().left(140)}, {"state", s.value("state").toString()},
                                          {"note", s.value("note").toString().left(160)}});
                }
                o["stepList"] = sl;
            }
            msgs.append(o);
        }
        QJsonArray plan;
        for (const QVariant& v : c.todos)
            plan.append(QJsonObject{{"content", v.toMap().value("content").toString()}, {"status", v.toMap().value("status").toString()}});
        list.append(QJsonObject{{"id", c.id}, {"title", c.title}, {"updated", double(c.updated)}, {"messages", msgs}, {"todos", plan}, {"folder", c.folder}});
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
        c.folder = o.value(QLatin1String("folder")).toString();
        for (const QJsonValue& tv : o.value(QLatin1String("todos")).toArray())
            c.todos << QVariantMap{{"content", tv.toObject().value(QLatin1String("content")).toString()}, {"status", tv.toObject().value(QLatin1String("status")).toString()}};
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
            m.durationMs = qint64(mo.value(QLatin1String("durationMs")).toDouble());
            m.steps = mo.value(QLatin1String("steps")).toInt();
            m.compact = mo.value(QLatin1String("compact")).toBool();
            m.summary = mo.value(QLatin1String("summary")).toString();
            m.tokensBefore = mo.value(QLatin1String("tokensBefore")).toInt();
            m.tokensAfter = mo.value(QLatin1String("tokensAfter")).toInt();
            if (m.compact && m.summary.isEmpty() && m.errorCode.isEmpty())
                m.stopped = true;                                           // a compaction that never finished (the program was closed)
            int n = 0;
            for (const QJsonValue& sv : mo.value(QLatin1String("stepList")).toArray()) {
                const QJsonObject so = sv.toObject();
                m.stepList << QVariantMap{{"no", ++n}, {"kind", so.value(QLatin1String("kind")).toString()}, {"target", so.value(QLatin1String("target")).toString()},
                                          {"state", so.value(QLatin1String("state")).toString()}, {"note", so.value(QLatin1String("note")).toString()}};
            }
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
