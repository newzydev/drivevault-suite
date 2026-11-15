// The AI assistant: retrieval and prompts (pure), the streaming reply parser, the chat client and the whole assistant against a fake llama-server on this
// computer (so the flow is tested without a 2.7 GB model), the model downloader against a local HTTP server, and - only with DP_TEST_REAL_LLM=1 and the
// model installed - the real model answering a question about a generated document.

#include "AssistantPrompts.h"
#include "ChatStream.h"
#include "DocumentAssistant.h"
#include "DocumentController.h"
#include "DocumentSearch.h"
#include "LlamaServer.h"
#include "ModelDownloader.h"
#include "pdfengine/PdfiumRuntime.h"

#include <QCryptographicHash>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPainter>
#include <QPdfWriter>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <functional>

using namespace dp;
using namespace dp::assistant;

namespace {

// ---- a fake llama-server ---------------------------------------------------------------------------------------------------------------
class FakeLlama : public QObject {
public:
    explicit FakeLlama(QObject* parent = nullptr) : QObject(parent)
    {
        listenOk = m_server.listen(QHostAddress::LocalHost, 0);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = m_server.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, this, [this, s] { handle(s); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }
    bool listenOk = false;
    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }
    QList<QJsonObject> requests;
    QStringList authorizations;
    std::function<QString(const QJsonObject&)> reply = [](const QJsonObject&) { return QStringLiteral("ok"); };
    int pieceDelayMs = 0;               // between the pieces of a streamed reply
    int failStatus = 0;                 // answer with this HTTP status and an error body

private:
    void handle(QTcpSocket* s)
    {
        m_buf[s] += s->readAll();
        const int end = m_buf[s].indexOf("\r\n\r\n");
        if (end < 0)
            return;
        const QByteArray head = m_buf[s].left(end);
        int length = 0;
        QString authorization;
        for (const QByteArray& line : head.split('\n')) {
            if (line.toLower().startsWith("content-length:"))
                length = line.mid(15).trimmed().toInt();
            if (line.toLower().startsWith("authorization:"))
                authorization = QString::fromLatin1(line.mid(14).trimmed());
        }
        if (m_buf[s].size() < end + 4 + length)
            return;
        const QByteArray body = m_buf[s].mid(end + 4, length);
        m_buf.remove(s);
        const QByteArray requestLine = head.left(head.indexOf('\r'));
        if (requestLine.contains("/health")) {
            s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 15\r\nConnection: close\r\n\r\n{\"status\":\"ok\"}");
            s->disconnectFromHost();
            return;
        }
        const QJsonObject json = QJsonDocument::fromJson(body).object();
        requests << json;
        authorizations << authorization;
        if (failStatus > 0) {
            const QByteArray err = R"({"error":{"message":"the model is broken"}})";
            s->write("HTTP/1.1 " + QByteArray::number(failStatus) + " Error\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(err.size()) +
                     "\r\nConnection: close\r\n\r\n" + err);
            s->disconnectFromHost();
            return;
        }
        const QString text = reply(json);
        if (!json.value("stream").toBool()) {
            const QByteArray out = QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"message", QJsonObject{{"content", text}}}}}}}).toJson(QJsonDocument::Compact);
            s->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(out.size()) + "\r\nConnection: close\r\n\r\n" + out);
            s->disconnectFromHost();
            return;
        }
        s->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nConnection: close\r\n\r\n");
        QStringList pieces;
        for (qsizetype i = 0; i < text.size(); i += 6)
            pieces << text.mid(i, 6);
        sendPieces(QPointer<QTcpSocket>(s), pieces, 0);
    }
    void sendPieces(QPointer<QTcpSocket> s, const QStringList& pieces, int index)
    {
        if (!s)
            return;
        if (index >= pieces.size()) {
            s->write("data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n");
            s->disconnectFromHost();
            return;
        }
        const QByteArray event = "data: " + QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"delta", QJsonObject{{"content", pieces.at(index)}}}}}}})
                                                .toJson(QJsonDocument::Compact) + "\n\n";
        s->write(event);
        s->flush();
        if (pieceDelayMs > 0)
            QTimer::singleShot(pieceDelayMs, this, [this, s, pieces, index] { sendPieces(s, pieces, index + 1); });
        else
            sendPieces(s, pieces, index + 1);
    }
    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_buf;
};

