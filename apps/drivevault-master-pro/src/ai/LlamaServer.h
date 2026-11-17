#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QProcess;
class QTimer;

namespace dm::ai {

// The local language model runs in its own process: llama.cpp's llama-server with a GGUF file, listening on 127.0.0.1 only on a random port and asking for a
// random key. It starts when the first question needs it (the Vulkan build uses the graphics card and falls back to the processor), and stops after a quiet
// quarter of an hour, when the program ends, or when this object is destroyed (also when the program crashes: the process sits in a Windows job that kills it).
class LlamaServer : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Starting, Ready, Failed };

    explicit LlamaServer(QNetworkAccessManager* net, QObject* parent = nullptr);
    ~LlamaServer() override;

    // Folders that may hold `llama-server.exe` besides the built-in ones: $DM_LLAMA_DIR, <app>\ai\llama, a development .tools\llm\llama-vulkan, and
    // <install dir>\ai\llama of the installed programs that ship the engine (PDF Pro). Empty when there is none.
    static void setExtraEngineDirs(const QStringList& installDirs);
    static constexpr int kContextTokens = 32768;          // the window of the server (-c): the prompt + the answer of one request; the agent keeps its conversation inside it
    static constexpr int kSmallContextTokens = 8192;     // used when the big window does not fit in memory
    int contextTokens() const { return m_ctx; }          // the window of the running server
    static QString serverExecutable();

    State state() const { return m_state; }
    QString baseUrl() const { return m_baseUrl; }          // http://127.0.0.1:port (the chat endpoint is baseUrl + /v1)
    QString apiKey() const { return m_apiKey; }
    QString modelPath() const { return m_model; }
    bool visionActive() const { return m_vision && m_state == State::Ready; }   // the server runs with the vision file: the model can look at pictures
    QString error() const { return m_error; }              // noEngine | noModel | startFailed (details in detail())
    QString detail() const { return m_detail; }
    bool usesGpu() const { return m_gpu; }
    bool isExternal() const { return m_external; }          // tests: a server that was not started by this object

    void start(const QString& modelPath);                  // asynchronous: stateChanged() reports Starting, then Ready or Failed; another model restarts it
    void stop();
    void touch();                                          // the model was used: restart the quiet-time clock
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
    QString m_baseUrl, m_apiKey, m_model, m_error, m_detail;
    bool m_gpu = true;
    int m_ctx = kContextTokens;
    bool m_vision = false;
    bool m_external = false;
    bool m_probing = false;
    int m_waited = 0;
    QByteArray m_log;
};

} // namespace dm::ai
