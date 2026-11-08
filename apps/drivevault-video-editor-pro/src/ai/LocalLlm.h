#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;
class QCryptographicHash;

namespace dv {

class SettingsManager;

// The local language model that understands free sentences for "Edit by prompt": a Qwen3.5 4B model (GGUF, Apache 2.0) run by llama.cpp's
// llama-server (MIT) as a SEPARATE process, bound to 127.0.0.1 on a random port. Everything stays on this PC.
// - The model file is downloaded only when the user presses a button (size and SHA-256 are checked) or can be placed in the model folder.
// - The server starts on the first request and stops after ten idle minutes (it holds about 3 GB of memory) and when the program closes.
// - The model only returns JSON (a grammar forces its shape); AiController validates it again before anything runs.
class LocalLlm : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed FINAL)               // the setting "use the language model for prompts"
    Q_PROPERTY(bool installed READ installed NOTIFY changed FINAL)                              // the model file is there
    Q_PROPERTY(bool engineAvailable READ engineAvailable CONSTANT FINAL)                        // llama-server.exe is there
    Q_PROPERTY(bool available READ available NOTIFY changed FINAL)                              // enabled && installed && engine
    Q_PROPERTY(QString state READ state NOTIFY changed FINAL)                                   // idle | starting | thinking
    Q_PROPERTY(bool busy READ busy NOTIFY changed FINAL)
    Q_PROPERTY(QString modelName READ modelName CONSTANT FINAL)
    Q_PROPERTY(int modelSizeMb READ modelSizeMb CONSTANT FINAL)
    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadChanged FINAL)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadChanged FINAL)

public:
    explicit LocalLlm(SettingsManager* settings, QObject* parent = nullptr);
    ~LocalLlm() override;

    bool enabled() const;
    void setEnabled(bool on);
    bool installed() const { return !modelPath().isEmpty(); }
    bool engineAvailable() const { return !serverExecutable().isEmpty(); }
    bool available() const { return enabled() && installed() && engineAvailable(); }
    QString state() const { return m_state; }
    bool busy() const { return m_state != QLatin1String("idle"); }
    QString modelName() const;
    int modelSizeMb() const;
    bool downloading() const { return m_reply != nullptr; }
    double downloadProgress() const { return m_progress; }

    // llama-server.exe: DV_LLAMA_DIR, <app>\\ai\\llama, or the suite .tools\\llm\\llama in a development tree.
    static QString serverExecutable();
    // The model file: the model folder, DV_LLM_MODEL, or the suite .tools\\llm in a development tree. Empty when it is not there.
    QString modelPath() const;
    QString modelFolder() const;

    Q_INVOKABLE QString download();                 // from Hugging Face; "" = started, otherwise why not
    Q_INVOKABLE void cancelDownload();
    Q_INVOKABLE QString remove();                   // deletes the model file from the model folder (never one outside it)
    QString downloadFrom(const QString& url, qint64 expectedSize, const QString& expectedSha256);   // tests (file:// works too)

    // Asks the model; the answer arrives in planFinished (content = the JSON text of the model). "" = started, otherwise why not.
    QString requestPlan(const QString& text);
    Q_INVOKABLE void stopServer();
    // Takes a request that is waiting for the server and the server itself down (tests, shutdown).
    void waitForIdle(int timeoutMs = 120000);

signals:
    void changed();
    void downloadChanged();
    void downloadFinished(bool ok, const QString& message);
    void planFinished(bool ok, const QByteArray& content, const QString& message);

private:
    void setState(const QString& state);
    void startServer();
    void pollHealth();
    void sendRequest();
    void failRequest(const QString& message);
    void finishDownload(bool ok, const QString& message);

    SettingsManager* m_settings;
    QString m_state = QStringLiteral("idle");
    QProcess* m_proc = nullptr;
    int m_port = 0;
    bool m_ready = false;
    QString m_pendingText;
    QNetworkAccessManager* m_net = nullptr;
    QNetworkReply* m_request = nullptr;
    QTimer m_idle;
    QTimer m_poll;
    qint64 m_startedAt = 0;

    QNetworkReply* m_reply = nullptr;
    QFile* m_file = nullptr;
    QCryptographicHash* m_hash = nullptr;
    QString m_target;
    qint64 m_expectedSize = 0;
    QString m_expectedSha;
    double m_progress = 0;
};

} // namespace dv
