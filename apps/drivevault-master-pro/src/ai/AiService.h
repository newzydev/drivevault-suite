#pragma once

#include "Compactor.h"
#include "GitInfo.h"
#include "HttpChat.h"
#include "LlamaServer.h"
#include "LocalAgent.h"
#include "MessageModel.h"
#include "ModelDownloader.h"
#include "ModelStore.h"

#include <QList>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace dm {
class Hub;
}

namespace dm::ai {

class AiService;

// How hard the model tries (the "effort" menu of the chat): what it costs in time, and what it may do.
struct Effort {
    QString id;                    // low | medium | high
    bool thinking = false;         // let the model reason before it answers (llama.cpp: enable_thinking)
    int maxTokens = 0;             // longest answer (the reasoning counts too)
    int maxSteps = 12;             // tool rounds of the agent
    int historyChars = 14000;      // how much of the conversation goes along (the window of the local server is 8192 tokens)
    static Effort of(const QString& id);
    static QStringList ids() { return {QStringLiteral("low"), QStringLiteral("medium"), QStringLiteral("high")}; }
};

// One of the two AI pages (ask = AI Q&A, agent = Agentic AI): its list of conversations, the open conversation and the request that is running. Both use the
// model on this PC (llama.cpp + a GGUF model, see LlamaServer). QML object `Ai.ask` / `Ai.agent`.
class AiChannel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode CONSTANT FINAL)
    Q_PROPERTY(QString notReady READ notReady NOTIFY settingsChanged FINAL)           // "" when a message can be sent, otherwise why not
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(QString status READ status NOTIFY busyChanged FINAL)                   // "" | starting | thinking | writing
    Q_PROPERTY(QObject* messageModel READ messageModel CONSTANT FINAL)                // the same messages as a list model: the page shows this one (no rebuilding of the boxes)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged FINAL)    // [{role, text, streaming, stopped, errorCode, errorDetail, activity: [..]}]
    Q_PROPERTY(QVariantList conversations READ conversations NOTIFY conversationsChanged FINAL)   // [{id, title}]
    Q_PROPERTY(QString currentId READ currentId NOTIFY conversationsChanged FINAL)
    Q_PROPERTY(QVariantMap lastMessage READ lastMessage NOTIFY messagesChanged FINAL)  // the newest message (the answer that works): its step list is pinned above the message box
    Q_PROPERTY(QString folder READ folder NOTIFY settingsChanged FINAL)               // agent: the working folder
    Q_PROPERTY(QString permission READ permission NOTIFY settingsChanged FINAL)       // agent mode: read | plan | ask | edit (see Permission below)
    Q_PROPERTY(QString effort READ effort NOTIFY settingsChanged FINAL)               // low | medium | high
    Q_PROPERTY(QVariantList todos READ todos NOTIFY todosChanged FINAL)               // agent: the task list the model keeps (todo_write): [{content, status}]
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY tasksChanged FINAL)               // the background tasks: commands (running and finished), a compaction, the model download
    Q_PROPERTY(bool tasksRunning READ tasksRunning NOTIFY tasksChanged FINAL)         // something runs in the background (the page keeps its clock going)
    Q_PROPERTY(QVariantMap git READ git NOTIFY gitChanged FINAL)                     // agent: the working folder in git (GitInfo::info: branch, changed files ...)
    Q_PROPERTY(QVariantMap context READ context NOTIFY contextChanged FINAL)          // {used, window, percent, exact}: how much of the model window the open conversation uses

