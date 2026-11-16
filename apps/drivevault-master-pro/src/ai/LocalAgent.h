#pragma once

#include "AgentTools.h"
#include "CommandRunner.h"
#include "WebTools.h"

#include <QJsonArray>
#include <QList>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <functional>

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
        bool allowEdits = false;                           // write_file is offered
        bool approveWrites = false;                        // ...but every write waits for the user (approvalNeeded / answerApproval)
        bool web = false;                                  // web_search and fetch_page
        bool allowCommands = false;                        // run_command is offered; EVERY command waits for the user (commandApprovalNeeded)
        bool planMode = false;                             // exit_plan_mode is offered; nothing is changed until the user approves the plan (planApprovalNeeded)
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
    bool waitingForApproval() const { return m_waiting; }
    void answerApproval(bool allow);                       // the user's answer to approvalNeeded

signals:
    void piece(const QString& text);                       // text of the model (the answer, or what it says between tool calls)
    void approvalNeeded(const QString& path, bool replaces, qint64 bytes, const QString& preview, bool diff);   // a write_file / edit_file waits (preview: the start of the content, or for edit_file a diff, diff = true): call answerApproval
    void commandApprovalNeeded(const QString& command);    // a run_command waits: call answerApproval
    void planApprovalNeeded(const QString& plan);          // exit_plan_mode waits: call answerApproval (allow = start the work: editing and commands are switched on)
    void planApproved();                                   // the user approved the plan
    void todosChanged(const QVariantList& todos);          // [{content, status: pending | in_progress | completed}], the whole list
    void activity(const QString& text);                    // "Read: notes.txt", "Web search: ..."
    void finished(bool ok, const QString& code, const QString& detail);   // code: "" | cancelled | tooManySteps | http<status> | network | badReply | empty

private:
    void step();
    void onReply();
    void runNextCall();
    void runControlCall(const QString& name, const QJsonObject& args, const std::function<void(const QString&, bool)>& result);
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
    int m_parseRetries = 0;                                // answers cut off inside a tool call, asked again
    int m_webCalls = 0;                                   // web tool calls of this task: a small model can search for ever
    QStringList m_webSeen;                                 // "name + arguments" of the web calls already made
    bool m_running = false;
    bool m_cancelled = false;
    bool m_wroteText = false;
    bool m_waiting = false;                                // a change, a command or a plan waits for the user
    std::function<void(bool)> m_onAnswer;                  // what to do with the user's answer
    CommandRunner* m_runner = nullptr;
    int m_generation = 0;                                  // bumps on every start(): a late answer of an old run does nothing
};

} // namespace dm::ai
