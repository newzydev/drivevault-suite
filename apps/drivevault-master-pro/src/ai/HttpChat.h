#pragma once

#include "Streams.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;

namespace dm::ai {

// One streaming chat request to the local model (llama-server, OpenAI style). The text arrives piece by piece. Only https, or http to this computer, is
// allowed (`urlAllowed`).
class HttpChat : public QObject {
    Q_OBJECT
public:
    struct Request {
        QString baseUrl;                                  // ends before /chat/completions (e.g. http://127.0.0.1:12345/v1)
        QString apiKey;                                   // may be empty
        QString model;
        QString system;
        QList<QPair<QString, QString>> messages;          // role (user | assistant), text; oldest first
        QStringList images;                               // pictures (base64 JPEG) of the LAST user message (only when the model can see)
        QJsonObject extraBody;                            // merged into the body (chat_template_kwargs, max_tokens ...)
        bool retry = false;                               // an online model: a lost connection, a timeout or a busy service is asked again (RetryPolicy); the local server never
        int timeoutMs = 600000;                           // no data for this long: the request is given up (and asked again when retry is set)
    };

    explicit HttpChat(QNetworkAccessManager* net, QObject* parent = nullptr);
    ~HttpChat() override;

    // "" when the request was sent, otherwise a code: noModel | badUrl | insecureUrl
    QString start(const Request& request);
    void cancel();
    bool running() const { return m_reply != nullptr || (m_retryTimer && m_retryTimer->isActive()); }

    // Pure helpers (tested): the URL a request goes to and whether it is allowed.
    static QString endpoint(const Request& request);
    static bool urlAllowed(const QString& url);          // https, or http to this computer
    static QByteArray body(const Request& request);

signals:
    void piece(const QString& text);
    void thinking();                                      // the model started to reason (its reasoning is not shown)
    void retracted(int chars);                            // a try failed after `chars` characters were shown: take them back (the answer starts again)
    void retrying(const QString& why, int retry, int maxRetries, int delayMs);   // the request is asked again after `delayMs`
    void finished(bool ok, const QString& code, const QString& detail);    // code: "" | cancelled | http<status> | network | stream | empty

private:
    void launch();                                        // sends m_request (also for a retry)
    bool retryLater(const QString& why, int retryAfterMs, bool retryable);    // schedules the next try; false = no more tries (or not for this request)
    void onReadyRead();
    void onFinished();

    QNetworkAccessManager* m_net;
    QNetworkReply* m_reply = nullptr;
    SseParser m_parser;
    QByteArray m_errorBody;
    QString m_streamError;
    bool m_sawText = false;
    bool m_done = false;
    bool m_cancelled = false;
    bool m_thinkingSignalled = false;
    Request m_request;
    QTimer* m_retryTimer = nullptr;
    int m_retries = 0;
    int m_emitted = 0;                                    // characters of the answer shown by the current try
};

} // namespace dm::ai
