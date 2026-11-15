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

// One streaming chat request to a model that speaks HTTP: an OpenAI style server (OpenAI, OpenRouter, Ollama, LM Studio, the local llama-server) or the
// Anthropic API. The text arrives piece by piece. The key travels only in the Authorization / x-api-key header of this one request, and only to https
// (or to this computer for http): a key is never sent in clear text over a network.
class HttpChat : public QObject {
    Q_OBJECT
public:
    enum class Kind { OpenAi, Anthropic };

    struct Request {
        Kind kind = Kind::OpenAi;
        QString baseUrl;                                  // OpenAi: ends before /chat/completions (e.g. https://api.openai.com/v1). Anthropic: https://api.anthropic.com
        QString apiKey;                                   // may be empty (a local server)
        QString model;
        QString system;
        QList<QPair<QString, QString>> messages;          // role (user | assistant), text; oldest first
        int maxTokens = 4096;
        QJsonObject extraBody;                            // merged into the body (e.g. chat_template_kwargs for the local server)
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
    void finished(bool ok, const QString& code, const QString& detail);    // code: "" | cancelled | http<status> | network | stream | empty

private:
    void onReadyRead();
    void onFinished();

    QNetworkAccessManager* m_net;
    QNetworkReply* m_reply = nullptr;
    Kind m_kind = Kind::OpenAi;
    SseParser m_parser;
    QByteArray m_errorBody;
    QString m_streamError;
    bool m_sawText = false;
    bool m_done = false;
    bool m_cancelled = false;
};

} // namespace dm::ai