public:
    struct Message {
        QString role;                      // user | assistant
        QString text;
        QStringList thumbs;                // user message: small previews (base64 JPEG, 480 px) of the attached pictures, saved with the conversation and shown in the bubble
        QStringList attachNames;           // user message: the files / pictures the user attached (shown as chips)
        QString extra;                     // user message: the text of the attached files, for the model only (saved, not shown)
        QStringList images;                // user message: attached pictures as base64 JPEG, for the model only (kept while the program runs, not saved)
        QStringList activity;
        QString errorCode, errorDetail;
        bool streaming = false;
        bool stopped = false;
        qint64 startedMs = 0;              // when the request began (epoch ms; 0 = a local answer of a slash command)
        qint64 durationMs = 0;             // how long it took (0 while it runs)
        int steps = 0;                     // tool steps of the request (the activity list keeps only the newest 300)
        QVariantList stepList;             // the step list the page shows: [{no, kind, target, state: running | waiting | done | error | stopped, note}] (the newest 400)
        QVariantMap live;                  // what the model is writing right now (not saved): {phase, tool, target, chars}; empty when nothing is
        bool compact = false;              // not a message: the card "the conversation was compacted"; `summary` replaces everything before it (see Compactor)
        QString summary;
        int tokensBefore = 0, tokensAfter = 0;
        QVariantMap approval;              // waits for the user's answer (not saved): {kind: "change", path, replaces, bytes, preview, diff} | {kind: "command", command} | {kind: "plan", plan}
    };
    struct Conversation {
        QString id, title;
        qint64 updated = 0;
        QList<Message> messages;
        QVariantList todos;                // the plan the AI keeps: [{content, status}]
        QString folder;                    // agent page: the working folder when the conversation began (the sidebar groups by it)
        int usedTokens = 0;                // what the server said the last request used (not saved; 0 = unknown)
    };

    AiChannel(const QString& mode, AiService* service);
    ~AiChannel() override;

    QString mode() const { return m_mode; }
    bool isAgent() const { return m_mode == QLatin1String("agent"); }
    QString notReady() const;
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QVariantList messages() const;
    QVariantMap lastMessage() const;
    QObject* messageModel() { return m_model; }
    QVariantList conversations() const;
    QString currentId() const { return m_currentId; }
    QString folder() const { return m_folder; }
    QString permission() const { return m_permission; }
    static QStringList permissions() { return {QStringLiteral("read"), QStringLiteral("plan"), QStringLiteral("ask"), QStringLiteral("edit")}; }
    bool canWrite() const { return m_permission == QLatin1String("ask") || m_permission == QLatin1String("edit"); }
    QString effort() const { return m_effort; }
    QVariantList todos() const;                            // the plan of the open conversation
    QVariantMap git() const { return m_git ? m_git->info() : QVariantMap{{"state", "none"}}; }
    QVariantMap context() const;
    // [{id: "cmd:3" | "compact" | "download", kind: command | compact | download, title, state: running | done | error | stopped, startedMs, endedMs, note, background, progress}]
    // - running ones first, then the commands, the newest first
    QVariantList tasks() const;
    bool tasksRunning() const;
    Q_INVOKABLE void stopTask(const QString& id);          // the stop button of a task (a command is killed, a compaction or a download is cancelled)
    Q_INVOKABLE void startDiagnosticTasks();               // diagnostics only (--ai-view tasks): starts two REAL harmless commands (a ping that runs 30 s, an echo) to look at the list
    CommandPool* pool() { return m_pool; }
    // Compacts the open conversation like Claude Code: the model writes a summary that replaces the old messages in what is sent from now on. `focus` is an
    // optional wish ("keep the file names"). "" = started, otherwise why not: busy | nothing | a notReady code. Also done by "/compact" and, before a message is
    // sent, when the history has grown near what is sent along (needsCompaction).
    Q_INVOKABLE QString compactNow(const QString& focus = QString());
    static QList<Message> afterLastCompact(const QList<Message>& messages);      // what the next request carries: the messages after the last finished compaction
    Q_INVOKABLE void refreshGit();                      // ask git again (also done after a change, a finished answer and a new folder)
    Q_INVOKABLE void newConversationIn(const QString& folder);   // the "+" of a group of the sidebar: a new conversation in that folder

    // A message that starts with "/" is a command of the page (agent page: /help /clear /new /mode /effort /web /todos /init). Returns true when it was handled
    // here; `prompt` is then empty (nothing goes to the model) - or, for /init, the text to send instead.
    bool slashCommand(const QString& text, QString* prompt);

    Q_INVOKABLE QString send(const QString& text, const QVariantList& attachments = {});   // "" = started, otherwise why not (notReady code, busy, empty). attachments: [{kind: file | image, name, text | data}] from AiService::readAttachment / clipboardImage
    Q_INVOKABLE void stop();
    Q_INVOKABLE void newConversation();
    Q_INVOKABLE void select(const QString& id);
    Q_INVOKABLE void remove(const QString& id);
    Q_INVOKABLE void setFolder(const QString& folder);
    // The agent's mode: read (look at the files and answer), plan (look, then write a plan; change nothing), ask (every file change waits for the user's
    // answer), edit (file changes are accepted without asking). Edits only ever happen inside the chosen folder.
    Q_INVOKABLE void setPermission(const QString& permission);
    Q_INVOKABLE void answerApproval(bool allow);               // the user's answer to a waiting file change
    Q_INVOKABLE void setEffort(const QString& effort);

    void load();                                          // conversations of this page
    void applySettings(const QVariantMap& values);        // used by the service when it reads the settings file
    QVariantMap settingsValues() const;
    void reloadReadiness() { emit settingsChanged(); }

    // Pure, tested: the messages that go to the model (the newest ones that fit `maxChars`; the first is always a user message).
    static QList<QPair<QString, QString>> historyFor(const QList<Message>& messages, int maxChars);

