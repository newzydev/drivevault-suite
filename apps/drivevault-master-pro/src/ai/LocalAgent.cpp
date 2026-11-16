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
    m_parseRetries = m_emptyNudges = m_todoNudges = m_continues = m_contextRetries = 0;
    m_budgetFactor = 1.0;
    m_compacted = false;
    m_todoList.clear();
    m_lastCall.clear();
    m_sameCall = 0;
    m_taskIndex = int(m_messages.size()) - 1;                   // the user's new message: the task
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
    // keep the conversation inside the window of the server (a long job reads many files and writes many pieces)
    m_toolTokens = qint64(QJsonDocument(tools).toJson(QJsonDocument::Compact).size()) * 2 / 5;
    const qint64 room = m_config.contextTokens - (m_config.maxTokens > 0 ? m_config.maxTokens : 2048) - 1024;
    const int before = int(m_messages.size());
    const qint64 sizeBefore = estimateTokens();
    compact(qint64(double(qMax<qint64>(room, 2048)) * m_budgetFactor));
    if (!m_compacted && (before != m_messages.size() || estimateTokens() < sizeBefore)) {
        m_compacted = true;
        emit activity(QStringLiteral("Made room: older results were shortened (read a file again if you need it)"));
    }
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
        const QString message = root.value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString();
        // The model's answer was cut off in the middle of a tool call (a long file does not fit in max_tokens), so the server could not read
        // the arguments and answered 500. Nothing was done: tell the model to make it shorter and ask again (twice at most).
        if (status == 500 && message.contains(QLatin1String("tool call"), Qt::CaseInsensitive) && m_parseRetries < 2) {
            ++m_parseRetries;
            emit activity(QStringLiteral("The answer was too long and was cut off: asking again for a shorter one"));
            m_messages.append(QJsonObject{{"role", "user"},
                                          {"content", QStringLiteral("Your last tool call was cut off because it was too long, so nothing was done. Make it much shorter: write "
                                                                     "a big file in parts - first create a small file with write_file (under about 80 lines, no long comments), then "
                                                                     "add each further part with edit_file. Do not repeat the long text.")}});
            step();
            return;
        }
        // the prompt does not fit in the window (my estimate was too low): shorten harder and ask again
        if ((status == 400 || status == 500) && m_contextRetries < 3
            && (message.contains(QLatin1String("context"), Qt::CaseInsensitive) || message.contains(QLatin1String("exceed"), Qt::CaseInsensitive)
                || message.contains(QLatin1String("too large"), Qt::CaseInsensitive))) {
            ++m_contextRetries;
            m_budgetFactor *= 0.55;
            emit activity(QStringLiteral("The conversation was too long for the model: shortening it and trying again"));
            step();
            return;
        }
        end(false, QStringLiteral("http%1").arg(status), message);
        return;
    }
    if (error != QNetworkReply::NoError) {
        end(false, QStringLiteral("network"), errorString);
        return;
    }
    const QJsonObject choice = root.value(QLatin1String("choices")).toArray().first().toObject();
    const QJsonObject message = choice.value(QLatin1String("message")).toObject();
    if (message.isEmpty()) {
        end(false, QStringLiteral("badReply"), {});
        return;
    }
    const QString content = message.value(QLatin1String("content")).toString().trimmed();
    const QJsonArray calls = message.value(QLatin1String("tool_calls")).toArray();
    const bool cutByLength = choice.value(QLatin1String("finish_reason")).toString() == QLatin1String("length");
    if (!content.isEmpty()) {
        if (m_wroteText && !m_joinNext)
            emit piece(QStringLiteral("\n\n"));
        emit piece(content);
        m_wroteText = true;
        m_joinNext = false;
    }
    if (calls.isEmpty()) {
        // ---- finishing: the job is done only when the model says so AND nothing it promised is left ----
        if (cutByLength && m_continues < 8) {                  // a long answer hit max_tokens: let it go on where it stopped
            ++m_continues;
            m_joinNext = true;                                 // the next piece goes on in the same text
            if (!content.isEmpty())
                m_messages.append(QJsonObject{{"role", "assistant"}, {"content", content}});
            if (nudge(QStringLiteral("The answer was long: going on"), QStringLiteral("Go on exactly where you stopped. Do not repeat what you already wrote.")))
                return;
        }
        if (content.isEmpty() && !m_wroteText) {
            if (m_emptyNudges++ < 3 && nudge(QStringLiteral("The model sent nothing: asking again"),
                                              QStringLiteral("You sent no answer. Continue the task with a tool call, or give the final answer now.")))
                return;
            end(false, QStringLiteral("empty"), {});
            return;
        }
        QStringList open;
        for (const QVariant& v : std::as_const(m_todoList))
            if (v.toMap().value("status").toString() != QLatin1String("completed"))
                open << v.toMap().value("content").toString();
        if (!open.isEmpty() && m_todoNudges < 3) {
            ++m_todoNudges;
            if (!content.isEmpty())
                m_messages.append(QJsonObject{{"role", "assistant"}, {"content", content}});
            if (nudge(QStringLiteral("The task list is not finished: going on"),
                      QStringLiteral("Your task list still has unfinished items:\n- %1\nIf they are really done, mark them completed with todo_write and give the final summary. "
                                     "Otherwise carry on with the next item now with a tool call.").arg(open.join(QStringLiteral("\n- ")))))
                return;
        }
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
    // A model that makes the very same call again and again (and gets the very same answer) is stuck. Third time in a row: it is told so and the call is not run;
    // eighth time: the job stops with a message instead of burning the time for ever.
    if (name != QLatin1String("todo_write") && name != QLatin1String("exit_plan_mode")) {
        const QString signature = name + QLatin1Char(' ') + QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact));
        m_sameCall = signature == m_lastCall ? m_sameCall + 1 : 1;
        m_lastCall = signature;
        if (m_sameCall >= 8) {
            end(false, QStringLiteral("stuck"), {});
            return;
        }
        if (m_sameCall >= 3) {
            emit activity(AgentTools::describe(name, args) + QStringLiteral(" (repeated)"));
            result(QStringLiteral("you have made exactly this call %1 times in a row with the same result. Do something different (another file, another approach) or, if the job "
                                  "is done, finish and tell the user what you did").arg(m_sameCall), true);
            return;
        }
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
        m_todoList = todos;
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