// A PDF with Thai text on every page (drawn with QPdfWriter, so it has a text layer) - or, with scan = true, only a picture of the page.
QString writeThaiPdf(const QTemporaryDir& dir, const QString& name, const QStringList& pages, bool scan = false)
{
    const QString path = dir.filePath(name);
    const int fontId = QFontDatabase::addApplicationFont(DP_TEST_FONT_THAI);
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins({}, QPageLayout::Point);
    writer.setResolution(100);
    QPainter painter(&writer);
    QFont font(QFontDatabase::applicationFontFamilies(fontId).value(0));
    font.setPixelSize(22);
    for (int i = 0; i < pages.size(); ++i) {
        if (i > 0)
            writer.newPage();
        if (scan) {
            QImage image(827, 1169, QImage::Format_RGB32);
            image.fill(Qt::white);
            QPainter ink(&image);
            ink.setFont(font);
            ink.drawText(60, 100, pages.at(i));
            ink.end();
            painter.drawImage(0, 0, image);
            continue;
        }
        painter.setFont(font);
        int y = 80;
        for (const QString& line : pages.at(i).split(QLatin1Char('\n'))) {
            painter.drawText(60, y, line);
            y += 40;
        }
    }
    painter.end();
    return path;
}

} // namespace

class TstAssistant : public QObject {
    Q_OBJECT

    static QList<Chunk> chunksOf(const QStringList& pages) { return chunkPages(pages); }

private slots:
    void cleanupTestCase() { dp::pdf::shutdownPdfium(); }

    // ---- retrieval --------------------------------------------------------------------------------------------------------------------------
    void chunksNeverCrossAPageAndKeepTheirSize()
    {
        QStringList pages;
        pages << QString("line one\nline two\n\nline three") << QString(2500, QLatin1Char('x')) << QString();
        const QList<Chunk> chunks = chunkPages(pages, 300);
        QVERIFY(chunks.size() >= 1 + 9);
        for (const Chunk& c : chunks) {
            QVERIFY(c.text.size() <= 300);
            QVERIFY(!c.text.isEmpty());
        }
        QCOMPARE(chunks.first().page, 0);
        QCOMPARE(chunks.first().text, QString("line one\nline two\nline three"));
        QCOMPARE(chunks.last().page, 1);                                  // the empty third page makes none
    }
    void thaiQuestionsFindTheRightPage()
    {
        const QStringList pages = {QString::fromUtf8("ประวัติความเป็นมาของบริษัท ก่อตั้งเมื่อปี 2540 โดยกลุ่มนักลงทุนในจังหวัดสงขลา"),
                                   QString::fromUtf8("งบประมาณประจำปี 2569 รวมทั้งสิ้น 1,250,000 บาท แบ่งเป็นค่าจ้างและค่าวัสดุ"),
                                   QString::fromUtf8("สรุปผลการดำเนินงานและข้อเสนอแนะสำหรับปีถัดไป คณะกรรมการเห็นชอบให้ขยายสาขา"),
                                   QString::fromUtf8("ภาคผนวก รายชื่อผู้เข้าร่วมประชุมและลายมือชื่อ")};
        const QList<Chunk> chunks = chunksOf(pages);
        const QList<int> budget = bestChunks(chunks, QString::fromUtf8("งบประมาณรวมทั้งสิ้นเท่าไหร่"));
        const auto pagesOf = [&](const QList<int>& chosen) { QList<int> p; for (const int i : chosen) p << chunks.at(i).page; return p; };
        QVERIFY(pagesOf(budget).contains(1));
        QVERIFY(!pagesOf(budget).contains(3));                              // the list of attendees has nothing to do with the budget
        const QList<int> founded = bestChunks(chunks, QString::fromUtf8("บริษัทก่อตั้งเมื่อไหร่"));
        QVERIFY(pagesOf(founded).contains(0));
        QVERIFY(!pagesOf(founded).contains(1));
        const QList<int> branches = bestChunks(chunks, QString::fromUtf8("คณะกรรมการเห็นชอบอะไร"));
        QVERIFY(branches.contains(2));
        // a question that is nothing but question words: the start of the document
        const QList<int> nothing = bestChunks(chunks, QString::fromUtf8("อะไรครับ"));
        QCOMPARE(nothing.first(), 0);
    }
    void englishQuestionsAndTheBudgetOfCharacters()
    {
        const QStringList pages = {QString("The invoice total is 4,200 USD, due on 30 September."), QString("Delivery address: 12 Harbour Road, Songkhla."),
                                   QString("Terms: payment within thirty days.")};
        const QList<Chunk> chunks = chunksOf(pages);
        const QList<int> total = bestChunks(chunks, QString("What is the invoice total?"));
        QCOMPARE(chunks.at(total.first()).page, 0);
        const QList<int> address = bestChunks(chunks, QString("Where is the delivery address?"));
        QCOMPARE(chunks.at(address.first()).page, 1);
        // the budget: never more characters than allowed (but always at least the best piece)
        QStringList many;
        for (int i = 0; i < 40; ++i)
            many << QString("report section %1 describes the budget of the project in detail, again and again").arg(i);
        const QList<Chunk> big = chunkPages(many, 120);
        const QList<int> chosen = bestChunks(big, QString("budget of the project"), 500, 8);
        int chars = 0;
        for (const int i : chosen)
            chars += int(big.at(i).text.size());
        QVERIFY(chosen.size() <= 8);
        QVERIFY(chars <= 500 + 120);
        QVERIFY(std::is_sorted(chosen.begin(), chosen.end()));      // in document order
    }