signals:
    void settingsChanged();
    void busyChanged();
    void messagesChanged();
    void conversationsChanged();
    void changed();                                        // anything worth saving to the settings file
    void todosChanged();
    void gitChanged();
    void contextChanged();
    void tasksChanged();

private:
    void scheduleGit();                                    // refreshGit in a moment (changes come in bursts)
    QString summaryNote() const;                           // the summary of the last compaction as a part of the system prompt ("" when there is none)
    bool needsCompaction() const;                          // the history that would go along is near what is kept: compact before the next message
    QString transcriptFor(const Conversation& conversation) const;   // the text the model summarizes
    void startCompaction(const QString& focus, bool automatic, const QString& pendingText, const QString& userText);
    void finishCompaction(bool ok, const QString& summary, const QString& error);
    void localExchange(const QString& userText, const QString& reply);        // a question and an answer that never reach the model (slash commands)
    QString projectNotes(QString* fileName) const;         // the start of CLAUDE.md / AGENTS.md of the working folder ("" when there is none)
    void clearTodos();
    Conversation* current();
    Conversation* find(const QString& id);
    Conversation& ensureConversation(const QString& firstText);
    void scheduleView();
    void setBusy(bool busy, const QString& status = {});
    void beginRequest(const QString& userText);
    void sendChat();                                       // a plain streamed chat (Q&A without tools)
    void sendTools();                                      // the agent loop: the folder (agent) and / or the web
    void withLocalServer(const std::function<void()>& go);        // runs `go` when the local model is ready (starts it when needed)
    void appendActivity(const QString& text);
    void onStep(int no, const QString& kind, const QString& target, const QString& state, const QString& note);
    void appendPiece(const QString& text);
    void setStatus(const QString& status);
    void finish(bool ok, const QString& code, const QString& detail);
    QString systemPrompt() const;
    void saveConversations();
    void cancelRunning();

    QString m_mode;
    AiService* m_service;
    QString m_folder;
    QString m_effort = QStringLiteral("medium");
    QString m_permission = QStringLiteral("edit");         // "edit" = automatic (since 1.23.0 the default: no questions for file changes and ordinary commands)
    QList<Conversation> m_conversations;
    QString m_currentId;
    QString m_runId;                                       // the conversation the running request writes into
    bool m_busy = false;
    QString m_status;
    HttpChat* m_http = nullptr;
    LocalAgent* m_localAgent = nullptr;
    MessageModel* m_model = nullptr;
    QTimer* m_keepAlive = nullptr;                         // while a request runs (or waits for the user) the model server is told every minute that it is in use
    CommandPool* m_pool = nullptr;                         // agent page: the commands as background tasks
    Compactor* m_compactor = nullptr;
    bool m_compacting = false;                             // a summary is being written (busy, status "compacting")
    QString m_pendingText;                                 // the message that waits for an automatic compaction
    QVariantList m_nextAttachments;                        // the attachments of the message that is being sent (also while it waits for a compaction)
    int m_compactBefore = 0;                               // the size (tokens, estimated) of what is being compacted
    GitInfo* m_git = nullptr;                              // agent page only
    QTimer* m_gitTimer = nullptr;
    QMetaObject::Connection m_waitLocal;
    QTimer* m_viewTimer = nullptr;
    int m_generation = 0;                                  // bumps on stop / switch: a late callback of an old request does nothing
};

