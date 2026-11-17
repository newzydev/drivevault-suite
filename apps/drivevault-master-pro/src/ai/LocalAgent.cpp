#include "LocalAgent.h"

#include "HttpChat.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace dm::ai {

static constexpr int kMaxWebCalls = 4;                     // web_search + fetch_page calls in one task
static constexpr const char* kDigestMarker = "Earlier in this job";   // starts the note that lists the steps whose results were dropped

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
                                      {"timeout_seconds", QJsonObject{{"type", "integer"}, {"description", QStringLiteral("limit in seconds, default 60, at most 300 (not for a background command)")}}},
                                      {"background", QJsonObject{{"type", "boolean"}, {"description", QStringLiteral("true for a command that runs for a long time or for ever (a server, a watcher, a long build): "
                                                                                                                     "it starts and you go on; read its output with command_output, stop it with stop_command")}}}},
                          {QStringLiteral("command")}));
    if (commands) {
        a.append(toolSpec("command_output", QStringLiteral("Shows what a background command (task) has written so far and whether it still runs."),
                          QJsonObject{{"task_id", QJsonObject{{"type", "integer"}, {"description", QStringLiteral("the number that run_command gave")}}}}, {QStringLiteral("task_id")}));
        a.append(toolSpec("stop_command", QStringLiteral("Stops a background command (task) that you started."),
                          QJsonObject{{"task_id", QJsonObject{{"type", "integer"}, {"description", QStringLiteral("the number that run_command gave")}}}}, {QStringLiteral("task_id")}));
    }
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
    m_stepNo = m_curNo = 0;
    m_webSeen.clear();
    ++m_generation;
    m_waiting = false;
    m_onAnswer = nullptr;
    m_afterCalls.clear();
    m_lastResultError = false;
    m_salvages = 0;
    m_serverRetries = 0;
    m_awaitingServer = false;
    m_cancelled = false;
    m_wroteText = false;
    m_running = true;
    step();
    return {};
}

void LocalAgent::resume()
{
    if (!m_awaitingServer || !m_running || m_cancelled)
        return;
    m_awaitingServer = false;
    step();
}

void LocalAgent::cancel()
{
    m_awaitingServer = false;
    if (!m_running)
        return;
    m_cancelled = true;
    m_waiting = false;
    m_onAnswer = nullptr;
    m_web->cancel();
    if (m_foregroundTask > 0) {                            // the command that the job waits for is killed (background tasks go on: the page lists them, the user stops them)
        ++m_generation;                                    // the answer of the killed command is not wanted
        if (m_pool)
            m_pool->stop(m_foregroundTask);
        m_foregroundTask = 0;
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
    // a background command ended while the model worked (or between two questions): it is told, with the end of the output
    for (const QString& n : std::as_const(m_taskNotes))
        m_messages.append(QJsonObject{{"role", "user"}, {"content", n}});
    m_taskNotes.clear();
    // keep the conversation inside the window of the server (a long job reads many files and writes many pieces)
    m_toolTokens = qint64(QJsonDocument(tools).toJson(QJsonDocument::Compact).size()) * 2 / 5;
    const qint64 room = m_config.contextTokens - (m_config.maxTokens > 0 ? m_config.maxTokens : 2048) - 1024;
    const int before = int(m_messages.size());
    const qint64 sizeBefore = estimateTokens();
    compact(qint64(double(qMax<qint64>(room, 2048)) * m_budgetFactor));
    if (!m_compacted && (before != m_messages.size() || estimateTokens() < sizeBefore)) {
        m_compacted = true;
        note(QStringLiteral("Made room: older results were shortened (read a file again if you need it)"));
    }
    // The answer is read as a stream: the page shows what is being written while it is written (a slow model needs minutes for a long answer), a tool call that
    // grows too long is stopped at once, and the part of a file that was written is not lost.
    QJsonObject body{{"model", m_config.model}, {"messages", m_messages}, {"tools", tools}, {"stream", true}, {"stream_options", QJsonObject{{"include_usage", true}}},
                     {"chat_template_kwargs", QJsonObject{{"enable_thinking", m_config.thinking}}}};
    if (m_config.maxTokens > 0)
        body["max_tokens"] = m_config.maxTokens;
    QNetworkRequest req{QUrl(m_config.baseUrl + QStringLiteral("/chat/completions"))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_config.apiKey.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_config.apiKey.toUtf8());
    req.setTransferTimeout(600000);                          // no data for ten minutes (a long prompt on a slow processor)
    m_stream = Stream();
    m_stream.progress.start();
    m_reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &LocalAgent::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &LocalAgent::onReply);
    emit generating(QStringLiteral("waiting"), {}, {}, 0);   // the prompt is being read
}

