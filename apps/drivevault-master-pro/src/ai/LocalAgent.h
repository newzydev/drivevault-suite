#pragma once

#include "AgentTools.h"
#include "BrowserBridge.h"
#include "CommandPool.h"
#include "CommandRunner.h"
#include "WebTools.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QSet>
#include <QObject>
#include <QTimer>
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
        bool templateKwargs = true;                        // send chat_template_kwargs (llama.cpp); off for an online model
        bool remote = false;                               // an online model: a lost connection, a timeout or a busy service is asked again with growing pauses (RetryPolicy)
        int requestTimeoutMs = 600000;                     // no data for this long: the request is given up (an online model: RetryPolicy::kTimeoutMs, then asked again)
        bool localServer = true;                           // the model is the llama-server of this PC: a lost connection starts it again (serverLost); off for an online model
        QString folder;                                   // "" = no file tools
        bool allowEdits = false;                           // write_file is offered
        bool reviewWork = true;                            // before a job that changed files ends, ask the model once to check its own work
        bool approveWrites = false;                       // ...but every write waits for the user (approvalNeeded / answerApproval)
        bool web = false;                                  // web_search and fetch_page
        bool autoCommands = false;                         // ordinary commands run without asking (the automatic mode); a risky one still waits (isRiskyCommand)
        bool allowCommands = false;                       // run_command is offered; EVERY command waits for the user (commandApprovalNeeded)
        dm::browser::BrowserBridge* browser = nullptr;     // browser_open / browser_read (and browser_look with `vision`) are offered when there is a folder
        bool vision = false;                               // the model can see pictures (the server runs with its vision file): browser_look
        bool planMode = false;                             // exit_plan_mode is offered; nothing is changed until the user approves the plan (planApprovalNeeded)
        QString system;
        QList<QPair<QString, QString>> history;            // user / assistant messages, oldest first, the last is the new question
        QStringList images;                                // pictures (base64 JPEG) the user attached to the new question: sent with it (only when the model can see)
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
    void setPool(CommandPool* pool);                       // the pool whose tasks the commands are (the page's list); without one the agent makes its own
    CommandPool* pool();
    void answerApproval(bool allow);                       // the user's answer to approvalNeeded
    void resume();                                         // after serverLost: the server runs again, ask the same question again
    // The path and the text of a write_file / append_file call whose arguments may end anywhere (a cut answer): the text taken back to its last complete line.
    // `complete` = the text was closed, nothing was cut. false when there is no path yet or almost no text. Pure, tested.
    // A command that is never run without asking, also in the automatic mode: deletes folders or many files, formats, changes the registry / services / accounts /
    // the system, installs or uninstalls, pushes / resets in git, or fetches from the Internet. Pure, tested.
    static bool isRiskyCommand(const QString& command);
    static bool salvageWrite(const QString& rawArguments, QString* path, QString* text, bool* complete);

signals:
    void piece(const QString& text);                       // text of the model (the answer, or what it says between tool calls)
    void approvalNeeded(const QString& path, bool replaces, qint64 bytes, const QString& preview, const QString& operation);   // a write_file / edit_file / append_file waits (operation: write | edit | append; preview: the start of the content, for an edit a diff): call answerApproval
    void commandApprovalNeeded(const QString& command);    // a run_command waits: call answerApproval
    void planApprovalNeeded(const QString& plan);          // exit_plan_mode waits: call answerApproval (allow = start the work: editing and commands are switched on)
    void planApproved();                                   // the user approved the plan
    void todosChanged(const QVariantList& todos);          // [{content, status: pending | in_progress | completed}], the whole list
    void activity(const QString& text);                    // "Read: notes.txt", "Web search: ..."
    // One line of the step list of the page. `no` numbers the steps of a run (a step is sent again when its state changes). kind: list | read | search | glob |
    // edit | write | command | websearch | fetch | todo | plan | note | other; target: the file, the query, the command ... (for a note: its text);
    // state: running | waiting (for the user's answer) | done | error; note: a short result ("exit code 0", the error text).
    // The connection to the model server was lost before an answer arrived (it stopped or crashed). The agent waits: start the server again and call resume().
    void serverLost();
    void usage(int promptTokens, int completionTokens);    // what the server counted for the last request (the context meter of the page)
    // While the model answers (the answer is read as a stream): phase waiting (the prompt is read) | thinking | writing (text) | tool (the arguments of a tool call
    // grow: `tool` is its name, `target` the file / command when it is already known, `chars` how much was written). An empty phase: the answer is complete.
    void generating(const QString& phase, const QString& tool, const QString& target, int chars);
    void textRetracted(int chars);                         // an online try failed after chars characters of text were shown: take them back (the answer starts again)
    void stepEvent(int no, const QString& kind, const QString& target, const QString& state, const QString& note);
    void finished(bool ok, const QString& code, const QString& detail);   // code: "" | cancelled | tooManySteps | http<status> | network | badReply | empty

