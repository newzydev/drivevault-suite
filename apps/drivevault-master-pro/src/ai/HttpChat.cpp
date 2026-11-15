#include "HttpChat.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace dm::ai {

HttpChat::HttpChat(QNetworkAccessManager* net, QObject* parent)
    : QObject(parent)
    , m_net(net)
{
}

HttpChat::~HttpChat()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
}

bool HttpChat::urlAllowed(const QString& url)
{
    const QUrl u(url);
    if (!u.isValid() || u.host().isEmpty())
        return false;
    if (u.scheme() == QLatin1String("https"))
        return true;
    return u.scheme() == QLatin1String("http")
           && (u.host() == QLatin1String("127.0.0.1") || u.host() == QLatin1String("localhost") || u.host() == QLatin1String("::1"));
}

QString HttpChat::endpoint(const Request& r)
{
    QString base = r.baseUrl.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    return base + (r.kind == Kind::OpenAi ? QStringLiteral("/chat/completions") : QStringLiteral("/v1/messages"));
}

QByteArray HttpChat::body(const Request& r)
{
    QJsonObject o;
    o["model"] = r.model;
    o["stream"] = true;
    QJsonArray messages;
    if (r.kind == Kind::OpenAi && !r.system.isEmpty())
        messages.append(QJsonObject{{"role", "system"}, {"content", r.system}});
    for (const auto& m : r.messages)
        messages.append(QJsonObject{{"role", m.first}, {"content", m.second}});
    o["messages"] = messages;
    if (r.kind == Kind::Anthropic) {
        o["max_tokens"] = r.maxTokens;
        if (!r.system.isEmpty())
            o["system"] = r.system;
    }
    for (auto it = r.extraBody.begin(); it != r.extraBody.end(); ++it)
        o[it.key()] = it.value();
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString HttpChat::start(const Request& r)
{
    if (r.model.trimmed().isEmpty())
        return QStringLiteral("noModel");
    const QString url = endpoint(r);
    if (!QUrl(url).isValid() || QUrl(url).host().isEmpty())
        return QStringLiteral("badUrl");
    if (!urlAllowed(url))
        return QStringLiteral("insecureUrl");
    m_kind = r.kind;
    m_parser = SseParser();
    m_errorBody.clear();
    m_streamError.clear();
    m_sawText = false;
    m_done = false;
    m_cancelled = false;

    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "text/event-stream");
    if (r.kind == Kind::Anthropic) {
        req.setRawHeader("x-api-key", r.apiKey.toUtf8());
        req.setRawHeader("anthropic-version", "2023-06-01");
    } else if (!r.apiKey.isEmpty()) {
        req.setRawHeader("Authorization", "Bearer " + r.apiKey.toUtf8());
    }
    req.setTransferTimeout(120000);          // two minutes without any data
    m_reply = m_net->post(req, body(r));
    connect(m_reply, &QNetworkReply::readyRead, this, &HttpChat::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &HttpChat::onFinished);
    return {};
}

void HttpChat::cancel()
{
    if (!m_reply)
        return;
    m_cancelled = true;
    m_reply->abort();
}

void HttpChat::onReadyRead()
{
    if (!m_reply)
        return;
    const QByteArray data = m_reply->readAll();
    const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 400) {
        m_errorBody += data;                                 // the explanation of the error, shown when the request ends
        return;
    }
    for (const SseEvent& event : m_parser.feed(data)) {
        const StreamPiece p = m_kind == Kind::OpenAi ? readOpenAiEvent(event) : readAnthropicEvent(event);
        if (!p.error.isEmpty())
            m_streamError = p.error;
        if (!p.text.isEmpty()) {
            m_sawText = true;
            emit piece(p.text);
        }
        if (p.done)
            m_done = true;
    }
}

void HttpChat::onFinished()
{
    QNetworkReply* reply = m_reply;
    if (!reply)
        return;
    onReadyRead();
    m_reply = nullptr;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QNetworkReply::NetworkError error = reply->error();
    const QString errorString = reply->errorString();
    reply->deleteLater();
    if (m_cancelled) {
        emit finished(false, QStringLiteral("cancelled"), {});
        return;
    }
    if (status >= 400) {
        const QJsonObject root = QJsonDocument::fromJson(m_errorBody).object();
        QString message = root.value(QLatin1String("error")).isObject() ? root.value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString()
                                                                         : root.value(QLatin1String("error")).toString();
        if (message.isEmpty())
            message = QString::fromUtf8(m_errorBody.left(300)).trimmed();
        emit finished(false, QStringLiteral("http%1").arg(status), message);
        return;
    }
    if (error != QNetworkReply::NoError && !m_done) {
        emit finished(false, QStringLiteral("network"), errorString);
        return;
    }
    if (!m_streamError.isEmpty()) {
        emit finished(false, QStringLiteral("stream"), m_streamError);
        return;
    }
    if (!m_sawText) {
        emit finished(false, QStringLiteral("empty"), {});
        return;
    }
    emit finished(true, {}, {});
}

} // namespace dm::ai
