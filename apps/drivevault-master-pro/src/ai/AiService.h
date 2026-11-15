#pragma once

#include "CliChat.h"
#include "HttpChat.h"
#include "LocalAgent.h"
#include "LlamaServer.h"
#include "ModelDownloader.h"
#include "ModelStore.h"

#include <QList>
#include <functional>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace dm {
class Hub;
}

namespace dm::ai {

class AiService;

// One of the two AI pages (ask = AI Q&A, agent = Agentic AI): its provider, its list of conversations, the open conversation and the request that is running.
// Providers: local (llama.cpp + a GGUF model), openai (any OpenAI style server: OpenAI, OpenRouter, Ollama, LM Studio ...), anthropic (API key), claude
// (Claude Code) and codex (Codex): the last two through the program the user has installed and signed in to. QML object `Ai.ask` / `Ai.agent`.
class AiChannel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString mode READ mode CONSTANT FINAL)
    Q_PROPERTY(QString provider READ provider NOTIFY settingsChanged FINAL)           // "" | local | openai | anthropic | claude | codex
    Q_PROPERTY(QString notReady READ notReady NOTIFY settingsChanged FINAL)           // "" when a message can be sent, otherwise why not
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(QString status READ status NOTIFY busyChanged FINAL)                   // "" | starting | writing
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged FINAL)     // [{role, text, streaming, stopped, errorCode, errorDetail, activity: [..]}]
    Q_PROPERTY(QVariantList conversations READ conversations NOTIFY conversationsChanged FINAL)   // [{id, title}]
    Q_PROPERTY(QString currentId READ currentId NOTIFY conversationsChanged FINAL)
    Q_PROPERTY(QString folder READ folder NOTIFY settingsChanged FINAL)               // agent: the working folder
    Q_PROPERTY(bool allowEdits READ allowEdits NOTIFY settingsChanged FINAL)          // agent: may edit files in the folder (false: read only)
    Q_PROPERTY(QStringList providers READ providers CONSTANT FINAL)                   // the providers this page can use

public:
    struct Message {
        QString role;                      // user | assistant
        QString text;
        QStringList activity;
        QString errorCode, errorDetail;
        bool streaming = false;
        bool stopped = false;
    };
    struct Conversation {
        QString id, title, sessionId;
        qint64 updated = 0;
        QList<Message> messages;
    };

    AiChannel(const QString& mode, AiService* service);
    ~AiChannel() override;

    QString mode() const { return m_mode; }
    bool isAgent() const { return m_mode == QLatin1String("agent"); }
    QString provider() const { return m_provider; }
    QString notReady() const;
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QVariantList messages() const;
    QVariantList conversations() const;
    QString currentId() const { return m_currentId; }
    QString folder() const { return m_folder; }
    bool allowEdits() const { return m_allowEdits; }
    QStringList providers() const;

    Q_INVOKABLE QString send(const QString& text);        // "" = started, otherwise why not (notReady code, busy, empty)
    Q_INVOKABLE void stop();
    Q_INVOKABLE void newConversation();
    Q_INVOKABLE void select(const QString& id);
    Q_INVOKABLE void remove(const QString& id);
    Q_INVOKABLE void setProvider(const QString& provider);
    Q_INVOKABLE void setFolder(const QString& folder);
    Q_INVOKABLE void setAllowEdits(bool allow);

    void load();                                          // conversations and settings of this page
    void applySettings(const QVariantMap& values);        // used by the service when it reads the settings file
    QVariantMap settingsValues() const;
    void reloadReadiness() { emit settingsChanged(); }

    // Pure, tested: the messages that go to a model (the newest ones that fit `maxChars`; the first is always a user message).
    static QList<QPair<QString, QString>> historyFor(const QList<Message>& messages, int maxChars);
    static QString transcriptFor(const QList<Message>& messages);         // for programs that take one prompt (Codex)

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
    void sendHttp(HttpChat::Request request);
    void sendLocal(const QString& userText);
    void sendCli(const QString& userText);
    void sendLocalAgent(const QString& userText);
    void withLocalServer(const std::function<void()>& go);        // runs `go` when the local model is ready (starts it when needed)
    void appendActivity(const QString& text);
    void appendPiece(const QString& text);
    void finish(bool ok, const QString& code, const QString& detail);
    QString systemPrompt() const;
    void saveConversations();
    void cancelRunning();

    QString m_mode;
    AiService* m_service;
    QString m_provider;
    QString m_folder;
    bool m_allowEdits = false;
    QList<Conversation> m_conversations;
    QString m_currentId;
    QString m_runId;                                       // the conversation the running request writes into
    bool m_busy = false;
    QString m_status;
    HttpChat* m_http = nullptr;
    CliChat* m_cli = nullptr;
    LocalAgent* m_localAgent = nullptr;
    QMetaObject::Connection m_waitLocal;
    QTimer* m_viewTimer = nullptr;
    int m_generation = 0;                                  // bumps on stop / switch: a late callback of an old request does nothing
};