    // ---- prompts ----------------------------------------------------------------------------------------------------------------------------
    void languageAndPageReferences()
    {
        QVERIFY(isThai(QString::fromUtf8("งบประมาณเท่าไหร่ครับ")));
        QVERIFY(!isThai(QString("How much is the budget?")));
        QVERIFY(isThai(QString::fromUtf8("ขอสรุป invoice นี้หน่อย")));
        QCOMPARE(citedPages(QString::fromUtf8("รวม 1.25 ล้านบาท (หน้า 2)"), 10), QList<int>({2}));
        QCOMPARE(citedPages(QString::fromUtf8("ดูหน้า 3, 5 และหน้า 4-6 ครับ"), 10), QList<int>({3, 4, 5, 6}));
        QCOMPARE(citedPages(QString("see page 7 and pp. 2-3"), 10), QList<int>({2, 3, 7}));
        QCOMPARE(citedPages(QString::fromUtf8("หน้า 99 และหน้า 0 และหน้า 2"), 10), QList<int>({2}));       // outside the document: dropped
        QVERIFY(citedPages(QString("no references here, 2026"), 10).isEmpty());
    }
    void reasoningBlocksAreRemoved()
    {
        QCOMPARE(withoutReasoning(QString("<think>hmm</think>\nAnswer")), QString("Answer"));
        QCOMPARE(withoutReasoning(QString("<think>still thinking")), QString());
        QCOMPARE(withoutReasoning(QString("plain")), QString("plain"));
    }
    void chatMessagesKeepTheLastTurnsOnly()
    {
        QList<QPair<QString, QString>> history;
        for (int i = 0; i < 10; ++i)
            history.append({i % 2 ? QString("assistant") : QString("user"), QString::number(i)});
        const QJsonArray messages = chatMessages(QString("system"), history, QString("now"), 4);
        QCOMPARE(messages.size(), 6);                                         // system + 4 turns + the new message
        QCOMPARE(messages.at(0).toObject().value("role").toString(), QString("system"));
        QCOMPARE(messages.at(1).toObject().value("content").toString(), QString("6"));
        QCOMPARE(messages.last().toObject().value("content").toString(), QString("now"));
        const QString excerpts = formatExcerpts(chunksOf({QString("alpha"), QString("beta")}), {0, 1}, false);
        QCOMPARE(excerpts, QString("[page 1]\nalpha\n\n[page 2]\nbeta"));
    }

    // ---- the streaming reply parser ---------------------------------------------------------------------------------------------------------
    void sseParserHandlesSplitEventsAndThaiBytes()
    {
        const QByteArray stream = "data: {\"choices\":[{\"delta\":{\"content\":\"" + QString::fromUtf8("สวัสดี").toUtf8() + "\"}}]}\n\n"
                                  "data: {\"choices\":[{\"delta\":{\"content\":\" world\"}}]}\n\n"
                                  "data: {\"choices\":[{\"delta\":{},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n";
        SseParser whole;
        const QStringList all = whole.feed(stream);
        QCOMPARE(all, QStringList({QString::fromUtf8("สวัสดี"), QString(" world")}));
        QVERIFY(whole.done());
        SseParser bytes;                                                      // one byte at a time: also cuts a Thai letter in the middle
        QStringList pieces;
        for (const char c : stream)
            pieces << bytes.feed(QByteArray(1, c));
        QCOMPARE(pieces, all);
        QVERIFY(bytes.done());
        SseParser failing;
        failing.feed("data: {\"error\":{\"message\":\"out of memory\"}}\n\n");
        QCOMPARE(failing.error(), QString("out of memory"));
    }

