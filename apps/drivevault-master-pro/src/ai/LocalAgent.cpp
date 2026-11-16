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
    m_web->cancel();
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
    emit activity(AgentTools::describe(name, args));
    if (name == QLatin1String("write_file") && m_files && m_config.approveWrites) {
        const AgentTools::WriteInfo info = m_files->inspectWrite(args);
        if (!info.ok) {
            result(info.error, true);
            return;
        }
        m_waiting = true;                                  // the user decides: answerApproval() goes on
        m_waitName = name;
        m_waitArgs = args;
        m_waitResult = result;
        emit approvalNeeded(info.path, info.exists, info.bytes, info.preview);
        return;
    }
    if (!m_files) {
        result(QStringLiteral("no folder is chosen, so there are no file tools"), true);
        return;
    }
    const AgentTools::Result r = m_files->run(name, args);
    result(r.text, r.error);
}

void LocalAgent::answerApproval(bool allow)
{
    if (!m_waiting)
        return;
    m_waiting = false;
    auto done = std::move(m_waitResult);
    m_waitResult = nullptr;
    if (!allow) {
        done(QStringLiteral("the user declined this change; do not try again, say what you would have done"), true);
        return;
    }
    const AgentTools::Result r = m_files->run(m_waitName, m_waitArgs);
    done(r.text, r.error);
}

void LocalAgent::end(bool ok, const QString& code, const QString& detail)
{
    if (!m_running)
        return;
    m_running = false;
    emit finished(ok, code, detail);
}

} // namespace dm::ai
