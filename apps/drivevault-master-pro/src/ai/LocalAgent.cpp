#include "LocalAgent.h"

#include "HttpChat.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace dm::ai {

static constexpr int kMaxWebCalls = 4;                     // web_search + fetch_page calls in one task

namespace {

QJsonObject toolSpec(const char* name, const QString& description, const QJsonObject& properties, const QStringList& required)
{
    QJsonArray req;
    for (const QString& r : required)
        req.append(r);
    return QJsonObject{{"type", "function"},
                       {"function", QJsonObject{{"name", QLatin1String(name)}, {"description", description},
                                                {"parameters", QJsonObject{{"type", "object"}, {"properties", properties}, {"required", req}}}}}};
}

// todo_write: always (with a folder); run_command: only when allowed; exit_plan_mode: only in plan mode
QJsonArray controlSpecs(bool commands, bool plan)
{
    QJsonArray a;
    const QJsonObject item{{"type", "object"},
                           {"properties", QJsonObject{{"content", QJsonObject{{"type", "string"}, {"description", QStringLiteral("what to do, short")}}},
                                                      {"status", QJsonObject{{"type", "string"}, {"enum", QJsonArray{"pending", "in_progress", "completed"}}}}}},
                           {"required", QJsonArray{"content", "status"}}};
    a.append(toolSpec("todo_write",
                      QStringLiteral("Keeps the task list that the user sees. Use it for a job with three or more steps: write the list first, keep exactly one item in_progress, "
                                     "set an item to completed as soon as it is done. Send the WHOLE list every time."),
                      QJsonObject{{"todos", QJsonObject{{"type", "array"}, {"items", item}, {"description", QStringLiteral("the whole list")}}}}, {QStringLiteral("todos")}));
    if (commands)
        a.append(toolSpec("run_command",
                          QStringLiteral("Runs one Windows command line (cmd.exe) in the working folder and returns its output and exit code. The user sees the command and must allow it "
                                         "every time, so run only what the job needs (build, test, git status, dir) and never anything that deletes or changes things outside the job. "
                                         "No input is possible: the command must not ask questions."),
                          QJsonObject{{"command", QJsonObject{{"type", "string"}, {"description", QStringLiteral("the command line")}}},
                                      {"timeout_seconds", QJsonObject{{"type", "integer"}, {"description", QStringLiteral("limit in seconds, default 60, at most 300")}}}},
                          {QStringLiteral("command")}));
    if (plan)
        a.append(toolSpec("exit_plan_mode",
                          QStringLiteral("Call this when your plan is ready. The user reads it and approves it (then you may change files and run commands) or asks you to revise it. "
                                         "Until then you cannot change anything."),
                          QJsonObject{{"plan", QJsonObject{{"type", "string"}, {"description", QStringLiteral("the plan: a short numbered list of the steps and the files you will change")}}}},
                          {QStringLiteral("plan")}));
    return a;
}

} // namespace

LocalAgent::LocalAgent(QNetworkAccessManager* net, QObject* parent)
    : QObject(parent)
    , m_net(net)
    , m_web(new WebTools(net, this))
{
}

LocalAgent::~LocalAgent()
{
    delete m_files;
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

QString LocalAgent::start(const Config& c)
{
    if (m_running)
        return QStringLiteral("busy");
    if (!HttpChat::urlAllowed(c.baseUrl + QStringLiteral("/chat/completions")))
        return QStringLiteral("badUrl");
    if (c.folder.isEmpty() && !c.web)
        return QStringLiteral("noTools");
    if (!c.folder.isEmpty() && !QFileInfo(c.folder).isDir())
        return QStringLiteral("noFolder");
    m_config = c;
    delete m_files;
    m_files = c.folder.isEmpty() ? nullptr : new AgentTools(c.folder, c.allowEdits);
    m_messages = QJsonArray();
    if (!c.system.isEmpty())
        m_messages.append(QJsonObject{{"role", "system"}, {"content", c.system}});
    for (const auto& m : c.history)
        m_messages.append(QJsonObject{{"role", m.first}, {"content", m.second}});
    m_steps = 0;
    m_webCalls = 0;
    m_webSeen.clear();
    ++m_generation;
    m_waiting = false;
    m_onAnswer = nullptr;
    m_cancelled = false;
    m_wroteText = false;
    m_running = true;
    step();
    return {};
}

void LocalAgent::cancel()
{
    if (!m_running)
        return;
    m_cancelled = true;
    m_waiting = false;
    m_onAnswer = nullptr;
    m_web->cancel();
    if (m_runner && m_runner->running()) {
        ++m_generation;                                    // the answer of the killed command is not wanted
        m_runner->cancel();
    }
    if (m_reply)
        m_reply->abort();
    else
        end(false, QStringLiteral("cancelled"), {});
}

void LocalAgent::step()
{
    if (m_steps >= m_config.maxSteps) {
        end(false, QStringLiteral("tooManySteps"), {});
        return;
    }
    ++m_steps;
    QJsonArray tools;
    if (m_files)
        for (const QJsonValue& v : m_files->specs())
            tools.append(v);
    if (m_config.web)
        for (const QJsonValue& v : WebTools::specs())
            tools.append(v);
    if (m_files)
        for (const QJsonValue& v : controlSpecs(m_config.allowCommands, m_config.planMode))
            tools.append(v);
    QJsonObject body{{"model", m_config.model}, {"messages", m_messages}, {"tools", tools}, {"stream", false},
                     {"chat_template_kwargs", QJsonObject{{"enable_thinking", m_config.thinking}}}};
    if (m_config.maxTokens > 0)
        body["max_tokens"] = m_config.maxTokens;
    QNetworkRequest req{QUrl(m_config.baseUrl + QStringLiteral("/chat/completions"))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_config.apiKey.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_config.apiKey.toUtf8());
    req.setTransferTimeout(600000);                          // a long page on a slow processor
    m_reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, &LocalAgent::onReply);
}

