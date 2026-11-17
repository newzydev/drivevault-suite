#pragma once

#include "Streams.h"

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>

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
    };

    explicit HttpChat(QNetworkAccessManager* net, QObject* parent = nullptr);
    ~HttpChat() override;

    // "" when the request was sent, otherwise a code: noModel | badUrl | insecureUrl
    QString start(const Request& request);
    void cancel();
    bool running() const { return m_reply != nullptr; }

    // Pure helpers (tested): the URL a request goes to and whether it is allowed.
    static QString endpoint(const Request& request);
    static bool urlAllowed(const QString& url);          // https, or http to this computer
    static QByteArray body(const Request& request);

signals:
    void piece(const QString& text);
    void thinking();                                      // the model started to reason (its reasoning is not shown)
    void finished(bool ok, const QString& code, const QString& detail);    // code: "" | cancelled | http<status> | network | stream | empty

private:
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
};

} // namespace dm::ai