private:
    void step();
    void onReadyRead();                                    // a piece of the streamed answer
    void onReply();                                        // the answer is complete (or failed)
    void processEvent(const QByteArray& event);            // one "data: {...}" event of the stream
    void reportProgress(bool force);                       // `generating`, at most every 250 ms
    // An answer that ended inside a tool call (cut by the length limit, or stopped by us because the call grew too long) or has arguments that are not valid
    // JSON: a write_file / append_file keeps the part that was written (it is saved, then the model continues with append_file); otherwise the model is asked
    // again for something shorter. true = taken over (a retry or the end of the job started); false = go on with `calls` (changed when a part was kept).
    bool handleBrokenCalls(QJsonArray* calls, const QString& content, bool cut);
    void runNextCall();
    void runControlCall(const QString& name, const QJsonObject& args, const std::function<void(const QString&, bool)>& result);
    void compact(qint64 budgetTokens);                     // keeps the conversation inside the window: shortens old results, then drops the oldest
    void shortenMessage(int index);
    qint64 estimateTokens() const;
    void stepStart(const QString& kind, const QString& target);     // a new line of the step list: running
    void stepEnd(const QString& state, const QString& note);        // the current line changes its state
    void note(const QString& text);                                 // a line that is not a tool call (also the old activity line)
    struct StreamCall {
        QString id, name, args;
    };
    struct Stream {                                        // the answer being read
        bool sse = false;                                  // a stream of events (else one JSON body, as the tests and older servers send it)
        QByteArray raw;                                    // the whole body (capped): needed for a JSON answer and for an error
        QByteArray pending;                                // events that are not complete yet
        QString content;
        bool contentStarted = false;
        qint64 reasoningChars = 0;
        QMap<int, StreamCall> calls;
        QString finish, error;
        QJsonObject usage;
        bool abortedLong = false;                          // we stopped the answer: a tool call grew too long
        QString lastPhase;                                 // what `generating` showed last (a change is shown at once)
        QElapsedTimer progress;
        int emitted = 0;                                   // characters of text this try showed (taken back when the try is repeated)
    };
    Stream m_stream;
    bool retryRemote(const QString& why, int retryAfterMs, bool retryable);   // an online model failed in a way that goes away: ask again after a pause (true = scheduled)
    void retractPartial();                                 // takes back the text of a failed try
    QTimer* m_retryTimer = nullptr;
    int m_retries = 0;                                     // tries after the first one for the request that is repeated now (0 again after an answer)
    bool m_wroteTextAtStart = false, m_joinNextAtStart = false;
    QString m_afterCalls;                                  // a note for the model, added after the result of a kept (cut) write
    bool m_lastResultError = false;
    int m_salvages = 0;                                    // parts of cut files kept in this job
    int m_serverRetries = 0;                               // times the model server was started again and the question asked again (2 at most per job)
    bool m_awaitingServer = false;                         // waiting for resume()
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
    void runBrowserCall(const QString& name, const QJsonObject& args, const std::function<void(const QString&, bool)>& result);
    int m_reviewCalls = 0;                                 // tool calls made after the final check began (the check and the polishing that follows are limited)
    bool m_forceFinish = false;                            // the limit was reached: no tools any more, the model must write its summary
    QHash<QString, int> m_editFails;                       // edit_file calls that failed in a row, per file (after two the model is told to rewrite the file instead)
    QSet<QString> m_previewed;                             // web pages the browser was asked to show in this job (once each: the page then follows the file by itself)
    int m_browserActions = 0;                              // clicks, typing, scrolling, dragging and tab changes of this job
    QJsonArray m_pendingImages;                          // screenshots the tools made: sent to the model as a user message after the tool results
    int m_looks = 0;                                       // browser_look calls of this job
    QStringList m_changedFiles;                           // files this job wrote / edited / appended (for the final check)
    bool m_reviewed = false;                               // the final check was asked for
    int m_todoNudges = 0;                                 // "the task list is not finished"
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
    QSet<QString> m_approvedFiles;                         // files the user allowed a write / append to: later parts of the same file need no new card
    bool m_waiting = false;                               // a change, a command or a plan waits for the user
    std::function<void(bool)> m_onAnswer;                  // what to do with the user's answer
    CommandPool* m_pool = nullptr;                         // the commands of this agent are tasks of this pool
    int m_foregroundTask = 0;                              // the command the job waits for now (0 = none)
    QSet<int> m_myTasks, m_bgTasks;                        // the tasks this agent started / the background ones among them
    QStringList m_taskNotes;                               // "task 3 has ended ...", told to the model at its next request
    int m_generation = 0;                                  // bumps on every start(): a late answer of an old run does nothing
};

} // namespace dm::ai
