#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QProcess;
class QTimer;

namespace dp::assistant {

// The language model of the AI assistant runs in its own process: llama.cpp's llama-server with a GGUF model (Qwen3.5-4B), listening on 127.0.0.1 only on a
// random port and asking for a random key. It starts when the user asks the assistant for the first time (the Vulkan build uses the graphics card, and
// falls back to the processor), and stops by itself after a quiet quarter of an hour, when the program ends, or when this object is destroyed (also when
// the program crashes: the process sits in a Windows job that kills it).
class LlamaServer : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Starting, Ready, Failed };

    explicit LlamaServer(QNetworkAccessManager* net, QObject* parent = nullptr);
    ~LlamaServer() override;

    // Where the files are looked for: $DP_LLAMA_DIR, <app>\ai\llama, the development .tools\llm\llama-vulkan (and the model: $DP_LLM_MODEL, <app>\ai\models,
    // <data>\models, .tools\llm). Empty when there is none.
    static QString serverExecutable();
    static QString modelFileName();
    static QString modelPath();
    static void setDataDir(const QString& dir);                 // the folder of settings.ini: models may live in <dir>\models
    static QString downloadFolder();                             // where "Download" puts the model

    State state() const { return m_state; }
    QString baseUrl() const { return m_baseUrl; }
    QString apiKey() const { return m_apiKey; }
    // "" | noEngine | noModel | startFailed: the reason for State::Failed (details in `detail`)
    QString error() const { return m_error; }
    QString detail() const { return m_detail; }
    bool usesGpu() const { return m_gpu; }

    void start();               // asynchronous: stateChanged() reports Starting, then Ready or Failed
    void stop();
    void touch();               // the assistant used the server: restart the quiet-time clock
    // Tests: pretend a server is already listening there.
    void useExternalServer(const QString& baseUrl, const QString& apiKey);

signals:
    void stateChanged();

private:
    void launch(bool gpu);
    void pollHealth();
    void fail(const QString& error, const QString& detail);
    void setState(State state);

    QNetworkAccessManager* m_net;
    QProcess* m_process = nullptr;
    QTimer* m_poll = nullptr;
    QTimer* m_idle = nullptr;
    State m_state = State::Stopped;
    QString m_baseUrl, m_apiKey, m_error, m_detail;
    bool m_gpu = true;
    bool m_external = false;
    bool m_probing = false;
    int m_waited = 0;                    // ms since the process was started
    QByteArray m_log;
};

} // namespace dp::assistant