// The AI of Master Pro: both pages, the settings of the providers, the models that are installed and the downloads. QML singleton `Ai`.
class AiService : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* ask READ ask CONSTANT FINAL)
    Q_PROPERTY(QObject* agent READ agent CONSTANT FINAL)
    Q_PROPERTY(QVariantMap cli READ cli NOTIFY cliChanged FINAL)                       // {claude: {found, path, version}, codex: {...}}
    Q_PROPERTY(bool engineFound READ engineFound NOTIFY modelsChanged FINAL)
    Q_PROPERTY(QVariantList catalog READ catalog NOTIFY modelsChanged FINAL)           // [{id, title, fileName, sizeText, level, installed, path}]
    Q_PROPERTY(QVariantList installedModels READ installedModels NOTIFY modelsChanged FINAL)    // [{name, path, sizeText}]
    Q_PROPERTY(QString localModel READ localModel NOTIFY settingsChanged FINAL)        // path of the chosen model
    Q_PROPERTY(QString modelsFolder READ modelsFolder CONSTANT FINAL)
    Q_PROPERTY(QVariantMap openai READ openai NOTIFY settingsChanged FINAL)            // {baseUrl, model, keySet}
    Q_PROPERTY(QVariantMap anthropic READ anthropic NOTIFY settingsChanged FINAL)      // {model, keySet}
    Q_PROPERTY(bool webEnabled READ webEnabled WRITE setWebEnabled NOTIFY settingsChanged FINAL)   // the local AI may search the web (web_search, fetch_page)
    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadChanged FINAL)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadChanged FINAL)
    Q_PROPERTY(QString downloadCode READ downloadCode NOTIFY downloadChanged FINAL)
    Q_PROPERTY(QString downloadMessage READ downloadMessage NOTIFY downloadChanged FINAL)
    Q_PROPERTY(QString downloadTitle READ downloadTitle NOTIFY downloadChanged FINAL)
    Q_PROPERTY(QVariantMap probe READ probe NOTIFY probeChanged FINAL)                 // {state: idle|checking|ready|failed, error, repo, fileName, size, sizeText}

public:
    // probePrograms false: no program is started to ask for its version (diagnostic runs and tests)
    explicit AiService(Hub* hub, QObject* parent = nullptr, bool probePrograms = true);
    ~AiService() override;

    QObject* ask() { return m_ask; }
    QObject* agent() { return m_agent; }
    AiChannel* askChannel() { return m_ask; }
    AiChannel* agentChannel() { return m_agent; }
    QVariantMap cli() const { return m_cli; }
    bool engineFound() const { return !LlamaServer::serverExecutable().isEmpty(); }
    QVariantList catalog() const;
    QVariantList installedModels() const;
    QString localModel() const { return m_localModel; }
    bool webEnabled() const { return m_web; }
    void setWebEnabled(bool on);
    QString modelsFolder() const;
    QVariantMap openai() const;
    QVariantMap anthropic() const;
    bool downloading() const { return m_downloader.busy(); }
    double downloadProgress() const { return m_downloader.progress(); }
    QString downloadCode() const { return m_downloader.code(); }
    QString downloadMessage() const { return m_downloader.message(); }
    QString downloadTitle() const { return m_downloadTitle; }
    QVariantMap probe() const { return m_probe; }

    Q_INVOKABLE void detectPrograms();                                  // looks for claude / codex and asks them for their version
    Q_INVOKABLE void setOpenAi(const QString& baseUrl, const QString& model, const QString& key);     // an empty key keeps the stored one
    Q_INVOKABLE void setAnthropic(const QString& model, const QString& key);
    Q_INVOKABLE void clearKey(const QString& provider);                 // openai | anthropic
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
    QString openAiKey() const;
    QString anthropicKey() const;
    QString openAiModel() const { return m_openaiModel; }
    QString openAiBaseUrl() const { return m_openaiBase; }
    QString anthropicModel() const { return m_anthropicModel; }
    QString cliPath(const QString& tool) const { return m_cli.value(tool).toMap().value(QStringLiteral("path")).toString(); }
    QString suiteSummary() const;                                       // the programs of the suite, for the system prompt
    void saveSettings();

    static QString anthropicBase() { return QStringLiteral("https://api.anthropic.com"); }

signals:
    void cliChanged();
    void modelsChanged();
    void settingsChanged();
    void downloadChanged();
    void probeChanged();

private:
    void loadSettings();
    QStringList modelFolders() const;
    void setProbe(const QVariantMap& probe);

    Hub* m_hub;
    bool m_probePrograms;
    QNetworkAccessManager* m_net;
    LlamaServer m_llama;
    ModelDownloader m_downloader;
    AiChannel* m_ask;
    AiChannel* m_agent;
    QString m_dir;
    QStringList m_installDirs;                             // install folders of the programs found by the hub (they may carry the engine and models)
    QVariantMap m_cli;
    QString m_localModel;
    bool m_web = true;                                     // the local AI may use the web tools (the owner asked for it; the switch is in the settings and on the page)
    QString m_openaiBase = QStringLiteral("https://api.openai.com/v1");
    QString m_openaiModel;
    QString m_openaiKeyEnc;
    QString m_anthropicModel = QStringLiteral("claude-sonnet-5-5");
    QString m_anthropicKeyEnc;
    QString m_downloadTitle;
    QString m_downloadFile;                                // where the running download will be
    QVariantMap m_probe;
    HfLink m_probeLink;
    HfInfo m_probeInfo;
    QNetworkReply* m_probeReply = nullptr;
};

} // namespace dm::ai
