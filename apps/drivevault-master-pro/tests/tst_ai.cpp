// The AI of Master Pro: stream parsers, HTTP chat (OpenAI style and Anthropic) against a fake server on this computer, the command line programs through a
// stand-in (stdin, events, cancel), model links and downloads, the key store and the two pages with their saved conversations. No real AI is called.
#include "Hub.h"
#include "ai/AgentTools.h"
#include "ai/AiService.h"
#include "ai/CommandPool.h"
#include "ai/Compactor.h"
#include "ai/CommandRunner.h"
#include "ai/GitInfo.h"
#include "ai/MessageModel.h"
#include "ai/LocalAgent.h"
#include "ai/WebTools.h"
#include "ai/HttpChat.h"
#include "ai/ModelDownloader.h"
#include "ai/ModelStore.h"
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
    bool dropping = false;                         // true: every request is dropped without an answer (the server died)
    QByteArray contentType = "text/event-stream";
    QByteArray body;
    QByteArray lastRequest;
    QList<QByteArray> queue;                       // one answer per request (when empty: `body`)
    QList<QByteArray> log;                         // every request, oldest first
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
                    log << data;
                    ++requests;
                    QByteArray answer = queue.isEmpty() ? body : queue.takeFirst();
                    if (dropping || answer == "DROP") {                         // the server dies without an answer: "Connection closed"
                        s->abort();
                        return;
                    }
                    int code = status;
                    if (answer.size() > 4 && answer.at(3) == '|' && QByteArray(answer.left(3)).toInt() >= 200) {     // "400|{...}": one answer with its own status
                        code = answer.left(3).toInt();
                        answer = answer.mid(4);
                    }
                    QByteArray out = "HTTP/1.1 " + QByteArray::number(code) + " X\r\nContent-Type: " + contentType + "\r\nContent-Length: " + QByteArray::number(answer.size())
                                     + "\r\nConnection: close\r\n\r\n" + answer;
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

// an OpenAI style answer of the (fake) model: text and / or tool calls
QJsonObject agentCall(const char* id, const char* name, const QJsonObject& args)
{
    return QJsonObject{{"id", QLatin1String(id)}, {"type", "function"},
                       {"function", QJsonObject{{"name", QLatin1String(name)}, {"arguments", QString::fromUtf8(QJsonDocument(args).toJson(QJsonDocument::Compact))}}}};
}

QByteArray agentReply(const QJsonArray& calls, const QString& content)
{
    QJsonObject message{{"role", "assistant"}};
    message["content"] = content.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(content);
    if (!calls.isEmpty())
        message["tool_calls"] = calls;
    return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"message", message}}}}}).toJson(QJsonDocument::Compact);
}

QByteArray replyWithReason(const QString& content, const QString& reason)
{
    QJsonObject message{{"role", "assistant"}, {"content", content}};
    return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"message", message}, {"finish_reason", reason}}}}}).toJson(QJsonDocument::Compact);
}

// ---- an answer as a stream of events (what llama-server sends with "stream": true) ----
QByteArray sseDelta(const QJsonObject& delta, const QString& finish = QString())
{
    QJsonObject choice{{"index", 0}, {"delta", delta}};
    choice["finish_reason"] = finish.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(finish);
    return "data: " + QJsonDocument(QJsonObject{{"choices", QJsonArray{choice}}}).toJson(QJsonDocument::Compact) + "\n\n";
}

QByteArray sseEnd(const QString& finish, int promptTokens, int completionTokens)
{
    return sseDelta({}, finish) + "data: " + QJsonDocument(QJsonObject{{"choices", QJsonArray()}, {"usage", QJsonObject{{"prompt_tokens", promptTokens}, {"completion_tokens", completionTokens}}}}).toJson(QJsonDocument::Compact)
           + "\n\ndata: [DONE]\n\n";
}

QByteArray sseText(const QStringList& parts, const QString& finish = QStringLiteral("stop"))
{
    QByteArray out = sseDelta({{"role", "assistant"}, {"content", QJsonValue(QJsonValue::Null)}});
    for (const QString& p : parts)
        out += sseDelta({{"content", p}});
    return out + sseEnd(finish, 100, 10);
}

QByteArray sseToolCall(const QString& name, const QStringList& argumentChunks, const QString& finish = QStringLiteral("tool_calls"))
{
    QByteArray out = sseDelta({{"role", "assistant"}, {"content", QJsonValue(QJsonValue::Null)}});
    for (int i = 0; i < argumentChunks.size(); ++i) {
        QJsonObject fn{{"arguments", argumentChunks.at(i)}};
        QJsonObject tc{{"index", 0}};
        if (i == 0) {
            tc["id"] = QStringLiteral("call1");
            tc["type"] = QStringLiteral("function");
            fn["name"] = name;
        }
        tc["function"] = fn;
        out += sseDelta({{"tool_calls", QJsonArray{tc}}});
    }
    return out + sseEnd(finish, 100, 10);
}

QByteArray errorBody(int status, const QString& message)
{
    return QByteArray::number(status) + "|" + QJsonDocument(QJsonObject{{"error", QJsonObject{{"message", message}}}}).toJson(QJsonDocument::Compact);
}

QString lastMessage(const QByteArray& request)
{
    return requestBody(request).value("messages").toArray().last().toObject().value("content").toString();
}

// runs the agent against a queue of answers (then `repeat` for ever) and returns how it ended, what it wrote and every request
struct RunResult {
    bool ok = false;
    QString code;
    QString text;
    QStringList activity;
    int requests = 0;
    QList<QByteArray> log;
};