// The AI of Master Pro: both pages, the model that is chosen, the models that are installed and the downloads. QML singleton `Ai`.
class AiService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* ask READ ask CONSTANT FINAL)
    Q_PROPERTY(QObject* agent READ agent CONSTANT FINAL)
    Q_PROPERTY(bool engineFound READ engineFound NOTIFY modelsChanged FINAL)
    Q_PROPERTY(QVariantList catalog READ catalog NOTIFY modelsChanged FINAL)           // [{id, title, fileName, sizeText, level, installed, path}]
    Q_PROPERTY(QVariantList installedModels READ installedModels NOTIFY modelsChanged FINAL)    // [{name, path, sizeText}]
    Q_PROPERTY(QString localModel READ localModel NOTIFY settingsChanged FINAL)        // path of the chosen model
    Q_PROPERTY(QString localModelName READ localModelName NOTIFY settingsChanged FINAL)   // its file name ("" when none)
    // Online models (owner's decision 2026-11-19): free models of NVIDIA (build.nvidia.com, NIM) with the key that is built into the program (EmbeddedKey.h) - the
    // user needs no key. Only when the user chose one; the messages (and, on the agent page, what the tools read) then leave this PC.
    Q_PROPERTY(QVariantList remoteModels READ remoteModels CONSTANT FINAL)             // [{id, title, maker, hint}]
    Q_PROPERTY(bool useRemote READ useRemote NOTIFY settingsChanged FINAL)             // the chosen model is an online one
    Q_PROPERTY(QString remoteModel READ remoteModel NOTIFY settingsChanged FINAL)      // id of the chosen online model ("" = none yet)
    Q_PROPERTY(QString activeModelLabel READ activeModelLabel NOTIFY settingsChanged FINAL)   // what the model button shows ("" = no model)
    Q_PROPERTY(QString modelsFolder READ modelsFolder CONSTANT FINAL)
    Q_PROPERTY(bool webEnabled READ webEnabled WRITE setWebEnabled NOTIFY settingsChanged FINAL)   // the AI may search the web (web_search, fetch_page)
    Q_PROPERTY(QObject* browser READ browser CONSTANT FINAL)                           // the link to the browser view (BrowserBridge)
    Q_PROPERTY(QVariantMap vision READ vision NOTIFY modelsChanged FINAL)              // {offered, installed, sizeText}: the vision file of the chosen model
    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadChanged FINAL)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadChanged FINAL)
    Q_PROPERTY(QString downloadCode READ downloadCode NOTIFY downloadChanged FINAL)
    Q_PROPERTY(QString downloadMessage READ downloadMessage NOTIFY downloadChanged FINAL)
    Q_PROPERTY(QString downloadTitle READ downloadTitle NOTIFY downloadChanged FINAL)
    Q_PROPERTY(QVariantMap probe READ probe NOTIFY probeChanged FINAL)                 // {state: idle|checking|ready|failed, error, repo, fileName, size, sizeText}

