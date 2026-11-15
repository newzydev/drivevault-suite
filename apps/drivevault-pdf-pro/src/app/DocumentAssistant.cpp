#include "DocumentAssistant.h"

#include "DocumentController.h"
#include "pdfengine/PdfDocument.h"

#include <QNetworkAccessManager>
#include <QPointer>
#include <QThreadPool>
#include <QVariantMap>

#include <algorithm>
#include <functional>
#include <memory>

namespace dp {

namespace {
constexpr int kSinglePassChars = 6000;     // a document up to this size is summarised in one go
constexpr int kGroupChars = 4500;          // the size of a part of a longer document
constexpr int kMaxGroups = 24;             // the most parts one summary reads (the rest is said to be left out)
}

DocumentAssistant::DocumentAssistant(DocumentController* controller, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_net(new QNetworkAccessManager(this))
    , m_server(m_net, this)
    , m_downloader(this)
{
    assistant::LlamaServer::setDataDir(controller->dataDir());
    m_viewTimer.setSingleShot(true);
    m_viewTimer.setInterval(60);
    connect(&m_viewTimer, &QTimer::timeout, this, &DocumentAssistant::refreshView);
    connect(&m_server, &assistant::LlamaServer::stateChanged, this, &DocumentAssistant::stateChanged);
    connect(&m_downloader, &assistant::ModelDownloader::finished, this, &DocumentAssistant::stateChanged);
    connect(controller, &DocumentController::documentChanged, this, &DocumentAssistant::abandonDocument);
}

DocumentAssistant::~DocumentAssistant() = default;

QString DocumentAssistant::state() const
{
    if (m_busy)
        return QStringLiteral("busy");
    if (m_server.state() == assistant::LlamaServer::State::Starting)
        return QStringLiteral("starting");
    if (m_server.state() == assistant::LlamaServer::State::Ready)
        return QStringLiteral("idle");
    if (!engineInstalled())
        return QStringLiteral("needsEngine");
    if (!modelInstalled())
        return QStringLiteral("needsModel");
    return QStringLiteral("idle");
}

bool DocumentAssistant::uiThai() const { return m_controller->language() == QLatin1String("th"); }

void DocumentAssistant::useServerForTests(const QString& baseUrl, const QString& apiKey)
{
    m_server.useExternalServer(baseUrl, apiKey);
}

// ---- the conversation as QML sees it ----------------------------------------------------------------------------------------------------

void DocumentAssistant::refreshView()
{
    QVariantList view;
    for (const Message& m : std::as_const(m_messages)) {
        QVariantList pages;
        for (const int p : m.pages)
            pages << p;
        view << QVariantMap{{"role", m.role}, {"text", m.text}, {"pages", pages}, {"streaming", m.streaming}, {"error", m.error}};
    }
    m_view = view;
    emit messagesChanged();
}

void DocumentAssistant::scheduleView()
{
    if (!m_viewTimer.isActive())
        m_viewTimer.start();
}

void DocumentAssistant::addMessage(const QString& role, const QString& text, bool streaming, bool error)
{
    Message m;
    m.role = role;
    m.text = text;
    m.streaming = streaming;
    m.error = error;
    m_messages.append(m);
    refreshView();
}

void DocumentAssistant::setStatus(const QString& status, int part, int parts, const QString& detail)
{
    m_status = status;
    m_part = part;
    m_parts = parts;
    m_detail = detail;
    emit stateChanged();
}

void DocumentAssistant::setBusy(bool busy)
{
    m_busy = busy;
    if (!busy)
        m_job = Job::None;
    emit stateChanged();
}

void DocumentAssistant::clear()
{
    stop();
    m_messages.clear();
    m_history.clear();
    m_status.clear();
    refreshView();
    emit stateChanged();
}

void DocumentAssistant::abandonDocument()
{
    if (m_controller->docId() == m_textDocId && m_textDocId >= 0)
        return;                                              // the same document (e.g. the view was turned)
    stop();
    m_messages.clear();
    m_history.clear();
    m_pages.clear();
    m_chunks.clear();
    m_textDocId = -1;
    m_status.clear();
    refreshView();
    emit stateChanged();
}

void DocumentAssistant::prepare()
{
    if (m_busy || !engineInstalled() || !modelInstalled())
        return;
    if (m_server.state() == assistant::LlamaServer::State::Ready || m_server.state() == assistant::LlamaServer::State::Starting)
        return;
    setStatus(QStringLiteral("starting"));
    m_server.start();
}

// ---- what a job needs first ---------------------------------------------------------------------------------------------------------------

void DocumentAssistant::failJob(const QString& status, const QString& detail)
{
    ++m_generation;
    if (m_stream)
        m_stream->disconnect(this);
    if (!m_messages.isEmpty() && m_messages.last().role == QLatin1String("assistant") && m_messages.last().streaming) {
        if (m_messages.last().text.trimmed().isEmpty())
            m_messages.removeLast();
        else
            m_messages.last().streaming = false;
        refreshView();
    }
    setBusy(false);
    setStatus(status, 0, 0, detail);
    if (status == QLatin1String("noText"))
        emit ocrSuggested();
}

void DocumentAssistant::withText(std::function<void()> next)
{
    const int id = m_controller->docId();
    if (!m_controller->isOpen() || id < 0) {
        failJob(QStringLiteral("noDocument"));
        return;
    }
    if (m_textDocId == id && !m_pages.isEmpty()) {
        next();
        return;
    }
    setStatus(QStringLiteral("reading"));
    const auto doc = m_controller->documentFor(id);
    if (!doc) {
        failJob(QStringLiteral("noDocument"));
        return;
    }
    const int generation = m_generation;
    QPointer<DocumentAssistant> self(this);
    QThreadPool::globalInstance()->start([self, doc, id, generation, next] {
        QStringList pages;
        for (int i = 0; i < doc->pageCount(); ++i)
            pages << doc->pageText(i);
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, id, generation, pages, next] {
            if (!self || generation != self->m_generation)
                return;
            qsizetype total = 0;
            for (const QString& p : pages)
                total += p.trimmed().size();
            if (total < 20) {
                self->failJob(QStringLiteral("noText"));
                return;
            }
            self->m_textDocId = id;
            self->m_pages = pages;
            self->m_chunks = assistant::chunkPages(pages);
            next();
        }, Qt::QueuedConnection);
    });
}