RunResult runAgent(const QString& folder, const QList<QByteArray>& answers, const QByteArray& repeat, const std::function<void(LocalAgent::Config&)>& tweak = nullptr)
{
    FakeServer server;
    server.contentType = "application/json";
    server.queue = answers;
    server.body = repeat;
    QNetworkAccessManager net;
    LocalAgent agent(&net);
    RunResult r;
    QObject::connect(&agent, &LocalAgent::piece, [&](const QString& t) { r.text += t; });
    QObject::connect(&agent, &LocalAgent::activity, [&](const QString& a) { r.activity << a; });
    QSignalSpy done(&agent, &LocalAgent::finished);
    LocalAgent::Config c;
    c.baseUrl = server.base() + "/v1";
    c.folder = folder;
    c.maxSteps = 100;
    c.history = {{"user", "the task"}};
    if (tweak)
        tweak(c);
    if (!agent.start(c).isEmpty())
        return r;
    if (!done.wait(60000))
        return r;
    r.ok = done.at(0).at(0).toBool();
    r.code = done.at(0).at(1).toString();
    r.requests = server.requests;
    r.log = server.log;
    return r;
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
        auto t = readOpenAiEvent({QString(), R"({"choices":[{"delta":{"reasoning_content":"hmm"}}]})"});
        QVERIFY(t.thinking && t.text.isEmpty());                            // the model is reasoning: nothing to show
        QVERIFY(!readOpenAiEvent({QString(), R"({"choices":[{"delta":{"content":"Hi"}}]})"}).thinking);
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
        AiService ai(&hub);
        auto* ask = qobject_cast<AiChannel*>(ai.ask());
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

    // Choosing another model must refresh the vision offer (1.27.1: the settings kept "no vision file" after switching from a model that has none to Qwen3.5 4B).
    void visionOfferFollowsTheChosenModel()
    {
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hubvision");
        dm::Hub hub(o);
        AiService ai(&hub);
        for (const char* name : {"Qwen3.5-2B-Q4_K_M.gguf", "Qwen3.5-4B-Q4_K_M.gguf"}) {
            QFile f(m_dir.filePath(QString::fromLatin1(name)));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
        }
        ai.selectLocalModel(m_dir.filePath("Qwen3.5-2B-Q4_K_M.gguf"));
        QCOMPARE(ai.vision().value("offered").toBool(), false);
        QSignalSpy changed(&ai, &AiService::modelsChanged);
        ai.selectLocalModel(m_dir.filePath("Qwen3.5-4B-Q4_K_M.gguf"));
        QVERIFY(changed.count() >= 1);
        QCOMPARE(ai.vision().value("offered").toBool(), true);
    }

    // ---- the folder dialog gives a file: address; the page must accept it (1.12.0 did not: Send stayed disabled) ---------------------------
    void folderFromTheDialogIsAPath()
    {
        const QString dir = m_dir.filePath("dialog folder");                  // a space, like "C:\My Project"
        QDir().mkpath(dir);
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hubdialog");
        dm::Hub hub(o);
        AiService ai(&hub);
        auto* agent = qobject_cast<AiChannel*>(ai.agent());
        agent->setFolder(QUrl::fromLocalFile(dir).toString());                // file:///C:/.../dialog%20folder
        QCOMPARE(QDir::cleanPath(agent->folder()), QDir::cleanPath(dir));
        agent->setFolder(QString());
        agent->setFolder(QStringLiteral("file:/") + QDir::cleanPath(dir));    // what 1.12.0 saved
        QCOMPARE(QDir::cleanPath(agent->folder()), QDir::cleanPath(dir));
        QVERIFY(QFileInfo(agent->folder()).isDir());
    }

    // ---- agent tools: only inside the folder ------------------------------------------------------------------------------------------
    void agentToolsStayInTheFolder()
    {
        QDir().mkpath(m_dir.filePath("work/sub"));
        auto write = [&](const QString& rel, const QByteArray& data) {
            QFile f(m_dir.filePath("work/" + rel));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
        };
        write("notes.txt", "alpha\nBeta line\ngamma\n");
        write("sub/deep.txt", "needle in a haystack\n");
        write("pic.bin", QByteArray("\0\1\2\3", 4));
        QFile outside(m_dir.filePath("secret.txt"));
        QVERIFY(outside.open(QIODevice::WriteOnly));
        outside.write("TOP SECRET");
        outside.close();

        AgentTools ro(m_dir.filePath("work"), false);
        QCOMPARE(ro.specs().size(), 4);                                    // list, read, search, glob: no write_file / edit_file when edits are not allowed
        auto list = ro.run("list_files", {});
        QVERIFY(!list.error && list.text.contains("notes.txt") && list.text.contains("sub/"));
        QVERIFY(ro.run("read_file", {{"path", "notes.txt"}}).text.contains("Beta line"));
        QVERIFY(ro.run("read_file", {{"path", "sub/deep.txt"}}).text.contains("needle"));
        QVERIFY(ro.run("read_file", {{"path", "pic.bin"}}).error);          // binary
        auto found = ro.run("search_text", {{"query", "NEEDLE"}});
        QVERIFY(found.text.contains("sub/deep.txt:1"));
        // leaving the folder is refused in every spelling
        for (const char* bad : {"../secret.txt", "..\\secret.txt", "sub/../../secret.txt", "C:/Windows/win.ini", "/etc/passwd", "\\\\server\\share\\x", "notes.txt:stream"}) {
            const auto r = ro.run("read_file", {{"path", bad}});
            QVERIFY2(r.error && !r.text.contains("SECRET"), bad);
        }
        QVERIFY(ro.run("list_files", {{"path", ".."}}).error);
        // read only: writing is refused even when the model asks for it
        QVERIFY(ro.run("write_file", {{"path", "x.txt"}, {"content", "x"}}).error);
        QVERIFY(!QFileInfo::exists(m_dir.filePath("work/x.txt")));

        AgentTools rw(m_dir.filePath("work"), true);
        QCOMPARE(rw.specs().size(), 7);
        auto w = rw.run("write_file", {{"path", "sub/new.txt"}, {"content", "hello"}});
        QVERIFY2(!w.error, qPrintable(w.text));
        QCOMPARE(rw.run("read_file", {{"path", "sub/new.txt"}}).text, QString("hello"));
        // replacing a file saves the old one first
        w = rw.run("write_file", {{"path", "notes.txt"}, {"content", "replaced"}});
        QVERIFY2(!w.error && w.text.contains(".drivevault-backup"), qPrintable(w.text));
        const auto backups = QDir(m_dir.filePath("work/.drivevault-backup")).entryList({"notes.txt.*.bak"}, QDir::Files);
        QCOMPARE(backups.size(), 1);
        QFile old(m_dir.filePath("work/.drivevault-backup/" + backups.first()));
        QVERIFY(old.open(QIODevice::ReadOnly));
        QVERIFY(old.readAll().contains("Beta line"));
        QVERIFY(rw.run("write_file", {{"path", "../escape.txt"}, {"content", "x"}}).error);
        QVERIFY(!QFileInfo::exists(m_dir.filePath("escape.txt")));
        QVERIFY(rw.run("write_file", {{"path", "nofolder/x.txt"}, {"content", "x"}}).error);        // no folder is created on the way
        QVERIFY(rw.run("run_command", {{"command", "dir"}}).error);                                  // there is no such tool
    }

    // ---- web tools ------------------------------------------------------------------------------------------------------------------
    void webAddressesAreChecked()
    {
        QVERIFY(WebTools::checkUrl(QUrl("https://example.com/page")).isEmpty());
        for (const char* bad : {"http://localhost/x", "http://127.0.0.1:8080/", "https://192.168.1.10/", "http://10.0.0.5/", "http://169.254.169.254/latest/meta-data",
                                "https://[::1]/", "http://[fd00::1]/", "http://[::ffff:127.0.0.1]/", "ftp://example.com/", "file:///C:/Windows/win.ini",
                                "https://user:pw@example.com/", "https://example.com:8443/", "http://intranet/", "https://printer.local/", "javascript:alert(1)"})
            QVERIFY2(!WebTools::checkUrl(QUrl(bad)).isEmpty(), bad);
        QVERIFY(WebTools::addressIsPublic(QHostAddress("8.8.8.8")));
        QVERIFY(WebTools::addressIsPublic(QHostAddress("2606:4700:4700::1111")));
        QVERIFY(!WebTools::addressIsPublic(QHostAddress("172.20.1.1")));
        QVERIFY(!WebTools::addressIsPublic(QHostAddress("100.64.0.1")));
        QVERIFY(!WebTools::addressIsPublic(QHostAddress("::ffff:10.1.2.3")));
    }
    void webResultsAndPagesAreParsed()
    {
        const QString html =
            "<div class=\"result\"><a rel=\"nofollow\" class=\"result__a\" href=\"//duckduckgo.com/l/?uddg=https%3A%2F%2Fhuggingface.co%2Funsloth%2FQwen3.5%2D4B%2DGGUF&amp;rut=abc\">unsloth/Qwen3.5-4B-GGUF &middot; Hugging Face</a>"
            "<a class=\"result__snippet\" href=\"//duckduckgo.com/l/?uddg=x\">Qwen3.5 <b>4B</b> in GGUF &amp; more</a></div>"
            "<div class=\"result\"><a class=\"result__a\" href=\"//duckduckgo.com/y.js?ad_domain=shop.example\">An advertisement</a></div>"
            "<div class=\"result\"><a class=\"result__a\" href=\"https://example.org/direct\">Direct link</a></div>";
        const auto hits = WebTools::parseSearch(html);
        QCOMPARE(hits.size(), 2);                                         // the advertisement (a duckduckgo address) is not a result
        QCOMPARE(hits.at(0).url, QString("https://huggingface.co/unsloth/Qwen3.5-4B-GGUF"));
        QVERIFY(hits.at(0).title.startsWith("unsloth/Qwen3.5-4B-GGUF"));
        QCOMPARE(hits.at(0).snippet, QString("Qwen3.5 4B in GGUF & more"));
        QVERIFY(WebTools::formatHits(hits).contains("1. unsloth"));
        QCOMPARE(WebTools::formatHits({}), QString("No results."));
        const QString text = WebTools::htmlToText("<html><head><title>t</title><style>p{}</style></head><body><nav>menu</nav><h1>Title</h1><p>One &amp; two</p>"
                                                  "<script>evil()</script><p>Three</p></body></html>", 1000);
        QCOMPARE(text, QString("Title\nOne & two\nThree"));
        QVERIFY(WebTools::htmlToText(QString(50000, 'x'), 100).size() < 200);
    }
    void webToolsRefuseThisComputer()
    {
        QNetworkAccessManager net;
        WebTools web(&net);
        QString out;
        bool err = false, called = false;
        web.run("fetch_page", {{"url", "http://127.0.0.1/secret"}}, [&](const QString& t, bool e) { out = t; err = e; called = true; });
        QVERIFY(called && err);
        QVERIFY(out.contains("not reachable"));
        called = false;
        web.run("web_search", {{"query", " "}}, [&](const QString& t, bool e) { out = t; err = e; called = true; });
        QVERIFY(called && err);
        QVERIFY(WebTools::handles("web_search") && WebTools::handles("fetch_page") && !WebTools::handles("read_file"));
    }
    // the real Internet: only when asked for (DM_TEST_WEB=1)
    void realWebSearchAndPage()
    {
        if (qEnvironmentVariable("DM_TEST_WEB") != QLatin1String("1"))
            QSKIP("set DM_TEST_WEB=1 to use the real Internet");
        QNetworkAccessManager net;
        WebTools web(&net);
        QString out;
        bool err = true, called = false;
        web.run("web_search", {{"query", "Qt 6.8 release notes"}}, [&](const QString& t, bool e) { out = t; err = e; called = true; });
        QTRY_VERIFY_WITH_TIMEOUT(called, 30000);
        QVERIFY2(!err && out.contains("http"), qPrintable(out.left(200)));
        fprintf(stderr, "REAL SEARCH: %s\n", qPrintable(out.left(300)));
        called = false;
        web.run("fetch_page", {{"url", "https://huggingface.co/unsloth/Qwen3.5-4B-GGUF"}}, [&](const QString& t, bool e) { out = t; err = e; called = true; });
        QTRY_VERIFY_WITH_TIMEOUT(called, 30000);
        QVERIFY2(!err && out.contains("Qwen3.5"), qPrintable(out.left(200)));
        fprintf(stderr, "REAL PAGE: %s\n", qPrintable(out.left(200).simplified()));
    }

    // ---- the local agent: tools, results, answer ------------------------------------------------------------------------------------
    void localAgentRunsToolsAndAnswers()
    {
        QDir().mkpath(m_dir.filePath("agentwork"));
        QFile f(m_dir.filePath("agentwork/todo.txt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("buy milk\n");
        f.close();
        FakeServer server;
        server.contentType = "application/json";
        server.queue << agentReply({agentCall("c1", "list_files", {}), agentCall("c2", "read_file", {{"path", "todo.txt"}})}, QString())
                     << agentReply({}, QStringLiteral("The file todo.txt says: buy milk."));
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QString text;
        QStringList activity;
        connect(&agent, &LocalAgent::piece, [&](const QString& t) { text += t; });
        connect(&agent, &LocalAgent::activity, [&](const QString& a) { activity << a; });
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.filePath("agentwork");
        c.system = "sys";
        c.history = {{"user", "what is in my folder?"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(done.wait(15000));
        QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        QCOMPARE(text, QString("The file todo.txt says: buy milk."));
        QCOMPARE(activity, QStringList({"List", "Read: todo.txt"}));
        QCOMPARE(server.requests, 2);
        // the second request carries the assistant's calls and both tool results
        const QJsonArray msgs = requestBody(server.log.at(1)).value("messages").toArray();
        QVERIFY(msgs.size() >= 5);
        QCOMPARE(msgs.at(msgs.size() - 1).toObject().value("role").toString(), QString("tool"));
        QVERIFY(msgs.at(msgs.size() - 1).toObject().value("content").toString().contains("buy milk"));
        QVERIFY(msgs.at(msgs.size() - 2).toObject().value("content").toString().contains("todo.txt"));
        // the request offers the tools; read only = no write_file
        const QJsonArray tools = requestBody(server.log.at(0)).value("tools").toArray();
        QCOMPARE(tools.size(), 5);                                         // the four read tools + todo_write
        // a refusal: it needs a folder or the web, and only this computer's server
        LocalAgent other(&net);
        c.folder.clear();
        c.web = false;
        QCOMPARE(other.start(c), QString("noTools"));
        c.folder = m_dir.path();
        c.baseUrl = "http://example.com/v1";
        QCOMPARE(other.start(c), QString("badUrl"));
    }
    void localAgentStopsAfterTooManySteps()
    {
        FakeServer server;
        server.contentType = "application/json";
        server.body = R"({"choices":[{"message":{"role":"assistant","content":null,"tool_calls":[{"id":"c","type":"function","function":{"name":"list_files","arguments":"{}"}}]}}]})";
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.path();
        c.history = {{"user", "loop"}};
        c.maxSteps = 3;
        QCOMPARE(agent.start(c), QString());
        QVERIFY(done.wait(15000));
        QCOMPARE(done.at(0).at(1).toString(), QString("tooManySteps"));
        QCOMPARE(server.requests, 3);
    }
    // a small model that searches for ever: the same call twice and more than four web calls get a "go on" message instead of the web
    void webCallsAreLimited()
    {
        FakeServer server;
        server.contentType = "application/json";
        auto fetch = [](const char* path) {                        // built with QJson: moc cannot read a raw string with escaped quotes
            const QString arguments = QString::fromUtf8(QJsonDocument(QJsonObject{{"url", QStringLiteral("http://127.0.0.1:1/") + QLatin1String(path)}}).toJson(QJsonDocument::Compact));
            const QJsonObject call{{"id", "c"}, {"type", "function"}, {"function", QJsonObject{{"name", "fetch_page"}, {"arguments", arguments}}}};
            const QJsonObject message{{"role", "assistant"}, {"content", QJsonValue()}, {"tool_calls", QJsonArray{call}}};
            return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"message", message}}}}}).toJson(QJsonDocument::Compact);
        };
        server.queue << fetch("a") << fetch("a") << fetch("b") << fetch("c") << fetch("d") << fetch("e")
                     << R"({"choices":[{"message":{"role":"assistant","content":"Done without more searching."}}]})";
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.path();
        c.web = true;
        c.history = {{"user", "search"}};
        c.maxSteps = 12;
        QCOMPARE(agent.start(c), QString());
        QVERIFY(done.wait(30000));
        QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        QCOMPARE(server.requests, 7);
        QStringList results;
        for (const QJsonValue& m : requestBody(server.log.at(6)).value("messages").toArray())
            if (m.toObject().value("role").toString() == QLatin1String("tool"))
                results << m.toObject().value("content").toString();
        QCOMPARE(results.size(), 6);
        QVERIFY(results.at(1).contains("do not repeat it"));        // the second call of "a"
        QVERIFY(results.at(5).contains("web limit"));               // the fifth different call
        QVERIFY(!results.at(4).contains("web limit"));              // the fourth different call still ran
    }
    // the answer was cut off inside a tool call (a long file): the server says 500, the agent asks again for a shorter one instead of failing
    void aCutOffToolCallIsAskedAgain()
    {
        FakeServer server;
        server.contentType = "application/json";
        const QByteArray cutOff = "500|" + QJsonDocument(QJsonObject{{"error", QJsonObject{{"message", "Failed to parse tool call arguments as JSON: missing closing quote"}}}}).toJson(QJsonDocument::Compact);
        server.queue << cutOff << agentReply({}, QStringLiteral("Made it shorter."));
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.path();
        c.history = {{"user", "write a big page"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(done.wait(15000));
        QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        QCOMPARE(server.requests, 2);
        const QJsonArray msgs = requestBody(server.log.at(1)).value("messages").toArray();
        QVERIFY(msgs.last().toObject().value("content").toString().contains("cut off"));
        // an ordinary 500 is still an error, and three cut-offs in a row stop
        FakeServer bad;
        bad.contentType = "application/json";
        bad.status = 500;
        bad.body = QJsonDocument(QJsonObject{{"error", QJsonObject{{"message", "out of memory"}}}}).toJson(QJsonDocument::Compact);
        LocalAgent agent2(&net);
        QSignalSpy done2(&agent2, &LocalAgent::finished);
        c.baseUrl = bad.base() + "/v1";
        QCOMPARE(agent2.start(c), QString());
        QVERIFY(done2.wait(15000));
        QCOMPARE(done2.at(0).at(1).toString(), QString("http500"));
        QCOMPARE(bad.requests, 1);
        FakeServer endless;
        endless.contentType = "application/json";
        endless.status = 500;
        endless.body = cutOff.mid(4);
        LocalAgent agent3(&net);
        QSignalSpy done3(&agent3, &LocalAgent::finished);
        c.baseUrl = endless.base() + "/v1";
        QCOMPARE(agent3.start(c), QString());
        QVERIFY(done3.wait(15000));
        QCOMPARE(done3.at(0).at(1).toString(), QString("http500"));
        QCOMPARE(endless.requests, 3);                                                       // the first + two more
    }
    // ---- a job that goes on until it is done ---------------------------------------------------------------------------------------------
    void aLongFileIsReadInParts()
    {
        QDir().mkpath(m_dir.filePath("parts"));
        QFile f(m_dir.filePath("parts/long.txt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        for (int i = 1; i <= 1000; ++i)
            f.write(QByteArray("line ") + QByteArray::number(i) + "\n");
        f.close();
        QFile small(m_dir.filePath("parts/small.txt"));
        QVERIFY(small.open(QIODevice::WriteOnly));
        small.write("a\nb\n");
        small.close();
        AgentTools tools(m_dir.filePath("parts"), false);
        const QString first = tools.run("read_file", {{"path", "long.txt"}}).text;
        QVERIFY(first.startsWith("[lines 1-600 of 1000] Read on with start_line=601."));
        QVERIFY(first.contains("line 600\n") && !first.contains("line 601\n"));
        const QString second = tools.run("read_file", {{"path", "long.txt"}, {"start_line", 601}}).text;
        QVERIFY(second.startsWith("[lines 601-1000 of 1000]") && !second.contains("Read on"));
        QVERIFY(second.contains("line 1000\n"));
        const QString middle = tools.run("read_file", {{"path", "long.txt"}, {"start_line", 10}, {"max_lines", 3}}).text;
        QVERIFY(middle.startsWith("[lines 10-12 of 1000]") && middle.contains("line 11\n"));
        QCOMPARE(tools.run("read_file", {{"path", "small.txt"}}).text, QString("a\nb\n"));      // a short file comes back exactly as it is
    }
    void aModelThatRepeatsItselfIsToldAndThenStopped()
    {
        const QByteArray repeat = agentReply({agentCall("c", "list_files", {})}, QString());
        const RunResult r = runAgent(m_dir.path(), {}, repeat);
        QVERIFY(!r.ok);
        QCOMPARE(r.code, QString("stuck"));
        QCOMPARE(r.requests, 8);                                                                // 2 real calls, 5 refused, the 8th ends the job
        QVERIFY(lastMessage(r.log.at(3)).contains("times in a row"));
        QVERIFY(!lastMessage(r.log.at(1)).contains("times in a row"));                         // the second call is still run
    }
    void anAnswerCutByTheLengthLimitGoesOn()
    {
        const RunResult r = runAgent(m_dir.path(), {replyWithReason("part one ", "length"), replyWithReason("part two", "stop")}, {});
        QVERIFY2(r.ok, qPrintable(r.code));
        QCOMPARE(r.text, QString("part onepart two"));                                          // one text, no blank line between the pieces
        QCOMPARE(r.requests, 2);
        QVERIFY(lastMessage(r.log.at(1)).contains("Go on"));
    }
    void aSilentAnswerIsAskedAgain()
    {
        const RunResult r = runAgent(m_dir.path(), {replyWithReason("", "stop"), replyWithReason("Done.", "stop")}, {});
        QVERIFY2(r.ok, qPrintable(r.code));
        QCOMPARE(r.text, QString("Done."));
        QCOMPARE(r.requests, 2);
        const RunResult never = runAgent(m_dir.path(), {}, replyWithReason("", "stop"));                      // a model that stays silent: an error in the end
        QVERIFY(!never.ok);
        QCOMPARE(never.code, QString("empty"));
    }
    void theJobIsNotDoneWhileTheTaskListIsOpen()
    {
        const QJsonArray open{QJsonObject{{"content", "write page"}, {"status", "in_progress"}}};
        const QJsonArray closed{QJsonObject{{"content", "write page"}, {"status", "completed"}}};
        const RunResult r = runAgent(m_dir.path(), {agentReply({agentCall("t1", "todo_write", {{"todos", open}})}, QString()), agentReply({}, QStringLiteral("I think that is all.")),
                                      agentReply({agentCall("t2", "todo_write", {{"todos", closed}})}, QString()), agentReply({}, QStringLiteral("All done."))}, {});
        QVERIFY2(r.ok, qPrintable(r.code));
        QCOMPARE(r.requests, 4);
        QVERIFY(lastMessage(r.log.at(2)).contains("unfinished") && lastMessage(r.log.at(2)).contains("write page"));
        QVERIFY(r.text.endsWith("All done."));
    }
    void aLongJobStaysInsideTheWindow()
    {
        QDir().mkpath(m_dir.filePath("win"));
        QFile f(m_dir.filePath("win/big.txt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        for (int i = 1; i <= 3000; ++i)
            f.write(QByteArray(40, 'x') + QByteArray::number(i) + "\n");
        f.close();
        QList<QByteArray> answers;
        for (int i = 0; i < 12; ++i)                                                            // twelve different 100 line pieces of the file
            answers << agentReply({agentCall("r", "read_file", {{"path", "big.txt"}, {"start_line", 1 + i * 100}, {"max_lines", 100}})}, QString());
        answers << agentReply({}, QStringLiteral("Read it all."));
        const RunResult r = runAgent(m_dir.filePath("win"), answers, {}, [&](LocalAgent::Config& c) {
            c.contextTokens = 3000;                                                            // a small window: about 5000 characters are left for the conversation
        });
        QVERIFY2(r.ok, qPrintable(r.code));
        QCOMPARE(r.requests, 13);
        QVERIFY(r.activity.join("\n").contains("Made room"));
        const QByteArray last = r.log.last();
        const QJsonArray msgs = requestBody(last).value("messages").toArray();
        QCOMPARE(msgs.first().toObject().value("content").toString(), QString("the task"));    // the task is never dropped
        // what was dropped is not forgotten: a short list of the steps stays right after the task
        QString digest;
        for (const QJsonValue& m : msgs)
            if (m.toObject().value("content").toString().startsWith("Earlier in this job"))
                digest = m.toObject().value("content").toString();
        QVERIFY2(!digest.isEmpty(), "no note about the dropped steps");
        QVERIFY(digest.contains("read_file big.txt (from line 1)"));
        QVERIFY2(last.size() < 16000, qPrintable(QString::number(last.size())));              // twelve whole 4000 character answers would be about 50000
        QVERIFY(msgs.size() <= 12);
    }
    void aPromptThatDoesNotFitIsShortenedAndAskedAgain()
    {
        const RunResult r = runAgent(m_dir.path(), {errorBody(400, "request (9000 tokens) exceeds the available context size (8192 tokens)"), replyWithReason("Fits now.", "stop")}, {});
        QVERIFY2(r.ok, qPrintable(r.code));
        QCOMPARE(r.requests, 2);
        QVERIFY(r.activity.join("\n").contains("too long"));
        const RunResult other = runAgent(m_dir.path(), {errorBody(400, "bad request")}, {});                  // another 400 is still an error
        QVERIFY(!other.ok);
        QCOMPARE(other.code, QString("http400"));
    }
    // ---- the step list of the page ---------------------------------------------------------------------------------------------------------
    void everyCallIsALineOfTheStepList()
    {
        QDir().mkpath(m_dir.filePath("steps"));
        QFile f(m_dir.filePath("steps/a.txt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("hello");
        f.close();
        FakeServer server;
        server.contentType = "application/json";
        server.queue << agentReply({agentCall("c1", "list_files", {}), agentCall("c2", "read_file", {{"path", "a.txt"}}), agentCall("c3", "read_file", {{"path", "missing.txt"}})}, QString())
                     << agentReply({}, QStringLiteral("Done."));
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        struct Ev { int no; QString kind, target, state, note; };
        QList<Ev> events;
        QObject::connect(&agent, &LocalAgent::stepEvent, [&](int no, const QString& kind, const QString& target, const QString& state, const QString& note) {
            events << Ev{no, kind, target, state, note};
        });
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.filePath("steps");
        c.history = {{"user", "look"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(done.wait(15000));
        QVERIFY(done.at(0).at(0).toBool());
        // each call: running, then done (or error with the reason)
        QCOMPARE(events.size(), 6);
        QCOMPARE(events.at(0).kind, QString("list"));
        QCOMPARE(events.at(0).state, QString("running"));
        QCOMPARE(events.at(1).state, QString("done"));
        QCOMPARE(events.at(1).no, events.at(0).no);                                              // the same line changes its state
        QCOMPARE(events.at(2).kind, QString("read"));
        QCOMPARE(events.at(2).target, QString("a.txt"));
        QCOMPARE(events.at(3).state, QString("done"));
        QCOMPARE(events.at(4).target, QString("missing.txt"));
        QCOMPARE(events.at(5).state, QString("error"));
        QVERIFY(events.at(5).note.contains("not found"));
        QVERIFY(events.at(2).no > events.at(0).no && events.at(4).no > events.at(2).no);
    }
    void aCommandShowsThatItWaitsForYou()
    {
        QDir().mkpath(m_dir.filePath("stepcmd"));
        FakeServer server;
        server.contentType = "application/json";
        server.queue << agentReply({agentCall("r1", "run_command", {{"command", "echo hi"}})}, QString()) << agentReply({}, QStringLiteral("ok"));
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QStringList states;
        QObject::connect(&agent, &LocalAgent::stepEvent, [&](int, const QString&, const QString&, const QString& state, const QString&) { states << state; });
        QSignalSpy asked(&agent, &LocalAgent::commandApprovalNeeded);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.filePath("stepcmd");
        c.allowEdits = true;
        c.approveWrites = true;
        c.allowCommands = true;
        c.history = {{"user", "run"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(asked.wait(15000));
        QCOMPARE(states, QStringList({"running", "waiting"}));                                  // it waits for the user: the line says so
        agent.answerApproval(true);
        QVERIFY(done.wait(30000));
        QCOMPARE(states, QStringList({"running", "waiting", "running", "done"}));
    }
    void theStepListIsKeptWithTheAnswerAndSurvivesARestart()
    {
        QDir().mkpath(m_dir.filePath("stepkeep"));
        QFile model(m_dir.filePath("step-model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();
        FakeServer server;
        server.contentType = "application/json";
        server.queue << agentReply({agentCall("c1", "list_files", {})}, QString()) << agentReply({}, QStringLiteral("One folder."));
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hubstep");
        dm::Hub hub(o);
        QString conversation;
        {
            AiService ai(&hub);
            if (!ai.engineFound())
                QSKIP("no llama.cpp engine on this PC");
            auto* agent = qobject_cast<AiChannel*>(ai.agent());
            ai.selectLocalModel(m_dir.filePath("step-model.gguf"));
            ai.llama()->useExternalServer(server.base(), "");
            ai.setWebEnabled(false);
            agent->setFolder(m_dir.filePath("stepkeep"));
            QCOMPARE(agent->send("what is here?"), QString());
            QTRY_VERIFY_WITH_TIMEOUT(!agent->busy(), 10000);
            const QVariantMap reply = agent->messages().at(1).toMap();
            const QVariantList steps = reply.value("stepList").toList();
            QCOMPARE(steps.size(), 1);
            QCOMPARE(steps.at(0).toMap().value("kind").toString(), QString("list"));
            QCOMPARE(steps.at(0).toMap().value("state").toString(), QString("done"));
            QVERIFY(reply.value("durationMs").toDouble() > 0);
            conversation = agent->conversations().first().toMap().value("id").toString();
        }
        AiService again(&hub);                                                                  // the program is started again: the list is read from the file
        auto* agent2 = qobject_cast<AiChannel*>(again.agent());
        agent2->select(conversation);
        const QVariantList steps2 = agent2->messages().at(1).toMap().value("stepList").toList();
        QCOMPARE(steps2.size(), 1);
        QCOMPARE(steps2.at(0).toMap().value("kind").toString(), QString("list"));
        QCOMPARE(steps2.at(0).toMap().value("state").toString(), QString("done"));
    }
    // ---- git of the working folder ---------------------------------------------------------------------------------------------------------
    void gitStatusIsParsed()
    {
        const QVariantMap a = GitInfo::parseStatus(QStringLiteral("## main...origin/main [ahead 2, behind 1]\n M src/a.cpp\nA  new.txt\n?? notes.md\nD  gone.txt\nR  old.txt -> renamed.txt\nUU conflict.txt\n"));
        QCOMPARE(a.value("branch").toString(), QString("main"));
        QCOMPARE(a.value("ahead").toInt(), 2);
        QCOMPARE(a.value("behind").toInt(), 1);
        QCOMPARE(a.value("count").toInt(), 6);
        QStringList statuses, paths;
        for (const QVariant& f : a.value("files").toList()) {
            statuses << f.toMap().value("status").toString();
            paths << f.toMap().value("path").toString();
        }
        QCOMPARE(statuses, QStringList({"M", "A", "?", "D", "R", "U"}));
        QVERIFY(paths.contains("renamed.txt") && !paths.contains("old.txt"));
        const QVariantMap b = GitInfo::parseStatus(QStringLiteral("## No commits yet on trunk\n?? a.txt\n"));
        QCOMPARE(b.value("branch").toString(), QString("trunk"));
        QCOMPARE(b.value("count").toInt(), 1);
        const QVariantMap c = GitInfo::parseStatus(QStringLiteral("## HEAD (no branch)\n"));
        QVERIFY(c.value("detached").toBool());
        QCOMPARE(c.value("count").toInt(), 0);
        QVariantMap d = GitInfo::parseStatus(QStringLiteral("## main\n M src/a.cpp\n M bin.dat\n D gone.txt\n"));
        GitInfo::applyNumstat(d, QStringLiteral("3\t1\tsrc/a.cpp\n-\t-\tbin.dat\n0\t5\tgone.txt\n"));
        QCOMPARE(d.value("added").toInt(), 3);
        QCOMPARE(d.value("removed").toInt(), 6);
        QCOMPARE(d.value("files").toList().at(0).toMap().value("added").toInt(), 3);
        QCOMPARE(d.value("files").toList().at(1).toMap().value("added").toInt(), -1);          // a binary file: no numbers
    }
    void gitReportsARealFolder()
    {
        if (GitInfo::gitExecutable().isEmpty())
            QSKIP("git is not on this PC");
        const QString dir = m_dir.filePath("repo");
        QDir().mkpath(dir);
        auto git = [&](const QStringList& args) {
            QProcess p;
            p.setWorkingDirectory(dir);
            p.start(GitInfo::gitExecutable(), QStringList{"-c", "user.name=t", "-c", "user.email=t@example.com"} + args);
            return p.waitForFinished(20000) && p.exitCode() == 0;
        };
        auto put = [&](const QString& name, const QByteArray& data) {
            QFile f(dir + "/" + name);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
        };
        QVERIFY(git({"init", "--initial-branch=main"}));
        put("a.txt", "one\ntwo\n");
        QVERIFY(git({"add", "."}));
        QVERIFY(git({"commit", "-m", "first"}));
        put("a.txt", "one\nthree\nfour\n");                                                    // 2 lines added, 1 removed
        put("new.txt", "x");                                                                  // not tracked
        GitInfo info;
        QSignalSpy changed(&info, &GitInfo::changed);
        info.refresh(dir);
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 20000);
        const QVariantMap m = info.info();
        QCOMPARE(m.value("state").toString(), QString("ok"));
        QCOMPARE(m.value("branch").toString(), QString("main"));
        QCOMPARE(m.value("count").toInt(), 2);
        QCOMPARE(m.value("added").toInt(), 2);
        QCOMPARE(m.value("removed").toInt(), 1);
        // a folder that is not a repository, and no folder
        QDir().mkpath(m_dir.filePath("plain"));
        const int before = changed.count();
        info.refresh(m_dir.filePath("plain"));
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() > before, 20000);
        QCOMPARE(info.info().value("state").toString(), QString("notRepo"));
        info.refresh(QString());
        QCOMPARE(info.info().value("state").toString(), QString("none"));
        // nothing in the repository was changed by looking (the file is as we left it)
        QCOMPARE(QFile(dir + "/a.txt").size(), qint64(15));                                    // "one\nthree\nfour\n"
    }
    void conversationsAreGroupedByTheirFolder()
    {
        const QString a = m_dir.filePath("groupA"), b = m_dir.filePath("groupB");
        QDir().mkpath(a);
        QDir().mkpath(b);
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hubgroup");
        dm::Hub hub(o);
        QString idA2, idB;
        {
            AiService ai(&hub);
            auto* agent = qobject_cast<AiChannel*>(ai.agent());
            agent->setFolder(a);
            QCOMPARE(agent->send("/help"), QString());                                        // a conversation without the model: it is saved in the folder of the moment
            QTest::qWait(8);
            agent->setFolder(b);                                                              // another folder: the next message begins a new conversation
            QCOMPARE(agent->send("/help"), QString());
            idB = agent->currentId();
            QTest::qWait(8);
            agent->setFolder(a);
            QCOMPARE(agent->send("/help"), QString());
            idA2 = agent->currentId();
            const QVariantList list = agent->conversations();
            QCOMPARE(list.size(), 3);
            // the group of the newest conversation first, the newest first inside a group
            QCOMPARE(list.at(0).toMap().value("id").toString(), idA2);
            QCOMPARE(list.at(0).toMap().value("group").toString(), QString("groupA"));
            QCOMPARE(list.at(1).toMap().value("group").toString(), QString("groupA"));
            QCOMPARE(list.at(2).toMap().value("id").toString(), idB);
            QCOMPARE(list.at(2).toMap().value("group").toString(), QString("groupB"));
            QCOMPARE(QDir::cleanPath(list.at(2).toMap().value("folder").toString()), QDir::cleanPath(b));
            // opening a conversation of another folder moves the work there
            agent->select(idB);
            QCOMPARE(QDir::cleanPath(agent->folder()), QDir::cleanPath(b));
            // the "+" of a group
            agent->newConversationIn(a);
            QCOMPARE(QDir::cleanPath(agent->folder()), QDir::cleanPath(a));
            QVERIFY(agent->currentId().isEmpty());
            QVERIFY(agent->messages().isEmpty());
            // the meter of the window
            agent->select(idA2);
            const QVariantMap ctx = agent->context();
            QCOMPARE(ctx.value("window").toInt(), LlamaServer::kContextTokens);
            QVERIFY(ctx.value("used").toInt() > 0 && ctx.value("percent").toInt() >= 0 && ctx.value("percent").toInt() <= 100);
            QVERIFY(!ctx.value("exact").toBool());                                              // no request yet: an estimate
        }
        AiService again(&hub);                                                                  // the group survives a restart
        auto* agent2 = qobject_cast<AiChannel*>(again.agent());
        const QVariantList list2 = agent2->conversations();
        QCOMPARE(list2.size(), 3);
        QCOMPARE(list2.at(0).toMap().value("group").toString(), QString("groupA"));
    }
    // ---- the answer is read as a stream: live progress, a too long write is stopped and its written part kept -------------------------------
    void aStreamedAnswerIsShownWhileItComes()
    {
        FakeServer server;
        server.contentType = "text/event-stream";
        server.queue << sseText({"Hel", "lo ", "world"});
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QStringList pieces, phases;
        int promptTokens = 0;
        QObject::connect(&agent, &LocalAgent::piece, [&](const QString& t) { pieces << t; });
        QObject::connect(&agent, &LocalAgent::generating, [&](const QString& phase, const QString&, const QString&, int) { phases << phase; });
        QObject::connect(&agent, &LocalAgent::usage, [&](int p, int) { promptTokens = p; });
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.path();
        c.history = {{"user", "hi"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(done.wait(15000));
        QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        QCOMPARE(pieces.join(QString()), QString("Hello world"));
        QVERIFY(pieces.size() >= 2);                                                            // it came in pieces, not as one block
        QCOMPARE(promptTokens, 100);                                                            // what the server counted
        QCOMPARE(phases.first(), QString("waiting"));                                           // the prompt is read first
        QVERIFY(phases.contains("writing"));
        QCOMPARE(phases.last(), QString());                                                     // and the line is cleared at the end
        const QJsonObject body = requestBody(server.log.at(0));
        QVERIFY(body.value("stream").toBool());
        QVERIFY(body.value("stream_options").toObject().value("include_usage").toBool());
    }
    void aStreamedToolCallIsRunAndTheJobGoesOn()
    {
        QDir().mkpath(m_dir.filePath("streamtool"));
        QFile f(m_dir.filePath("streamtool/a.txt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("hello");
        f.close();
        FakeServer server;
        server.contentType = "text/event-stream";
        server.queue << sseToolCall("read_file", {"{\"pa", "th\":\"a.", "txt\"}"}) << sseText({"It says hello."});
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QStringList tools, targets;
        QObject::connect(&agent, &LocalAgent::generating, [&](const QString& phase, const QString& tool, const QString& target, int) {
            if (phase == "tool") { tools << tool; targets << target; }
        });
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.filePath("streamtool");
        c.history = {{"user", "read it"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(done.wait(15000));
        QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        QCOMPARE(server.requests, 2);
        QVERIFY(lastMessage(server.log.at(1)).contains("hello"));                               // the arguments of the pieces were put together
        QVERIFY(tools.contains("read_file"));
    }
    void aTooLongWriteIsStoppedItsPartIsKeptAndTheFileGoesOnWithAppend()
    {
        QDir().mkpath(m_dir.filePath("bigfile"));
        QStringList chunks{QStringLiteral("{\"path\":\"big.html\",\"content\":\"")};
        for (int i = 0; i < 80; ++i) {                                                          // far more than a tool call may be: 80 x 20 lines
            QString part;
            for (int k = 0; k < 20; ++k)
                part += QStringLiteral("<p>line %1</p>\\n").arg(i * 20 + k, 4, 10, QLatin1Char('0'));
            chunks << part;
        }
        FakeServer server;
        server.contentType = "text/event-stream";
        server.queue << sseToolCall("write_file", chunks, "length")                             // cut by the length limit, in the middle of the text
                     << sseToolCall("append_file", {QStringLiteral("{\"path\":\"big.html\",\"content\":\"<p>the end</p>\\n\"}")})
                     << sseText({"The page is written."});
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QSignalSpy asked(&agent, &LocalAgent::approvalNeeded);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.filePath("bigfile");
        c.allowEdits = true;
        c.approveWrites = true;
        c.reviewWork = false;
        c.maxTokens = 8192;
        c.history = {{"user", "write a big page"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(asked.wait(15000));                                                             // the kept part waits for the user like any write
        QCOMPARE(asked.at(0).at(0).toString(), QString("big.html"));
        QCOMPARE(asked.at(0).at(4).toString(), QString("write"));
        agent.answerApproval(true);
        QVERIFY(done.wait(15000));                                                              // the next part (append) needs no second Allow for the same file
        QCOMPARE(asked.count(), 1);
        QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        QFile f(m_dir.filePath("bigfile/big.html"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY(text.startsWith("<p>line 0000</p>\n"));
        QVERIFY(text.endsWith("<p>the end</p>\n"));
        QVERIFY2(text.size() > 4000 && text.size() < 9000, qPrintable(QString::number(text.size())));        // stopped at the small limit, not at the 80 parts
        QVERIFY(text.contains("</p>\n<p>the end</p>\n"));                                       // the kept part ends at a complete line
        // the model was told where to go on
        QCOMPARE(server.requests, 3);
        const QJsonArray msgs = requestBody(server.log.at(1)).value("messages").toArray();
        const QString note = msgs.last().toObject().value("content").toString();
        QVERIFY(note.contains("stopped") && note.contains("append_file") && note.contains("Do NOT read the file again"));
        // and what it remembers of its call is the kept part, not the giant text
        QVERIFY(requestBody(server.log.at(1)).size() > 0);
        for (const QJsonValue& m : msgs)
            if (m.toObject().contains("tool_calls")) {
                const QString args = m.toObject().value("tool_calls").toArray().first().toObject().value("function").toObject().value("arguments").toString();
                QVERIFY(QJsonDocument::fromJson(args.toUtf8()).isObject());                     // valid JSON again
                QVERIFY(args.size() < 9000);
            }
    }
    void aCutOffWriteWithNothingToKeepIsAskedAgain()
    {
        FakeServer server;
        server.contentType = "text/event-stream";
        server.queue << sseToolCall("write_file", {"{\"path\":\"x.txt\",\"con"}, "length") << sseText({"Shorter now."});
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.path();
        c.allowEdits = true;
        c.history = {{"user", "write"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(done.wait(15000));
        QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        QCOMPARE(server.requests, 2);
        QVERIFY(lastMessage(server.log.at(1)).contains("too long"));
    }
    void aCutTextIsTakenBackToItsLastLine()
    {
        QString path, text;
        bool complete = true;
        // cut inside the third line
        QVERIFY(LocalAgent::salvageWrite(QStringLiteral("{\"path\":\"s.txt\",\"content\":\"this is the first line\\nthis is the second line\\nthis is the thi"), &path, &text, &complete));
        QCOMPARE(path, QString("s.txt"));
        QCOMPARE(text, QString("this is the first line\nthis is the second line\n"));
        QVERIFY(!complete);
        // cut in the middle of an escape and of a \u escape
        QVERIFY(LocalAgent::salvageWrite(QStringLiteral("{\"path\":\"a\",\"content\":\"this is the first line\\nthis is the second line\\n\\"), &path, &text, &complete));
        QCOMPARE(text, QString("this is the first line\nthis is the second line\n"));
        QVERIFY(LocalAgent::salvageWrite(QStringLiteral("{\"path\":\"a\",\"content\":\"this is the first line\\nthis is the second \\u0e"), &path, &text, &complete));
        QCOMPARE(text, QString("this is the first line\n"));
        // a Thai text is kept whole (escaped or not), a closed string is complete
        QVERIFY(LocalAgent::salvageWrite(QStringLiteral("{\"path\":\"t.txt\",\"content\":\"สวัสดีครับ นี่คือข้อความภาษาไทย\"} oops"), &path, &text, &complete));
        QCOMPARE(text, QString("สวัสดีครับ นี่คือข้อความภาษาไทย"));
        QVERIFY(complete);
        // nothing to keep: no path yet, no content yet, a few characters only
        QVERIFY(!LocalAgent::salvageWrite(QStringLiteral("{\"content\":\"some text that is long enough to keep\""), &path, &text, &complete));
        QVERIFY(!LocalAgent::salvageWrite(QStringLiteral("{\"path\":\"x\",\"con"), &path, &text, &complete));
        QVERIFY(!LocalAgent::salvageWrite(QStringLiteral("{\"path\":\"x\",\"content\":\"hi"), &path, &text, &complete));
    }
    void appendAddsTextAtTheEnd()
    {
        QDir().mkpath(m_dir.filePath("appendwork"));
        QFile f(m_dir.filePath("appendwork/a.txt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("one\n");
        f.close();
        AgentTools rw(m_dir.filePath("appendwork"), true);
        const AgentTools::WriteInfo info = rw.inspectAppend({{"path", "a.txt"}, {"content", "two\n"}});
        QVERIFY(info.ok && info.exists);
        QCOMPARE(info.bytes, qint64(8));                                                        // the size the file would have
        QCOMPARE(info.preview, QString("two\n"));
        QVERIFY(!rw.run("append_file", {{"path", "a.txt"}, {"content", "two\n"}}).error);
        QVERIFY(!rw.run("append_file", {{"path", "new.txt"}, {"content", "first\n"}}).error);   // created when it is missing
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(f.readAll()), QString("one\ntwo\n"));
        f.close();
        QCOMPARE(QFile(m_dir.filePath("appendwork/new.txt")).size(), qint64(6));
        QVERIFY(rw.run("append_file", {{"path", "../out.txt"}, {"content", "x"}}).error);       // the same folder rule
        QVERIFY(rw.run("append_file", {{"path", "a.txt"}, {"content", ""}}).error);
        QVERIFY(AgentTools(m_dir.filePath("appendwork"), false).run("append_file", {{"path", "a.txt"}, {"content", "x"}}).error);   // read only
        QVERIFY(QDir(m_dir.filePath("appendwork")).entryList(QDir::Files | QDir::Hidden).size() == 2);                              // no backup copies
    }

    // ---- the list of messages does not flicker; the model server is not lost in the middle of a job -----------------------------------------
    void theMessageModelOnlyReportsWhatChanged()
    {
        MessageModel model;
        QSignalSpy changed(&model, &QAbstractItemModel::dataChanged), inserted(&model, &QAbstractItemModel::rowsInserted), removed(&model, &QAbstractItemModel::rowsRemoved),
            reset(&model, &QAbstractItemModel::modelReset);
        auto row = [](const QString& text) { return QVariant(QVariantMap{{"role", "assistant"}, {"text", text}, {"stepList", QVariantList{QVariantMap{{"kind", "read"}}}}}); };
        QCOMPARE(model.sync("c1", {row("a"), row("b")}), 2);
        QCOMPARE(reset.count(), 1);                                                              // a new conversation: everything is new
        QCOMPARE(model.rowCount(), 2);
        QCOMPARE(model.sync("c1", {row("a"), row("b")}), 0);                                     // nothing changed: nothing is reported (the boxes stay as they are)
        QCOMPARE(changed.count() + inserted.count() + removed.count() + reset.count(), 1);
        QCOMPARE(model.sync("c1", {row("a"), row("b, and more")}), 1);                           // the last answer grew: one row changed
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.at(0).at(0).value<QModelIndex>().row(), 1);
        QCOMPARE(model.data(model.index(1), Qt::UserRole + 1).toMap().value("text").toString(), QString("b, and more"));
        QCOMPARE(model.sync("c1", {row("a"), row("b, and more"), row("c")}), 1);                  // a new message: one row inserted
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(model.rowCount(), 3);
        QCOMPARE(model.sync("c1", {row("a")}), 0);
        QCOMPARE(removed.count(), 1);
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.sync("c2", {row("x")}), 1);                                               // another conversation: reset
        QCOMPARE(reset.count(), 2);
        QCOMPARE(model.roleNames().value(Qt::UserRole + 1), QByteArray("modelData"));
    }
    void thePageGetsItsMessagesAsAModel()
    {
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hubmodel");
        dm::Hub hub(o);
        AiService ai(&hub);
        auto* agent = qobject_cast<AiChannel*>(ai.agent());
        auto* model = qobject_cast<QAbstractItemModel*>(agent->messageModel());
        QVERIFY(model);
        QCOMPARE(model->rowCount(), 0);
        QCOMPARE(agent->send("/help"), QString());
        QCOMPARE(model->rowCount(), agent->messages().size());                                   // the model follows the channel
        QCOMPARE(model->rowCount(), 2);
        QCOMPARE(model->data(model->index(0, 0), Qt::UserRole + 1).toMap().value("role").toString(), QString("user"));
        agent->newConversation();
        QCOMPARE(model->rowCount(), 0);
    }
    void theModelServerThatStoppedIsStartedAgainAndTheQuestionAskedAgain()
    {
        FakeServer server;
        server.contentType = "application/json";
        // (Qt itself sends a request once more when the connection was closed before any answer, so the number of dropped requests is not the number of lost answers:
        // the server drops everything until the test says that it is back)
        server.dropping = true;
        server.body = agentReply({}, QStringLiteral("Back again."));
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QSignalSpy lost(&agent, &LocalAgent::serverLost);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.path();
        c.history = {{"user", "hello"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(lost.wait(15000));                                                               // the connection was closed: the agent waits for the server
        QCOMPARE(done.count(), 0);
        agent.resume();                                                                          // (the page started the server again - and it dies again)
        QTRY_VERIFY2_WITH_TIMEOUT(lost.count() == 2, qPrintable(QStringLiteral("lost=%1 requests=%2 done=%3").arg(lost.count()).arg(server.requests).arg(done.count())), 15000);   // it stopped once more
        server.dropping = false;                                                                 // now it stays up
        agent.resume();
        QVERIFY(done.wait(15000));
        QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        QVERIFY(server.requests >= 3);
        QCOMPARE(requestBody(server.log.last()).value("messages").toArray().size(), requestBody(server.log.at(0)).value("messages").toArray().size());   // the same question
        // three times is too much: the job ends with the network error
        FakeServer dead;
        dead.contentType = "application/json";
        dead.body = "DROP";
        LocalAgent agent2(&net);
        QSignalSpy lost2(&agent2, &LocalAgent::serverLost);
        QSignalSpy done2(&agent2, &LocalAgent::finished);
        c.baseUrl = dead.base() + "/v1";
        QCOMPARE(agent2.start(c), QString());
        QVERIFY(lost2.wait(15000));
        agent2.resume();
        QTRY_VERIFY_WITH_TIMEOUT(lost2.count() == 2, 15000);
        agent2.resume();
        QVERIFY(done2.wait(15000));
        QVERIFY(!done2.at(0).at(0).toBool());
        QCOMPARE(done2.at(0).at(1).toString(), QString("network"));
    }

    // ---- background tasks ------------------------------------------------------------------------------------------------------------------
    void theCommandPoolKeepsTasksAndStopsThem()
    {
        QDir().mkpath(m_dir.filePath("pool"));
        CommandPool pool;
        QSignalSpy finished(&pool, &CommandPool::finished);
        QString text;
        bool error = true, got = false;
        // a command that is waited for
        const int a = pool.start(m_dir.filePath("pool"), "echo hi", 0, false, [&](const QString& t, bool e) { text = t; error = e; got = true; });
        QVERIFY(a > 0);
        QTRY_VERIFY_WITH_TIMEOUT(got, 20000);
        QVERIFY(!error && text.startsWith("exit code 0"));
        QCOMPARE(pool.info(a).value("state").toString(), QString("done"));
        QCOMPARE(pool.info(a).value("note").toString(), QString("exit code 0"));
        // a background command: the answer comes at once, the command goes on
        got = false;
        const int b = pool.start(m_dir.filePath("pool"), "ping -n 30 127.0.0.1", 0, true, [&](const QString& t, bool e) { text = t; error = e; got = true; });
        QVERIFY(b > 0 && got);
        QVERIFY(text.contains("background") && text.contains(QString::number(b)) && !error);
        QCOMPARE(pool.info(b).value("state").toString(), QString("running"));
        QCOMPARE(pool.runningCount(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(pool.output(b).contains("Pinging"), 20000);                     // what it wrote so far can be read while it runs
        // at most four run at the same time
        QList<int> ids{b};
        for (int i = 0; i < 3; ++i)
            ids << pool.start(m_dir.filePath("pool"), "ping -n 30 127.0.0.1", 0, true, [](const QString&, bool) {});
        QCOMPARE(pool.runningCount(), CommandPool::kMaxRunning);
        QCOMPARE(pool.start(m_dir.filePath("pool"), "echo no", 0, true, [](const QString&, bool) {}), 0);
        // stopping
        const int before = finished.count();
        QVERIFY(pool.stop(b));
        QTRY_VERIFY_WITH_TIMEOUT(pool.info(b).value("state").toString() == "stopped", 20000);
        QVERIFY(finished.count() > before);
        QVERIFY(!pool.stop(b));                                                                 // not running any more
        QVERIFY(pool.info(b).value("endedMs").toDouble() >= pool.info(b).value("startedMs").toDouble());
        for (int id : ids)
            pool.stop(id);
        QTRY_VERIFY_WITH_TIMEOUT(pool.runningCount() == 0, 20000);
        const QVariantList list = pool.list();
        QCOMPARE(list.first().toMap().value("id").toInt(), ids.last());                         // the newest first
    }
    void aBackgroundCommandRunsWhileTheAgentGoesOn()
    {
        QDir().mkpath(m_dir.filePath("bgwork"));
        FakeServer server;
        server.contentType = "application/json";
        server.queue << agentReply({agentCall("r1", "run_command", {{"command", "ping -n 4 127.0.0.1 > nul & echo bg-marker"}, {"background", true}})}, QString())
                     << agentReply({agentCall("o1", "command_output", {{"task_id", 1}})}, QString())
                     << agentReply({}, QStringLiteral("It runs in the background."))
                     << agentReply({}, QStringLiteral("Seen."));
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QSignalSpy asked(&agent, &LocalAgent::commandApprovalNeeded);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.filePath("bgwork");
        c.allowEdits = true;
        c.approveWrites = true;
        c.allowCommands = true;
        c.history = {{"user", "start it"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(asked.wait(15000));                                                              // a background command waits for the user like any command
        agent.answerApproval(true);
        QVERIFY(done.wait(30000));
        QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        QCOMPARE(server.requests, 3);                                                            // the agent did not wait for the command
        QVERIFY(lastMessage(server.log.at(1)).contains("Started in the background as task 1"));
        const QString out = lastMessage(server.log.at(2));                                       // command_output
        QVERIFY(out.contains("task 1") && out.contains("running"));
        // the command ends by itself: the next request tells the model
        QTRY_VERIFY_WITH_TIMEOUT(agent.pool()->info(1).value("state").toString() == "done", 20000);
        QVERIFY(agent.pool()->output(1).contains("bg-marker"));
        done.clear();
        c.history = {{"user", "and now?"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(done.wait(15000));
        const QJsonArray msgs = requestBody(server.log.at(3)).value("messages").toArray();
        QString note;
        for (const QJsonValue& m : msgs)
            if (m.toObject().value("content").toString().startsWith("The background task 1"))
                note = m.toObject().value("content").toString();
        QVERIFY2(!note.isEmpty(), "the model was not told that the task ended");
        QVERIFY(note.contains("done") && note.contains("bg-marker"));
    }
    void theAgentCanStopItsBackgroundCommand()
    {
        QDir().mkpath(m_dir.filePath("bgstop"));
        FakeServer server;
        server.contentType = "application/json";
        server.queue << agentReply({agentCall("r1", "run_command", {{"command", "ping -n 60 127.0.0.1 > nul"}, {"background", true}})}, QString())
                     << agentReply({agentCall("s1", "stop_command", {{"task_id", 1}}), agentCall("s2", "stop_command", {{"task_id", 99}})}, QString())
                     << agentReply({}, QStringLiteral("Stopped."));
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QSignalSpy asked(&agent, &LocalAgent::commandApprovalNeeded);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.filePath("bgstop");
        c.allowEdits = true;
        c.approveWrites = true;
        c.allowCommands = true;
        c.history = {{"user", "start and stop"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(asked.wait(15000));
        agent.answerApproval(true);
        QVERIFY(done.wait(30000));
        const QJsonArray msgs = requestBody(server.log.at(2)).value("messages").toArray();
        QVERIFY(msgs.at(msgs.size() - 2).toObject().value("content").toString().contains("task 1 was stopped"));
        QVERIFY(msgs.last().toObject().value("content").toString().contains("there is no task 99"));    // only its own tasks
        QTRY_VERIFY_WITH_TIMEOUT(agent.pool()->info(1).value("state").toString() == "stopped", 20000);
    }

    // ---- compacting the conversation -------------------------------------------------------------------------------------------------------
    void theModelIsAskedForASummary()
    {
        FakeServer server;
        server.contentType = "application/json";
        server.queue << replyWithReason("SUMMARY", "stop") << errorBody(500, "boom") << replyWithReason("", "stop");
        QNetworkAccessManager net;
        Compactor compactor(&net);
        QSignalSpy done(&compactor, &Compactor::finished);
        QCOMPARE(compactor.start(server.base() + "/v1", "key1", "User: hello\n\nAssistant: hi", "keep the file names", 900), QString());
        QVERIFY(compactor.running());
        QCOMPARE(compactor.start(server.base() + "/v1", "key1", "x", {}, 900), QString("busy"));
        QVERIFY(done.wait(15000));
        QVERIFY(done.at(0).at(0).toBool());
        QCOMPARE(done.at(0).at(1).toString(), QString("SUMMARY"));
        const QJsonObject body = requestBody(server.log.at(0));
        QVERIFY(server.log.at(0).toLower().contains("authorization: bearer key1"));
        QCOMPARE(body.value("stream").toBool(), true == false);                                  // one plain answer
        QCOMPARE(body.value("max_tokens").toInt(), 900);
        QCOMPARE(body.value("chat_template_kwargs").toObject().value("enable_thinking").toBool(), false);
        QVERIFY(!body.contains("tools"));                                                         // a summary, no tools
        const QJsonArray msgs = body.value("messages").toArray();
        QVERIFY(msgs.at(0).toObject().value("content").toString().contains("keep the file names"));
        QVERIFY(msgs.at(1).toObject().value("content").toString().contains("User: hello"));
        // a server error and an empty answer are reported, not hidden
        done.clear();
        QCOMPARE(compactor.start(server.base() + "/v1", {}, "x", {}, 0), QString());
        QVERIFY(done.wait(15000));
        QVERIFY(!done.at(0).at(0).toBool());
        QCOMPARE(done.at(0).at(2).toString(), QString("http500"));
        done.clear();
        QCOMPARE(compactor.start(server.base() + "/v1", {}, "x", {}, 0), QString());
        QVERIFY(done.wait(15000));
        QCOMPARE(done.at(0).at(2).toString(), QString("empty"));
        QCOMPARE(compactor.start("http://example.com/v1", {}, "x", {}, 0), QString("badUrl"));     // only this computer
    }
    void aConversationIsCompactedAndGoesOnWithTheSummary()
    {
        QDir().mkpath(m_dir.filePath("compactwork"));
        QFile model(m_dir.filePath("compact-model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();
        FakeServer server;
        server.contentType = "application/json";
        server.queue << agentReply({}, QStringLiteral("A1: the first answer")) << agentReply({}, QStringLiteral("A2: the second answer"))
                     << replyWithReason("SUMMARY TEXT: the user wants a page", "stop") << agentReply({}, QStringLiteral("A3: the third answer"));
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hubcompact");
        dm::Hub hub(o);
        QString conversation;
        {
            AiService ai(&hub);
            if (!ai.engineFound())
                QSKIP("no llama.cpp engine on this PC");
            auto* agent = qobject_cast<AiChannel*>(ai.agent());
            ai.selectLocalModel(m_dir.filePath("compact-model.gguf"));
            ai.llama()->useExternalServer(server.base(), "");
            ai.setWebEnabled(false);
            agent->setFolder(m_dir.filePath("compactwork"));
            QCOMPARE(agent->compactNow(), QString("nothing"));                                  // nothing to compact yet
            QCOMPARE(agent->send("first question"), QString());
            QTRY_VERIFY_WITH_TIMEOUT(!agent->busy(), 10000);
            QCOMPARE(agent->send("second question"), QString());
            QTRY_VERIFY_WITH_TIMEOUT(!agent->busy(), 10000);
            QVERIFY(agent->context().value("canCompact").toBool());
            QCOMPARE(agent->send("/compact keep the names"), QString());                        // the command: the same as the button
            QVERIFY(agent->busy());
            QCOMPARE(agent->status(), QString("compacting"));
            QCOMPARE(agent->send("hello"), QString("busy"));                                    // nothing else while it writes the summary
            QTRY_VERIFY_WITH_TIMEOUT(!agent->busy(), 10000);
            const QVariantMap card = agent->messages().last().toMap();
            QVERIFY(card.value("compact").toBool());
            QCOMPARE(card.value("summary").toString(), QString("SUMMARY TEXT: the user wants a page"));
            QVERIFY(card.value("tokensBefore").toInt() > 0);
            QCOMPARE(card.value("errorCode").toString(), QString());
            QCOMPARE(agent->messages().at(agent->messages().size() - 2).toMap().value("text").toString(), QString("/compact keep the names"));
            // what the model was asked to summarize: the whole conversation and the wish
            const QJsonArray ask = requestBody(server.log.at(2)).value("messages").toArray();
            const QString sys = ask.at(0).toObject().value("content").toString(), usr = ask.at(1).toObject().value("content").toString();
            QVERIFY(sys.contains("keep the names"));
            QVERIFY(usr.contains("User: first question") && usr.contains("A1: the first answer") && usr.contains("User: second question") && usr.contains("A2: the second answer"));
            // the conversation goes on with the summary instead of the old messages
            QCOMPARE(agent->send("third question"), QString());
            QTRY_VERIFY_WITH_TIMEOUT(!agent->busy(), 10000);
            QCOMPARE(server.requests, 4);
            const QJsonArray next = requestBody(server.log.at(3)).value("messages").toArray();
            QVERIFY(next.at(0).toObject().value("content").toString().contains("SUMMARY TEXT: the user wants a page"));
            QCOMPARE(next.size(), 2);                                                           // the system message and the new question only
            QCOMPARE(next.at(1).toObject().value("content").toString(), QString("third question"));
            QCOMPARE(agent->messages().last().toMap().value("text").toString(), QString("A3: the third answer"));
            conversation = agent->currentId();
        }
        AiService again(&hub);                                                                  // the summary is saved with the conversation
        auto* agent2 = qobject_cast<AiChannel*>(again.agent());
        agent2->select(conversation);
        bool found = false;
        for (const QVariant& m : agent2->messages())
            if (m.toMap().value("compact").toBool() && m.toMap().value("summary").toString().startsWith("SUMMARY TEXT"))
                found = true;
        QVERIFY(found);
        QCOMPARE(AiChannel::afterLastCompact(QList<AiChannel::Message>{}).size(), 0);
    }
    void aLongHistoryIsCompactedBeforeTheNextMessage()
    {
        QDir().mkpath(m_dir.filePath("autowork"));
        QFile model(m_dir.filePath("auto-model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();
        const QString longText(6000, QLatin1Char('w'));
        FakeServer server;
        server.contentType = "application/json";
        server.queue << agentReply({}, "A1 " + longText) << agentReply({}, "A2 " + longText) << replyWithReason("AUTO SUMMARY", "stop") << agentReply({}, QStringLiteral("A3"));
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hubauto");
        dm::Hub hub(o);
        AiService ai(&hub);
        if (!ai.engineFound())
            QSKIP("no llama.cpp engine on this PC");
        auto* agent = qobject_cast<AiChannel*>(ai.agent());
        ai.selectLocalModel(m_dir.filePath("auto-model.gguf"));
        ai.llama()->useExternalServer(server.base(), "");
        ai.setWebEnabled(false);
        agent->setFolder(m_dir.filePath("autowork"));
        agent->setEffort("low");                                                                // a history of 16000 characters is kept: 12000 start a compaction
        QCOMPARE(agent->send("q1"), QString());
        QTRY_VERIFY_WITH_TIMEOUT(!agent->busy(), 10000);
        QCOMPARE(agent->send("q2"), QString());
        QTRY_VERIFY_WITH_TIMEOUT(!agent->busy(), 10000);
        QCOMPARE(server.requests, 2);
        QCOMPARE(agent->send("q3"), QString());                                                 // the history is long: the summary is written first, then q3 goes on
        QTRY_VERIFY_WITH_TIMEOUT(!agent->busy() && server.requests == 4, 15000);
        const QVariantList msgs = agent->messages();
        int cardAt = -1, q3At = -1;
        for (int i = 0; i < msgs.size(); ++i) {
            if (msgs.at(i).toMap().value("compact").toBool())
                cardAt = i;
            if (msgs.at(i).toMap().value("text").toString() == "q3")
                q3At = i;
        }
        QVERIFY(cardAt >= 0 && q3At > cardAt);                                                  // card, then the message that waited
        QCOMPARE(msgs.at(cardAt).toMap().value("summary").toString(), QString("AUTO SUMMARY"));
        const QJsonArray last = requestBody(server.log.at(3)).value("messages").toArray();
        QVERIFY(last.at(0).toObject().value("content").toString().contains("AUTO SUMMARY"));
        QCOMPARE(last.size(), 2);
        QCOMPARE(msgs.last().toMap().value("text").toString(), QString("A3"));
    }
    void theAgentPageUsesTheLocalModel()
    {
        FakeServer server;
        server.contentType = "application/json";
        server.queue << R"({"choices":[{"message":{"role":"assistant","content":null,"tool_calls":[{"id":"c1","type":"function","function":{"name":"list_files","arguments":"{}"}}]}}]})"
                     << R"({"choices":[{"message":{"role":"assistant","content":"There is one file."}}]})";
        QDir().mkpath(m_dir.filePath("pagework"));
        QFile model(m_dir.filePath("fake-model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hub4");
        dm::Hub hub(o);
        AiService ai(&hub);
        auto* agent = qobject_cast<AiChannel*>(ai.agent());
        ai.selectLocalModel(m_dir.filePath("fake-model.gguf"));
        ai.llama()->useExternalServer(server.base(), "");
        QCOMPARE(agent->notReady(), QString("noFolder"));
        agent->setFolder(m_dir.filePath("pagework"));
        agent->setPermission("read");                                            // read only: three tools
        ai.setWebEnabled(false);
        if (!ai.engineFound())
            QSKIP("no llama.cpp engine on this PC");
        QCOMPARE(agent->notReady(), QString());
        QCOMPARE(agent->send("what files are here?"), QString());
        QTRY_VERIFY_WITH_TIMEOUT(!agent->busy(), 15000);
        const QVariantMap reply = agent->messages().at(1).toMap();
        QCOMPARE(reply.value("errorCode").toString(), QString());
        QCOMPARE(reply.value("text").toString(), QString("There is one file."));
        QCOMPARE(reply.value("activity").toStringList(), QStringList({"List"}));
        // the Q&A page without web and folder is a plain chat (no tools in the request)
        QCOMPARE(requestBody(server.log.at(0)).value("tools").toArray().size(), 5);
    }

    // ---- the pages: model, effort, conversations ---------------------------------------------------------------------------------------
    void effortLevelsChangeWhatTheModelMayDo()
    {
        const Effort low = Effort::of("low"), medium = Effort::of("medium"), high = Effort::of("high");
        QVERIFY(!low.thinking && !medium.thinking && high.thinking);              // only "high" lets the model reason first
        QVERIFY(low.maxTokens < medium.maxTokens && medium.maxTokens < high.maxTokens);
        QVERIFY(low.maxSteps < medium.maxSteps && medium.maxSteps < high.maxSteps);
        QCOMPARE(Effort::of("nonsense").id, QString("medium"));                  // an unknown name is the normal level
        QCOMPARE(Effort::ids(), QStringList({"low", "medium", "high"}));
    }
    void aChatSendsItsEffortToTheModel()
    {
        FakeServer server;
        server.body = sse({R"({"choices":[{"delta":{"reasoning_content":"let me think"}}]})", R"({"choices":[{"delta":{"content":"42"}}]})", "[DONE]"});
        QFile model(m_dir.filePath("effort-model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hub-effort");
        dm::Hub hub(o);
        AiService ai(&hub);
        if (!ai.engineFound())
            QSKIP("no llama.cpp engine on this PC");
        auto* ask = qobject_cast<AiChannel*>(ai.ask());
        QCOMPARE(ask->effort(), QString("medium"));                              // the default
        ai.selectLocalModel(m_dir.filePath("effort-model.gguf"));
        ai.llama()->useExternalServer(server.base(), "");
        ai.setWebEnabled(false);                                                  // a plain chat: one streamed request
        QStringList statuses;
        connect(ask, &AiChannel::busyChanged, [&] { statuses << ask->status(); });
        for (const QString& level : {QString("low"), QString("medium"), QString("high")}) {
            ask->setEffort(level);
            QCOMPARE(ask->effort(), level);
            QCOMPARE(ask->send("question " + level), QString());
            QTRY_VERIFY_WITH_TIMEOUT(!ask->busy(), 10000);
            const QJsonObject body = requestBody(server.lastRequest);
            const Effort e = Effort::of(level);
            QCOMPARE(body.value("chat_template_kwargs").toObject().value("enable_thinking").toBool(), e.thinking);
            QCOMPARE(body.value("max_tokens").toInt(), e.maxTokens);
            QVERIFY(!body.contains("tools"));
        }
        QVERIFY2(statuses.contains("thinking"), qPrintable(statuses.join(",")));      // the reasoning was announced, not shown
        const QVariantList msgs = ask->messages();
        QCOMPARE(msgs.last().toMap().value("text").toString(), QString("42"));
        QVERIFY(!msgs.last().toMap().value("text").toString().contains("think"));
        ask->setEffort("bogus");
        QCOMPARE(ask->effort(), QString("medium"));
    }
    void theModelIsChosenAndRemembered()
    {
        const QString dataDir = m_dir.filePath("hub-model");
        QDir().mkpath(dataDir + "/models");
        for (const char* name : {"Qwen3.5-4B-Q4_K_M.gguf", "other-model.gguf"}) {
            QFile f(dataDir + "/models/" + name);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(1500000, 'm'));
        }
        {
            dm::Hub::Options o;
            o.registryRoot = kRoot;
            o.dataDir = dataDir;
            dm::Hub hub(o);
            AiService ai(&hub);
            QCOMPARE(ai.localModelName(), QString("Qwen3.5-4B-Q4_K_M.gguf"));    // nothing chosen yet: the tested default is picked
            QVERIFY(ai.installedModels().size() >= 2);
            ai.selectLocalModel(dataDir + "/models/other-model.gguf");
            QCOMPARE(ai.localModelName(), QString("other-model.gguf"));
            ai.selectLocalModel(dataDir + "/models/missing.gguf");                // a file that is not there is not chosen
            ai.selectLocalModel(dataDir + "/models/readme.txt");
            QCOMPARE(ai.localModelName(), QString("other-model.gguf"));
            auto* agent = qobject_cast<AiChannel*>(ai.agent());
            agent->setEffort("high");
            agent->setFolder(m_dir.path());
        }
        {   // a new start: the choice comes back
            dm::Hub::Options o;
            o.registryRoot = kRoot;
            o.dataDir = dataDir;
            dm::Hub hub(o);
            AiService ai(&hub);
            QCOMPARE(ai.localModelName(), QString("other-model.gguf"));
            auto* agent = qobject_cast<AiChannel*>(ai.agent());
            QCOMPARE(agent->effort(), QString("high"));
            QCOMPARE(agent->folder(), QDir::cleanPath(m_dir.path()));
            QCOMPARE(qobject_cast<AiChannel*>(ai.ask())->effort(), QString("medium"));       // each page has its own level
        }
    }
    void aConversationRunsAndIsSaved()
    {
        FakeServer server;
        server.body = sse({R"({"choices":[{"delta":{"content":"Answer one"}}]})", "[DONE]"});
        QFile model(m_dir.filePath("conv-model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();
        const QString dataDir = m_dir.filePath("hub-conv");
        QString firstId;
        {
            dm::Hub::Options o;
            o.registryRoot = kRoot;
            o.dataDir = dataDir;
            dm::Hub hub(o);
            AiService ai(&hub);
            if (!ai.engineFound())
                QSKIP("no llama.cpp engine on this PC");
            auto* ask = qobject_cast<AiChannel*>(ai.ask());
            const QString first = ask->notReady();                                  // this PC may or may not have a model already
            QVERIFY(first == "" || first == "noModel");
            ai.selectLocalModel(m_dir.filePath("conv-model.gguf"));
            ai.llama()->useExternalServer(server.base(), "");
            ai.setWebEnabled(false);
            QCOMPARE(ask->notReady(), QString());
            QCOMPARE(ask->send("  "), QString("empty"));
            QCOMPARE(ask->send("What is Master Pro?"), QString());
            QCOMPARE(ask->send("again"), QString("busy"));
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
            // the agent page has its own conversations
            QCOMPARE(qobject_cast<AiChannel*>(ai.agent())->conversations().size(), 0);
            // the settings file has no key or anything of the removed providers
            QFile f(dataDir + "/ai/settings.json");
            QVERIFY(f.open(QIODevice::ReadOnly));
            const QByteArray settings = f.readAll();
            QVERIFY(!settings.contains("openai") && !settings.contains("anthropic") && !settings.contains("key"));
        }
        {   // a new start: the conversation comes back
            dm::Hub::Options o;
            o.registryRoot = kRoot;
            o.dataDir = dataDir;
            dm::Hub hub(o);
            AiService ai(&hub);
            auto* ask = qobject_cast<AiChannel*>(ai.ask());
            QCOMPARE(ask->conversations().size(), 1);
            ask->select(firstId);
            QCOMPARE(ask->messages().size(), 2);
            ask->remove(firstId);
            QCOMPARE(ask->conversations().size(), 0);
        }
    }
    void anErrorStaysInTheConversation()
    {
        FakeServer server;
        server.status = 500;
        server.contentType = "application/json";
        server.body = R"({"error":{"message":"model crashed"}})";
        QFile model(m_dir.filePath("err-model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hub-err");
        dm::Hub hub(o);
        AiService ai(&hub);
        if (!ai.engineFound())
            QSKIP("no llama.cpp engine on this PC");
        auto* ask = qobject_cast<AiChannel*>(ai.ask());
        ai.selectLocalModel(m_dir.filePath("err-model.gguf"));
        ai.llama()->useExternalServer(server.base(), "");
        ai.setWebEnabled(false);
        QCOMPARE(ask->send("hi"), QString());
        QTRY_VERIFY_WITH_TIMEOUT(!ask->busy(), 10000);
        const QVariantMap reply = ask->messages().at(1).toMap();
        QCOMPARE(reply.value("errorCode").toString(), QString("http500"));
        QCOMPARE(reply.value("errorDetail").toString(), QString("model crashed"));
    }
    void theAgentNeedsAFolder()
    {
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hub3");
        dm::Hub hub(o);
        AiService ai(&hub);
        auto* agent = qobject_cast<AiChannel*>(ai.agent());
        const QString why = agent->notReady();
        QVERIFY(why == "noEngine" || why == "noModel" || why == "noFolder");
        agent->setFolder(m_dir.path());
        QCOMPARE(agent->folder(), QDir::cleanPath(m_dir.path()));
        QCOMPARE(agent->permission(), QString("edit"));                         // the default since 1.23.0: automatic (risky commands still ask)
        agent->setPermission("ask");
        QCOMPARE(agent->permission(), QString("ask"));
    }

    // The real model with the three effort levels through the Q&A page (web off): "deep" must announce its thinking and still answer.
    void realLocalEffortLevels()
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
        o.dataDir = m_dir.filePath("hub-real-effort");
        dm::Hub hub(o);
        AiService ai(&hub);
        ai.selectLocalModel(model);
        ai.setWebEnabled(false);
        auto* ask = qobject_cast<AiChannel*>(ai.ask());
        for (const QString& level : {QString("low"), QString("high")}) {
            ask->newConversation();
            ask->setEffort(level);
            QStringList statuses;
            auto c = connect(ask, &AiChannel::busyChanged, [&] { statuses << ask->status(); });
            QElapsedTimer timer;
            timer.start();
            QCOMPARE(ask->send("A bat and a ball cost 1.10 in total. The bat costs 1.00 more than the ball. How much is the ball? Answer in one sentence."), QString());
            QTRY_VERIFY_WITH_TIMEOUT(!ask->busy(), 240000);
            disconnect(c);
            const QVariantMap reply = ask->messages().last().toMap();
            fprintf(stderr, "REAL EFFORT %s: %lld ms, statuses=%s, answer=%s\n", qPrintable(level), timer.elapsed(), qPrintable(statuses.join(",")), qPrintable(reply.value("text").toString().left(150)));
            QCOMPARE(reply.value("errorCode").toString(), QString());
            QVERIFY(reply.value("text").toString().contains("0.05") || reply.value("text").toString().contains("5 cents"));
            QCOMPARE(statuses.contains("thinking"), level == "high");
        }
        ai.llama()->stop();
    }

    // ---- the agent's mode (permission) and the question before a file is changed ---------------------------------------------------------
    void aWriteCanBeInspectedBeforeItHappens()
    {
        QDir().mkpath(m_dir.filePath("inspect"));
        QFile f(m_dir.filePath("inspect/a.txt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("old");
        f.close();
        AgentTools rw(m_dir.filePath("inspect"), true);
        auto info = rw.inspectWrite({{"path", "a.txt"}, {"content", "new content"}});
        QVERIFY(info.ok && info.exists);
        QCOMPARE(info.bytes, qint64(11));
        QCOMPARE(info.preview, QString("new content"));
        QVERIFY(!rw.inspectWrite({{"path", "b.txt"}, {"content", "x"}}).exists);
        QVERIFY(!rw.inspectWrite({{"path", "../b.txt"}, {"content", "x"}}).ok);                 // the same folder rule as the write itself
        QVERIFY(!AgentTools(m_dir.filePath("inspect"), false).inspectWrite({{"path", "a.txt"}, {"content", "x"}}).ok);
        QCOMPARE(QFile(m_dir.filePath("inspect/a.txt")).size(), qint64(3));                    // looking changed nothing
    }
    void theAgentWaitsForTheUserBeforeWriting()
    {
        QDir().mkpath(m_dir.filePath("askwork"));
        for (const bool allow : {false, true}) {
            const QString name = allow ? "allowed.txt" : "declined.txt";
            FakeServer server;
            server.contentType = "application/json";
            server.queue << agentReply({agentCall("w1", "write_file", {{"path", name}, {"content", "hello"}})}, QString())
                         << agentReply({}, QStringLiteral("Done."));
            QNetworkAccessManager net;
            LocalAgent agent(&net);
            QSignalSpy asked(&agent, &LocalAgent::approvalNeeded);
            QSignalSpy done(&agent, &LocalAgent::finished);
            LocalAgent::Config c;
            c.baseUrl = server.base() + "/v1";
            c.folder = m_dir.filePath("askwork");
            c.allowEdits = true;
            c.approveWrites = true;
            c.reviewWork = false;
            c.history = {{"user", "write a file"}};
            QCOMPARE(agent.start(c), QString());
            QVERIFY(asked.wait(15000));
            QCOMPARE(asked.at(0).at(0).toString(), name);
            QVERIFY(agent.waitingForApproval());
            QVERIFY(!QFileInfo::exists(m_dir.filePath("askwork/" + name)));              // nothing is written while the user decides
            QCOMPARE(server.requests, 1);                                                // and the model is not asked again
            agent.answerApproval(allow);
            QVERIFY(done.wait(15000));
            QVERIFY(done.at(0).at(0).toBool());
            QCOMPARE(QFileInfo::exists(m_dir.filePath("askwork/" + name)), allow);
            const QJsonArray msgs = requestBody(server.log.at(1)).value("messages").toArray();
            const QString toolText = msgs.last().toObject().value("content").toString();
            QCOMPARE(toolText.contains("declined"), !allow);                             // the model is told what happened
        }
    }
    void aJobThatChangedFilesChecksItsOwnWorkOnceBeforeItEnds()
    {
        QDir().mkpath(m_dir.filePath("selfcheck"));
        FakeServer server;
        server.contentType = "application/json";
        server.queue << agentReply({agentCall("w1", "write_file", {{"path", "a.html"}, {"content", "<p>hi</p>"}})}, QString())
                     << agentReply({}, QStringLiteral("Done."))                                      // would end here without the check
                     << agentReply({agentCall("e1", "append_file", {{"path", "a.html"}, {"text", "<p>bye</p>"}})}, QString())   // the check found a gap
                     << agentReply({}, QStringLiteral("Checked and finished."));
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.filePath("selfcheck");
        c.allowEdits = true;
        c.history = {{"user", "write a page"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(done.wait(15000));
        QVERIFY(done.at(0).at(0).toBool());
        QCOMPARE(server.requests, 4);                                                              // asked once, not again after the second "done"
        QVERIFY(lastMessage(server.log.at(2)).contains("check your own work") && lastMessage(server.log.at(2)).contains("a.html"));
        QFile f(m_dir.filePath("selfcheck/a.html"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(f.readAll()), QString("<p>hi</p><p>bye</p>"));
        // a job that changed nothing is not asked to check anything
        FakeServer plain;
        plain.contentType = "application/json";
        plain.queue << agentReply({}, QStringLiteral("Nothing to do."));
        LocalAgent agent2(&net);
        QSignalSpy done2(&agent2, &LocalAgent::finished);
        c.baseUrl = plain.base() + "/v1";
        QCOMPARE(agent2.start(c), QString());
        QVERIFY(done2.wait(15000));
        QCOMPARE(plain.requests, 1);
    }
    // ---- the automatic mode: no questions for ordinary commands; edit_file that survives a wrong indentation -----------------------------------
    void riskyCommandsAreTheOnesThatStillAskInTheAutomaticMode()
    {
        for (const char* ok : {"dir", "echo hello", "cmake --build build", "ctest --test-dir build", "npm test", "npm run build", "python main.py", "git status", "git diff",
                               "git add index.html", "git commit -m x", "type index.html", "powershell -File build.ps1", "node server.js", "dotnet build"})
            QVERIFY2(!LocalAgent::isRiskyCommand(QString::fromLatin1(ok)), ok);
        for (const char* bad : {"rd /s /q build", "rmdir /s /q x", "del /f /q *.*", "rm -rf x", "Remove-Item -Recurse -Force x", "format c:", "reg add HKCU\\x /v a", "shutdown /s",
                                "git push origin main", "git reset --hard", "git clean -fd", "curl http://x.y/a.exe -o a.exe", "powershell iwr https://x.y", "npm install left-pad",
                                "pip install requests", "winget install x", "echo a & del x.txt", "cd C:\\Windows & dir", "powershell -enc AAAA", "start http://example.com"})
            QVERIFY2(LocalAgent::isRiskyCommand(QString::fromLatin1(bad)), bad);
    }
    void theAutomaticModeRunsAnOrdinaryCommandWithoutAskingButAsksForARiskyOne()
    {
        QDir().mkpath(m_dir.filePath("autocmd"));
        FakeServer server;
        server.contentType = "application/json";
        server.queue << agentReply({agentCall("c1", "run_command", {{"command", "echo auto-marker"}})}, QString())
                     << agentReply({agentCall("c2", "run_command", {{"command", "rd /s /q somewhere"}})}, QString())
                     << agentReply({}, QStringLiteral("Done."));
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QSignalSpy asked(&agent, &LocalAgent::commandApprovalNeeded);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.filePath("autocmd");
        c.allowEdits = true;
        c.allowCommands = true;
        c.autoCommands = true;
        c.reviewWork = false;
        c.history = {{"user", "run"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(asked.wait(20000));                                                              // only the risky one asks
        QCOMPARE(asked.at(0).at(0).toString(), QString("rd /s /q somewhere"));
        QVERIFY(lastMessage(server.log.at(1)).contains("auto-marker"));                         // the first one ran at once, its output went to the model
        agent.answerApproval(false);
        QVERIFY(done.wait(20000));
        QCOMPARE(asked.count(), 1);
    }
    void theFinalCheckAndThePolishingAfterItAreLimitedThenTheJobEndsWithASummary()
    {
        QDir().mkpath(m_dir.filePath("polish"));
        FakeServer server;
        server.contentType = "application/json";
        server.queue << agentReply({agentCall("w1", "write_file", {{"path", "a.txt"}, {"content", "hello"}})}, QString())
                     << agentReply({}, QStringLiteral("Done."));                                    // the final check starts here
        for (int i = 0; i < 10; ++i)                                                                // ... and the model keeps looking and fiddling
            server.queue << agentReply({agentCall(QString("r%1").arg(i).toLatin1().constData(), "read_file", {{"path", "a.txt"}, {"start_line", i + 1}})}, QString());
        server.queue << agentReply({}, QStringLiteral("Summary: written and checked."));
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.filePath("polish");
        c.allowEdits = true;
        c.maxSteps = 60;
        c.history = {{"user", "write a.txt"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(done.wait(30000));
        QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        QCOMPARE(server.requests, 13);
        const QJsonObject lastBody = requestBody(server.log.at(12));
        QVERIFY(!lastBody.contains("tools"));                                                       // no tools in the last request: only words
        QVERIFY(lastMessage(server.log.at(12)).contains("That is enough checking"));
        QVERIFY(requestBody(server.log.at(11)).contains("tools"));                                  // until then the tools were there
    }
    void afterTwoFailedEditsInARowTheModelIsToldToRewriteTheFile()
    {
        QDir().mkpath(m_dir.filePath("editfail"));
        QFile f(m_dir.filePath("editfail/page.html"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("<p>hello</p>\n");
        f.close();
        FakeServer server;
        server.contentType = "application/json";
        server.queue << agentReply({agentCall("e1", "edit_file", {{"path", "page.html"}, {"old_string", "<p>nothing like it</p>"}, {"new_string", "x"}})}, QString())
                     << agentReply({agentCall("e2", "edit_file", {{"path", "page.html"}, {"old_string", "<h9>still not there</h9>"}, {"new_string", "y"}})}, QString())
                     << agentReply({}, QStringLiteral("I will rewrite it."));
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.filePath("editfail");
        c.allowEdits = true;
        c.reviewWork = false;
        c.history = {{"user", "change it"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(done.wait(15000));
        QVERIFY(!lastMessage(server.log.at(1)).contains("STOP using edit_file"));       // the first miss is just an error
        QVERIFY(lastMessage(server.log.at(2)).contains("STOP using edit_file") && lastMessage(server.log.at(2)).contains("page.html"));
    }
    void editFileFindsTheTextEvenWithAnotherIndentationOrLineEndings()
    {
        QString out, err;
        const QString file = "<div>\r\n    <p>one</p>\r\n    <p>two</p>\r\n</div>\r\n";
        QVERIFY(AgentTools::replaceIgnoringIndentation(file, "<p>one</p>\n<p>two</p>", "<p>uno</p>\n<p>dos</p>", false, &out, &err));
        QCOMPARE(out, QString("<div>\r\n    <p>uno</p>\r\n    <p>dos</p>\r\n</div>\r\n"));            // the indentation of the replaced lines is kept, the line endings too
        QVERIFY(!AgentTools::replaceIgnoringIndentation(file, "<p>three</p>", "x", false, &out, &err));
        QVERIFY2(err.contains("nearest line") || err.isEmpty(), qPrintable(err));
        QVERIFY(!AgentTools::replaceIgnoringIndentation("a\nb\na\nb\n", "a\nb", "z", false, &out, &err));                            // two places: not unique
        QVERIFY(err.contains("2 places"));
        QVERIFY(AgentTools::replaceIgnoringIndentation("a\nb\na\nb\n", "a\nb", "z", true, &out, &err));
        QCOMPARE(out, QString("z\nz\n"));
    }
    // ---- attachments of the message box ---------------------------------------------------------------------------------------------------
    void aTextFileCanBeAttachedAndABinaryOrBigOneAndAPictureWithoutVisionCannot()
    {
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hub-attach");
        dm::Hub hub(o);
        AiService ai(&hub);
        auto write = [&](const QString& name, const QByteArray& data) {
            QFile f(m_dir.filePath(name));
            f.open(QIODevice::WriteOnly);
            f.write(data);
            return m_dir.filePath(name);
        };
        const QVariantMap ok = ai.readAttachment(QUrl::fromLocalFile(write("note.txt", QString::fromUtf8("สวัสดี\nline 2").toUtf8())).toString());
        QVERIFY(ok.value("ok").toBool());
        QCOMPARE(ok.value("kind").toString(), QString("file"));
        QCOMPARE(ok.value("name").toString(), QString("note.txt"));
        QVERIFY(ok.value("text").toString().startsWith(QString::fromUtf8("สวัสดี")));
        QCOMPARE(ai.readAttachment(write("bin.dat", QByteArray("ab\0cd", 5))).value("error").toString(), QString("binary"));
        QCOMPARE(ai.readAttachment(write("big.txt", QByteArray(500000, 'a'))).value("error").toString(), QString("tooBig"));
        QCOMPARE(ai.readAttachment(m_dir.filePath("nothing.txt")).value("error").toString(), QString("notFound"));
        QImage img(50, 50, QImage::Format_RGB32);
        img.fill(Qt::red);
        QVERIFY(img.save(m_dir.filePath("p.png")));
        QCOMPARE(ai.readAttachment(m_dir.filePath("p.png")).value("error").toString(), QString("noVision"));   // no vision file: no pictures
        QCOMPARE(ai.clipboardImage().value("error").toString(), QString("noVision"));
    }
    void anAttachedFileGoesToTheModelWithTheQuestionAndAPictureAsAnImagePart()
    {
        QDir().mkpath(m_dir.filePath("attwork"));
        FakeServer server;
        server.contentType = "application/json";
        server.queue << agentReply({}, QStringLiteral("Seen."));
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.filePath("attwork");
        c.reviewWork = false;
        c.history = {{"user", "what is in the picture?"}};
        c.images = {"QUJD"};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(done.wait(15000));
        const QJsonArray msgs = requestBody(server.log.at(0)).value("messages").toArray();
        const QJsonValue content = msgs.last().toObject().value("content");
        QVERIFY(content.isArray());
        QCOMPARE(content.toArray().at(0).toObject().value("text").toString(), QString("what is in the picture?"));
        QCOMPARE(content.toArray().at(1).toObject().value("type").toString(), QString("image_url"));
        QVERIFY(content.toArray().at(1).toObject().value("image_url").toObject().value("url").toString().startsWith("data:image/jpeg;base64,QUJD"));
    }
    void theMessageKeepsTheNamesAndTheTextOfItsAttachedFilesForTheModel()
    {
        QDir().mkpath(m_dir.filePath("attwork2"));
        QFile model(m_dir.filePath("attach-model.gguf"));                                   // a stand-in for the model file (the test never listed a real one: it passed by luck before)
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();
        FakeServer server;
        server.contentType = "application/json";
        server.queue << agentReply({}, QStringLiteral("Read it."));
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hub-attach2");
        dm::Hub hub(o);
        AiService ai(&hub);
        if (!ai.engineFound())
            QSKIP("no llama.cpp engine on this PC");
        auto* agent = qobject_cast<AiChannel*>(ai.agent());
        ai.selectLocalModel(m_dir.filePath("attach-model.gguf"));
        ai.llama()->useExternalServer(server.base(), "");
        ai.setWebEnabled(false);
        agent->setFolder(m_dir.filePath("attwork2"));
        QImage shot(600, 300, QImage::Format_RGB32);
        shot.fill(Qt::blue);
        const QString shotData = ai.browserBridge()->encodeImage(QVariant::fromValue(shot), 1024);
        const QVariantList atts{QVariantMap{{"kind", "file"}, {"name", "report.txt"}, {"text", "TOTAL = 42"}}, QVariantMap{{"kind", "image"}, {"name", "shot.jpg"}, {"data", shotData}}};
        QCOMPARE(agent->send("summarise the file", atts), QString());
        QTRY_VERIFY_WITH_TIMEOUT(!agent->busy(), 15000);
        QCOMPARE(server.requests, 1);
        const QString sent = lastMessage(server.log.at(0));
        QVERIFY(sent.contains("summarise the file") && sent.contains("[Attached file: report.txt]") && sent.contains("TOTAL = 42"));     // the model reads the file
        const QVariantList msgs = agent->messages();
        QCOMPARE(msgs.at(0).toMap().value("attachNames").toStringList(), (QStringList{"report.txt", "shot.jpg"}));
        const QStringList thumbs = msgs.at(0).toMap().value("thumbs").toStringList();               // the bubble shows a preview of the picture
        QCOMPARE(thumbs.size(), 1);
        QImage preview;
        QVERIFY(preview.loadFromData(QByteArray::fromBase64(thumbs.at(0).toLatin1())));
        QCOMPARE(preview.width(), 480);
        QCOMPARE(msgs.at(0).toMap().value("text").toString(), QString("summarise the file"));       // the bubble shows only what the user typed
    }
    // ---- the browser of the program: the bridge, what the agent may open, and the three tools ----------------------------------------------------
    void theBrowserBridgeWaitsForTheViewAnswersAndTimesOut()
    {
        dm::browser::BrowserBridge bridge;
        QSignalSpy wanted(&bridge, &dm::browser::BrowserBridge::viewWanted);
        QSignalSpy command(&bridge, &dm::browser::BrowserBridge::command);
        QVariantMap got;
        int calls = 0;
        bridge.request("open", {{"url", "http://localhost:1/"}}, [&](const QVariantMap& r) { got = r; ++calls; });
        QCOMPARE(wanted.count(), 1);                                                        // the page is asked to make the view
        QCOMPARE(command.count(), 0);                                                       // nothing is sent while there is no view
        bridge.setViewReady(true);
        QCOMPARE(command.count(), 1);
        QCOMPARE(command.at(0).at(1).toString(), QString("open"));
        bridge.reply(command.at(0).at(0).toInt(), {{"ok", true}, {"text", "Opened"}});
        QCOMPARE(calls, 1);
        QCOMPARE(got.value("text").toString(), QString("Opened"));
        bridge.reply(command.at(0).at(0).toInt(), {{"ok", true}});                          // a second answer to the same command is ignored
        QCOMPARE(calls, 1);
        // cancelAll ends every waiting command once
        int cancelled = 0;
        bridge.request("read", {}, [&](const QVariantMap& r) { cancelled += r.value("error").toString() == "cancelled"; });
        bridge.request("read", {}, [&](const QVariantMap& r) { cancelled += r.value("error").toString() == "cancelled"; });
        bridge.cancelAll();
        QCOMPARE(cancelled, 2);
        // a picture is encoded small enough for the model (at most 1024 wide, JPEG)
        QImage big(2000, 1000, QImage::Format_ARGB32);
        big.fill(Qt::darkGreen);
        const QString data = bridge.encodeImage(QVariant::fromValue(big), 1024);
        QVERIFY(!data.isEmpty());
        QImage back;
        QVERIFY(back.loadFromData(QByteArray::fromBase64(data.toLatin1())));
        QCOMPARE(back.width(), 1024);
        QVERIFY(bridge.encodeImage(QVariant(), 1024).isEmpty());
    }
    // A download the user started is saved in the Downloads folder: the name is safe on Windows and never overwrites a file; runnable kinds are flagged.
    void aDownloadGetsASafeUniqueNameAndRunnableKindsAreFlagged()
    {
        dm::browser::BrowserBridge bridge;
        bridge.setDownloadsFolder(m_dir.filePath("dl"));
        const QString folder = bridge.downloadsFolder();
        QCOMPARE(folder, m_dir.filePath("dl"));
        QVERIFY(QDir(folder).exists());                                             // created when it is missing
        QCOMPARE(bridge.uniqueDownloadName(folder, "report.pdf"), QString("report.pdf"));
        QCOMPARE(bridge.uniqueDownloadName(folder, "..\\..\\evil/../a:b*c?.txt"), QString("a_b_c_.txt"));   // no path, no forbidden characters
        QCOMPARE(bridge.uniqueDownloadName(folder, "con.txt"), QString("_con.txt"));                        // a Windows device name
        QCOMPARE(bridge.uniqueDownloadName(folder, "  "), QString("download"));
        QFile one(QDir(folder).filePath("report.pdf"));
        QVERIFY(one.open(QIODevice::WriteOnly));
        one.close();
        QCOMPARE(bridge.uniqueDownloadName(folder, "report.pdf"), QString("report (2).pdf"));             // never over an existing file
        QFile two(QDir(folder).filePath("report (2).pdf"));
        QVERIFY(two.open(QIODevice::WriteOnly));
        two.close();
        QCOMPARE(bridge.uniqueDownloadName(folder, "report.pdf"), QString("report (3).pdf"));
        QVERIFY(bridge.uniqueDownloadName(folder, QString(300, 'x') + ".zip").size() <= 160);
        QVERIFY(bridge.riskyDownload("setup.EXE") && bridge.riskyDownload("run.ps1") && bridge.riskyDownload("a.lnk"));
        QVERIFY(!bridge.riskyDownload("photo.jpg") && !bridge.riskyDownload("report.pdf") && !bridge.riskyDownload("noextension"));
    }
    void theAgentMayOpenOnlyItsOwnFilesThisComputerAndWhenAllowedThePublicWeb()
    {
        using dm::browser::BrowserBridge;
        QDir().mkpath(m_dir.filePath("site/sub"));
        QFile f(m_dir.filePath("site/index.html"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("<p>x</p>");
        f.close();
        QFile o(m_dir.filePath("outside.html"));
        QVERIFY(o.open(QIODevice::WriteOnly));
        o.write("<p>y</p>");
        o.close();
        const QString folder = m_dir.filePath("site");
        QCOMPARE(BrowserBridge::checkAgentUrl(QUrl::fromLocalFile(m_dir.filePath("site/index.html")), folder, false), QString());
        QVERIFY(!BrowserBridge::checkAgentUrl(QUrl::fromLocalFile(m_dir.filePath("outside.html")), folder, false).isEmpty());            // another folder
        QVERIFY(!BrowserBridge::checkAgentUrl(QUrl::fromLocalFile(m_dir.filePath("site/../outside.html")), folder, true).isEmpty());      // .. out of the folder
        QVERIFY(!BrowserBridge::checkAgentUrl(QUrl::fromLocalFile(m_dir.filePath("site/missing.html")), folder, false).isEmpty());        // not there
        QCOMPARE(BrowserBridge::checkAgentUrl(QUrl("http://localhost:3000/"), folder, false), QString());                                // a dev server of the job
        QCOMPARE(BrowserBridge::checkAgentUrl(QUrl("http://127.0.0.1:8080/x"), folder, false), QString());
        QVERIFY(!BrowserBridge::checkAgentUrl(QUrl("https://example.com/"), folder, false).isEmpty());                                   // the web switch is off
        QCOMPARE(BrowserBridge::checkAgentUrl(QUrl("https://example.com/"), folder, true), QString());
        QVERIFY(!BrowserBridge::checkAgentUrl(QUrl("http://192.168.1.5/"), folder, true).isEmpty());                                     // the local network
        QVERIFY(!BrowserBridge::checkAgentUrl(QUrl("http://user:pw@example.com/"), folder, true).isEmpty());
        QVERIFY(!BrowserBridge::checkAgentUrl(QUrl("ftp://example.com/"), folder, true).isEmpty());
        QVERIFY(!BrowserBridge::checkAgentUrl(QUrl("javascript:alert(1)"), folder, true).isEmpty());
        QVERIFY(!BrowserBridge::checkAgentUrl(QUrl("index.html"), folder, true).isEmpty());                                              // not a full address
    }
    void theBrowserToolsOpenReadAndLookAndAPictureGoesToTheModelOnce()
    {
        QDir().mkpath(m_dir.filePath("brwork"));
        QFile page(m_dir.filePath("brwork/index.html"));
        QVERIFY(page.open(QIODevice::WriteOnly));
        page.write("<p>x</p>");
        page.close();
        dm::browser::BrowserBridge bridge;
        QStringList ops;
        QObject::connect(&bridge, &dm::browser::BrowserBridge::command, &bridge, [&](int id, const QString& op, const QVariantMap& args) {   // a stand-in for the real view
            ops << op + QLatin1Char(':') + args.value("scroll").toString();
            QTimer::singleShot(0, &bridge, [&bridge, id, op] {
                if (op == "look")
                    bridge.reply(id, {{"ok", true}, {"text", "Picture, part 1"}, {"image", "QUJD"}});
                else
                    bridge.reply(id, {{"ok", true}, {"text", "page text of " + op}});
            });
        });
        bridge.setViewReady(true);
        const QString url = QUrl::fromLocalFile(m_dir.filePath("brwork/index.html")).toString();
        FakeServer server;
        server.contentType = "application/json";
        server.queue << agentReply({agentCall("b1", "browser_open", {{"url", url}})}, QString())
                     << agentReply({agentCall("b2", "browser_look", {{"scroll", "top"}})}, QString())
                     << agentReply({agentCall("b3", "browser_look", {{"scroll", "down"}}), agentCall("b4", "browser_read", {})}, QString())
                     << agentReply({}, QStringLiteral("The page looks fine."));
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.filePath("brwork");
        c.browser = &bridge;
        c.vision = true;
        c.reviewWork = false;
        c.history = {{"user", "check the page"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(done.wait(15000));
        QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        QCOMPARE(ops, QStringList({"open:", "look:top", "look:down", "read:"}));
        // the tools were offered
        QStringList names;
        for (const QJsonValue& t : requestBody(server.log.at(0)).value("tools").toArray())
            names << t.toObject().value("function").toObject().value("name").toString();
        QVERIFY(names.contains("browser_open") && names.contains("browser_read") && names.contains("browser_look"));
        // after the first look the model gets the picture as a user message; after the second look the first picture is gone (only one picture stays in the window)
        auto imagesIn = [](const QJsonArray& messages) {
            int n = 0;
            for (const QJsonValue& m : messages)
                if (m.toObject().value("content").isArray())
                    for (const QJsonValue& part : m.toObject().value("content").toArray())
                        n += part.toObject().value("type").toString() == "image_url";
            return n;
        };
        QCOMPARE(imagesIn(requestBody(server.log.at(1)).value("messages").toArray()), 0);        // the request that follows browser_open
        QCOMPARE(imagesIn(requestBody(server.log.at(2)).value("messages").toArray()), 1);        // the first look
        QCOMPARE(imagesIn(requestBody(server.log.at(3)).value("messages").toArray()), 1);        // the second look replaced it
        const QString second = QString::fromUtf8(server.log.at(3));
        QVERIFY(second.contains("an earlier screenshot of the page was removed"));
    }
    void aPlainQuestionCarriesTheAttachedPicturesAsImageParts()
    {
        HttpChat::Request r;
        r.model = "local";
        r.system = "sys";
        r.messages = {{"user", "first"}, {"assistant", "answer"}, {"user", "what is in the picture?"}};
        r.images = {"QUJD", "REVG"};
        const QJsonArray msgs = QJsonDocument::fromJson(HttpChat::body(r)).object().value("messages").toArray();
        QCOMPARE(msgs.size(), 4);
        QVERIFY(msgs.at(1).toObject().value("content").isString());                         // earlier messages stay plain text
        const QJsonArray parts = msgs.last().toObject().value("content").toArray();         // only the last question has the pictures
        QCOMPARE(parts.size(), 3);
        QCOMPARE(parts.at(0).toObject().value("text").toString(), QString("what is in the picture?"));
        QCOMPARE(parts.at(1).toObject().value("image_url").toObject().value("url").toString(), QString("data:image/jpeg;base64,QUJD"));
        r.images.clear();
        QVERIFY(QJsonDocument::fromJson(HttpChat::body(r)).object().value("messages").toArray().last().toObject().value("content").isString());   // no pictures: as before
    }
    void theBrowserWatchesTheFolderOfAPageAndTellsWhenAFileChanged()
    {
        QDir().mkpath(m_dir.filePath("livewatch"));
        auto write = [&](const QString& name, const QByteArray& data) {
            QFile f(m_dir.filePath("livewatch/" + name));
            f.open(QIODevice::WriteOnly);
            f.write(data);
        };
        write("index.html", "<p>one</p>");
        write("notes.txt", "x");                                                            // not a kind of file a page uses: it is not watched
        dm::browser::BrowserBridge bridge;
        QSignalSpy changed(&bridge, &dm::browser::BrowserBridge::liveFilesChanged);
        bridge.watchFolder(m_dir.filePath("livewatch"));
        bridge.watchFolder(m_dir.filePath("livewatch"));                                    // twice is one
        write("notes.txt", "yy");
        QTest::qWait(1200);
        QCOMPARE(changed.count(), 0);
        write("index.html", "<p>two two</p>");
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 1, 5000);
        QCOMPARE(QDir::cleanPath(changed.at(0).at(0).toString()), QDir::cleanPath(m_dir.filePath("livewatch")));
        write("style.css", "p{color:red}");                                                 // a new file of the page
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() >= 2, 5000);
        bridge.unwatchAll();
        const int n = changed.count();
        write("index.html", "<p>three three three</p>");
        QTest::qWait(1200);
        QCOMPARE(changed.count(), n);                                                       // nothing is watched any more
        QCOMPARE(bridge.toLocalFile(QUrl::fromLocalFile(m_dir.filePath("livewatch/index.html")).toString()), m_dir.filePath("livewatch/index.html"));
        QVERIFY(bridge.toLocalFile("https://example.com/").isEmpty());
        QVERIFY(bridge.aiMayInteract("file:///C:/work/index.html") && bridge.aiMayInteract("http://localhost:3000/") && bridge.aiMayInteract("http://127.0.0.1:8080/x"));
        QVERIFY(!bridge.aiMayInteract("https://example.com/") && !bridge.aiMayInteract("http://192.168.1.5/") && !bridge.aiMayInteract("ftp://localhost/") && !bridge.aiMayInteract("about:blank"));
    }
    void aWebPageTheAgentWritesIsShownLiveOncePerPageAndTheNewBrowserToolsPassTheirArguments()
    {
        QDir().mkpath(m_dir.filePath("livejob"));
        dm::browser::BrowserBridge bridge;
        bridge.setCompiledIn(true);
        QSignalSpy preview(&bridge, &dm::browser::BrowserBridge::previewRequested);
        QList<QPair<QString, QVariantMap>> seen;
        QObject::connect(&bridge, &dm::browser::BrowserBridge::command, &bridge, [&](int id, const QString& op, const QVariantMap& args) {
            seen.append({op, args});
            QTimer::singleShot(0, &bridge, [&bridge, id] { bridge.reply(id, {{"ok", true}, {"text", "done"}}); });
        });
        bridge.setViewReady(true);
        FakeServer server;
        server.contentType = "application/json";
        server.queue << agentReply({agentCall("w1", "write_file", {{"path", "index.html"}, {"content", "<p>a</p>"}})}, QString())
                     << agentReply({agentCall("w2", "append_file", {{"path", "index.html"}, {"text", "<p>b</p>"}}), agentCall("w3", "write_file", {{"path", "other.html"}, {"content", "<p>c</p>"}}),
                                    agentCall("w4", "write_file", {{"path", "style.css"}, {"content", "p{}"}})}, QString())
                     << agentReply({agentCall("k1", "browser_click", {{"target", "Go"}}), agentCall("k2", "browser_type", {{"target", "#name"}, {"text", "Somchai"}}),
                                    agentCall("k3", "browser_scroll", {{"direction", "down"}}), agentCall("k4", "browser_drag", {{"from", "#handle"}, {"to", "#track"}}),
                                    agentCall("k5", "browser_tab", {{"action", "switch"}, {"index", 2}}), agentCall("k6", "browser_open", {{"url", "index.html"}, {"new_tab", true}}),
                                    agentCall("k7", "browser_scroll", {{"direction", "sideways"}}), agentCall("k8", "browser_click", {})}, QString())
                     << agentReply({}, QStringLiteral("Done."));
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.filePath("livejob");
        c.allowEdits = true;
        c.browser = &bridge;
        c.reviewWork = false;
        c.history = {{"user", "make and try a page"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(done.wait(20000));
        QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        QCOMPARE(preview.count(), 2);                                                       // index.html (written, then appended: once) and other.html; style.css is not a page
        QVERIFY(preview.at(0).at(0).toString().endsWith("/livejob/index.html") && preview.at(1).at(0).toString().endsWith("/livejob/other.html"));
        QStringList ops;
        for (const auto& s : seen)
            ops << s.first;
        QCOMPARE(ops, QStringList({"click", "type", "scroll", "drag", "tab", "open"}));     // the two wrong calls (direction sideways, no target) never reached the page
        QCOMPARE(seen.at(0).second.value("target").toString(), QString("Go"));
        QCOMPARE(seen.at(1).second.value("text").toString(), QString("Somchai"));
        QCOMPARE(seen.at(2).second.value("direction").toString(), QString("down"));
        QCOMPARE(seen.at(3).second.value("to").toString(), QString("#track"));
        QCOMPARE(seen.at(4).second.value("index").toInt(), 2);
        QVERIFY(seen.at(5).second.value("newTab").toBool());
        QVERIFY(seen.at(5).second.value("url").toString().endsWith("/livejob/index.html"));
        const QJsonArray msgs = requestBody(server.log.at(3)).value("messages").toArray();
        int errors = 0;
        for (const QJsonValue& m : msgs)
            if (m.toObject().value("role").toString() == "tool" && m.toObject().value("content").toString().startsWith("Error:"))
                ++errors;
        QCOMPARE(errors, 2);
    }
    void withoutVisionThereIsNoLookToolAndAnAddressOutsideTheFolderIsRefused()
    {
        QDir().mkpath(m_dir.filePath("brnov"));
        dm::browser::BrowserBridge bridge;
        bridge.setViewReady(true);
        FakeServer server;
        server.contentType = "application/json";
        server.queue << agentReply({agentCall("b1", "browser_open", {{"url", "file:///C:/Windows/win.ini"}}), agentCall("b2", "browser_look", {})}, QString())
                     << agentReply({}, QStringLiteral("Cannot."));
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.filePath("brnov");
        c.browser = &bridge;
        c.vision = false;
        c.reviewWork = false;
        c.history = {{"user", "open it"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(done.wait(15000));
        QStringList names;
        for (const QJsonValue& t : requestBody(server.log.at(0)).value("tools").toArray())
            names << t.toObject().value("function").toObject().value("name").toString();
        QVERIFY(names.contains("browser_open") && names.contains("browser_read") && !names.contains("browser_look"));
        const QJsonArray msgs = requestBody(server.log.at(1)).value("messages").toArray();
        QStringList results;
        for (const QJsonValue& m : msgs)
            if (m.toObject().value("role").toString() == "tool")
                results << m.toObject().value("content").toString();
        QCOMPARE(results.size(), 2);
        QVERIFY2(results.at(0).contains("cannot be opened") && results.at(0).contains("outside the working folder"), qPrintable(results.at(0)));
        QVERIFY(results.at(1).contains("cannot see pictures"));
        // a job without a browser (the page of an ordinary build) is not offered the tools
        FakeServer plain;
        plain.contentType = "application/json";
        plain.queue << agentReply({}, QStringLiteral("ok"));
        LocalAgent agent2(&net);
        QSignalSpy done2(&agent2, &LocalAgent::finished);
        c.baseUrl = plain.base() + "/v1";
        c.browser = nullptr;
        QCOMPARE(agent2.start(c), QString());
        QVERIFY(done2.wait(15000));
        QVERIFY(!QString::fromUtf8(plain.log.at(0)).contains("browser_open"));
    }
    void theVisionFileOfAModelIsFoundNextToItAndIsNotListedAsAModel()
    {
        QDir().mkpath(m_dir.filePath("visionmodels"));
        auto make = [&](const QString& name) {
            QFile f(m_dir.filePath("visionmodels/" + name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QByteArray(1100000, 'x'));
        };
        make("Qwen3.5-4B-Q4_K_M.gguf");
        const QString model = m_dir.filePath("visionmodels/Qwen3.5-4B-Q4_K_M.gguf");
        QVERIFY(dm::ai::ModelStore::visionFileFor(model).isEmpty());
        make("mmproj-F16.gguf");                                                           // the file as Hugging Face names it: also not a model
        make("Qwen3.5-4B-Q4_K_M.mmproj.gguf");
        QCOMPARE(dm::ai::ModelStore::visionFileFor(model), m_dir.filePath("visionmodels/Qwen3.5-4B-Q4_K_M.mmproj.gguf"));
        const auto models = dm::ai::ModelStore::installed({m_dir.filePath("visionmodels")});
        QCOMPARE(models.size(), 1);                                                         // the vision file is not a model of its own
        QCOMPARE(models.at(0).name, QString("Qwen3.5-4B-Q4_K_M.gguf"));
        const dm::ai::VisionFile offer = dm::ai::ModelStore::visionFileOf("Qwen3.5-4B-Q4_K_M.gguf");
        QCOMPARE(offer.fileName, QString("Qwen3.5-4B-Q4_K_M.mmproj.gguf"));
        QVERIFY(offer.url.startsWith("https://huggingface.co/unsloth/Qwen3.5-4B-GGUF/resolve/e87f176") && offer.url.endsWith("/mmproj-F16.gguf"));
        QCOMPARE(offer.sha256.size(), 64);
        QVERIFY(offer.size > 600000000);
        QVERIFY(dm::ai::ModelStore::visionFileOf("Qwen3.5-9B-Q4_K_M.gguf").fileName.isEmpty());   // only the checked file is on offer
    }
    // ---- edit_file, glob_files ------------------------------------------------------------------------------------------------------------
    void editReplacesOneExactPiece()
    {
        QDir().mkpath(m_dir.filePath("editwork/sub"));
        auto put = [&](const QString& rel, const QByteArray& data) {
            QFile f(m_dir.filePath("editwork/" + rel));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
        };
        put("a.txt", "one two\nthree two\n");
        put("sub/page.html", "<p>x</p>");
        AgentTools rw(m_dir.filePath("editwork"), true);
        // not unique / not found / same text / empty: refused, nothing changes
        QVERIFY(rw.run("edit_file", {{"path", "a.txt"}, {"old_string", "two"}, {"new_string", "2"}}).error);
        QVERIFY(rw.run("edit_file", {{"path", "a.txt"}, {"old_string", "nine"}, {"new_string", "9"}}).error);
        QVERIFY(rw.run("edit_file", {{"path", "a.txt"}, {"old_string", "one"}, {"new_string", "one"}}).error);
        QVERIFY(rw.run("edit_file", {{"path", "a.txt"}, {"old_string", ""}, {"new_string", "x"}}).error);
        QVERIFY(rw.run("edit_file", {{"path", "../a.txt"}, {"old_string", "one"}, {"new_string", "1"}}).error);
        QCOMPARE(QFile(m_dir.filePath("editwork/a.txt")).size(), qint64(18));
        // the diff the user sees before it happens
        const AgentTools::WriteInfo info = rw.inspectEdit({{"path", "a.txt"}, {"old_string", "one two"}, {"new_string", "uno"}});
        QVERIFY(info.ok && info.exists);
        QVERIFY(info.preview.contains("- one two") && info.preview.contains("+ uno"));
        QCOMPARE(QFile(m_dir.filePath("editwork/a.txt")).size(), qint64(18));                  // looking changed nothing
        // a unique piece, then every occurrence
        QVERIFY(!rw.run("edit_file", {{"path", "a.txt"}, {"old_string", "one"}, {"new_string", "1"}}).error);
        QVERIFY(!rw.run("edit_file", {{"path", "a.txt"}, {"old_string", "two"}, {"new_string", "2"}, {"replace_all", true}}).error);
        QFile f(m_dir.filePath("editwork/a.txt"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(f.readAll()), QString("1 2\nthree 2\n"));
        QVERIFY(!QDir(m_dir.filePath("editwork/.drivevault-backup")).entryList(QDir::Files).isEmpty());   // the old versions are kept
        // read only: no edit
        QVERIFY(AgentTools(m_dir.filePath("editwork"), false).run("edit_file", {{"path", "a.txt"}, {"old_string", "1"}, {"new_string", "x"}}).error);
        // glob: a name in any folder, or a path pattern
        const QString byName = rw.run("glob_files", {{"pattern", "*.html"}}).text;
        QCOMPARE(byName, QString("sub/page.html"));
        QCOMPARE(rw.run("glob_files", {{"pattern", "sub/*.html"}}).text, QString("sub/page.html"));
        QCOMPARE(rw.run("glob_files", {{"pattern", "**/*.txt"}}).text, QString("a.txt"));
        QCOMPARE(rw.run("glob_files", {{"pattern", "*.zip"}}).text, QString("no file matches"));
    }

    // ---- run_command ---------------------------------------------------------------------------------------------------------------------
    void aCommandRunsInTheFolderAndReports()
    {
        QDir().mkpath(m_dir.filePath("cmdwork"));
        CommandRunner runner;
        auto runIt = [&](const QString& command, int seconds, QString* text, bool* error) {
            bool finished = false;
            QVERIFY(runner.run(m_dir.filePath("cmdwork"), command, seconds, [&](const QString& t, bool e) { *text = t; *error = e; finished = true; }));
            QVERIFY(!runner.run(m_dir.filePath("cmdwork"), "echo second", 5, nullptr));          // one at a time
            QTRY_VERIFY_WITH_TIMEOUT(finished, 20000);
        };
        QString text;
        bool error = false;
        runIt("echo hello& cd", 20, &text, &error);
        QVERIFY(!error);
        QVERIFY(text.startsWith("exit code 0"));
        QVERIFY(text.contains("hello"));
        QVERIFY(text.contains("cmdwork"));                                                       // it starts in the working folder
        runIt("exit 3", 20, &text, &error);
        QVERIFY(error);
        QVERIFY(text.startsWith("exit code 3"));
        runIt("ping -n 30 127.0.0.1 > nul", 1, &text, &error);                                   // stopped after the time limit
        QVERIFY(error);
        QVERIFY(text.contains("time limit"));
        // stopped by the user
        bool finished = false;
        QVERIFY(runner.run(m_dir.filePath("cmdwork"), "ping -n 30 127.0.0.1 > nul", 60, [&](const QString& t, bool e) { text = t; error = e; finished = true; }));
        runner.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(finished, 20000);
        QVERIFY(error && text.contains("stopped by the user"));
        // long output keeps the beginning and the end
        const QString big = QString(20000, QLatin1Char('a')) + QStringLiteral("END");
        const QString cut = CommandRunner::cut(big);
        QVERIFY(cut.size() < big.size() && cut.endsWith("END") && cut.contains("left out"));
        QCOMPARE(CommandRunner::cut("short"), QString("short"));
    }
    void everyCommandWaitsForTheUser()
    {
        QDir().mkpath(m_dir.filePath("cmdask"));
        for (const bool allow : {false, true}) {
            FakeServer server;
            server.contentType = "application/json";
            server.queue << agentReply({agentCall("r1", "run_command", {{"command", "echo from-the-agent"}})}, QString())
                         << agentReply({}, QStringLiteral("Done."));
            QNetworkAccessManager net;
            LocalAgent agent(&net);
            QSignalSpy asked(&agent, &LocalAgent::commandApprovalNeeded);
            QSignalSpy done(&agent, &LocalAgent::finished);
            LocalAgent::Config c;
            c.baseUrl = server.base() + "/v1";
            c.folder = m_dir.filePath("cmdask");
            c.allowEdits = true;
            c.approveWrites = true;
            c.allowCommands = true;
            c.history = {{"user", "run it"}};
            QCOMPARE(agent.start(c), QString());
            QVERIFY(asked.wait(15000));
            QCOMPARE(asked.at(0).at(0).toString(), QString("echo from-the-agent"));
            QVERIFY(agent.waitingForApproval());
            QCOMPARE(server.requests, 1);                                                        // nothing runs and the model is not asked again while the user decides
            agent.answerApproval(allow);
            QVERIFY(done.wait(30000));
            QVERIFY(done.at(0).at(0).toBool());
            const QString toolText = requestBody(server.log.at(1)).value("messages").toArray().last().toObject().value("content").toString();
            QCOMPARE(toolText.contains("from-the-agent"), allow);
            QCOMPARE(toolText.contains("did not allow"), !allow);
        }
        // not offered, not run: a mode without commands
        FakeServer server;
        server.contentType = "application/json";
        server.queue << agentReply({agentCall("r1", "run_command", {{"command", "echo no"}})}, QString()) << agentReply({}, QStringLiteral("ok"));
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.filePath("cmdask");
        c.history = {{"user", "run it"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(done.wait(15000));
        QVERIFY(requestBody(server.log.at(1)).value("messages").toArray().last().toObject().value("content").toString().contains("not allowed"));
    }

    // ---- the task list and the plan --------------------------------------------------------------------------------------------------------
    void theTaskListGoesToThePage()
    {
        FakeServer server;
        server.contentType = "application/json";
        const QJsonArray todos{QJsonObject{{"content", "read files"}, {"status", "completed"}}, QJsonObject{{"content", "write page"}, {"status", "in_progress"}},
                               QJsonObject{{"content", "check"}, {"status", "bogus"}}};
        server.queue << agentReply({agentCall("t1", "todo_write", {{"todos", todos}})}, QString()) << agentReply({}, QStringLiteral("ok"));
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QSignalSpy changed(&agent, &LocalAgent::todosChanged);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.path();
        c.history = {{"user", "go"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(done.wait(15000));
        QCOMPARE(changed.count(), 1);
        const QVariantList list = changed.at(0).at(0).toList();
        QCOMPARE(list.size(), 3);
        QCOMPARE(list.at(0).toMap().value("status").toString(), QString("completed"));
        QCOMPARE(list.at(1).toMap().value("status").toString(), QString("in_progress"));
        QCOMPARE(list.at(2).toMap().value("status").toString(), QString("pending"));            // an unknown status becomes pending
    }
    void aPlanMustBeApprovedBeforeAnythingChanges()
    {
        QDir().mkpath(m_dir.filePath("planwork"));
        for (const bool approve : {false, true}) {
            FakeServer server;
            server.contentType = "application/json";
            server.queue << agentReply({agentCall("p1", "exit_plan_mode", {{"plan", "1. write index.html"}})}, QString()) << agentReply({}, QStringLiteral("ok"));
            QNetworkAccessManager net;
            LocalAgent agent(&net);
            QSignalSpy asked(&agent, &LocalAgent::planApprovalNeeded);
            QSignalSpy approved(&agent, &LocalAgent::planApproved);
            QSignalSpy done(&agent, &LocalAgent::finished);
            LocalAgent::Config c;
            c.baseUrl = server.base() + "/v1";
            c.folder = m_dir.filePath("planwork");
            c.planMode = true;
            c.history = {{"user", "make a page"}};
            QCOMPARE(agent.start(c), QString());
            QVERIFY(asked.wait(15000));
            QCOMPARE(asked.at(0).at(0).toString(), QString("1. write index.html"));
            QCOMPARE(server.requests, 1);
            // in plan mode the model has no way to change anything
            QStringList names;
            for (const QJsonValue& t : requestBody(server.log.at(0)).value("tools").toArray())
                names << t.toObject().value("function").toObject().value("name").toString();
            QVERIFY(names.contains("exit_plan_mode") && !names.contains("write_file") && !names.contains("edit_file") && !names.contains("run_command"));
            agent.answerApproval(approve);
            QVERIFY(done.wait(15000));
            QCOMPARE(approved.count(), approve ? 1 : 0);
            const QString toolText = requestBody(server.log.at(1)).value("messages").toArray().last().toObject().value("content").toString();
            QCOMPARE(toolText.contains("approved"), approve);
            // after the approval the next request offers the change tools (and the plan tool is gone)
            QStringList after;
            for (const QJsonValue& t : requestBody(server.log.at(1)).value("tools").toArray())
                after << t.toObject().value("function").toObject().value("name").toString();
            QCOMPARE(after.contains("edit_file"), approve);
            QCOMPARE(after.contains("run_command"), approve);
            QCOMPARE(after.contains("exit_plan_mode"), !approve);
        }
    }

    // ---- slash commands of the agent page --------------------------------------------------------------------------------------------------
    void slashCommandsStayOnThisPage()
    {
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hubslash");
        dm::Hub hub(o);
        AiService ai(&hub);
        auto* agent = qobject_cast<AiChannel*>(ai.agent());
        agent->setFolder(m_dir.filePath("slashwork"));                                          // not ready (no model needed): the commands still work
        QCOMPARE(agent->send("/mode plan"), QString());
        QCOMPARE(agent->permission(), QString("plan"));
        QCOMPARE(agent->send("/mode accept"), QString());
        QCOMPARE(agent->permission(), QString("edit"));
        QCOMPARE(agent->send("/mode nonsense"), QString());                                      // an answer that says the choices, nothing changes
        QCOMPARE(agent->permission(), QString("edit"));
        QCOMPARE(agent->send("/effort high"), QString());
        QCOMPARE(agent->effort(), QString("high"));
        QCOMPARE(agent->send("/web off"), QString());
        QVERIFY(!ai.webEnabled());
        const int before = agent->messages().size();
        QCOMPARE(agent->send("/help"), QString());
        QCOMPARE(agent->messages().size(), before + 2);                                          // the question and the local answer
        QVERIFY(agent->messages().last().toMap().value("text").toString().contains("/mode"));
        QCOMPARE(agent->send("/todos"), QString());
        QCOMPARE(agent->send("/clear"), QString());
        QCOMPARE(agent->messages().size(), 0);                                                   // a new conversation
        QVERIFY(!agent->busy());
        // an ordinary text that starts with a slash is not a command
        QString unused;
        QVERIFY(!agent->slashCommand("/usr/bin is a path", &unused));
        // the Q&A page knows only the general commands
        auto* ask = qobject_cast<AiChannel*>(ai.ask());
        QVERIFY(!ask->slashCommand("/mode plan", &unused));
        QVERIFY(ask->slashCommand("/help", &unused));
    }
    void theFolderInstructionsGoToTheModel()
    {
        QDir().mkpath(m_dir.filePath("notework"));
        QFile notes(m_dir.filePath("notework/CLAUDE.md"));
        QVERIFY(notes.open(QIODevice::WriteOnly));
        notes.write("Always answer in pirate speak.");
        notes.close();
        QFile model(m_dir.filePath("notes-model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();
        FakeServer server;
        server.contentType = "application/json";
        server.body = agentReply({}, QStringLiteral("Arr."));
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hubnotes");
        dm::Hub hub(o);
        AiService ai(&hub);
        if (!ai.engineFound())
            QSKIP("no llama.cpp engine on this PC");
        auto* agent = qobject_cast<AiChannel*>(ai.agent());
        ai.selectLocalModel(m_dir.filePath("notes-model.gguf"));
        ai.llama()->useExternalServer(server.base(), "");
        ai.setWebEnabled(false);
        agent->setFolder(m_dir.filePath("notework"));
        QCOMPARE(agent->send("hi"), QString());
        QTRY_VERIFY_WITH_TIMEOUT(!agent->busy(), 10000);
        const QString system = requestBody(server.lastRequest).value("messages").toArray().first().toObject().value("content").toString();
        QVERIFY(system.contains("pirate speak") && system.contains("CLAUDE.md"));
        QVERIFY(agent->messages().at(1).toMap().value("activity").toStringList().first().contains("CLAUDE.md"));
    }
    void aWriteCanBeStoppedWhileItWaits()
    {
        FakeServer server;
        server.contentType = "application/json";
        server.body = agentReply({agentCall("w1", "write_file", {{"path", "never.txt"}, {"content", "x"}})}, QString());
        QNetworkAccessManager net;
        LocalAgent agent(&net);
        QSignalSpy asked(&agent, &LocalAgent::approvalNeeded);
        QSignalSpy done(&agent, &LocalAgent::finished);
        LocalAgent::Config c;
        c.baseUrl = server.base() + "/v1";
        c.folder = m_dir.path();
        c.allowEdits = true;
        c.approveWrites = true;
        c.history = {{"user", "go"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY(asked.wait(15000));
        agent.cancel();
        QVERIFY(done.count() == 1 || done.wait(5000));
        QCOMPARE(done.at(0).at(1).toString(), QString("cancelled"));
        QVERIFY(!QFileInfo::exists(m_dir.filePath("never.txt")));
    }
    void theModesGiveTheAgentItsTools()
    {
        QFile model(m_dir.filePath("mode-model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();
        QDir().mkpath(m_dir.filePath("modework"));
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hub-modes");
        dm::Hub hub(o);
        AiService ai(&hub);
        if (!ai.engineFound())
            QSKIP("no llama.cpp engine on this PC");
        auto* agent = qobject_cast<AiChannel*>(ai.agent());
        QCOMPARE(agent->permission(), QString("edit"));                                     // the default: automatic
        agent->setPermission("bogus");
        QCOMPARE(agent->permission(), QString("edit"));
        ai.selectLocalModel(m_dir.filePath("mode-model.gguf"));
        agent->setFolder(m_dir.filePath("modework"));
        ai.setWebEnabled(false);
        // read: 4 file tools + todo_write; plan: + exit_plan_mode; ask / edit: + edit_file, write_file, append_file, run_command, command_output, stop_command
        struct Case { const char* mode; int tools; bool plan; };
        for (const Case& k : {Case{"read", 5, false}, Case{"plan", 6, true}, Case{"ask", 11, false}, Case{"edit", 11, false}}) {
            FakeServer server;
            server.contentType = "application/json";
            server.body = agentReply({}, QStringLiteral("ok"));
            ai.llama()->useExternalServer(server.base(), "");
            agent->newConversation();
            agent->setPermission(k.mode);
            QCOMPARE(agent->permission(), QString(k.mode));
            QCOMPARE(agent->send("hello"), QString());
            QTRY_VERIFY_WITH_TIMEOUT(!agent->busy(), 10000);
            const QJsonObject body = requestBody(server.lastRequest);
            QCOMPARE(body.value("tools").toArray().size(), k.tools);                       // changes and commands only in ask and edit, the plan tool only in plan
            QStringList names;
            for (const QJsonValue& t : body.value("tools").toArray())
                names << t.toObject().value("function").toObject().value("name").toString();
            QCOMPARE(names.contains("write_file"), QString(k.mode) == "ask" || QString(k.mode) == "edit");
            QCOMPARE(names.contains("run_command"), QString(k.mode) == "ask" || QString(k.mode) == "edit");
            QCOMPARE(names.contains("exit_plan_mode"), k.plan);
            const QString system = body.value("messages").toArray().first().toObject().value("content").toString();
            QCOMPARE(system.contains("PLAN"), k.plan);
        }
    }
    void theChannelAsksAndWritesOnAllow()
    {
        QFile model(m_dir.filePath("ask-model.gguf"));
        QVERIFY(model.open(QIODevice::WriteOnly));
        model.write("x");
        model.close();
        QDir().mkpath(m_dir.filePath("askpage"));
        FakeServer server;
        server.contentType = "application/json";
        server.queue << agentReply({agentCall("w1", "write_file", {{"path", "note.txt"}, {"content", "from the AI"}})}, QString())
                     << agentReply({}, QStringLiteral("Saved the note."));
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hub-askpage");
        dm::Hub hub(o);
        AiService ai(&hub);
        if (!ai.engineFound())
            QSKIP("no llama.cpp engine on this PC");
        auto* agent = qobject_cast<AiChannel*>(ai.agent());
        ai.selectLocalModel(m_dir.filePath("ask-model.gguf"));
        ai.llama()->useExternalServer(server.base(), "");
        ai.setWebEnabled(false);
        agent->setFolder(m_dir.filePath("askpage"));
        agent->setPermission("ask");                                                        // the mode that asks (the default is automatic since 1.23.0)
        QCOMPARE(agent->send("write a note"), QString());
        QTRY_VERIFY_WITH_TIMEOUT(agent->status() == "waiting", 10000);
        const QVariantMap approval = agent->messages().at(1).toMap().value("approval").toMap();
        QCOMPARE(approval.value("path").toString(), QString("note.txt"));
        QCOMPARE(approval.value("replaces").toBool(), false);
        QVERIFY(agent->busy());
        QVERIFY(!QFileInfo::exists(m_dir.filePath("askpage/note.txt")));
        agent->answerApproval(true);
        QTRY_VERIFY_WITH_TIMEOUT(!agent->busy(), 10000);
        QFile written(m_dir.filePath("askpage/note.txt"));
        QVERIFY(written.open(QIODevice::ReadOnly));
        QCOMPARE(written.readAll(), QByteArray("from the AI"));
        const QVariantMap reply = agent->messages().at(1).toMap();
        QCOMPARE(reply.value("text").toString(), QString("Saved the note."));
        QVERIFY(reply.value("approval").toMap().isEmpty());
    }

    // The real model in "ask first" mode: it must ask before it writes, and the file appears only after the user allows it.
    void realLocalAgentAsksBeforeWriting()
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
        QDir().mkpath(m_dir.filePath("realwrite"));
        QNetworkAccessManager net;
        LlamaServer server(&net);
        server.start(model);
        QTRY_VERIFY_WITH_TIMEOUT(server.state() == LlamaServer::State::Ready || server.state() == LlamaServer::State::Failed, 240000);
        QVERIFY2(server.state() == LlamaServer::State::Ready, qPrintable(server.error()));
        LocalAgent agent(&net);
        QSignalSpy asked(&agent, &LocalAgent::approvalNeeded);
        QSignalSpy done(&agent, &LocalAgent::finished);
        QString text;
        connect(&agent, &LocalAgent::piece, [&](const QString& t) { text += t; });
        LocalAgent::Config c;
        c.baseUrl = server.baseUrl() + "/v1";
        c.apiKey = server.apiKey();
        c.folder = m_dir.filePath("realwrite");
        c.allowEdits = true;
        c.approveWrites = true;
        c.system = "You work inside the folder the user chose, with tools. Do what the user asks with the tools, then say briefly what you did.";
        c.history = {{"user", "Create a file named greeting.txt that contains exactly: Hello from the agent"}};
        QCOMPARE(agent.start(c), QString());
        QVERIFY2(asked.wait(240000), "the model never asked to write");
        fprintf(stderr, "REAL ASK: wants to write %s (%lld bytes)\n", qPrintable(asked.at(0).at(0).toString()), asked.at(0).at(2).toLongLong());
        QVERIFY(!QFileInfo::exists(m_dir.filePath("realwrite/greeting.txt")));                // not yet
        agent.answerApproval(true);
        QVERIFY(done.wait(240000));
        QVERIFY2(done.at(0).at(0).toBool(), qPrintable(done.at(0).at(1).toString()));
        QFile f(m_dir.filePath("realwrite/greeting.txt"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString content = QString::fromUtf8(f.readAll());
        fprintf(stderr, "REAL ASK: file content=[%s] answer=%s\n", qPrintable(content.trimmed()), qPrintable(text.left(120)));
        QVERIFY(content.contains("Hello from the agent"));
        server.stop();
    }

    // The real local model as an agent: it must call the file tool by itself, and (with DM_TEST_WEB=1) the web tool.
    // the job that failed for the owner: a whole landing page, with the real model through the whole page code (prompt, streaming, parts, compaction).
    // Opt in: DM_TEST_REAL_LLM=1 and DM_TEST_MODEL=<a .gguf>; it takes minutes and only reports (it fails only when no file was written).
    void realLocalBigPage()
    {
        if (qEnvironmentVariable("DM_TEST_REAL_LLM") != QLatin1String("1") || qEnvironmentVariable("DM_TEST_MODEL").isEmpty())
            QSKIP("set DM_TEST_REAL_LLM=1 and DM_TEST_MODEL=<model.gguf> to run the real local model");
        QDir().mkpath(m_dir.filePath("realpage"));
        dm::Hub::Options o;
        o.registryRoot = kRoot;
        o.dataDir = m_dir.filePath("hubrealpage");
        dm::Hub hub(o);
        AiService ai(&hub);
        auto* agent = qobject_cast<AiChannel*>(ai.agent());
        ai.selectLocalModel(qEnvironmentVariable("DM_TEST_MODEL"));
        ai.setWebEnabled(false);
        agent->setFolder(m_dir.filePath("realpage"));
        agent->setPermission("edit");                                                           // no questions: this is a test of the work itself
        QElapsedTimer clock;
        clock.start();
        int seenSteps = 0;
        QString lastLive;
        connect(agent, &AiChannel::messagesChanged, [&] {                                      // a time line on stderr: every new step and what is being written
            const QVariantMap last = agent->messages().last().toMap();
            const QVariantList sl = last.value("stepList").toList();
            for (; seenSteps < sl.size(); ++seenSteps)
                fprintf(stderr, "[%4lld s] step %d: %s %s\n", clock.elapsed() / 1000, seenSteps + 1, qPrintable(sl.at(seenSteps).toMap().value("kind").toString()),
                        qPrintable(sl.at(seenSteps).toMap().value("target").toString().left(60)));
            const QVariantMap live = last.value("live").toMap();
            const QString l = live.value("phase").toString() + " " + live.value("tool").toString() + " " + live.value("target").toString() + " " + QString::number(live.value("chars").toInt() / 2000 * 2000);
            if (l != lastLive) {
                lastLive = l;
                fprintf(stderr, "[%4lld s]   live: %s\n", clock.elapsed() / 1000, qPrintable(l));
            }
        });
        QCOMPARE(agent->send("สร้างไฟล์ index.html เป็นหน้า landing page ที่สวยงาม ดูแพง ซึ่งจะแนะนำเกี่ยวกับสวนสาธารณะเทศบาลนครหาดใหญ่ มีส่วนหัว เมนู Hero ส่วนแนะนำ และส่วนท้าย ใส่ CSS ในไฟล์เดียวกัน"), QString());
        QTRY_VERIFY_WITH_TIMEOUT(!agent->busy(), 1200000);
        const QVariantMap reply = agent->messages().last().toMap();
        const QVariantList steps = reply.value("stepList").toList();
        QStringList lines;
        for (const QVariant& s : steps)
            lines << s.toMap().value("kind").toString() + " " + s.toMap().value("target").toString() + " [" + s.toMap().value("state").toString() + "] " + s.toMap().value("note").toString().left(60);
        QFile page(m_dir.filePath("realpage/index.html"));
        const qint64 size = page.exists() ? page.size() : -1;
        fprintf(stderr, "REAL PAGE: %lld s, error=%s, index.html=%lld bytes\nSTEPS:\n%s\nANSWER: %s\n", clock.elapsed() / 1000, qPrintable(reply.value("errorCode").toString()), size,
                qPrintable(lines.join("\n")), qPrintable(reply.value("text").toString().left(300)));
        QVERIFY2(size > 0, "no index.html was written");
    }
    void realLocalAgentUsesTools()
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
        QDir().mkpath(m_dir.filePath("realwork"));
        QFile f(m_dir.filePath("realwork/shopping.txt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("Shopping list\n- 3 apples\n- 2 liters of milk\n- bread\n");
        f.close();
        QNetworkAccessManager net;
        LlamaServer server(&net);
        server.start(model);
        QTRY_VERIFY_WITH_TIMEOUT(server.state() == LlamaServer::State::Ready || server.state() == LlamaServer::State::Failed, 240000);
        QVERIFY2(server.state() == LlamaServer::State::Ready, qPrintable(server.error()));

        auto ask = [&](const QString& question, bool web, QString* text, QStringList* activity, QString* code) {
            LocalAgent agent(&net);
            connect(&agent, &LocalAgent::piece, [&](const QString& t) { *text += t; });
            connect(&agent, &LocalAgent::activity, [&](const QString& a) { *activity << a; });
            QSignalSpy done(&agent, &LocalAgent::finished);
            LocalAgent::Config c;
            c.baseUrl = server.baseUrl() + "/v1";
            c.apiKey = server.apiKey();
            c.folder = m_dir.filePath("realwork");
            c.web = web;
            c.system = "You are an assistant that works inside the folder the user chose, with tools. Look at the files before you answer. Answer briefly.";
            c.history = {{"user", question}};
            QCOMPARE(agent.start(c), QString());
            QVERIFY(done.wait(300000));
            *code = done.at(0).at(1).toString();
        };
        QString text, code;
        QStringList activity;
        ask("How many liters of milk are on my shopping list? Look in the folder.", false, &text, &activity, &code);
        fprintf(stderr, "REAL AGENT files: code=%s activity=%s answer=%s\n", qPrintable(code), qPrintable(activity.join(" | ")), qPrintable(text.left(200)));
        QVERIFY2(code.isEmpty(), qPrintable(code));
        QVERIFY(!activity.isEmpty());
        QVERIFY(text.contains("2"));

        if (qEnvironmentVariable("DM_TEST_WEB") == QLatin1String("1")) {
            text.clear();
            activity.clear();
            ask("Search the web to find out what the capital city of Thailand is, then answer in one sentence.", true, &text, &activity, &code);
            fprintf(stderr, "REAL AGENT web: code=%s activity=%s answer=%s\n", qPrintable(code), qPrintable(activity.join(" | ")), qPrintable(text.left(200)));
            QVERIFY2(code.isEmpty(), qPrintable(code));
            QVERIFY(activity.join(" ").contains("Web search"));
            QVERIFY(text.contains("Bangkok", Qt::CaseInsensitive) || text.contains("กรุงเทพ"));
        }
        server.stop();
    }

    // ---- keys ------------------------------------------------------------------------------------------------------------------

    // ---- the two pages ----------------------------------------------------------------------------------------------------------
};

QTEST_MAIN(TstAi)
#include "tst_ai.moc"
