// The AI of Master Pro: stream parsers, HTTP chat (OpenAI style and Anthropic) against a fake server on this computer, the command line programs through a
// stand-in (stdin, events, cancel), model links and downloads, the key store and the two pages with their saved conversations. No real AI is called.
#include "Hub.h"
#include "ai/AiService.h"
#include "ai/CliChat.h"
#include "ai/HttpChat.h"
#include "ai/ModelDownloader.h"
#include "ai/ModelStore.h"
#include "ai/SecretStore.h"
#include "ai/Streams.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

using namespace dm::ai;

namespace {

// A tiny HTTP server: one answer for every request; remembers the last request.
class FakeServer : public QTcpServer {
public:
    int status = 200;
    QByteArray contentType = "text/event-stream";
    QByteArray body;
    QByteArray lastRequest;
    int requests = 0;

    FakeServer()
    {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, this, [this, s] {
                    QByteArray data = s->property("data").toByteArray() + s->readAll();
                    s->setProperty("data", data);
                    const int end = data.indexOf("\r\n\r\n");
                    if (end < 0)
                        return;
                    const int cl = QRegularExpression(QStringLiteral("content-length: *(\\d+)"), QRegularExpression::CaseInsensitiveOption)
                                       .match(QString::fromLatin1(data.left(end))).captured(1).toInt();
                    if (data.size() < end + 4 + cl)
                        return;
                    lastRequest = data;
                    ++requests;
                    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: " + contentType + "\r\nContent-Length: " + QByteArray::number(body.size())
                                     + "\r\nConnection: close\r\n\r\n" + body;
                    s->write(out);
                    s->disconnectFromHost();
                });
            }
        });
        listen(QHostAddress::LocalHost, 0);
    }
    QString base() const { return QStringLiteral("http://127.0.0.1:%1").arg(serverPort()); }
};

QByteArray sse(const QStringList& datas)
{
    QByteArray out;
    for (const QString& d : datas)
        out += "data: " + d.toUtf8() + "\n\n";
    return out;
}

QJsonObject requestBody(const QByteArray& request)
{
    return QJsonDocument::fromJson(request.mid(request.indexOf("\r\n\r\n") + 4)).object();
}

} // namespace