    // ---- the chat client ------------------------------------------------------------------------------------------------------------------------
    void chatStreamDeliversPiecesInOrderAndReportsFailures()
    {
        FakeLlama server;
        QVERIFY(server.listenOk);
        QNetworkAccessManager net;
        server.reply = [](const QJsonObject&) { return QString::fromUtf8("คำตอบจากโมเดลจำลอง ยาวพอที่จะถูกแบ่งเป็นหลายส่วน"); };
        ChatStream stream(&net);
        QStringList got;
        connect(&stream, &ChatStream::piece, this, [&](const QString& t) { got << t; });
        QSignalSpy done(&stream, &ChatStream::finished);
        stream.start(server.baseUrl(), "test-key", chatMessages("s", {}, "q"), 100);
        QTRY_VERIFY_WITH_TIMEOUT(done.count() == 1, 5000);
        QVERIFY(done.first().at(0).toBool());
        QCOMPARE(got.join(QString()), server.reply({}));
        QVERIFY(got.size() > 2);
        QCOMPARE(server.authorizations.first(), QString("Bearer test-key"));
        const QJsonObject sent = server.requests.first();
        QVERIFY(sent.value("stream").toBool());
        QCOMPARE(sent.value("chat_template_kwargs").toObject().value("enable_thinking").toBool(true), false);
        // without streaming: the whole text in one piece
        got.clear();
        QSignalSpy plain(&stream, &ChatStream::finished);
        stream.start(server.baseUrl(), "k", chatMessages("s", {}, "q"), 100, 0.2, false);
        QTRY_VERIFY_WITH_TIMEOUT(plain.count() == 1, 5000);
        QCOMPARE(got.size(), 1);
        // a server that fails
        server.failStatus = 500;
        QSignalSpy bad(&stream, &ChatStream::finished);
        stream.start(server.baseUrl(), "k", chatMessages("s", {}, "q"), 100);
        QTRY_VERIFY_WITH_TIMEOUT(bad.count() == 1, 5000);
        QVERIFY(!bad.first().at(0).toBool());
        QVERIFY(!bad.first().at(1).toString().isEmpty());
        // a server that is not there
        QSignalSpy gone(&stream, &ChatStream::finished);
        stream.start(QStringLiteral("http://127.0.0.1:9"), "k", chatMessages("s", {}, "q"), 100);
        QTRY_VERIFY_WITH_TIMEOUT(gone.count() == 1, 8000);
        QVERIFY(!gone.first().at(0).toBool());
    }
    void chatStreamCanBeCancelled()
    {
        FakeLlama server;
        QVERIFY(server.listenOk);
        server.pieceDelayMs = 30;
        server.reply = [](const QJsonObject&) { return QString(300, QLatin1Char('a')); };
        QNetworkAccessManager net;
        ChatStream stream(&net);
        QStringList got;
        connect(&stream, &ChatStream::piece, this, [&](const QString& t) { got << t; });
        QSignalSpy done(&stream, &ChatStream::finished);
        stream.start(server.baseUrl(), "k", chatMessages("s", {}, "q"), 100);
        QTRY_VERIFY_WITH_TIMEOUT(got.size() >= 2, 5000);
        stream.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(done.count() == 1, 5000);
        QVERIFY(!done.first().at(0).toBool());
        QCOMPARE(done.first().at(1).toString(), QString("cancelled"));
        QVERIFY(got.join(QString()).size() < 300);
    }