void DocumentAssistant::withServer(std::function<void()> next)
{
    using State = assistant::LlamaServer::State;
    if (m_server.state() == State::Ready) {
        m_server.touch();
        next();
        return;
    }
    setStatus(QStringLiteral("starting"));
    const int generation = m_generation;
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(&m_server, &assistant::LlamaServer::stateChanged, this, [this, connection, generation, next] {
        if (generation != m_generation) {
            disconnect(*connection);
            return;
        }
        if (m_server.state() == State::Ready) {
            disconnect(*connection);
            next();
        } else if (m_server.state() == State::Failed) {
            disconnect(*connection);
            failJob(m_server.error().isEmpty() ? QStringLiteral("startFailed") : m_server.error(), m_server.detail());
        }
    });
    m_server.start();
}

// ---- asking ------------------------------------------------------------------------------------------------------------------------------------

void DocumentAssistant::ask(const QString& questionIn)
{
    const QString question = questionIn.trimmed();
    if (question.isEmpty() || m_busy)
        return;
    m_cancelled = false;
    m_job = Job::Ask;
    setBusy(true);
    setStatus({});
    addMessage(QStringLiteral("user"), question);
    addMessage(QStringLiteral("assistant"), QString(), true);
    withText([this, question] { withServer([this, question] { runAsk(question); }); });
}