class TstAi : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    const QString kRoot = QStringLiteral("Software\\DriveVaultMasterAiTest");

    // runs one HttpChat request to the end
    struct HttpResult {
        QString text;
        bool ok = false;
        QString code, detail;
    };
    HttpResult runHttp(HttpChat::Request r)
    {
        QNetworkAccessManager net;
        HttpChat chat(&net);
        HttpResult res;
        connect(&chat, &HttpChat::piece, [&](const QString& t) { res.text += t; });
        QSignalSpy done(&chat, &HttpChat::finished);
        const QString err = chat.start(r);
        if (!err.isEmpty()) {
            res.code = err;
            return res;
        }
        if (!done.wait(10000)) {
            res.code = QStringLiteral("timeout");
            return res;
        }
        res.ok = done.at(0).at(0).toBool();
        res.code = done.at(0).at(1).toString();
        res.detail = done.at(0).at(2).toString();
        return res;
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }
    void cleanupTestCase() { QSettings(QStringLiteral("HKEY_CURRENT_USER\\%1").arg(kRoot), QSettings::NativeFormat).remove(QString()); }

    // ---- streams ------------------------------------------------------------------------------------------------------------------
    void sseSplitsEventsAcrossChunks()
    {
        SseParser p;
        QVERIFY(p.feed("data: {\"a\":1}\n").isEmpty());
        auto ev = p.feed("\ndata: two\r\n\r\n: keep-alive\n\nevent: x\ndata: y\ndata: z\n\n");
        QCOMPARE(ev.size(), 3);
        QCOMPARE(ev.at(0).data, QString("{\"a\":1}"));
        QCOMPARE(ev.at(1).data, QString("two"));
        QCOMPARE(ev.at(2).event, QString("x"));
        QCOMPARE(ev.at(2).data, QString("y\nz"));
    }
    void streamEventsAreRead()
    {
        auto o = readOpenAiEvent({QString(), R"({"choices":[{"delta":{"content":"Hi"}}]})"});
        QCOMPARE(o.text, QString("Hi"));
        QVERIFY(readOpenAiEvent({QString(), "[DONE]"}).done);
        QCOMPARE(readOpenAiEvent({QString(), R"({"error":{"message":"bad key"}})"}).error, QString("bad key"));
        auto a = readAnthropicEvent({"content_block_delta", R"({"type":"content_block_delta","delta":{"type":"text_delta","text":"Yo"}})"});
        QCOMPARE(a.text, QString("Yo"));
        QVERIFY(readAnthropicEvent({"message_stop", "{}"}).done);
        QVERIFY(!readAnthropicEvent({"error", R"({"error":{"message":"overloaded"}})"}).error.isEmpty());
    }

    // ---- HTTP chat ----------------------------------------------------------------------------------------------------------------
    void openAiStyleRequestAndAnswer()
    {
        FakeServer server;
        server.body = sse({R"({"choices":[{"delta":{"content":"Hello "}}]})", R"({"choices":[{"delta":{"content":"world"}}]})", "[DONE]"});
        HttpChat::Request r;
        r.baseUrl = server.base() + "/v1/";
        r.apiKey = "sk-test";
        r.model = "m1";
        r.system = "be brief";
        r.messages = {{"user", "hi"}, {"assistant", "yo"}, {"user", "again"}};
        const HttpResult res = runHttp(r);
        QVERIFY2(res.ok, qPrintable(res.code + res.detail));
        QCOMPARE(res.text, QString("Hello world"));
        QVERIFY(server.lastRequest.startsWith("POST /v1/chat/completions"));
        QVERIFY(server.lastRequest.toLower().contains("authorization: bearer sk-test"));
        const QJsonObject b = requestBody(server.lastRequest);
        QCOMPARE(b.value("model").toString(), QString("m1"));
        QVERIFY(b.value("stream").toBool());
        QCOMPARE(b.value("messages").toArray().size(), 4);               // system + 3
        QCOMPARE(b.value("messages").toArray().first().toObject().value("role").toString(), QString("system"));
    }
    void anthropicRequestAndAnswer()
    {
        FakeServer server;
        server.body = "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"Bonjour\"}}\n\n"
                      "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n";
        HttpChat::Request r;
        r.kind = HttpChat::Kind::Anthropic;
        r.baseUrl = server.base();
        r.apiKey = "ak-test";
        r.model = "claude-x";
        r.system = "sys";
        r.messages = {{"user", "salut"}};
        const HttpResult res = runHttp(r);
        QVERIFY2(res.ok, qPrintable(res.code + res.detail));
        QCOMPARE(res.text, QString("Bonjour"));
        QVERIFY(server.lastRequest.startsWith("POST /v1/messages"));
        QVERIFY(server.lastRequest.toLower().contains("x-api-key: ak-test"));
        QVERIFY(server.lastRequest.toLower().contains("anthropic-version: 2023-06-01"));
        const QJsonObject b = requestBody(server.lastRequest);
        QCOMPARE(b.value("system").toString(), QString("sys"));
        QVERIFY(b.value("max_tokens").toInt() > 0);
        QCOMPARE(b.value("messages").toArray().size(), 1);               // the system text is not a message
    }
    void httpErrorsAreReported()
    {
        FakeServer server;
        server.status = 401;
        server.contentType = "application/json";
        server.body = R"({"error":{"message":"Incorrect API key"}})";
        HttpChat::Request r;
        r.baseUrl = server.base() + "/v1";
        r.model = "m";
        r.messages = {{"user", "x"}};
        const HttpResult res = runHttp(r);
        QVERIFY(!res.ok);
        QCOMPARE(res.code, QString("http401"));
        QCOMPARE(res.detail, QString("Incorrect API key"));
    }
    void aKeyIsNeverSentInClearText()
    {
        HttpChat::Request r;
        r.baseUrl = "http://example.com/v1";                              // http to another computer
        r.apiKey = "secret";
        r.model = "m";
        r.messages = {{"user", "x"}};
        QCOMPARE(runHttp(r).code, QString("insecureUrl"));
        r.baseUrl = "ftp://example.com";
        QVERIFY(!runHttp(r).ok);
        QVERIFY(HttpChat::urlAllowed("https://api.openai.com/v1/chat/completions"));
        QVERIFY(HttpChat::urlAllowed("http://localhost:11434/v1/chat/completions"));
        QVERIFY(!HttpChat::urlAllowed("http://192.168.1.5:11434/v1"));
        r.baseUrl = "https://api.openai.com/v1";
        r.model = " ";
        QCOMPARE(runHttp(r).code, QString("noModel"));
    }

    // ---- command line programs ------------------------------------------------------------------------------------------------------
    void cliOptionsAreFixed()
    {
        CliChat::Request r;
        r.tool = CliChat::Tool::Claude;
        QStringList a = CliChat::arguments(r);
        QVERIFY(a.contains("-p") && a.contains("--restricted"));
        QCOMPARE(a.at(a.indexOf("--permission-mode") + 1), QString("plan"));
        r.allowEdits = true;
        a = CliChat::arguments(r);
        QCOMPARE(a.at(a.indexOf("--permission-mode") + 1), QString("acceptEdits"));
        QVERIFY(!a.contains("--dangerously-skip-permissions"));
        r.sessionId = "11111111-2222-3333-4444-555555555555";
        QVERIFY(CliChat::arguments(r).contains("--resume"));
        r.sessionId = "x & calc.exe";                                     // never goes on a command line
        QVERIFY(!CliChat::arguments(r).contains("--resume"));
        r.tool = CliChat::Tool::Codex;
        r.allowEdits = false;
        a = CliChat::arguments(r);
        QCOMPARE(a.first(), QString("exec"));
        QCOMPARE(a.at(a.indexOf("-s") + 1), QString("read-only"));
        QCOMPARE(a.last(), QString("-"));                                 // the prompt comes on stdin
    }
    void cliEventsAreRead()
    {
        bool streamed = false;
        auto ev = CliChat::readLine(CliChat::Tool::Claude, R"({"type":"stream_event","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":"A"}}})", &streamed);
        QCOMPARE(ev.size(), 1);
        QCOMPARE(ev.first().text, QString("A"));
        QVERIFY(streamed);
        ev = CliChat::readLine(CliChat::Tool::Claude, R"({"type":"assistant","message":{"content":[{"type":"text","text":"A"},{"type":"tool_use","name":"Read","input":{"file_path":"C:\\x\\a.txt"}}]}})", &streamed);
        QCOMPARE(ev.size(), 1);                                          // the text was streamed already: only the tool is new
        QVERIFY(ev.first().activity.startsWith("Read: "));
        QVERIFY(!streamed);
        ev = CliChat::readLine(CliChat::Tool::Claude, R"({"type":"result","is_error":true,"result":"Not logged in","session_id":"s"})", &streamed);
        QCOMPARE(ev.first().error, QString("Not logged in"));
        QVERIFY(ev.first().done);
        ev = CliChat::readLine(CliChat::Tool::Codex, R"({"type":"item.completed","item":{"type":"agent_message","text":"Done"}})", &streamed);
        QCOMPARE(ev.first().text, QString("Done"));
        QVERIFY(ev.first().textIsWhole);
        QVERIFY(CliChat::readLine(CliChat::Tool::Codex, "not json", &streamed).isEmpty());
    }
    void cliRunsThroughTheStandIn()
    {
        const QString exe = QStringLiteral(FAKE_CLI_PATH);
        const QString argsFile = m_dir.filePath("args.txt");
        qputenv("DM_FAKE_ARGS", argsFile.toUtf8());

        // the question arrives on stdin, never on the command line
        qputenv("DM_FAKE_MODE", "echo");
        {
            CliChat chat;
            QString text;
            connect(&chat, &CliChat::piece, [&](const QString& t) { text += t; });
            QSignalSpy done(&chat, &CliChat::finished);
            CliChat::Request r;
            r.executable = exe;
            r.workDir = m_dir.path();
            r.prompt = "hello & echo pwned \"quoted\"\nsecond line";
            QCOMPARE(chat.start(r), QString());
            QVERIFY(done.wait(10000));
            QVERIFY(done.at(0).at(0).toBool());
            QCOMPARE(text, r.prompt);
            QFile f(argsFile);
            QVERIFY(f.open(QIODevice::ReadOnly));
            QVERIFY(!f.readAll().contains("pwned"));
        }
        // a whole turn: pieces, a break between text blocks, the tool as activity, the session id
        qputenv("DM_FAKE_MODE", "claude-ok");
        {
            CliChat chat;
            QString text, session;
            QStringList activity;
            connect(&chat, &CliChat::piece, [&](const QString& t) { text += t; });
            connect(&chat, &CliChat::activity, [&](const QString& a) { activity << a; });
            connect(&chat, &CliChat::sessionId, [&](const QString& s) { session = s; });
            QSignalSpy done(&chat, &CliChat::finished);
            CliChat::Request r;
            r.executable = exe;
            r.workDir = m_dir.path();
            r.prompt = "go";
            QCOMPARE(chat.start(r), QString());
            QVERIFY(done.wait(10000));
            QVERIFY(done.at(0).at(0).toBool());
            QCOMPARE(text, QString("Hello\n\nWorld"));
            QCOMPARE(activity, QStringList{"Read: notes.txt"});
            QCOMPARE(session, QString("11111111-2222-3333-4444-555555555555"));
        }
        // an error of the program (not signed in)
        qputenv("DM_FAKE_MODE", "claude-error");
        {
            CliChat chat;
            QSignalSpy done(&chat, &CliChat::finished);
            CliChat::Request r;
            r.executable = exe;
            r.workDir = m_dir.path();
            r.prompt = "go";
            QCOMPARE(chat.start(r), QString());
            QVERIFY(done.wait(10000));
            QVERIFY(!done.at(0).at(0).toBool());
            QCOMPARE(done.at(0).at(1).toString(), QString("failed"));
            QCOMPARE(done.at(0).at(2).toString(), QString("Not logged in"));
        }
        // Codex events
        qputenv("DM_FAKE_MODE", "codex-ok");
        {
            CliChat chat;
            QString text;
            connect(&chat, &CliChat::piece, [&](const QString& t) { text += t; });
            QSignalSpy done(&chat, &CliChat::finished);
            CliChat::Request r;
            r.tool = CliChat::Tool::Codex;
            r.executable = exe;
            r.workDir = m_dir.path();
            r.prompt = "go";
            QCOMPARE(chat.start(r), QString());
            QVERIFY(done.wait(10000));
            QVERIFY(done.at(0).at(0).toBool());
            QCOMPARE(text, QString("Done."));
        }
        // cancel kills the program
        qputenv("DM_FAKE_MODE", "hang");
        {
            CliChat chat;
            QSignalSpy done(&chat, &CliChat::finished);
            CliChat::Request r;
            r.executable = exe;
            r.workDir = m_dir.path();
            r.prompt = "go";
            QCOMPARE(chat.start(r), QString());
            QTest::qWait(300);
            QVERIFY(chat.running());
            chat.cancel();
            QVERIFY(done.wait(10000));
            QCOMPARE(done.at(0).at(1).toString(), QString("cancelled"));
        }
        // a missing program or folder is refused before anything starts
        {
            CliChat chat;
            CliChat::Request r;
            r.executable = m_dir.filePath("nothing.exe");
            r.workDir = m_dir.path();
            QCOMPARE(chat.start(r), QString("noProgram"));
            r.executable = exe;
            r.workDir = m_dir.filePath("no-such-folder");
            QCOMPARE(chat.start(r), QString("noFolder"));
        }
        qunsetenv("DM_FAKE_MODE");
        qunsetenv("DM_FAKE_ARGS");
    }

    // ---- history ------------------------------------------------------------------------------------------------------------------
    void historyMergesAndCaps()
    {
        using M = AiChannel::Message;
        auto msg = [](const char* role, const char* text) { M m; m.role = role; m.text = text; return m; };
        QList<M> list{msg("assistant", "stray"), msg("user", "q1"), msg("assistant", ""), msg("user", "q2"), msg("assistant", "a2"), msg("user", "q3")};
        auto h = AiChannel::historyFor(list, 1000);
        QCOMPARE(h.size(), 3);                                            // q1+q2 merged (the empty reply between them is dropped), a2, q3
        QCOMPARE(h.first().first, QString("user"));
        QVERIFY(h.first().second.contains("q1") && h.first().second.contains("q2"));
        h = AiChannel::historyFor(list, 4);                               // only the newest fits; it must be a question
        QCOMPARE(h.size(), 1);
        QCOMPARE(h.first().second, QString("q3"));
        const QString t = AiChannel::transcriptFor(list);
        QVERIFY(t.contains("Conversation so far") && t.endsWith("q3"));
    }

    // ---- models ------------------------------------------------------------------------------------------------------------------
    void hfLinksAreChecked()
    {
        auto ok = ModelStore::parseHfLink("https://huggingface.co/unsloth/Qwen3.5-4B-GGUF/blob/main/Qwen3.5-4B-Q4_K_M.gguf");
        QVERIFY(ok.ok);
        QCOMPARE(ok.url, QString("https://huggingface.co/unsloth/Qwen3.5-4B-GGUF/resolve/main/Qwen3.5-4B-Q4_K_M.gguf"));
        QCOMPARE(ok.fileName, QString("Qwen3.5-4B-Q4_K_M.gguf"));
        QCOMPARE(ok.repo, QString("unsloth/Qwen3.5-4B-GGUF"));
        QCOMPARE(ModelStore::parseHfLink("https://evil.example/unsloth/x/resolve/main/a.gguf").error, QString("notHuggingFace"));
        QCOMPARE(ModelStore::parseHfLink("https://huggingface.co.evil.example/a/b/resolve/main/a.gguf").error, QString("notHuggingFace"));
        QCOMPARE(ModelStore::parseHfLink("http://huggingface.co/a/b/resolve/main/a.gguf").error, QString("badLink"));
        QCOMPARE(ModelStore::parseHfLink("https://huggingface.co/a/b/resolve/main/model.safetensors").error, QString("notGguf"));
        QCOMPARE(ModelStore::parseHfLink("https://huggingface.co/a/b/resolve/main/../x.gguf").error, QString("badLink"));
        QCOMPARE(ModelStore::parseHfLink("https://huggingface.co/a/b").error, QString("badLink"));
        QCOMPARE(ModelStore::parseHfLink("not a link").error, QString("badLink"));
    }
    void hfHeadersGiveSizeAndChecksum()
    {
        const auto link = ModelStore::parseHfLink("https://huggingface.co/o/r/resolve/main/m.gguf");
        QMap<QString, QString> h{{"x-linked-size", "123456789"}, {"x-linked-etag", "\"" + QString(64, 'a') + "\""}, {"x-repo-commit", QString(40, 'b')}};
        auto info = ModelStore::readHfHeaders(302, h, link);
        QVERIFY(info.ok);
        QCOMPARE(info.size, 123456789LL);
        QCOMPARE(info.sha256, QString(64, 'a'));
        QCOMPARE(info.pinnedUrl, QString("https://huggingface.co/o/r/resolve/%1/m.gguf").arg(QString(40, 'b')));    // pinned to the commit
        h.remove("x-linked-etag");
        QCOMPARE(ModelStore::readHfHeaders(302, h, link).error, QString("noChecksum"));
        QCOMPARE(ModelStore::readHfHeaders(404, h, link).error, QString("http404"));
    }
    void catalogIsPinned()
    {
        const auto list = ModelStore::catalog();
        QVERIFY(list.size() >= 3);
        for (const CatalogEntry& e : list) {
            QVERIFY(e.url.startsWith("https://huggingface.co/"));
            QVERIFY(e.url.contains(QRegularExpression("/resolve/[0-9a-f]{40}/")));
            QCOMPARE(e.sha256.size(), 64);
            QVERIFY(e.size > 100000000LL);
        }
    }
    void installedModelsAreListed()
    {
        QDir().mkpath(m_dir.filePath("models"));
        QFile big(m_dir.filePath("models/a.gguf"));
        QVERIFY(big.open(QIODevice::WriteOnly));
        big.write(QByteArray(2000000, 'x'));
        big.close();
        QFile tiny(m_dir.filePath("models/stub.gguf"));
        QVERIFY(tiny.open(QIODevice::WriteOnly));
        tiny.write("x");
        tiny.close();
        const auto list = ModelStore::installed({m_dir.filePath("models"), m_dir.filePath("models")});
        QCOMPARE(list.size(), 1);                                         // a stub is not a model; no duplicates
        QCOMPARE(list.first().name, QString("a.gguf"));
    }
    void downloaderChecksTheFile()
    {
        const QByteArray data(3000000, 'q');
        const QString sha = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
        FakeServer server;
        server.contentType = "application/octet-stream";
        server.body = data;
        {
            ModelDownloader d;
            QSignalSpy done(&d, &ModelDownloader::finished);
            QCOMPARE(d.downloadFile(server.base() + "/m.gguf", m_dir.filePath("dl"), "m.gguf", data.size(), sha), QString());
            QVERIFY(done.wait(20000));
            QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
            QFile f(m_dir.filePath("dl/m.gguf"));
            QVERIFY(f.open(QIODevice::ReadOnly));
            QCOMPARE(f.readAll(), data);
        }
        {
            ModelDownloader d;                                             // a wrong checksum: the file is deleted and the error says so
            QSignalSpy done(&d, &ModelDownloader::finished);
            QCOMPARE(d.downloadFile(server.base() + "/m.gguf", m_dir.filePath("dl2"), "m.gguf", data.size(), QString(64, '0')), QString());
            QVERIFY(done.wait(20000));
            QVERIFY(!done.at(0).at(0).toBool());
            QVERIFY(!QFileInfo::exists(m_dir.filePath("dl2/m.gguf")));
        }
        {
            ModelDownloader d;
            QVERIFY(!d.downloadFile("http://example.com/m.gguf", m_dir.filePath("dl3"), "m.gguf", 10, QString(64, '0')).isEmpty() || d.code() == "failed");
        }
    }

    // The real local model (llama.cpp + Qwen): only when asked for (it loads 2.7 GB): set DM_TEST_REAL_LLM=1 (the engine and model are found like in the program).
    void realLocalModelAnswers()
    {
        if (qEnvironmentVariable("DM_TEST_REAL_LLM") != QLatin1String("1"))
            QSKIP("set DM_TEST_REAL_LLM=1 to run the real local model");
        QString model = qEnvironmentVariable("DM_TEST_MODEL");
        for (QDir d(QCoreApplication::applicationDirPath()); model.isEmpty() && d.exists(); ) {
            if (QFileInfo::exists(d.filePath(".tools/llm/Qwen3.5-4B-Q4_K_M.gguf")))
                model = d.filePath(".tools/llm/Qwen3.5-4B-Q4_K_M.gguf");
            if (!d.cdUp())
                break;
        }
        QVERIFY2(!model.isEmpty() && !LlamaServer::serverExecutable().isEmpty(), "no engine or model on this PC");
        QNetworkAccessManager net;
        LlamaServer server(&net);
        server.start(model);
        QTRY_VERIFY_WITH_TIMEOUT(server.state() == LlamaServer::State::Ready || server.state() == LlamaServer::State::Failed, 240000);
        QVERIFY2(server.state() == LlamaServer::State::Ready, qPrintable(server.error() + " " + server.detail()));
        HttpChat::Request r;
        r.baseUrl = server.baseUrl() + "/v1";
        r.apiKey = server.apiKey();
        r.model = "local";
        r.messages = {{"user", "Reply with the single word: pong"}};
        r.extraBody = QJsonObject{{"chat_template_kwargs", QJsonObject{{"enable_thinking", false}}}};
        const HttpResult res = runHttp(r);
        QVERIFY2(res.ok, qPrintable(res.code + " " + res.detail));
        fprintf(stderr, "REAL LLM gpu=%d answer: %s\n", server.usesGpu(), qPrintable(res.text.left(120)));
        QVERIFY(res.text.contains("pong", Qt::CaseInsensitive));
        server.stop();
    }

    // The same through the page: provider "local", the real engine and model, a question and the streamed answer in the conversation.
    void realLocalPage()
    {
        if (qEnvironmentVariable("DM_TEST_REAL_LLM") != QLatin1String("1"))
            QSKIP("set DM_TEST_REAL_LLM=1 to run the real local model");
        QString model = qEnvironmentVariable("DM_TEST_MODEL");
        for (QDir d(QCoreApplication::applicationDirPath()); model.isEmpty() && d.exists(); ) {
            if (QFileInfo::exists(d.filePath(".tools/llm/Qwen3.5-4B-Q4_K_M.gguf")))
                model = d.filePath(".tools/llm/Qwen3.5-4B-Q4_K_M.gguf");
            if (!d.cdUp())
                break;
        }
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hub-real");
        dm::Hub hub(o);
        AiService ai(&hub, nullptr, false);
        auto* ask = qobject_cast<AiChannel*>(ai.ask());
        ask->setProvider("local");
        ai.selectLocalModel(model);
        QCOMPARE(ask->notReady(), QString());
        QCOMPARE(ask->send("Which programs are in the DriveVault suite? Answer in one short sentence."), QString());
        QTRY_VERIFY_WITH_TIMEOUT(!ask->busy(), 240000);
        const QVariantMap reply = ask->messages().at(1).toMap();
        fprintf(stderr, "REAL PAGE answer: %s\n", qPrintable(reply.value("text").toString().left(200)));
        QCOMPARE(reply.value("errorCode").toString(), QString());
        QVERIFY(reply.value("text").toString().size() > 10);
        ai.llama()->stop();
    }

    // ---- keys ------------------------------------------------------------------------------------------------------------------
    void keysAreEncrypted()
    {
        const QString stored = SecretStore::protect("sk-very-secret");
        QVERIFY(!stored.isEmpty());
        QVERIFY(!stored.contains("secret"));
        QCOMPARE(SecretStore::unprotect(stored), QString("sk-very-secret"));
        QVERIFY(SecretStore::unprotect("garbage").isEmpty());
        QVERIFY(SecretStore::protect("").isEmpty());
    }

    // ---- the two pages ----------------------------------------------------------------------------------------------------------
    void aConversationRunsAndIsSaved()
    {
        FakeServer server;
        server.body = sse({R"({"choices":[{"delta":{"content":"Answer one"}}]})", "[DONE]"});
        const QString dataDir = m_dir.filePath("hub");
        QString firstId;
        {
            dm::Hub::Options o;
            o.registryRoot = kRoot;
            o.dataDir = dataDir;
            dm::Hub hub(o);
            AiService ai(&hub, nullptr, false);
            auto* ask = qobject_cast<AiChannel*>(ai.ask());
            QCOMPARE(ask->notReady(), QString("noProvider"));
            QCOMPARE(ask->send("hello"), QString("noProvider"));
            ask->setProvider("openai");
            QCOMPARE(ask->notReady(), QString("noModel"));
            ai.setOpenAi(server.base() + "/v1", "gpt-test", "");           // this computer: no key needed
            QCOMPARE(ask->notReady(), QString());
            QCOMPARE(ask->send("  "), QString("empty"));
            QCOMPARE(ask->send("What is Master Pro?"), QString());
            QCOMPARE(ask->send("again"), QString("busy"));
            QSignalSpy idle(ask, &AiChannel::busyChanged);
            QTRY_VERIFY_WITH_TIMEOUT(!ask->busy(), 10000);
            const QVariantList msgs = ask->messages();
            QCOMPARE(msgs.size(), 2);
            QCOMPARE(msgs.at(1).toMap().value("text").toString(), QString("Answer one"));
            QVERIFY(!msgs.at(1).toMap().value("streaming").toBool());
            QCOMPARE(msgs.at(1).toMap().value("errorCode").toString(), QString());
            // the system prompt knows the suite
            QVERIFY(requestBody(server.lastRequest).value("messages").toArray().first().toObject().value("content").toString().contains("DriveVault"));
            QCOMPARE(ask->conversations().size(), 1);
            firstId = ask->currentId();
            QVERIFY(!firstId.isEmpty());
            // the agent page has its own conversations and only the CLI programs
            auto* agent = qobject_cast<AiChannel*>(ai.agent());
            QCOMPARE(agent->conversations().size(), 0);
            QCOMPARE(agent->providers(), QStringList({"claude", "codex"}));
            agent->setProvider("openai");                                  // not a provider of this page
            QCOMPARE(agent->provider(), QString());
            // the key is not in the settings file in clear text
            ai.setOpenAi(server.base() + "/v1", "gpt-test", "sk-plain-key");
            QFile f(dataDir + "/ai/settings.json");
            QVERIFY(f.open(QIODevice::ReadOnly));
            QVERIFY(!f.readAll().contains("sk-plain-key"));
            QVERIFY(ai.openai().value("keySet").toBool());
        }
        {   // a new start: the conversation and the settings come back
            dm::Hub::Options o;
            o.registryRoot = kRoot;
            o.dataDir = dataDir;
            dm::Hub hub(o);
            AiService ai(&hub, nullptr, false);
            auto* ask = qobject_cast<AiChannel*>(ai.ask());
            QCOMPARE(ask->provider(), QString("openai"));
            QCOMPARE(ask->conversations().size(), 1);
            ask->select(firstId);
            QCOMPARE(ask->messages().size(), 2);
            QCOMPARE(ai.openai().value("model").toString(), QString("gpt-test"));
            ask->remove(firstId);
            QCOMPARE(ask->conversations().size(), 0);
        }
    }
    void anErrorStaysInTheConversation()
    {
        FakeServer server;
        server.status = 429;
        server.contentType = "application/json";
        server.body = R"({"error":{"message":"rate limited"}})";
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hub2");
        dm::Hub hub(o);
        AiService ai(&hub, nullptr, false);
        auto* ask = qobject_cast<AiChannel*>(ai.ask());
        ask->setProvider("openai");
        ai.setOpenAi(server.base() + "/v1", "m", "");
        QCOMPARE(ask->send("hi"), QString());
        QTRY_VERIFY_WITH_TIMEOUT(!ask->busy(), 10000);
        const QVariantMap reply = ask->messages().at(1).toMap();
        QCOMPARE(reply.value("errorCode").toString(), QString("http429"));
        QCOMPARE(reply.value("errorDetail").toString(), QString("rate limited"));
    }
    void theAgentNeedsAFolder()
    {
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hub3");
        dm::Hub hub(o);
        AiService ai(&hub, nullptr, false);
        auto* agent = qobject_cast<AiChannel*>(ai.agent());
        agent->setProvider("claude");
        const QString why = agent->notReady();
        QVERIFY(why == "noProgram" || why == "noFolder");                  // depends on whether claude is installed on this PC
        agent->setFolder(m_dir.path());
        QCOMPARE(agent->folder(), QDir::cleanPath(m_dir.path()));
        QVERIFY(!agent->allowEdits());                                     // read only until the user says otherwise
        agent->setAllowEdits(true);
        QVERIFY(agent->allowEdits());
    }
};

QTEST_MAIN(TstAi)
#include "tst_ai.moc"