    // ---- the assistant against the fake server -----------------------------------------------------------------------------------------------
    void answersFromTheMatchingPageWithItsNumberAndRemembersTheTalk()
    {
        QTemporaryDir dir;
        DocumentController controller(dir.filePath("data"));
        controller.setLanguage("th");
        DocumentAssistant assistant(&controller);
        FakeLlama server;
        QVERIFY(server.listenOk);
        assistant.useServerForTests(server.baseUrl(), "test-key");
        const QString path = writeThaiPdf(dir, "report.pdf",
                                          {QString::fromUtf8("ประวัติบริษัท\nก่อตั้งเมื่อปี 2540 ที่จังหวัดสงขลา"),
                                           QString::fromUtf8("งบประมาณปี 2569\nรวมทั้งสิ้น 1,250,000 บาท"),
                                           QString::fromUtf8("สรุปผล\nขยายสาขาในปีหน้า")});
        QCOMPARE(controller.open(path), QString());
        QCOMPARE(controller.pageCount(), 3);
        server.reply = [](const QJsonObject&) { return QString::fromUtf8("งบประมาณรวม 1,250,000 บาท (หน้า 2)"); };
        QSignalSpy states(&assistant, &DocumentAssistant::stateChanged);
        assistant.ask(QString::fromUtf8("งบประมาณรวมทั้งสิ้นเท่าไหร่"));
        QVERIFY(assistant.busy());
        QTRY_VERIFY_WITH_TIMEOUT(!assistant.busy(), 15000);
        QCOMPARE(server.requests.size(), 1);
        const QJsonArray messages = server.requests.first().value("messages").toArray();
        QVERIFY(messages.first().toObject().value("content").toString().contains(QString::fromUtf8("ภาษาไทย")));          // the Thai system prompt
        const QString user = messages.last().toObject().value("content").toString();
        QVERIFY2(user.contains(QStringLiteral("1,250,000")), qPrintable(user));                  // the excerpt of page 2 is in the question
        QVERIFY(user.contains(QString::fromUtf8("[หน้า 2]")));
        QVERIFY(!user.contains(QString::fromUtf8("ขยายสาขา")));                                  // and not the page about something else
        QCOMPARE(server.authorizations.first(), QString("Bearer test-key"));
        const QVariantList view = assistant.messages();
        QCOMPARE(view.size(), 2);
        QCOMPARE(view.at(0).toMap().value("role").toString(), QString("user"));
        const QVariantMap answer = view.at(1).toMap();
        QCOMPARE(answer.value("text").toString(), QString::fromUtf8("งบประมาณรวม 1,250,000 บาท (หน้า 2)"));
        QCOMPARE(answer.value("pages").toList(), QVariantList{QVariant(2)});
        QVERIFY(!answer.value("streaming").toBool());
        QCOMPARE(assistant.status(), QString());
        // the follow-up carries the first turn
        server.reply = [](const QJsonObject&) { return QString::fromUtf8("ก่อตั้งปี 2540 (หน้า 1)"); };
        assistant.ask(QString::fromUtf8("แล้วบริษัทก่อตั้งเมื่อไหร่"));
        QTRY_VERIFY_WITH_TIMEOUT(!assistant.busy(), 15000);
        const QJsonArray second = server.requests.last().value("messages").toArray();
        bool remembered = false;
        for (const QJsonValue& m : second)
            remembered = remembered || m.toObject().value("content").toString().contains(QString::fromUtf8("1,250,000 บาท (หน้า 2)"));
        QVERIFY(remembered);
        QCOMPARE(assistant.messages().size(), 4);
        // a new conversation forgets it
        assistant.clear();
        QVERIFY(assistant.messages().isEmpty());
        // another document: the conversation goes
        assistant.ask(QString::fromUtf8("ขยายสาขาเมื่อไหร่"));
        QTRY_VERIFY_WITH_TIMEOUT(!assistant.busy(), 15000);
        QCOMPARE(assistant.messages().size(), 2);
        QCOMPARE(controller.open(writeThaiPdf(dir, "other.pdf", {QString::fromUtf8("อีกไฟล์หนึ่ง ที่มีข้อความยาวพอสมควรสำหรับทดสอบ")})), QString());
        QVERIFY(assistant.messages().isEmpty());
    }
    void summarisesAShortDocumentInOneGo()
    {
        QTemporaryDir dir;
        DocumentController controller(dir.filePath("data"));
        controller.setLanguage("en");
        DocumentAssistant assistant(&controller);
        FakeLlama server;
        QVERIFY(server.listenOk);
        assistant.useServerForTests(server.baseUrl(), "k");
        QCOMPARE(controller.open(writeThaiPdf(dir, "short.pdf", {QString::fromUtf8("หนังสือเชิญประชุม\nวันที่ 30 กันยายน 2569")})), QString());
        server.reply = [](const QJsonObject&) { return QString("A short invitation to a meeting on 30 September 2569."); };
        assistant.summarize();
        QTRY_VERIFY_WITH_TIMEOUT(!assistant.busy(), 15000);
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(server.requests.first().value("stream").toBool());
        QVERIFY(server.requests.first().value("messages").toArray().first().toObject().value("content").toString().contains("Summarise"));    // UI language: English
        QVERIFY(server.requests.first().value("messages").toArray().last().toObject().value("content").toString().contains(QString::fromUtf8("30 กันยายน 2569")));
        const QVariantList view = assistant.messages();
        QCOMPARE(view.size(), 2);
        QCOMPARE(view.at(0).toMap().value("text").toString(), QString("@summary"));
        QCOMPARE(view.at(1).toMap().value("text").toString(), QString("A short invitation to a meeting on 30 September 2569."));
    }
    void summarisesALongDocumentPartByPart()
    {
        QTemporaryDir dir;
        DocumentController controller(dir.filePath("data"));
        controller.setLanguage("th");
        DocumentAssistant assistant(&controller);
        FakeLlama server;
        QVERIFY(server.listenOk);
        assistant.useServerForTests(server.baseUrl(), "k");
        QStringList pages;
        for (int i = 1; i <= 24; ++i)
            pages << QString::fromUtf8("หัวข้อที่ %1\n").arg(i) + QString::fromUtf8("รายละเอียดของหัวข้อนี้ยาวพอสมควรเพื่อให้เอกสารรวมกันเกินขนาดที่สรุปได้ในครั้งเดียว ").repeated(4);
        QCOMPARE(controller.open(writeThaiPdf(dir, "long.pdf", pages)), QString());
        int streamed = 0, plain = 0;
        server.reply = [&](const QJsonObject& request) {
            if (request.value("stream").toBool()) {
                ++streamed;
                return QString::fromUtf8("สรุปรวมทั้งเอกสาร");
            }
            ++plain;
            return QString::fromUtf8("สรุปส่วนที่ %1").arg(plain);
        };
        QStringList seen;
        connect(&assistant, &DocumentAssistant::stateChanged, this, [&] {
            if (assistant.status() == QLatin1String("part") && (seen.isEmpty() || seen.last() != QString::number(assistant.statusPart())))
                seen << QString::number(assistant.statusPart());
        });
        assistant.summarize();
        QTRY_VERIFY_WITH_TIMEOUT(!assistant.busy(), 30000);
        QVERIFY2(plain >= 2, qPrintable(QString::number(plain)));                              // read part by part
        QCOMPARE(streamed, 1);                                                                 // and one streamed final summary
        QVERIFY(seen.size() >= 2);
        QCOMPARE(seen.first(), QString("1"));
        // the final request is made from the part summaries, not from the pages
        const QString last = server.requests.last().value("messages").toArray().last().toObject().value("content").toString();
        QVERIFY(last.contains(QString::fromUtf8("สรุปส่วนที่ 1")));
        QVERIFY(!last.contains(QString::fromUtf8("รายละเอียดของหัวข้อนี้")));
        QCOMPARE(assistant.messages().last().toMap().value("text").toString(), QString::fromUtf8("สรุปรวมทั้งเอกสาร"));
    }
    void stopKeepsWhatWasWritten()
    {
        QTemporaryDir dir;
        DocumentController controller(dir.filePath("data"));
        controller.setLanguage("th");
        DocumentAssistant assistant(&controller);
        FakeLlama server;
        QVERIFY(server.listenOk);
        server.pieceDelayMs = 40;
        server.reply = [](const QJsonObject&) { return QString(400, QLatin1Char('b')); };
        assistant.useServerForTests(server.baseUrl(), "k");
        QCOMPARE(controller.open(writeThaiPdf(dir, "d.pdf", {QString::fromUtf8("เอกสารทดสอบการหยุดคำตอบกลางคัน ที่มีข้อความพอสมควร")})), QString());
        assistant.ask(QString::fromUtf8("เอกสารนี้เกี่ยวกับอะไร"));
        QTRY_VERIFY_WITH_TIMEOUT(assistant.messages().size() == 2 && assistant.messages().at(1).toMap().value("text").toString().size() >= 6, 10000);
        assistant.stop();
        QTRY_VERIFY_WITH_TIMEOUT(!assistant.busy(), 5000);
        const QString text = assistant.messages().at(1).toMap().value("text").toString();
        QVERIFY(!text.isEmpty() && text.size() < 400);
        QVERIFY(!assistant.messages().at(1).toMap().value("streaming").toBool());
        // and it can be asked again
        server.pieceDelayMs = 0;
        server.reply = [](const QJsonObject&) { return QString("fine"); };
        assistant.ask(QString::fromUtf8("อีกครั้ง"));
        QTRY_VERIFY_WITH_TIMEOUT(!assistant.busy(), 10000);
        QCOMPARE(assistant.messages().last().toMap().value("text").toString(), QString("fine"));
    }
    void aScanWithoutTextSuggestsOcrAndAFailingModelIsReported()
    {
        QTemporaryDir dir;
        DocumentController controller(dir.filePath("data"));
        controller.setLanguage("th");
        DocumentAssistant assistant(&controller);
        FakeLlama server;
        QVERIFY(server.listenOk);
        assistant.useServerForTests(server.baseUrl(), "k");
        QSignalSpy ocr(&assistant, &DocumentAssistant::ocrSuggested);
        QCOMPARE(controller.open(writeThaiPdf(dir, "scan.pdf", {QString::fromUtf8("ข้อความที่เป็นภาพ")}, true)), QString());
        assistant.ask(QString::fromUtf8("เอกสารนี้เกี่ยวกับอะไร"));
        QTRY_VERIFY_WITH_TIMEOUT(!assistant.busy(), 15000);
        QCOMPARE(assistant.status(), QString("noText"));
        QCOMPARE(ocr.count(), 1);
        QCOMPARE(server.requests.size(), 0);                                  // the model was not asked for nothing
        QCOMPARE(assistant.messages().size(), 1);                             // the question stays, the empty answer goes
        // a model that answers with an error
        assistant.clear();
        QCOMPARE(controller.open(writeThaiPdf(dir, "text.pdf", {QString::fromUtf8("เอกสารที่มีข้อความพอสมควรสำหรับการทดสอบ")})), QString());
        server.failStatus = 500;
        assistant.ask(QString::fromUtf8("ทดสอบ"));
        QTRY_VERIFY_WITH_TIMEOUT(!assistant.busy(), 15000);
        QCOMPARE(assistant.status(), QString("failed"));
        QVERIFY(!assistant.detail().isEmpty());
        QVERIFY(assistant.state() != QLatin1String("busy"));
    }

