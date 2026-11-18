#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace dm::ai {

// "Compacting" a conversation like Claude Code does: the model on this PC is asked ONCE, without tools, to write a summary of the conversation so far
// (the user's goal and wishes, what was done to which files, errors and how they were solved, what is left); the conversation then goes on with that
// summary in place of the old messages. One plain (not streamed) request to the local llama-server; nothing leaves this computer.
class Compactor : public QObject {
    Q_OBJECT
public:
    explicit Compactor(QNetworkAccessManager* net, QObject* parent = nullptr);
    ~Compactor() override;

    // "" when started, otherwise badUrl | busy
    QString start(const QString& baseUrl, const QString& apiKey, const QString& transcript, const QString& focus, int maxTokens, const QString& model = QStringLiteral("local"));
    void cancel();
    bool running() const { return m_reply != nullptr; }

    static QString systemPrompt(const QString& focus);              // pure, tested
    static constexpr int kSummaryTokens = 1800;                     // the longest summary

signals:
    void finished(bool ok, const QString& summary, const QString& error);   // error: cancelled | http<status> | network | empty | badReply (+ the server's words in `summary`)

private:
    QNetworkAccessManager* m_net;
    QNetworkReply* m_reply = nullptr;
    bool m_cancelled = false;
};

} // namespace dm::ai
