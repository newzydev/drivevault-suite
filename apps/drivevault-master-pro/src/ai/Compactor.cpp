#include "Compactor.h"
#include "RetryPolicy.h"

#include "HttpChat.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace dm::ai {

Compactor::Compactor(QNetworkAccessManager* net, QObject* parent)
    : QObject(parent)
    , m_net(net)
{
    m_retryTimer = new QTimer(this);
    m_retryTimer->setSingleShot(true);
    connect(m_retryTimer, &QTimer::timeout, this, [this] { send(); });
}

Compactor::~Compactor()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

QString Compactor::systemPrompt(const QString& focus)
{
    QString s = QStringLiteral(
        "You compress a conversation between a user and an AI assistant that works on files in a folder on the user's computer, so that the assistant can go on with "
        "only your summary. Write the summary in the language the conversation is in (Thai or English). Keep: the user's goal and every wish, rule or preference the user gave; "
        "decisions that were made; the files that were created, changed or read (their paths) and what was done to them; commands that were run and the results that matter; "
        "errors and how they were solved; the state of the work now and exactly what is left to do (the plan); anything the assistant must not forget. "
        "Leave out greetings, repeated attempts and long file contents (keep only the facts that are needed). Use short sections with bullet points, at most about 500 words. "
        "Write only the summary, nothing before or after it.");
    if (!focus.trimmed().isEmpty())
        s += QStringLiteral("\n\nThe user asks you to pay special attention to: ") + focus.trimmed().left(500);
    return s;
}

QString Compactor::start(const QString& baseUrl, const QString& apiKey, const QString& transcript, const QString& focus, int maxTokens, const QString& model, bool retry)
{
    if (running())
        return QStringLiteral("busy");
    if (!HttpChat::urlAllowed(baseUrl + QStringLiteral("/chat/completions")))
        return QStringLiteral("badUrl");
    m_cancelled = false;
    const QJsonArray messages{QJsonObject{{"role", "system"}, {"content", systemPrompt(focus)}},
                              QJsonObject{{"role", "user"}, {"content", QStringLiteral("The conversation to summarize:\n\n") + transcript}}};
    QJsonObject body{{"model", model}, {"messages", messages}, {"stream", false}, {"max_tokens", maxTokens > 0 ? maxTokens : kSummaryTokens}};
    if (model == QLatin1String("local"))                     // llama.cpp only (an online service may refuse a field it does not know)
        body["chat_template_kwargs"] = QJsonObject{{"enable_thinking", false}};
    m_url = baseUrl + QStringLiteral("/chat/completions");
    m_apiKey = apiKey;
    m_body = QJsonDocument(body).toJson(QJsonDocument::Compact);
    m_retry = retry;
    m_retries = 0;
    m_retryTimer->stop();
    send();
    return {};
}

void Compactor::send()
{
    QNetworkRequest req{QUrl(m_url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!m_apiKey.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_apiKey.toUtf8());
    req.setTransferTimeout(m_retry ? RetryPolicy::kTimeoutMs : 600000);        // an online model that sends nothing for two minutes is given up and asked again
    m_reply = m_net->post(req, m_body);
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        const int retryAfter = RetryPolicy::retryAfterMs(reply->rawHeader("Retry-After"));
        reply->deleteLater();
        if (m_cancelled) {
            emit finished(false, {}, QStringLiteral("cancelled"));
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(data).object();
        if (status >= 400) {
            if (retryLater(retryAfter, RetryPolicy::httpRetryable(status)))
                return;
            emit finished(false, root.value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString(), QStringLiteral("http%1").arg(status));
            return;
        }
        if (error != QNetworkReply::NoError) {
            if (retryLater(0, RetryPolicy::networkRetryable(error)))
                return;
            emit finished(false, errorString, QStringLiteral("network"));
            return;
        }
        const QJsonObject message = root.value(QLatin1String("choices")).toArray().first().toObject().value(QLatin1String("message")).toObject();
        if (message.isEmpty()) {
            if (retryLater(0, true))                          // an answer that is not an answer: the service had a hiccup
                return;
            emit finished(false, {}, QStringLiteral("badReply"));
            return;
        }
        const QString text = message.value(QLatin1String("content")).toString().trimmed();
        if (text.isEmpty()) {
            if (retryLater(0, true))
                return;
            emit finished(false, {}, QStringLiteral("empty"));
            return;
        }
        emit finished(true, text, {});
    });
}

// An online model failed in a way that goes away by itself: the same request is sent again after a pause that grows (RetryPolicy). false = no more tries.
bool Compactor::retryLater(int retryAfterMs, bool retryable)
{
    if (!m_retry || !retryable || m_retries >= RetryPolicy::kMaxRetries)
        return false;
    ++m_retries;
    m_retryTimer->start(RetryPolicy::delayMs(m_retries, retryAfterMs));
    return true;
}

void Compactor::cancel()
{
    if (!m_reply) {
        if (m_retryTimer->isActive()) {                       // cancelled while waiting for the next try
            m_retryTimer->stop();
            emit finished(false, {}, QStringLiteral("cancelled"));
        }
        return;
    }
    m_cancelled = true;
    m_reply->abort();
}

} // namespace dm::ai