    // ---- the model download -----------------------------------------------------------------------------------------------------------------------
    void theModelIsDownloadedChecksAndResumes()
    {
        const QByteArray model = QByteArray(900000, 'm') + QByteArray("end");
        const QString sha = QString::fromLatin1(QCryptographicHash::hash(model, QCryptographicHash::Sha256).toHex());
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        int cut = 300000;
        QStringList ranges;
        connect(&server, &QTcpServer::newConnection, this, [&] {
            while (QTcpSocket* s = server.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, s, [&, s] {
                    const QByteArray req = s->readAll();
                    qint64 from = 0;
                    for (const QByteArray& line : req.split('\n'))
                        if (line.toLower().startsWith("range:")) {
                            ranges << QString::fromLatin1(line.trimmed());
                            from = line.mid(line.indexOf('=') + 1).split('-').value(0).trimmed().toLongLong();
                        }
                    QByteArray body = model.mid(int(from));
                    QByteArray head = from > 0 ? "HTTP/1.1 206 Partial Content\r\n" : "HTTP/1.1 200 OK\r\n";
                    if (from > 0)
                        head += "Content-Range: bytes " + QByteArray::number(from) + "-" + QByteArray::number(model.size() - 1) + "/" + QByteArray::number(model.size()) + "\r\n";
                    head += "Content-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n";
                    if (cut > 0) {
                        body = body.left(cut);
                        cut = 0;
                        s->write(head + body);
                        s->flush();
                        s->abort();
                        return;
                    }
                    s->write(head + body);
                    s->disconnectFromHost();
                });
            }
        });
        QTemporaryDir dir;
        ModelDownloader loader;
        const QString url = QStringLiteral("http://127.0.0.1:%1/model.gguf").arg(server.serverPort());
        QSignalSpy done(&loader, &ModelDownloader::finished);
        QCOMPARE(loader.downloadFile(url, dir.filePath("m"), "model.gguf", model.size(), sha), QString());
        QTRY_VERIFY_WITH_TIMEOUT(done.count() == 1, 15000);
        QVERIFY(!done.first().at(0).toBool());                                // the connection died
        QCOMPARE(loader.code(), QString("failed"));
        const qint64 part = QFileInfo(dir.filePath("m/.part/model.gguf.part")).size();
        QVERIFY(part > 0 && part < model.size());
        QSignalSpy second(&loader, &ModelDownloader::finished);
        ranges.clear();
        QCOMPARE(loader.downloadFile(url, dir.filePath("m"), "model.gguf", model.size(), sha), QString());
        QTRY_VERIFY_WITH_TIMEOUT(second.count() == 1, 15000);
        QVERIFY2(second.first().at(0).toBool(), qPrintable(second.first().at(1).toString()));
        QVERIFY(!ranges.isEmpty() && ranges.first().contains("bytes=" + QString::number(part) + "-"));
        QCOMPARE(loader.code(), QString("installed"));
        QFile f(dir.filePath("m/model.gguf"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), model);
        // a wrong checksum: refused and deleted
        QTemporaryDir other;
        QSignalSpy bad(&loader, &ModelDownloader::finished);
        QCOMPARE(loader.downloadFile(url, other.filePath("m"), "model.gguf", model.size(), QString(64, QLatin1Char('0'))), QString());
        QTRY_VERIFY_WITH_TIMEOUT(bad.count() == 1, 15000);
        QVERIFY(!bad.first().at(0).toBool());
        QVERIFY(!QFileInfo::exists(other.filePath("m/model.gguf")));
        QVERIFY(!QFileInfo::exists(other.filePath("m/.part/model.gguf.part")));
    }
    void theDownloadIsPinned()
    {
        QVERIFY(ModelDownloader::defaultUrl().startsWith("https://huggingface.co/unsloth/Qwen3.5-4B-GGUF/resolve/"));
        QVERIFY(!ModelDownloader::defaultUrl().contains("/main/"));
        QCOMPARE(ModelDownloader::defaultSha256().size(), 64);
        QVERIFY(ModelDownloader::defaultSize() > 2000000000LL && ModelDownloader::defaultSize() < 3000000000LL);
    }

    // ---- the real model (DP_TEST_REAL_LLM=1) --------------------------------------------------------------------------------------------------
    void theRealModelAnswersAboutADocument()
    {
        if (qEnvironmentVariable("DP_TEST_REAL_LLM") != QLatin1String("1") || LlamaServer::serverExecutable().isEmpty() || LlamaServer::modelPath().isEmpty())
            QSKIP("set DP_TEST_REAL_LLM=1 with the llama.cpp build and the model installed to run the real model");
        QTemporaryDir dir;
        DocumentController controller(dir.filePath("data"));
        controller.setLanguage("th");
        DocumentAssistant assistant(&controller);
        QCOMPARE(controller.open(writeThaiPdf(dir, "real.pdf",
                                              {QString::fromUtf8("ประวัติบริษัท\nบริษัทก่อตั้งเมื่อปี 2540 ที่จังหวัดสงขลา"),
                                               QString::fromUtf8("งบประมาณปี 2569\nงบประมาณรวมทั้งสิ้น 1,250,000 บาท แบ่งเป็นค่าจ้าง 900,000 บาท"),
                                               QString::fromUtf8("สรุปผล\nคณะกรรมการเห็นชอบให้ขยายสาขาในปีหน้า")})), QString());
        QElapsedTimer timer;
        timer.start();
        assistant.ask(QString::fromUtf8("งบประมาณรวมทั้งสิ้นกี่บาท"));
        QTRY_VERIFY_WITH_TIMEOUT(!assistant.busy(), 300000);
        const QString answer = assistant.messages().last().toMap().value("text").toString();
        fprintf(stderr, "REALLLM first answer in %.1f s (GPU %d): %s\n", double(timer.elapsed()) / 1000.0, int(assistant.usesGpu()), answer.toUtf8().constData());
        QVERIFY2(answer.contains(QStringLiteral("1,250,000")) || answer.contains(QStringLiteral("1250000")), qPrintable(answer));
        QVERIFY2(answer.contains(QString::fromUtf8("หน้า 2")) || answer.contains(QStringLiteral("2")), qPrintable(answer));
        assistant.summarize();
        QTRY_VERIFY_WITH_TIMEOUT(!assistant.busy(), 300000);
        const QString summary = assistant.messages().last().toMap().value("text").toString();
        fprintf(stderr, "REALLLM summary: %s\n", summary.toUtf8().constData());
        QVERIFY(summary.size() > 30);
    }
};

QTEST_MAIN(TstAssistant)
#include "tst_assistant.moc"
