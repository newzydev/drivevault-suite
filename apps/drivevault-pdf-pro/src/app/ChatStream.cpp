#include "ChatStream.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace dp::assistant {

QStringList SseParser::feed(const QByteArray& bytes)
{
    QStringList pieces;
    m_buffer += bytes;
    for (;;) {
        const int end = m_buffer.indexOf("\n\n");
        if (end < 0)
            break;
        const QByteArray event = m_buffer.left(end);
        m_buffer.remove(0, end + 2);
        for (const QByteArray& line : event.split('\n')) {
            const QByteArray trimmed = line.trimmed();
            if (!trimmed.startsWith("data:"))
                continue;
            const QByteArray data = trimmed.mid(5).trimmed();
            if (data == "[DONE]") {
                m_done = true;
                continue;
            }
            const QJsonObject json = QJsonDocument::fromJson(data).object();
            if (json.contains(QStringLiteral("error"))) {
                const QJsonValue e = json.value(QStringLiteral("error"));
                m_error = e.isObject() ? e.toObject().value(QStringLiteral("message")).toString() : e.toString();
                if (m_error.isEmpty())
                    m_error = QStringLiteral("The model server reported an error.");
                continue;
            }
            const QJsonArray choices = json.value(QStringLiteral("choices")).toArray();
            if (choices.isEmpty())
                continue;
            const QJsonObject choice = choices.at(0).toObject();
            const QString content = choice.value(QStringLiteral("delta")).toObject().value(QStringLiteral("content")).toString();
            if (!content.isEmpty())
                pieces << content;
            if (!choice.value(QStringLiteral("finish_reason")).isNull() && choice.contains(QStringLiteral("finish_reason")))
                m_done = true;
        }
    }
    return pieces;
}

ChatStream::ChatStream(QNetworkAccessManager* net, QObject* parent)
    : QObject(parent)
    , m_net(net)
{
}

ChatStream::~ChatStream()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

void ChatStream::start(const QString& baseUrl, const QString& apiKey, const QJsonArray& messages, int maxTokens, double temperature, bool stream)
{
    cancel();
    m_parser = SseParser();
    m_plain.clear();
    m_stream = stream;
    m_cancelled = false;
    m_failed = false;
    QNetworkRequest request{QUrl(baseUrl + QStringLiteral("/v1/chat/completions"))};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (!apiKey.isEmpty())
        request.setRawHeader("Authorization", "Bearer " + apiKey.toUtf8());
    request.setTransferTimeout(0);      // the first token of a long prompt can take a while; the user can stop
    QJsonObject body{{"model", "local"},
                     {"messages", messages},
                     {"stream", stream},
                     {"max_tokens", maxTokens},
                     {"temperature", temperature},
                     {"cache_prompt", true},
                     {"chat_template_kwargs", QJsonObject{{"enable_thinking", false}}}};     // Qwen3.x: answer at once, no reasoning block
    m_reply = m_net->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(m_reply, &QNetworkReply::readyRead, this, &ChatStream::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &ChatStream::onFinished);
}

void ChatStream::cancel()
{
    if (!m_reply)
        return;
    m_cancelled = true;
    m_reply->abort();             // onFinished() reports it
}

void ChatStream::onReadyRead()
{
    if (!m_reply)
        return;
    const QByteArray bytes = m_reply->readAll();
    if (!m_stream) {
        m_plain += bytes;
        return;
    }
    for (const QString& text : m_parser.feed(bytes))
        emit piece(text);
}

void ChatStream::onFinished()
{
    QNetworkReply* reply = m_reply;
    if (!reply)
        return;
    if (reply->isOpen())
        onReadyRead();                // an aborted reply is closed: nothing to read
    m_reply = nullptr;
    reply->deleteLater();
    if (m_cancelled) {
        emit finished(false, QStringLiteral("cancelled"));
        return;
    }
    if (reply->error() != QNetworkReply::NoError && !m_parser.done()) {
        emit finished(false, reply->errorString());
        return;
    }
    if (!m_stream) {
        const QJsonObject json = QJsonDocument::fromJson(m_plain).object();
        const QString text = json.value(QStringLiteral("choices")).toArray().at(0).toObject().value(QStringLiteral("message")).toObject()
                                 .value(QStringLiteral("content")).toString();
        if (!text.isEmpty())
            emit piece(text);
        emit finished(!text.isEmpty(), text.isEmpty() ? QStringLiteral("The model sent no answer.") : QString());
        return;
    }
    if (!m_parser.error().isEmpty()) {
        emit finished(false, m_parser.error());
        return;
    }
    emit finished(true, QString());
}

} // namespace dp::assistant
