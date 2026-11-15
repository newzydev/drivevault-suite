#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

namespace dp::assistant {

// Server-sent events of an OpenAI style streaming reply ("data: {...}\n\n" ... "data: [DONE]"): feed it the bytes as they arrive, get the text
// pieces. Pure, so it can be tested without a network.
class SseParser {
public:
    // Appends bytes; returns the content pieces of the complete events (and sets done / error when the stream says so).
    QStringList feed(const QByteArray& bytes);
    bool done() const { return m_done; }
    QString error() const { return m_error; }

private:
    QByteArray m_buffer;
    bool m_done = false;
    QString m_error;
};

// One streaming chat request to a llama.cpp server (http://127.0.0.1:port/v1/chat/completions). The text arrives piece by piece.
class ChatStream : public QObject {
    Q_OBJECT
public:
    explicit ChatStream(QNetworkAccessManager* net, QObject* parent = nullptr);
    ~ChatStream() override;

    // baseUrl without a trailing slash; apiKey may be empty. `stream` false waits for the whole answer (the pieces then arrive in one go).
    void start(const QString& baseUrl, const QString& apiKey, const QJsonArray& messages, int maxTokens, double temperature = 0.3, bool stream = true);
    void cancel();
    bool running() const { return m_reply != nullptr; }

signals:
    void piece(const QString& text);                  // a new piece of the answer
    void finished(bool ok, const QString& error);     // after the last piece, or on failure (a cancel is ok == false, error "cancelled")

private:
    void onReadyRead();
    void onFinished();

    QNetworkAccessManager* m_net;
    QNetworkReply* m_reply = nullptr;
    SseParser m_parser;
    QByteArray m_plain;           // the body when it is not a stream
    bool m_stream = true;
    bool m_cancelled = false;
    bool m_failed = false;
};

} // namespace dp::assistant
