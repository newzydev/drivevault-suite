#include "HttpChat.h"
#include "RetryPolicy.h"

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
    m_retryTimer = new QTimer(this);
    m_retryTimer->setSingleShot(true);
    connect(m_retryTimer, &QTimer::timeout, this, [this] { launch(); });
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
    return base + QStringLiteral("/chat/completions");
}

QByteArray HttpChat::body(const Request& r)
{
    QJsonObject o;
    o["model"] = r.model;
    o["stream"] = true;
    QJsonArray messages;
    if (!r.system.isEmpty())
        messages.append(QJsonObject{{"role", "system"}, {"content", r.system}});
    for (int i = 0; i < r.messages.size(); ++i) {
        const auto& m = r.messages.at(i);
        if (i == r.messages.size() - 1 && m.first == QLatin1String("user") && !r.images.isEmpty()) {       // the question carries the user's pictures
            QJsonArray parts{QJsonObject{{"type", "text"}, {"text", m.second}}};
            for (const QString& img : r.images)
                parts.append(QJsonObject{{"type", "image_url"}, {"image_url", QJsonObject{{"url", QStringLiteral("data:image/jpeg;base64,") + img}}}});
            messages.append(QJsonObject{{"role", m.first}, {"content", parts}});
        } else {
            messages.append(QJsonObject{{"role", m.first}, {"content", m.second}});
        }
    }
    o["messages"] = messages;
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
    m_request = r;
    m_retries = 0;
    m_retryTimer->stop();
    launch();
    return {};
}

void HttpChat::launch()
{
    const Request& r = m_request;
    const QString url = endpoint(r);
    m_emitted = 0;
    m_parser = SseParser();
    m_errorBody.clear();
    m_streamError.clear();
    m_sawText = false;
    m_done = false;
    m_cancelled = false;
    m_thinkingSignalled = false;

    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "text/event-stream");
    if (!r.apiKey.isEmpty()) {
        req.setRawHeader("Authorization", "Bearer " + r.apiKey.toUtf8());
    }
    req.setTransferTimeout(r.timeoutMs);     // no data for this long (a try that hangs is given up)
    m_reply = m_net->post(req, body(r));
    connect(m_reply, &QNetworkReply::readyRead, this, &HttpChat::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &HttpChat::onFinished);
}

bool HttpChat::retryLater(const QString& why, int retryAfterMs, bool retryable)
{
    if (!m_request.retry || !retryable || m_retries >= RetryPolicy::kMaxRetries)
        return false;
    ++m_retries;
    const int delay = RetryPolicy::delayMs(m_retries, retryAfterMs);
    if (m_emitted > 0) {                                  // what this try showed is taken back: the next try writes the answer from its start
        emit retracted(m_emitted);
        m_emitted = 0;
    }
    emit retrying(why, m_retries, RetryPolicy::kMaxRetries, delay);
    m_retryTimer->start(delay);
    return true;
}

void HttpChat::cancel()
{
    if (!m_reply) {
        if (m_retryTimer->isActive()) {                   // cancelled while waiting for the next try
            m_retryTimer->stop();
            emit finished(false, QStringLiteral("cancelled"), {});
        }
        return;
    }
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
        const StreamPiece p = readOpenAiEvent(event);
        if (p.thinking && !m_thinkingSignalled) {
            m_thinkingSignalled = true;
            emit thinking();
        }
        if (!p.error.isEmpty())
            m_streamError = p.error;
        if (!p.text.isEmpty()) {
            m_sawText = true;
            m_emitted += int(p.text.size());
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
    const int retryAfter = RetryPolicy::retryAfterMs(reply->rawHeader("Retry-After"));
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
        if (retryLater(QStringLiteral("HTTP %1").arg(status), retryAfter, RetryPolicy::httpRetryable(status)))
            return;
        emit finished(false, QStringLiteral("http%1").arg(status), message);
        return;
    }
    if (error != QNetworkReply::NoError && !m_done) {
        if (retryLater(errorString, 0, RetryPolicy::networkRetryable(error)))
            return;
        emit finished(false, QStringLiteral("network"), errorString);
        return;
    }
    if (!m_streamError.isEmpty()) {
        if (!m_sawText && retryLater(m_streamError, 0, true))   // an error inside the stream before any text: the service had a hiccup
            return;
        emit finished(false, QStringLiteral("stream"), m_streamError);
        return;
    }
    if (!m_sawText) {
        if (retryLater(QStringLiteral("empty answer"), 0, true))
            return;
        emit finished(false, QStringLiteral("empty"), {});
        return;
    }
    emit finished(true, {}, {});
}

} // namespace dm::ai
