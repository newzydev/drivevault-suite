#pragma once

#include "AssistantPrompts.h"
#include "ChatStream.h"
#include "DocumentSearch.h"
#include "LlamaServer.h"
#include "ModelDownloader.h"

#include <QDir>
#include <functional>
#include <QList>
#include <QObject>
#include <QPair>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

class QNetworkAccessManager;

namespace dp {

class DocumentController;

// The AI assistant of the open document: answers questions about it and summarises it, with a language model that runs on this computer (llama.cpp +
// Qwen3.5-4B, see LlamaServer). Nothing leaves the computer. The assistant only reads: it never changes the document. Questions are answered from the
// pieces of the text that match them best (BM25), with the page numbers; a summary reads the whole text (long ones part by part). QML singleton "Assistant".
class DocumentAssistant : public QObject {
    Q_OBJECT
    // needsEngine | needsModel | idle | starting | busy
    Q_PROPERTY(QString state READ state NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged FINAL)
    // "" | starting | reading | waiting | writing | part (statusPart of statusParts) | noDocument | noText | noEngine | noModel | startFailed | failed
    Q_PROPERTY(QString status READ status NOTIFY stateChanged FINAL)
    Q_PROPERTY(int statusPart READ statusPart NOTIFY stateChanged FINAL)
    Q_PROPERTY(int statusParts READ statusParts NOTIFY stateChanged FINAL)
    Q_PROPERTY(QString detail READ detail NOTIFY stateChanged FINAL)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged FINAL)    // [{role: user|assistant, text, pages: [n], streaming, error}]
    Q_PROPERTY(bool modelInstalled READ modelInstalled NOTIFY stateChanged FINAL)
    Q_PROPERTY(bool engineInstalled READ engineInstalled CONSTANT FINAL)
    Q_PROPERTY(bool usesGpu READ usesGpu NOTIFY stateChanged FINAL)
    Q_PROPERTY(QObject* downloader READ downloader CONSTANT FINAL)
    Q_PROPERTY(QString modelFolder READ modelFolder CONSTANT FINAL)

public:
    explicit DocumentAssistant(DocumentController* controller, QObject* parent = nullptr);
    ~DocumentAssistant() override;

    QString state() const;
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    int statusPart() const { return m_part; }
    int statusParts() const { return m_parts; }
    QString detail() const { return m_detail; }
    QVariantList messages() const { return m_view; }
    bool modelInstalled() const { return !assistant::LlamaServer::modelPath().isEmpty(); }
    bool engineInstalled() const { return !assistant::LlamaServer::serverExecutable().isEmpty(); }
    bool usesGpu() const { return m_server.usesGpu(); }
    QObject* downloader() { return &m_downloader; }
    QString modelFolder() const { return QDir::toNativeSeparators(assistant::LlamaServer::downloadFolder()); }

    Q_INVOKABLE void ask(const QString& question);
    Q_INVOKABLE void summarize();
    Q_INVOKABLE void stop();                 // cancels what is running (the text written so far stays)
    Q_INVOKABLE void clear();                // forgets the conversation
    Q_INVOKABLE void prepare();              // starts the model in the background (the first question is then answered sooner)

    // Tests: use a server that is already running (e.g. a fake one) instead of starting llama-server.
    void useServerForTests(const QString& baseUrl, const QString& apiKey);
    assistant::LlamaServer* serverForTests() { return &m_server; }

signals:
    void stateChanged();
    void messagesChanged();
    void ocrSuggested();                      // the document has no text: the panel offers the OCR tool

private:
    struct Message {
        QString role;
        QString text;
        QList<int> pages;
        bool streaming = false;
        bool error = false;
    };
    enum class Job { None, Ask, Summary };

    void addMessage(const QString& role, const QString& text, bool streaming = false, bool error = false);
    void finishAssistantMessage(bool error);
    void refreshView();
    void scheduleView();
    void setStatus(const QString& status, int part = 0, int parts = 0, const QString& detail = {});
    void setBusy(bool busy);
    void abandonDocument();

    // the text of the open document (extracted on a worker thread the first time)
    void withText(std::function<void()> next);
    // the model server (started when needed)
    void withServer(std::function<void()> next);
    void failJob(const QString& status, const QString& detail = {});

    void runAsk(const QString& question);
    void runSummary();
    void summaryStep();
    void startFinalSummary(const QString& text, bool joined);
    void streamAnswer(const QJsonArray& messages, int maxTokens, double temperature, const QString& question);
    bool uiThai() const;

    DocumentController* m_controller;
    QNetworkAccessManager* m_net;
    assistant::LlamaServer m_server;
    assistant::ModelDownloader m_downloader;
    assistant::ChatStream* m_stream = nullptr;
    QTimer m_viewTimer;

    QList<Message> m_messages;
    QVariantList m_view;
    QList<QPair<QString, QString>> m_history;
    bool m_busy = false;
    Job m_job = Job::None;
    bool m_cancelled = false;
    int m_generation = 0;                 // bumps on stop / document change: late callbacks of an old job do nothing
    QString m_status;
    QString m_detail;
    int m_part = 0, m_parts = 0;

    // text cache
    int m_textDocId = -1;
    QStringList m_pages;
    QList<assistant::Chunk> m_chunks;
    bool m_textBusy = false;

    // summary plan
    QStringList m_groups;
    QStringList m_partials;
    int m_nextGroup = 0;
    int m_summaryLevel = 0;
    bool m_summaryThai = true;
    int m_summarisedPages = 0;
    QString m_summaryNote;
};

} // namespace dp
