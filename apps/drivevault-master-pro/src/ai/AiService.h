#pragma once

#include "HttpChat.h"
#include "LlamaServer.h"
#include "LocalAgent.h"
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
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged FINAL)     // [{role, text, streaming, stopped, errorCode, errorDetail, activity: [..]}]
    Q_PROPERTY(QVariantList conversations READ conversations NOTIFY conversationsChanged FINAL)   // [{id, title}]
    Q_PROPERTY(QString currentId READ currentId NOTIFY conversationsChanged FINAL)
    Q_PROPERTY(QString folder READ folder NOTIFY settingsChanged FINAL)               // agent: the working folder
    Q_PROPERTY(QString permission READ permission NOTIFY settingsChanged FINAL)       // agent mode: read | plan | ask | edit (see Permission below)
    Q_PROPERTY(QString effort READ effort NOTIFY settingsChanged FINAL)               // low | medium | high

public:
    struct Message {
        QString role;                      // user | assistant
        QString text;
        QStringList activity;
        QString errorCode, errorDetail;
        bool streaming = false;
        bool stopped = false;
        QVariantMap approval;              // {path, replaces, bytes, preview}: a write waits for the user's answer (not saved)
    };
    struct Conversation {
        QString id, title;
        qint64 updated = 0;
        QList<Message> messages;
    };

    AiChannel(const QString& mode, AiService* service);
    ~AiChannel() override;

    QString mode() const { return m_mode; }
    bool isAgent() const { return m_mode == QLatin1String("agent"); }
    QString notReady() const;
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QVariantList messages() const;
    QVariantList conversations() const;
    QString currentId() const { return m_currentId; }
    QString folder() const { return m_folder; }
    QString permission() const { return m_permission; }
    static QStringList permissions() { return {QStringLiteral("read"), QStringLiteral("plan"), QStringLiteral("ask"), QStringLiteral("edit")}; }
    bool canWrite() const { return m_permission == QLatin1String("ask") || m_permission == QLatin1String("edit"); }
    QString effort() const { return m_effort; }

    Q_INVOKABLE QString send(const QString& text);        // "" = started, otherwise why not (notReady code, busy, empty)
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

private:
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
    QString m_permission = QStringLiteral("ask");
    QList<Conversation> m_conversations;
    QString m_currentId;
    QString m_runId;                                       // the conversation the running request writes into
    bool m_busy = false;
    QString m_status;
    HttpChat* m_http = nullptr;
    LocalAgent* m_localAgent = nullptr;
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
    Q_PROPERTY(QString modelsFolder READ modelsFolder CONSTANT FINAL)
    Q_PROPERTY(bool webEnabled READ webEnabled WRITE setWebEnabled NOTIFY settingsChanged FINAL)   // the AI may search the web (web_search, fetch_page)
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

    Q_INVOKABLE void selectLocalModel(const QString& path);
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
    ModelDownloader m_downloader;
    AiChannel* m_ask;
    AiChannel* m_agent;
    QString m_dir;
    QStringList m_installDirs;                             // install folders of the programs found by the hub (they may carry the engine and models)
    QString m_localModel;
    bool m_web = true;                                     // the AI may use the web tools (the owner asked for it; the switch is in the settings and on the page)
    QString m_downloadTitle;
    QString m_downloadFile;                                // where the running download will be
    QVariantMap m_probe;
    HfLink m_probeLink;
    HfInfo m_probeInfo;
    QNetworkReply* m_probeReply = nullptr;
};

} // namespace dm::ai