void DocumentAssistant::runAsk(const QString& question)
{
    bool letters = false;
    for (const QChar c : question)
        letters = letters || c.isLetter();
    const bool thai = letters ? assistant::isThai(question) : uiThai();
    const QList<int> chosen = assistant::bestChunks(m_chunks, question);
    const QString excerpts = assistant::formatExcerpts(m_chunks, chosen, thai);
    const QString user = (thai ? QStringLiteral("ส่วนของเอกสารที่เกี่ยวข้อง:\n%1\n\nคำถาม: %2") : QStringLiteral("Document excerpts:\n%1\n\nQuestion: %2")).arg(excerpts, question);
    streamAnswer(assistant::chatMessages(assistant::answerSystemPrompt(thai), m_history, user), 700, 0.2, question);
}

void DocumentAssistant::streamAnswer(const QJsonArray& messages, int maxTokens, double temperature, const QString& question)
{
    if (!m_stream)
        m_stream = new assistant::ChatStream(m_net, this);
    m_stream->disconnect(this);
    setStatus(QStringLiteral("waiting"));
    auto raw = std::make_shared<QString>();
    const int generation = m_generation;
    connect(m_stream, &assistant::ChatStream::piece, this, [this, raw, generation](const QString& text) {
        if (generation != m_generation || m_messages.isEmpty())
            return;
        *raw += text;
        m_messages.last().text = assistant::withoutReasoning(*raw).trimmed();
        if (m_status != QLatin1String("writing") && !m_messages.last().text.isEmpty())
            setStatus(QStringLiteral("writing"));
        scheduleView();
    });
    connect(m_stream, &assistant::ChatStream::finished, this, [this, generation, question](bool ok, const QString& error) {
        if (generation != m_generation)
            return;
        const bool cancelled = error == QLatin1String("cancelled");
        if (!ok && !cancelled) {
            failJob(QStringLiteral("failed"), error);
            return;
        }
        if (!m_messages.isEmpty()) {
            Message& m = m_messages.last();
            m.streaming = false;
            if (!m.text.trimmed().isEmpty())
                m.pages = assistant::citedPages(m.text, m_controller->pageCount());
            if (m_job == Job::Summary && !m_summaryNote.isEmpty() && !cancelled)
                m.text += QStringLiteral("\n\n") + m_summaryNote;
            if (!m.text.trimmed().isEmpty() && !cancelled) {
                m_history.append({QStringLiteral("user"), question.isEmpty() ? (m_summaryThai ? QStringLiteral("สรุปเอกสาร") : QStringLiteral("Summarise the document")) : question});
                m_history.append({QStringLiteral("assistant"), m.text});
            }
            if (m.text.trimmed().isEmpty())
                m_messages.removeLast();
        }
        refreshView();
        setBusy(false);
        setStatus({});
        m_server.touch();
    });
    m_stream->start(m_server.baseUrl(), m_server.apiKey(), messages, maxTokens, temperature, true);
}

void DocumentAssistant::stop()
{
    if (!m_busy && !m_textBusy) {
        return;
    }
    m_cancelled = true;
    if (m_stream && m_stream->running()) {
        m_stream->cancel();                       // its finished() handler keeps the text written so far
        return;
    }
    ++m_generation;                               // waiting for the text or the model: just give up
    failJob(QString());
}

// ---- summarising -----------------------------------------------------------------------------------------------------------------------------

void DocumentAssistant::summarize()
{
    if (m_busy)
        return;
    m_cancelled = false;
    m_job = Job::Summary;
    m_summaryNote.clear();
    setBusy(true);
    setStatus({});
    addMessage(QStringLiteral("user"), QStringLiteral("@summary"));
    addMessage(QStringLiteral("assistant"), QString(), true);
    withText([this] { withServer([this] { runSummary(); }); });
}