void LocalAgent::onReply()
{
    QNetworkReply* reply = m_reply;
    if (!reply)
        return;
    m_reply = nullptr;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorString = reply->errorString();
    reply->deleteLater();
    if (m_cancelled) {
        end(false, QStringLiteral("cancelled"), {});
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(data).object();
    if (status >= 400) {
        end(false, QStringLiteral("http%1").arg(status), root.value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString());
        return;
    }
    if (error != QNetworkReply::NoError) {
        end(false, QStringLiteral("network"), errorString);
        return;
    }
    const QJsonObject message = root.value(QLatin1String("choices")).toArray().first().toObject().value(QLatin1String("message")).toObject();
    if (message.isEmpty()) {
        end(false, QStringLiteral("badReply"), {});
        return;
    }
    const QString content = message.value(QLatin1String("content")).toString().trimmed();
    const QJsonArray calls = message.value(QLatin1String("tool_calls")).toArray();
    if (!content.isEmpty()) {
        if (m_wroteText)
            emit piece(QStringLiteral("\n\n"));
        emit piece(content);
        m_wroteText = true;
    }
    if (calls.isEmpty()) {
        if (!m_wroteText)
            end(false, QStringLiteral("empty"), {});
        else
            end(true, {}, {});
        return;
    }
    // the model asks for tools: run them one after the other, hand the results back, ask again
    QJsonObject assistant{{"role", "assistant"}, {"tool_calls", calls}};
    assistant["content"] = message.value(QLatin1String("content"));
    m_messages.append(assistant);
    m_calls = calls;
    m_callIndex = 0;
    runNextCall();
}

void LocalAgent::runNextCall()
{
    if (m_cancelled) {
        end(false, QStringLiteral("cancelled"), {});
        return;
    }
    if (m_callIndex >= m_calls.size()) {
        step();
        return;
    }
    const QJsonObject call = m_calls.at(m_callIndex++).toObject();
    const QJsonObject fn = call.value(QLatin1String("function")).toObject();
    const QString name = fn.value(QLatin1String("name")).toString();
    const QString id = call.value(QLatin1String("id")).toString();
    QJsonObject args;
    bool argsOk = true;
    if (fn.value(QLatin1String("arguments")).isObject()) {
        args = fn.value(QLatin1String("arguments")).toObject();
    } else {
        const QString raw = fn.value(QLatin1String("arguments")).toString();
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(raw.toUtf8(), &pe);
        if (!raw.trimmed().isEmpty() && pe.error != QJsonParseError::NoError)
            argsOk = false;
        args = doc.object();
    }
    auto result = [this, id](const QString& text, bool isError) {
        m_messages.append(QJsonObject{{"role", "tool"}, {"tool_call_id", id}, {"content", isError ? QStringLiteral("Error: ") + text : text}});
        runNextCall();
    };
    if (!argsOk) {
        emit activity(name);
        result(QStringLiteral("the arguments are not valid JSON"), true);
        return;
    }
    if (WebTools::handles(name)) {
        emit activity(WebTools::describe(name, args));
        if (!m_config.web) {
            result(QStringLiteral("web access is switched off by the user"), true);
            return;
        }
        // A small model may search again and again and never do the work (seen: 12 searches in a row, then the step limit).
        // The same call twice, or more than kMaxWebCalls in one task, gets a message that tells it to go on with what it has.
        const QString key = name + QLatin1Char(' ') + QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
        if (m_webSeen.contains(key)) {
            result(QStringLiteral("you already made exactly this call and have its result above; do not repeat it. Use it and continue the task"), true);
            return;
        }
        if (m_webCalls >= kMaxWebCalls) {
            result(QStringLiteral("the web limit for this task is reached (%1 calls); do not search again. Do the task now with what you have").arg(kMaxWebCalls), true);
            return;
        }
        m_webSeen.append(key);
        ++m_webCalls;
        m_web->run(name, args, result);
        return;
    }
    if (name == QLatin1String("todo_write") || name == QLatin1String("run_command") || name == QLatin1String("exit_plan_mode")) {
        if (!m_files) {
            result(QStringLiteral("no folder is chosen, so this tool is not available"), true);
            return;
        }
        runControlCall(name, args, result);
        return;
    }
    emit activity(AgentTools::describe(name, args));
    if (AgentTools::changesFiles(name) && m_files && m_config.approveWrites) {
        const AgentTools::WriteInfo info = name == QLatin1String("edit_file") ? m_files->inspectEdit(args) : m_files->inspectWrite(args);
        if (!info.ok) {
            result(info.error, true);
            return;
        }
        m_waiting = true;                                  // the user decides: answerApproval() goes on
        m_onAnswer = [this, name, args, result](bool allow) {
            if (!allow) {
                result(QStringLiteral("the user declined this change; do not try again, say what you would have done"), true);
                return;
            }
            const AgentTools::Result r = m_files->run(name, args);
            result(r.text, r.error);
        };
        emit approvalNeeded(info.path, info.exists, info.bytes, info.preview, name == QLatin1String("edit_file"));
        return;
    }
    if (!m_files) {
        result(QStringLiteral("no folder is chosen, so there are no file tools"), true);
        return;
    }
    const AgentTools::Result r = m_files->run(name, args);
    result(r.text, r.error);
}

// todo_write (the list the user sees), run_command (after the user allowed it) and exit_plan_mode (the user reads the plan)
void LocalAgent::runControlCall(const QString& name, const QJsonObject& args, const std::function<void(const QString&, bool)>& result)
{
    if (name == QLatin1String("todo_write")) {
        QVariantList todos;
        for (const QJsonValue& v : args.value(QLatin1String("todos")).toArray()) {
            const QJsonObject o = v.toObject();
            const QString content = o.value(QLatin1String("content")).toString().simplified().left(200);
            QString status = o.value(QLatin1String("status")).toString();
            if (content.isEmpty())
                continue;
            if (status != QLatin1String("in_progress") && status != QLatin1String("completed"))
                status = QStringLiteral("pending");
            todos << QVariantMap{{"content", content}, {"status", status}};
            if (todos.size() >= 30)
                break;
        }
        emit activity(QStringLiteral("Task list: %1 items").arg(todos.size()));
        emit todosChanged(todos);
        result(QStringLiteral("The task list is saved. Go on with the next item and set items to completed when they are done."), false);
        return;
    }
    if (name == QLatin1String("run_command")) {
        const QString command = args.value(QLatin1String("command")).toString().trimmed();
        emit activity(QStringLiteral("Run: ") + command.simplified().left(100));
        if (!m_config.allowCommands) {
            result(QStringLiteral("running commands is not allowed in this mode"), true);
            return;
        }
        if (command.isEmpty() || command.size() > 2000 || command.contains(QChar(0))) {
            result(QStringLiteral("the command is empty or too long"), true);
            return;
        }
        const int seconds = args.value(QLatin1String("timeout_seconds")).toInt(CommandRunner::kDefaultSeconds);
        const QString folder = m_config.folder;
        const int gen = m_generation;
        m_waiting = true;                                  // EVERY command waits for the user
        m_onAnswer = [this, command, seconds, folder, gen, result](bool allow) {
            if (!allow) {
                result(QStringLiteral("the user did not allow this command; do not try it again, say what you wanted to do"), true);
                return;
            }
            if (!m_runner)
                m_runner = new CommandRunner(this);
            const bool started = m_runner->run(folder, command, seconds, [this, gen, result](const QString& text, bool error) {
                if (gen == m_generation)
                    result(text, error);
            });
            if (!started)
                result(QStringLiteral("another command is still running"), true);
        };
        emit commandApprovalNeeded(command);
        return;
    }
    // exit_plan_mode
    const QString plan = args.value(QLatin1String("plan")).toString().trimmed();
    emit activity(QStringLiteral("Plan ready"));
    if (!m_config.planMode) {
        result(QStringLiteral("you are not in plan mode"), true);
        return;
    }
    if (plan.isEmpty()) {
        result(QStringLiteral("the plan is empty"), true);
        return;
    }
    m_waiting = true;
    m_onAnswer = [this, result](bool allow) {
        if (!allow) {
            result(QStringLiteral("the user did not approve this plan yet. Ask what to change, revise the plan, and call exit_plan_mode again"), true);
            return;
        }
        m_config.planMode = false;                         // the work may start: every change and command still waits for the user
        m_config.allowEdits = true;
        m_config.approveWrites = true;
        m_config.allowCommands = true;
        m_files->setAllowEdits(true);
        emit planApproved();
        result(QStringLiteral("The user approved the plan. Carry it out now: you may change files and run commands (the user is asked before each one). Keep the task list up to date."), false);
    };
    emit planApprovalNeeded(plan);
}

void LocalAgent::answerApproval(bool allow)
{
    if (!m_waiting)
        return;
    m_waiting = false;
    auto answer = std::move(m_onAnswer);
    m_onAnswer = nullptr;
    if (answer)
        answer(allow);
}

void LocalAgent::end(bool ok, const QString& code, const QString& detail)
{
    if (!m_running)
        return;
    m_running = false;
    emit finished(ok, code, detail);
}

} // namespace dm::ai
