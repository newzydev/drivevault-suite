#pragma once

#include "AgentTools.h"
#include "WebTools.h"

#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace dm::ai {

// The agent that works with a model on this PC: it asks the model (llama-server, OpenAI style with tools), runs the tools the model asks for - files inside the
// folder the user chose (AgentTools: read, search, and write only when allowed) and, when the user switched it on, the web (WebTools) - gives the results
// back and asks again, until the model answers in words. Every step is shown as an activity line. At most `maxSteps` rounds; it can be cancelled at any time.
class LocalAgent : public QObject {
    Q_OBJECT
public:
    struct Config {
        QString baseUrl;                                   // http://127.0.0.1:port/v1 (this computer only)
        QString apiKey;
        QString model = QStringLiteral("local");
        QString folder;                                    // "" = no file tools
        bool allowEdits = false;
        bool web = false;                                  // web_search and fetch_page
        QString system;
        QList<QPair<QString, QString>> history;            // user / assistant messages, oldest first, the last is the new question
        int maxSteps = 12;
        bool thinking = false;                             // let the model reason first (slower, better on hard questions)
        int maxTokens = 0;                                 // 0 = the server default
    };

    explicit LocalAgent(QNetworkAccessManager* net, QObject* parent = nullptr);
    ~LocalAgent() override;

    // "" when started, otherwise: badUrl | noTools | noFolder | busy
    QString start(const Config& config);
    void cancel();
    bool running() const { return m_running; }

signals:
    void piece(const QString& text);                       // text of the model (the answer, or what it says between tool calls)
    void activity(const QString& text);                    // "Read: notes.txt", "Web search: ..."
    void finished(bool ok, const QString& code, const QString& detail);   // code: "" | cancelled | tooManySteps | http<status> | network | badReply | empty

private:
    void step();
    void onReply();
    void runNextCall();
    void end(bool ok, const QString& code, const QString& detail);

    QNetworkAccessManager* m_net;
    QNetworkReply* m_reply = nullptr;
    Config m_config;
    AgentTools* m_files = nullptr;
    WebTools* m_web = nullptr;
    QJsonArray m_messages;
    QJsonArray m_calls;                                    // the tool calls of the last reply, run one after the other
    int m_callIndex = 0;
    int m_steps = 0;
    bool m_running = false;
    bool m_cancelled = false;
    bool m_wroteText = false;
};

} // namespace dm::ai