public:
    explicit AiService(Hub* hub, QObject* parent = nullptr);
    ~AiService() override;

    QObject* ask() { return m_ask; }
    QObject* agent() { return m_agent; }
    QObject* browser() { return &m_browser; }
    dm::browser::BrowserBridge* browserBridge() { return &m_browser; }
    QVariantMap vision() const;
    // Attachments of the message box. Both return {ok, kind: file | image, name, text | data (base64 JPEG), error}. A file: text only (no binary, at most 60,000 characters);
    // a picture: scaled to 1024 px, only when the model can see (the vision file is installed) and only on the agent page.
    Q_INVOKABLE QVariantMap readAttachment(const QString& fileUrlOrPath) const;
    Q_INVOKABLE bool clipboardHasImage() const;
    Q_INVOKABLE QVariantMap clipboardImage() const;
    Q_INVOKABLE QString downloadVision();                               // "" = started (the vision file of the chosen model, after the user pressed the button)
    AiChannel* askChannel() { return m_ask; }
    AiChannel* agentChannel() { return m_agent; }
    bool engineFound() const { return !LlamaServer::serverExecutable().isEmpty(); }
    QVariantList catalog() const;
    QVariantList installedModels() const;
    QString localModel() const { return m_localModel; }
    QString localModelName() const;
    QString modelsFolder() const;
    bool webEnabled() const { return m_web; }
    void setWebEnabled(bool on);
    bool downloading() const { return m_downloader.busy(); }
    double downloadProgress() const { return m_downloader.progress(); }
    QString downloadCode() const { return m_downloader.code(); }
    QString downloadMessage() const { return m_downloader.message(); }
    QString downloadTitle() const { return m_downloadTitle; }
    QVariantMap probe() const { return m_probe; }

    static constexpr int kRemoteContextTokens = 65536;                  // the window the program plans with for an online model (they have more; this keeps requests small and fast)
    QVariantList remoteModels() const;
    bool useRemote() const { return m_useRemote; }
    QString remoteModel() const { return m_remoteModel; }
    QString activeModelLabel() const;
    bool remoteActive() const { return m_useRemote && !m_remoteModel.isEmpty(); }
    static QString embeddedNvidiaKey();                                 // the key built into the program ("" when this build has none)
    void setRemoteBaseUrl(const QString& url) { m_remoteBase = url; }   // tests: a fake service on this computer
    // What a request goes to: the llama-server of this PC, or the online service when an online model is chosen.
    QString apiBaseUrl() const;                                         // ends before /chat/completions
    QString apiKey() const;
    QString apiModel() const;
    bool apiVision() const;                                             // the model can see pictures (online models: not used)
    int contextWindow() const;

    Q_INVOKABLE void selectLocalModel(const QString& path);
    Q_INVOKABLE void selectRemoteModel(const QString& id);              // an id of remoteModels(); the choice is saved
    Q_INVOKABLE QString downloadCatalog(const QString& id);             // "" = started
    Q_INVOKABLE void probeLink(const QString& link);                    // asks Hugging Face about a pasted link (size, checksum); downloads nothing
    Q_INVOKABLE QString downloadProbed();                               // downloads the file the last probe found
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE void refreshModels();

    // for the channels
    QNetworkAccessManager* net() { return m_net; }
    LlamaServer* llama() { return &m_llama; }
    Hub* hub() { return m_hub; }
    QString dataFolder() const { return m_dir; }
    QString suiteSummary() const;                                       // the programs of the suite, for the system prompt
    void saveSettings();

signals:
    void modelsChanged();
    void settingsChanged();
    void downloadChanged();
    void probeChanged();

private:
    void loadSettings();
    void pickModelIfNone();                                // no model chosen (or it is gone): the first installed one, the default catalogue model first
    QStringList modelFolders() const;
    void setProbe(const QVariantMap& probe);

    Hub* m_hub;
    QNetworkAccessManager* m_net;
    LlamaServer m_llama;
    dm::browser::BrowserBridge m_browser;
    ModelDownloader m_downloader;
    AiChannel* m_ask;
    AiChannel* m_agent;
    QString m_dir;
    QStringList m_installDirs;                             // install folders of the programs found by the hub (they may carry the engine and models)
    QString m_localModel;
    bool m_useRemote = false;                              // the chosen model is the online one (m_remoteModel)
    QString m_remoteModel;
    QString m_remoteBase;                                  // tests only: replaces the address of the service
    bool m_web = true;                                    // the AI may use the web tools (the owner asked for it; the switch is in the settings and on the page)
    QString m_downloadTitle;
    QString m_downloadFile;                                // where the running download will be
    QVariantMap m_probe;
    HfLink m_probeLink;
    HfInfo m_probeInfo;
    QNetworkReply* m_probeReply = nullptr;
};

} // namespace dm::ai