// ---- keeping a long job inside the window ---------------------------------------------------------------------------------------------
namespace {

qint64 charsOf(const QJsonObject& m)
{
    qint64 n = 16;
    const QJsonValue c = m.value(QLatin1String("content"));
    if (c.isString())
        n += c.toString().size();
    for (const QJsonValue& t : m.value(QLatin1String("tool_calls")).toArray()) {
        const QJsonObject f = t.toObject().value(QLatin1String("function")).toObject();
        n += 30 + f.value(QLatin1String("name")).toString().size() + f.value(QLatin1String("arguments")).toString().size();
    }
    return n;
}

QString shorten(const QString& text, int keep)
{
    return text.size() <= keep + 60 ? text : text.left(keep) + QStringLiteral(" ...(%1 characters left out)").arg(text.size() - keep);
}

} // namespace

// a rough size: about 2.5 characters to a token (Thai and code are denser than English)
qint64 LocalAgent::estimateTokens() const
{
    qint64 chars = 0;
    for (const QJsonValue& v : m_messages)
        chars += charsOf(v.toObject());
    return chars * 2 / 5 + m_toolTokens;
}

void LocalAgent::shortenMessage(int index)
{
    QJsonObject m = m_messages.at(index).toObject();
    const QString role = m.value(QLatin1String("role")).toString();
    if (role == QLatin1String("tool")) {
        const QString text = m.value(QLatin1String("content")).toString();
        if (text.size() > 360)
            m["content"] = text.left(300) + QStringLiteral("\n... (%1 characters left out to save room; read it again if you need it)").arg(text.size() - 300);
    } else if (role == QLatin1String("assistant")) {
        const QJsonValue c = m.value(QLatin1String("content"));
        if (c.isString() && c.toString().size() > 700)
            m["content"] = shorten(c.toString(), 400);
        QJsonArray calls = m.value(QLatin1String("tool_calls")).toArray();
        for (int i = 0; i < calls.size(); ++i) {                      // the text of an old write_file / edit_file is long and already done
            QJsonObject call = calls.at(i).toObject();
            QJsonObject fn = call.value(QLatin1String("function")).toObject();
            const QJsonObject args = QJsonDocument::fromJson(fn.value(QLatin1String("arguments")).toString().toUtf8()).object();
            QJsonObject out;
            for (auto it = args.begin(); it != args.end(); ++it)
                out[it.key()] = it.value().isString() ? QJsonValue(shorten(it.value().toString(), 120)) : it.value();
            fn["arguments"] = QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));
            call["function"] = fn;
            calls[i] = call;
        }
        if (!calls.isEmpty())
            m["tool_calls"] = calls;
    }
    m_messages[index] = m;
}

// Over the budget: first the long pieces of the old messages are shortened (the newest six stay whole), then the oldest messages are dropped (a call with its
// results together). The system message and the user's task always stay.
void LocalAgent::compact(qint64 budgetTokens)
{
    if (estimateTokens() <= budgetTokens)
        return;
    const int firstOld = qMax(m_taskIndex + 1, 1);
    for (int i = firstOld; i < int(m_messages.size()) - 6 && estimateTokens() > budgetTokens; ++i)
        shortenMessage(i);
    const bool hasSystem = !m_messages.isEmpty() && m_messages.at(0).toObject().value(QLatin1String("role")).toString() == QLatin1String("system");
    while (estimateTokens() > budgetTokens && m_messages.size() > 4) {
        int i;
        if ((hasSystem ? 1 : 0) < m_taskIndex)
            i = hasSystem ? 1 : 0;                                     // older conversation before the task
        else if (m_taskIndex + 1 < int(m_messages.size()) - 4)
            i = m_taskIndex + 1;                                       // the oldest step of this job
        else
            break;
        int count = 1;
        if (m_messages.at(i).toObject().contains(QLatin1String("tool_calls")))
            while (i + count < int(m_messages.size()) && m_messages.at(i + count).toObject().value(QLatin1String("role")).toString() == QLatin1String("tool"))
                ++count;
        for (int k = 0; k < count; ++k)
            m_messages.removeAt(i);
        if (i < m_taskIndex)
            m_taskIndex -= count;
    }
}

bool LocalAgent::nudge(const QString& activityText, const QString& text)
{
    emit activity(activityText);
    m_messages.append(QJsonObject{{"role", "user"}, {"content", text}});
    step();
    return true;
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