// ---- the streamed answer -------------------------------------------------------------------------------------------------------------------
static constexpr int kMaxToolArgChars = 6000;                // a tool call longer than this (about 60 lines of a file) is stopped at once: a big file is written in small parts, one after the other

static QString argumentTarget(const QString& tool, const QString& args)
{
    const QRegularExpression re(tool == QLatin1String("run_command") ? QStringLiteral("\"command\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"")
                                                                      : QStringLiteral("\"path\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\""));
    const QRegularExpressionMatch m = re.match(args);
    if (!m.hasMatch())
        return {};
    return QJsonDocument::fromJson(QByteArray("[\"") + m.captured(1).toUtf8() + "\"]").array().first().toString().left(160);
}

void LocalAgent::reportProgress(bool force)
{
    QString phase, tool, target;
    int chars = 0;
    if (!m_stream.calls.isEmpty()) {
        const StreamCall& c = m_stream.calls.last();
        phase = QStringLiteral("tool");
        tool = c.name;
        target = argumentTarget(c.name, c.args);
        chars = int(c.args.size());
    } else if (m_stream.contentStarted) {
        phase = QStringLiteral("writing");
        chars = int(m_stream.content.size());
    } else if (m_stream.reasoningChars > 0) {
        phase = QStringLiteral("thinking");
        chars = int(m_stream.reasoningChars);
    } else {
        return;
    }
    const QString key = phase + QLatin1Char('|') + tool;
    const bool changed = key != m_stream.lastPhase;                     // a new phase (or a new tool) is shown at once, the growing number every 250 ms
    if (!force && !changed && m_stream.progress.isValid() && m_stream.progress.elapsed() < 250)
        return;
    m_stream.lastPhase = key;
    m_stream.progress.restart();
    emit generating(phase, tool, target, chars);
}

void LocalAgent::processEvent(const QByteArray& event)
{
    for (const QByteArray& rawLine : event.split('\n')) {
        QByteArray line = rawLine.trimmed();
        if (!line.startsWith("data:"))
            continue;
        line = line.mid(5).trimmed();
        if (line.isEmpty() || line == "[DONE]")
            continue;
        const QJsonObject obj = QJsonDocument::fromJson(line).object();
        if (obj.contains(QLatin1String("error"))) {
            const QJsonValue e = obj.value(QLatin1String("error"));
            m_stream.error = e.isObject() ? e.toObject().value(QLatin1String("message")).toString() : e.toString();
            if (m_stream.error.isEmpty())
                m_stream.error = QStringLiteral("error");
            continue;
        }
        if (obj.contains(QLatin1String("usage")))
            m_stream.usage = obj.value(QLatin1String("usage")).toObject();
        const QJsonArray choices = obj.value(QLatin1String("choices")).toArray();
        if (choices.isEmpty())
            continue;
        const QJsonObject choice = choices.first().toObject();
        const QJsonObject delta = choice.value(QLatin1String("delta")).toObject();
        const QString text = delta.value(QLatin1String("content")).toString();
        if (!text.isEmpty()) {
            if (!m_stream.contentStarted) {
                m_stream.content += text;                                   // white space at the start waits for the first real character
                if (!m_stream.content.trimmed().isEmpty()) {
                    m_stream.contentStarted = true;
                    if (m_wroteText && !m_joinNext)
                        emit piece(QStringLiteral("\n\n"));
                    m_joinNext = false;
                    m_wroteText = true;
                    QString first = m_stream.content;
                    first.remove(QRegularExpression(QStringLiteral("^\\s+")));
                    emit piece(first);
                }
            } else {
                m_stream.content += text;
                emit piece(text);
            }
        }
        m_stream.reasoningChars += delta.value(QLatin1String("reasoning_content")).toString().size() + delta.value(QLatin1String("reasoning")).toString().size();
        const QJsonArray tcs = delta.value(QLatin1String("tool_calls")).toArray();
        for (int i = 0; i < tcs.size(); ++i) {
            const QJsonObject tc = tcs.at(i).toObject();
            StreamCall& call = m_stream.calls[tc.contains(QLatin1String("index")) ? tc.value(QLatin1String("index")).toInt() : i];
            if (!tc.value(QLatin1String("id")).toString().isEmpty())
                call.id = tc.value(QLatin1String("id")).toString();
            const QJsonObject fn = tc.value(QLatin1String("function")).toObject();
            if (!fn.value(QLatin1String("name")).toString().isEmpty())
                call.name = fn.value(QLatin1String("name")).toString();
            call.args += fn.value(QLatin1String("arguments")).toString();
            if (call.args.size() > kMaxToolArgChars)
                m_stream.abortedLong = true;
        }
        const QJsonValue fr = choice.value(QLatin1String("finish_reason"));
        if (fr.isString() && !fr.toString().isEmpty())
            m_stream.finish = fr.toString();
        if (m_stream.abortedLong)
            return;
    }
}

void LocalAgent::onReadyRead()
{
    if (!m_reply)
        return;
    QByteArray chunk = m_reply->readAll();
    if (m_stream.raw.size() < 2000000)
        m_stream.raw += chunk;
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    m_stream.sse = m_reply->header(QNetworkRequest::ContentTypeHeader).toString().contains(QLatin1String("event-stream"), Qt::CaseInsensitive) && status < 400;
    if (!m_stream.sse || m_stream.abortedLong)
        return;
    chunk.replace("\r\n", "\n");
    m_stream.pending += chunk;
    int at;
    while (!m_stream.abortedLong && (at = m_stream.pending.indexOf("\n\n")) >= 0) {
        const QByteArray event = m_stream.pending.left(at);
        m_stream.pending.remove(0, at + 2);
        processEvent(event);
    }
    if (m_stream.abortedLong) {
        reportProgress(true);
        m_reply->abort();                                       // the call is too long: no need to wait for the rest
        return;
    }
    reportProgress(false);
}

void LocalAgent::onReply()
{
    QNetworkReply* reply = m_reply;
    if (!reply)
        return;
    m_reply = nullptr;
    int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = reply->readAll();
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorString = reply->errorString();
    const bool eventStream = reply->header(QNetworkRequest::ContentTypeHeader).toString().contains(QLatin1String("event-stream"), Qt::CaseInsensitive) && status < 400;
    reply->deleteLater();
    emit generating({}, {}, {}, 0);                                   // the answer is complete
    if (m_cancelled) {
        end(false, QStringLiteral("cancelled"), {});
        return;
    }
    QJsonObject root;
    bool cutError = false;                                            // the server said that it could not read a tool call (it was cut)
    if (eventStream) {                                                // the answer was read as a stream: put it together
        m_stream.sse = true;
        if (m_stream.raw.size() < 2000000)
            m_stream.raw += data;
        if (!m_stream.abortedLong) {                                  // what came with the end, and an event that has no blank line after it
            QByteArray tail = data;
            tail.replace("\r\n", "\n");
            m_stream.pending += tail;
            int at;
            while (!m_stream.abortedLong && (at = m_stream.pending.indexOf("\n\n")) >= 0) {
                const QByteArray event = m_stream.pending.left(at);
                m_stream.pending.remove(0, at + 2);
                processEvent(event);
            }
            if (!m_stream.abortedLong && !m_stream.pending.trimmed().isEmpty())
                processEvent(m_stream.pending);
        }
        QJsonArray callsArray;
        int n = 0;
        for (auto it = m_stream.calls.constBegin(); it != m_stream.calls.constEnd(); ++it) {
            if (it.value().name.isEmpty())
                continue;
            callsArray.append(QJsonObject{{"id", it.value().id.isEmpty() ? QStringLiteral("call_%1").arg(++n) : it.value().id}, {"type", "function"},
                                          {"function", QJsonObject{{"name", it.value().name}, {"arguments", it.value().args}}}});
        }
        const QString text = m_stream.content.trimmed();
        QJsonObject msg{{"role", "assistant"}};
        msg["content"] = text.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(text);
        if (!callsArray.isEmpty())
            msg["tool_calls"] = callsArray;
        root = QJsonObject{{"choices", QJsonArray{QJsonObject{{"message", msg}, {"finish_reason", m_stream.abortedLong ? QStringLiteral("length") : m_stream.finish}}}},
                           {"usage", m_stream.usage}};
        if (!m_stream.error.isEmpty()) {
            if (m_stream.error.contains(QLatin1String("tool call"), Qt::CaseInsensitive) && !callsArray.isEmpty()) {
                cutError = true;                                      // a cut tool call: go on with the pieces that arrived
            } else if (text.isEmpty() && callsArray.isEmpty()) {
                status = 500;                                         // an error in the stream: the same as an error answer
                root = QJsonObject{{"error", QJsonObject{{"message", m_stream.error}}}};
            }
        }
    } else {
        if (m_stream.raw.size() < 2000000)
            m_stream.raw += data;
        root = QJsonDocument::fromJson(m_stream.raw).object();
    }
    if (status >= 400) {
        const QString message = root.value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString();
        // The model's answer was cut off in the middle of a tool call (a long file does not fit in max_tokens), so the server could not read
        // the arguments and answered 500. Nothing was done: tell the model to make it shorter and ask again (twice at most).
        if (status == 500 && message.contains(QLatin1String("tool call"), Qt::CaseInsensitive) && m_parseRetries < 2) {
            ++m_parseRetries;
            note(QStringLiteral("The answer was too long and was cut off: asking again for a shorter one"));
            m_messages.append(QJsonObject{{"role", "user"},
                                          {"content", QStringLiteral("Your last tool call was cut off because it was too long, so nothing was done. Make it much shorter: write "
                                                                     "a big file in parts - first create it with write_file (under about 80 lines, no long comments), then "
                                                                     "add each further part with append_file. Do not repeat the long text.")}});
            step();
            return;
        }
        // the prompt does not fit in the window (my estimate was too low): shorten harder and ask again
        if ((status == 400 || status == 500) && m_contextRetries < 3
            && (message.contains(QLatin1String("context"), Qt::CaseInsensitive) || message.contains(QLatin1String("exceed"), Qt::CaseInsensitive)
                || message.contains(QLatin1String("too large"), Qt::CaseInsensitive))) {
            ++m_contextRetries;
            m_budgetFactor *= 0.55;
            note(QStringLiteral("The conversation was too long for the model: shortening it and trying again"));
            step();
            return;
        }
        end(false, QStringLiteral("http%1").arg(status), message);
        return;
    }
    if (error != QNetworkReply::NoError && !m_stream.abortedLong) {
        const bool lost = error == QNetworkReply::RemoteHostClosedError || error == QNetworkReply::ConnectionRefusedError || error == QNetworkReply::UnknownNetworkError;
        const bool nothing = m_stream.calls.isEmpty() && m_stream.content.trimmed().isEmpty();
        if (lost && nothing && m_serverRetries < 2) {                   // the server stopped or crashed: it is started again and the same question asked again
            ++m_serverRetries;
            m_awaitingServer = true;
            note(QStringLiteral("The model server stopped: starting it again"));
            emit serverLost();
            return;
        }
        if (!(lost && m_stream.sse && !m_stream.calls.isEmpty())) {     // (a tool call that was being written when it stopped is kept below like a cut one)
            end(false, QStringLiteral("network"), errorString);
            return;
        }
        cutError = true;
    }
    const QJsonObject usageObject = root.value(QLatin1String("usage")).toObject();
    if (usageObject.contains(QLatin1String("prompt_tokens")))
        emit usage(usageObject.value(QLatin1String("prompt_tokens")).toInt(), usageObject.value(QLatin1String("completion_tokens")).toInt());
    const QJsonObject choice = root.value(QLatin1String("choices")).toArray().first().toObject();
    const QJsonObject message = choice.value(QLatin1String("message")).toObject();
    if (message.isEmpty()) {
        end(false, QStringLiteral("badReply"), {});
        return;
    }
    const QString content = message.value(QLatin1String("content")).toString().trimmed();
    QJsonArray calls = message.value(QLatin1String("tool_calls")).toArray();
    const bool cutByLength = choice.value(QLatin1String("finish_reason")).toString() == QLatin1String("length");
    if (!m_stream.sse && !content.isEmpty()) {                         // (a streamed answer was shown piece by piece while it came)
        if (m_wroteText && !m_joinNext)
            emit piece(QStringLiteral("\n\n"));
        emit piece(content);
        m_wroteText = true;
        m_joinNext = false;
    }
    if (!calls.isEmpty() && handleBrokenCalls(&calls, content, cutByLength || m_stream.abortedLong || cutError))
        return;
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

// The text of a (possibly cut) "content" string of a write_file / append_file call, decoded: `rawArguments` may end anywhere. `complete` = the string was closed
// (nothing was cut). Needs the path (it comes first in the call) and some text. A cut text is taken back to its last complete line.
bool LocalAgent::salvageWrite(const QString& rawArguments, QString* path, QString* text, bool* complete)
{
    auto decode = [](const QString& escaped) { return QJsonDocument::fromJson(QByteArray("[\"") + escaped.toUtf8() + "\"]").array().first().toString(); };
    const QRegularExpressionMatch pm = QRegularExpression(QStringLiteral("\"path\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"")).match(rawArguments);
    const QRegularExpressionMatch cm = QRegularExpression(QStringLiteral("\"(?:text|content)\"\\s*:\\s*\"")).match(rawArguments);
    if (!pm.hasMatch() || !cm.hasMatch())
        return false;
    const QString p = decode(pm.captured(1)).trimmed();
    if (p.isEmpty())
        return false;
    QString body;                                                      // the raw (still escaped) characters of the string, up to its closing quote
    bool closed = false;
    for (qsizetype i = cm.capturedEnd(); i < rawArguments.size(); ++i) {
        const QChar c = rawArguments.at(i);
        if (c == QLatin1Char('\\')) {
            if (i + 1 >= rawArguments.size())
                break;                                                 // an escape that was cut in the middle
            const QChar n = rawArguments.at(i + 1);
            if (n == QLatin1Char('u')) {
                if (i + 5 >= rawArguments.size())
                    break;                                             // a \uXXXX that was cut
                body += rawArguments.mid(i, 6);
                i += 5;
            } else {
                body += c;
                body += n;
                ++i;
            }
            continue;
        }
        if (c == QLatin1Char('"')) {
            closed = true;
            break;
        }
        body += c;
    }
    QString t = decode(body);
    if (!closed && t.contains(QLatin1Char('\n')))
        t = t.left(t.lastIndexOf(QLatin1Char('\n')) + 1);              // the last line may be cut: it is written again in the next part
    if (t.trimmed().size() < 20)
        return false;
    *path = p;
    *text = t;
    *complete = closed;
    return true;
}

// The commands of this agent are tasks of a pool (the page lists them). The channel gives its own pool, so that the tasks outlive a job; without one the agent
// makes its own. A background task that ends is announced to the model at its next request.
void LocalAgent::setPool(CommandPool* pool)
{
    if (m_pool == pool)
        return;
    m_pool = pool;
    if (m_pool)
        connect(m_pool, &CommandPool::finished, this, [this](int id) {
            if (!m_bgTasks.contains(id))
                return;
            const QVariantMap info = m_pool->info(id);
            QString out = m_pool->output(id);
            if (out.size() > 1200)
                out = QStringLiteral("...") + out.right(1200);
            m_taskNotes << QStringLiteral("The background task %1 (%2) has ended: %3. What it wrote last:\n%4")
                               .arg(id).arg(info.value("title").toString(), info.value("state").toString() + (info.value("note").toString().isEmpty() ? QString() : QStringLiteral(" - ") + info.value("note").toString()),
                                    out.isEmpty() ? QStringLiteral("(nothing)") : out);
        });
}

CommandPool* LocalAgent::pool()
{
    if (!m_pool)
        setPool(new CommandPool(this));
    return m_pool;
}

bool LocalAgent::handleBrokenCalls(QJsonArray* calls, const QString& content, bool cut)
{
    bool broken = false, kept = false;
    QString path, tool;
    qsizetype keptChars = 0;
    QString keptTail;
    for (int i = 0; i < calls->size(); ++i) {
        QJsonObject call = calls->at(i).toObject();
        QJsonObject fn = call.value(QLatin1String("function")).toObject();
        const QJsonValue av = fn.value(QLatin1String("arguments"));
        if (!av.isString() || av.toString().trimmed().isEmpty() || QJsonDocument::fromJson(av.toString().toUtf8()).isObject())
            continue;                                                  // the arguments are fine
        broken = true;
        const QString name = fn.value(QLatin1String("name")).toString();
        QString p, t;
        bool complete = false;
        if ((name == QLatin1String("write_file") || name == QLatin1String("append_file")) && m_salvages < 60 && salvageWrite(av.toString(), &p, &t, &complete)) {
            fn["arguments"] = QString::fromUtf8(QJsonDocument(QJsonObject{{"path", p}, {"text", t}}).toJson(QJsonDocument::Compact));
            call["function"] = fn;
            calls->replace(i, call);
            if (!complete) {                                           // a cut part: it is saved, the model writes the rest with append_file
                kept = true;
                path = p;
                tool = name;
                keptChars = t.size();
                keptTail = t.right(200);
                ++m_salvages;
            }
        }
    }
    if (!broken)
        return false;
    if (kept) {
        note(QStringLiteral("The answer was too long: the part that was written is kept, the rest is added in parts"));
        Q_UNUSED(keptTail)
        m_afterCalls = QStringLiteral("Your %1 call was stopped after about %2 characters: one call may only hold a small part (about 60 lines). That part was saved in %3 (the result above "
                                      "shows how the file ends). Go on at once with append_file (path first, then text): the next part only, about 60 lines, starting exactly "
                                      "where the file ends, without repeating anything. Do NOT read the file again. Part after part until the file is complete, then say that you "
                                      "are done.").arg(tool).arg(keptChars).arg(path);
        return false;                                                  // the kept part is written like any other call
    }
    if (!cut)
        return false;                                                  // not cut, only wrong: the model is told by the tool result
    // cut, and nothing of it can be kept: ask for something shorter
    if (m_parseRetries < 3) {
        ++m_parseRetries;
        note(QStringLiteral("The answer was too long and was cut off: asking again for a shorter one"));
        if (!content.isEmpty())
            m_messages.append(QJsonObject{{"role", "assistant"}, {"content", content}});
        m_messages.append(QJsonObject{{"role", "user"},
                                      {"content", QStringLiteral("Your last tool call was stopped because it was too long, so nothing was done. Write a big file in SMALL parts: first create it with "
                                                                 "write_file (about 60 lines at most), then add each further part with append_file. Give the arguments in this order: "
                                                                 "path first, then text. Do not repeat the long text.")}});
        step();
        return true;
    }
    end(false, QStringLiteral("cutOff"), {});
    return true;
}

void LocalAgent::runNextCall()
{
    if (m_cancelled) {
        end(false, QStringLiteral("cancelled"), {});
        return;
    }
    if (m_callIndex >= m_calls.size()) {
        if (!m_afterCalls.isEmpty()) {                                 // after a kept (cut) write: where to go on
            if (!m_lastResultError)
                m_messages.append(QJsonObject{{"role", "user"}, {"content", m_afterCalls}});
            m_afterCalls.clear();
        }
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
    {   // what the page shows as one line of the step list: it starts "running" here and ends in `result` (done / error)
        QString kind, target;
        if (name == QLatin1String("list_files")) { kind = QStringLiteral("list"); target = args.value(QLatin1String("path")).toString(QStringLiteral(".")); }
        else if (name == QLatin1String("read_file")) { kind = QStringLiteral("read"); target = args.value(QLatin1String("path")).toString(); }
        else if (name == QLatin1String("search_text")) { kind = QStringLiteral("search"); target = args.value(QLatin1String("query")).toString(); }
        else if (name == QLatin1String("glob_files")) { kind = QStringLiteral("glob"); target = args.value(QLatin1String("pattern")).toString(); }
        else if (name == QLatin1String("edit_file")) { kind = QStringLiteral("edit"); target = args.value(QLatin1String("path")).toString(); }
        else if (name == QLatin1String("write_file")) { kind = QStringLiteral("write"); target = args.value(QLatin1String("path")).toString(); }
        else if (name == QLatin1String("append_file")) { kind = QStringLiteral("append"); target = args.value(QLatin1String("path")).toString(); }
        else if (name == QLatin1String("run_command")) { kind = QStringLiteral("command"); target = args.value(QLatin1String("command")).toString(); }
        else if (name == QLatin1String("web_search")) { kind = QStringLiteral("websearch"); target = args.value(QLatin1String("query")).toString(); }
        else if (name == QLatin1String("fetch_page")) { kind = QStringLiteral("fetch"); target = args.value(QLatin1String("url")).toString(); }
        else if (name == QLatin1String("command_output")) { kind = QStringLiteral("taskout"); target = QString::number(args.value(QLatin1String("task_id")).toInt()); }
        else if (name == QLatin1String("stop_command")) { kind = QStringLiteral("taskstop"); target = QString::number(args.value(QLatin1String("task_id")).toInt()); }
        else if (name == QLatin1String("todo_write")) { kind = QStringLiteral("todo"); }
        else if (name == QLatin1String("exit_plan_mode")) { kind = QStringLiteral("plan"); }
        else { kind = QStringLiteral("other"); target = name; }
        stepStart(kind, target.simplified().left(140));
    }
    auto result = [this, id](const QString& text, bool isError) {
        m_lastResultError = isError;
        stepEnd(isError ? QStringLiteral("error") : QStringLiteral("done"), text);
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
    if (name == QLatin1String("todo_write") || name == QLatin1String("run_command") || name == QLatin1String("exit_plan_mode") || name == QLatin1String("command_output")
        || name == QLatin1String("stop_command")) {
        if (!m_files) {
            result(QStringLiteral("no folder is chosen, so this tool is not available"), true);
            return;
        }
        runControlCall(name, args, result);
        return;
    }
    emit activity(AgentTools::describe(name, args));
    if (AgentTools::changesFiles(name) && m_files && m_config.approveWrites) {
        const AgentTools::WriteInfo info = name == QLatin1String("edit_file") ? m_files->inspectEdit(args)
                                           : name == QLatin1String("append_file") ? m_files->inspectAppend(args) : m_files->inspectWrite(args);
        if (!info.ok) {
            result(info.error, true);
            return;
        }
        if (name == QLatin1String("append_file") && m_approvedFiles.contains(info.path)) {
            const AgentTools::Result r = m_files->run(name, args);   // a further part of a file the user already allowed
            result(r.text, r.error);
            return;
        }
        m_waiting = true;                                  // the user decides: answerApproval() goes on
        const QString approvedPath = info.path;
        m_onAnswer = [this, name, args, result, approvedPath](bool allow) {
            if (!allow) {
                result(QStringLiteral("the user declined this change; do not try again, say what you would have done"), true);
                return;
            }
            if (name != QLatin1String("edit_file"))
                m_approvedFiles.insert(approvedPath);
            stepEnd(QStringLiteral("running"), {});
            const AgentTools::Result r = m_files->run(name, args);
            result(r.text, r.error);
        };
        stepEnd(QStringLiteral("waiting"), {});
        emit approvalNeeded(info.path, info.exists, info.bytes, info.preview, name == QLatin1String("edit_file") ? QStringLiteral("edit")
                                                                               : name == QLatin1String("append_file") ? QStringLiteral("append") : QStringLiteral("write"));
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
        const bool background = args.value(QLatin1String("background")).toBool();
        const QString folder = m_config.folder;
        const int gen = m_generation;
        m_waiting = true;                                  // EVERY command waits for the user (a background one too)
        m_onAnswer = [this, command, seconds, background, folder, gen, result](bool allow) {
            if (!allow) {
                result(QStringLiteral("the user did not allow this command; do not try it again, say what you wanted to do"), true);
                return;
            }
            stepEnd(QStringLiteral("running"), {});
            const int id = pool()->start(folder, command, seconds, background, [this, gen, result](const QString& text, bool error) {
                if (gen != m_generation)
                    return;                                // the job was stopped: nobody waits for this answer
                m_foregroundTask = 0;
                result(text, error);
            });
            if (id == 0) {
                result(QStringLiteral("too many commands are running (at most %1 at the same time): stop one first (stop_command)").arg(CommandPool::kMaxRunning), true);
                return;
            }
            m_myTasks.insert(id);
            if (background)
                m_bgTasks.insert(id);
            else
                m_foregroundTask = id;
        };
        stepEnd(QStringLiteral("waiting"), {});
        emit commandApprovalNeeded(command);
        return;
    }
    if (name == QLatin1String("command_output") || name == QLatin1String("stop_command")) {
        const int id = args.value(QLatin1String("task_id")).toInt();
        emit activity(QStringLiteral("Task %1").arg(id));
        if (!m_config.allowCommands || !pool()->known(id) || !m_myTasks.contains(id)) {
            result(QStringLiteral("there is no task %1 of yours (the tasks are the ones that run_command started)").arg(id), true);
            return;
        }
        const QVariantMap info = pool()->info(id);
        if (name == QLatin1String("stop_command")) {
            if (pool()->stop(id))
                result(QStringLiteral("task %1 was stopped").arg(id), false);
            else
                result(QStringLiteral("task %1 is not running (%2)").arg(id).arg(info.value("state").toString()), true);
            return;
        }
        const QString out = pool()->output(id);
        result(QStringLiteral("task %1 (%2): %3%4\n%5").arg(id).arg(info.value("title").toString(), info.value("state").toString(),
                                                            info.value("note").toString().isEmpty() ? QString() : QStringLiteral(" - ") + info.value("note").toString(),
                                                            out.isEmpty() ? QStringLiteral("(no output yet)") : out),
               false);
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
    stepEnd(QStringLiteral("waiting"), {});
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
    QStringList dropped;                                               // what the dropped steps were: the model keeps a trace of its own work
    auto isDigest = [this](int index) {
        return index < int(m_messages.size()) && m_messages.at(index).toObject().value(QLatin1String("role")).toString() == QLatin1String("user")
               && m_messages.at(index).toObject().value(QLatin1String("content")).toString().startsWith(QLatin1String(kDigestMarker));
    };
    while (estimateTokens() > budgetTokens && m_messages.size() > 4) {
        const int firstStep = m_taskIndex + 1 + (isDigest(m_taskIndex + 1) ? 1 : 0);      // the note about earlier steps stays
        int i;
        if ((hasSystem ? 1 : 0) < m_taskIndex)
            i = hasSystem ? 1 : 0;                                     // older conversation before the task
        else if (firstStep < int(m_messages.size()) - 4)
            i = firstStep;                                             // the oldest step of this job
        else
            break;
        int count = 1;
        if (m_messages.at(i).toObject().contains(QLatin1String("tool_calls")))
            while (i + count < int(m_messages.size()) && m_messages.at(i + count).toObject().value(QLatin1String("role")).toString() == QLatin1String("tool"))
                ++count;
        if (i > m_taskIndex) {                                         // a step of this job: write down what it was
            for (const QJsonValue& t : m_messages.at(i).toObject().value(QLatin1String("tool_calls")).toArray()) {
                const QJsonObject fn = t.toObject().value(QLatin1String("function")).toObject();
                const QJsonObject args = QJsonDocument::fromJson(fn.value(QLatin1String("arguments")).toString().toUtf8()).object();
                QString what;
                for (const char* key : {"path", "command", "query", "pattern", "url"})
                    if (what.isEmpty())
                        what = args.value(QLatin1String(key)).toString();
                if (args.contains(QLatin1String("start_line")))
                    what += QStringLiteral(" (from line %1)").arg(args.value(QLatin1String("start_line")).toInt());
                dropped << (fn.value(QLatin1String("name")).toString() + (what.isEmpty() ? QString() : QLatin1Char(' ') + what.simplified().left(100)));
            }
        }
        for (int k = 0; k < count; ++k)
            m_messages.removeAt(i);
        if (i < m_taskIndex)
            m_taskIndex -= count;
    }
    if (!dropped.isEmpty()) {                                          // "Earlier in this job ...": a short list right after the task
        QString lines;
        for (const QString& d : std::as_const(dropped).mid(0, 80))
            lines += QStringLiteral("\n- ") + d;
        if (isDigest(m_taskIndex + 1)) {
            QJsonObject note = m_messages.at(m_taskIndex + 1).toObject();
            QString content = note.value(QLatin1String("content")).toString();
            if (content.size() < 6000)                                 // the list itself stays short
                note["content"] = content + lines;
            m_messages[m_taskIndex + 1] = note;
        } else {
            m_messages.insert(m_taskIndex + 1, QJsonObject{{"role", "user"}, {"content", QLatin1String(kDigestMarker) + QStringLiteral(" (the results were dropped to save room; this is only what you did, oldest first - read a file again if you need its text):") + lines}});
        }
    }
}

// ---- the step list of the page --------------------------------------------------------------------------------------------------------
void LocalAgent::stepStart(const QString& kind, const QString& target)
{
    m_curNo = ++m_stepNo;
    m_curKind = kind;
    m_curTarget = target;
    emit stepEvent(m_curNo, kind, target, QStringLiteral("running"), {});
}

void LocalAgent::stepEnd(const QString& state, const QString& note)
{
    emit stepEvent(m_curNo, m_curKind, m_curTarget, state, note.simplified().left(160));
}

// something the agent did that is not a tool call (made room, asked again, went on): one line of the list, and the old activity line
void LocalAgent::note(const QString& text)
{
    emit activity(text);
    emit stepEvent(++m_stepNo, QStringLiteral("note"), text, QStringLiteral("done"), {});
}

bool LocalAgent::nudge(const QString& activityText, const QString& text)
{
    note(activityText);
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