void DocumentAssistant::runSummary()
{
    m_summaryThai = uiThai();
    qsizetype total = 0;
    for (const QString& p : std::as_const(m_pages))
        total += p.trimmed().size();
    if (total <= kSinglePassChars) {
        QString all;
        for (const QString& p : std::as_const(m_pages))
            if (!p.trimmed().isEmpty())
                all += (all.isEmpty() ? QString() : QStringLiteral("\n\n")) + p.trimmed();
        startFinalSummary(all, false);
        return;
    }
    // a long document: parts of about kGroupChars characters (whole pages, a long page in pieces), summarised one after the other
    m_groups.clear();
    QString current;
    int pagesRead = 0;
    bool truncated = false;
    for (int page = 0; page < m_pages.size() && !truncated; ++page) {
        const QString text = m_pages.at(page).trimmed();
        if (text.isEmpty()) {
            ++pagesRead;
            continue;
        }
        const QList<assistant::Chunk> pieces = assistant::chunkPages({text}, kGroupChars);
        for (const assistant::Chunk& piece : pieces) {
            if (!current.isEmpty() && current.size() + piece.text.size() + 2 > kGroupChars) {
                m_groups << current;
                current.clear();
                if (m_groups.size() >= kMaxGroups) {
                    truncated = true;
                    break;
                }
            }
            current += (current.isEmpty() ? QString() : QStringLiteral("\n\n")) + piece.text;
        }
        if (!truncated)
            ++pagesRead;
    }
    if (!current.isEmpty() && !truncated)
        m_groups << current;
    m_summarisedPages = pagesRead;
    if (truncated) {
        m_summaryNote = m_summaryThai ? QStringLiteral("(สรุปจากหน้า 1–%1 เท่านั้น เอกสารยาวเกินกว่าจะอ่านทั้งหมดในครั้งเดียว)").arg(pagesRead)
                                      : QStringLiteral("(Summary of pages 1–%1 only: the document is too long to read in one go.)").arg(pagesRead);
    }
    m_partials.clear();
    m_nextGroup = 0;
    m_summaryLevel = 0;
    summaryStep();
}

void DocumentAssistant::summaryStep()
{
    if (m_nextGroup >= m_groups.size()) {
        const QString joined = m_partials.join(QStringLiteral("\n\n"));
        if (joined.size() > kSinglePassChars && m_summaryLevel < 2 && m_partials.size() > 1) {
            // the summaries of the parts are still long: summarise them in groups once more
            m_groups.clear();
            QString current;
            for (const QString& s : std::as_const(m_partials)) {
                if (!current.isEmpty() && current.size() + s.size() + 2 > kGroupChars) {
                    m_groups << current;
                    current.clear();
                }
                current += (current.isEmpty() ? QString() : QStringLiteral("\n\n")) + s;
            }
            if (!current.isEmpty())
                m_groups << current;
            m_partials.clear();
            m_nextGroup = 0;
            ++m_summaryLevel;
            summaryStep();
            return;
        }
        startFinalSummary(joined, true);
        return;
    }
    if (!m_stream)
        m_stream = new assistant::ChatStream(m_net, this);
    m_stream->disconnect(this);
    setStatus(QStringLiteral("part"), m_nextGroup + 1, int(m_groups.size()));
    const int generation = m_generation;
    connect(m_stream, &assistant::ChatStream::piece, this, [this, generation](const QString& text) {
        if (generation == m_generation)
            m_partials.append(assistant::withoutReasoning(text).trimmed());     // a request without streaming delivers the whole text at once
    });
    connect(m_stream, &assistant::ChatStream::finished, this, [this, generation](bool ok, const QString& error) {
        if (generation != m_generation)
            return;
        if (!ok) {
            if (error == QLatin1String("cancelled")) {
                if (!m_messages.isEmpty() && m_messages.last().text.isEmpty())
                    m_messages.removeLast();
                refreshView();
                setBusy(false);
                setStatus({});
            } else {
                failJob(QStringLiteral("failed"), error);
            }
            return;
        }
        ++m_nextGroup;
        summaryStep();
    });
    m_stream->start(m_server.baseUrl(), m_server.apiKey(),
                    assistant::chatMessages(assistant::summarySystemPrompt(m_summaryThai, true), {}, m_groups.at(m_nextGroup)), 350, 0.2, false);
}

void DocumentAssistant::startFinalSummary(const QString& text, bool joined)
{
    const QString system = joined ? assistant::joinSummariesSystemPrompt(m_summaryThai) : assistant::summarySystemPrompt(m_summaryThai, false);
    streamAnswer(assistant::chatMessages(system, {}, text), 900, 0.2, QString());
}

} // namespace dp
