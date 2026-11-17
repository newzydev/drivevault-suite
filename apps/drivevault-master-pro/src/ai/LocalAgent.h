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
        int maxSteps = 12;                                 // a safety net, not a plan: a real job ends when the model is done (see "finishing" in the .cpp)
        int contextTokens = 8192;                          // the window of the server: older tool results are shortened / dropped to stay inside it
        bool thinking = false;                            // let the model reason first (slower, better on hard questions)
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
    // One line of the step list of the page. `no` numbers the steps of a run (a step is sent again when its state changes). kind: list | read | search | glob |
    // edit | write | command | websearch | fetch | todo | plan | note | other; target: the file, the query, the command ... (for a note: its text);
    // state: running | waiting (for the user's answer) | done | error; note: a short result ("exit code 0", the error text).
    void stepEvent(int no, const QString& kind, const QString& target, const QString& state, const QString& note);
    void finished(bool ok, const QString& code, const QString& detail);   // code: "" | cancelled | tooManySteps | http<status> | network | badReply | empty

private:
    void step();
    void onReply();
    void runNextCall();
    void runControlCall(const QString& name, const QJsonObject& args, const std::function<void(const QString&, bool)>& result);
    void compact(qint64 budgetTokens);                     // keeps the conversation inside the window: shortens old results, then drops the oldest
    void shortenMessage(int index);
    qint64 estimateTokens() const;
    void stepStart(const QString& kind, const QString& target);     // a new line of the step list: running
    void stepEnd(const QString& state, const QString& note);        // the current line changes its state
    void note(const QString& text);                                 // a line that is not a tool call (also the old activity line)
    int m_stepNo = 0;
    int m_curNo = 0;
    QString m_curKind, m_curTarget;                                  // the step of the tool call that runs now
    bool nudge(const QString& activityText, const QString& text);   // appends a user message, shows an activity line and asks the model again (always true)
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
    int m_emptyNudges = 0;                                 // "you sent no answer"
    int m_todoNudges = 0;                                  // "the task list is not finished"
    int m_continues = 0;                                   // answers cut by the length limit, continued
    int m_contextRetries = 0;                              // "the prompt does not fit": shortened harder and asked again
    double m_budgetFactor = 1.0;                           // 1 = use the whole window; lowered after the server said it does not fit
    int m_taskIndex = 0;                                   // the message with the user's task: never dropped
    qint64 m_toolTokens = 0;                               // the size of the tool list of this run, in tokens
    bool m_compacted = false;
    bool m_joinNext = false;                               // the next piece of text continues a cut answer (no blank line between)
    QVariantList m_todoList;                               // the last list of todo_write
    QString m_lastCall;                                    // "name args" of the last file / command call and how often in a row
    int m_sameCall = 0;
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
