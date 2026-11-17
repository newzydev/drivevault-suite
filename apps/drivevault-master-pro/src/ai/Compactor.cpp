#include "Compactor.h"

#include "HttpChat.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace dm::ai {

Compactor::Compactor(QNetworkAccessManager* net, QObject* parent)
    : QObject(parent)
    , m_net(net)
{
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

QString Compactor::start(const QString& baseUrl, const QString& apiKey, const QString& transcript, const QString& focus, int maxTokens)
{
    if (m_reply)
        return QStringLiteral("busy");
    if (!HttpChat::urlAllowed(baseUrl + QStringLiteral("/chat/completions")))
        return QStringLiteral("badUrl");
    m_cancelled = false;
    const QJsonArray messages{QJsonObject{{"role", "system"}, {"content", systemPrompt(focus)}},
                              QJsonObject{{"role", "user"}, {"content", QStringLiteral("The conversation to summarize:\n\n") + transcript}}};
    QJsonObject body{{"model", "local"}, {"messages", messages}, {"stream", false}, {"max_tokens", maxTokens > 0 ? maxTokens : kSummaryTokens},
                     {"chat_template_kwargs", QJsonObject{{"enable_thinking", false}}}};
    QNetworkRequest req{QUrl(baseUrl + QStringLiteral("/chat/completions"))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!apiKey.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
    req.setTransferTimeout(600000);
    m_reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        const QNetworkReply::NetworkError error = reply->error();
        const QString errorString = reply->errorString();
        reply->deleteLater();
        if (m_cancelled) {
            emit finished(false, {}, QStringLiteral("cancelled"));
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(data).object();
        if (status >= 400) {
            emit finished(false, root.value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString(), QStringLiteral("http%1").arg(status));
            return;
        }
        if (error != QNetworkReply::NoError) {
            emit finished(false, errorString, QStringLiteral("network"));
            return;
        }
        const QJsonObject message = root.value(QLatin1String("choices")).toArray().first().toObject().value(QLatin1String("message")).toObject();
        if (message.isEmpty()) {
            emit finished(false, {}, QStringLiteral("badReply"));
            return;
        }
        const QString text = message.value(QLatin1String("content")).toString().trimmed();
        if (text.isEmpty()) {
            emit finished(false, {}, QStringLiteral("empty"));
            return;
        }
        emit finished(true, text, {});
    });
    return {};
}

void Compactor::cancel()
{
    if (!m_reply)
        return;
    m_cancelled = true;
    m_reply->abort();
}

} // namespace dm::ai
