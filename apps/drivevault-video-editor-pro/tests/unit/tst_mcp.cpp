// MCP server: the JSON-RPC protocol, the tools on a real project with real media (undo included), the AI switch, and the HTTP layer
// (token, localhost, no browser origin) over a real socket.

#include "TestMedia.h"

#include "ai/AiController.h"
#include "core/AppPaths.h"
#include "mcp/McpServer.h"
#include "project/ProjectManager.h"
#include "project/RecentProjectsModel.h"
#include "settings/SettingsManager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

using namespace dv;

class TstMcp : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    std::unique_ptr<SettingsManager> m_settings;
    std::unique_ptr<RecentProjectsModel> m_recents;
    std::unique_ptr<ProjectManager> m_pm;
    std::unique_ptr<AiController> m_ai;
    std::unique_ptr<McpServer> m_mcp;

    QString p(const QString& name) const { return m_dir.filePath(name); }

    QJsonObject rpc(const QString& method, const QJsonObject& params = {}, int id = 1)
    {
        QJsonObject req{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", params}};
        const QByteArray reply = m_mcp->handleMessage(QJsonDocument(req).toJson(QJsonDocument::Compact));
        return QJsonDocument::fromJson(reply).object();
    }
    // Calls a tool; returns {isError, text, json}.
    struct Result { bool isError = false; QString text; QJsonObject json; };
    Result tool(const QString& name, const QJsonObject& args = {})
    {
        const QJsonObject res = rpc("tools/call", {{"name", name}, {"arguments", args}}).value("result").toObject();
        Result r;
        r.isError = res.value("isError").toBool();
        r.text = res.value("content").toArray().at(0).toObject().value("text").toString();
        r.json = QJsonDocument::fromJson(r.text.toUtf8()).object();
        return r;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        paths::setOverrideRoot(p("app"));
        m_settings = std::make_unique<SettingsManager>();
        m_settings->load();
        m_recents = std::make_unique<RecentProjectsModel>(m_settings.get());
        m_recents->load();
        m_pm = std::make_unique<ProjectManager>(m_settings.get(), m_recents.get());
        m_ai = std::make_unique<AiController>(m_pm.get(), m_settings.get());
        m_mcp = std::make_unique<McpServer>(m_pm.get(), m_ai.get(), m_settings.get());
    }
    void cleanupTestCase()
    {
        m_mcp.reset();
        m_ai.reset();
        m_pm.reset();
        paths::setOverrideRoot({});
    }

    // ---- protocol ---------------------------------------------------------------------------------------------------
    void initializeNegotiatesVersionAndAdvertisesTools()
    {
        const QJsonObject r = rpc("initialize", {{"protocolVersion", "2025-03-26"}}).value("result").toObject();
        QCOMPARE(r.value("protocolVersion").toString(), QString("2025-03-26"));
        QVERIFY(r.value("capabilities").toObject().contains("tools"));
        QCOMPARE(r.value("serverInfo").toObject().value("name").toString(), QString("drivevault-video-editor-pro"));
        // an unknown (future) version gets the newest one this server speaks
        QCOMPARE(rpc("initialize", {{"protocolVersion", "2099-01-01"}}).value("result").toObject().value("protocolVersion").toString(), QString("2025-06-18"));
    }
    void notificationsGetNoResponseAndErrorsAreJsonRpc()
    {
        QVERIFY(m_mcp->handleMessage(R"({"jsonrpc":"2.0","method":"notifications/initialized"})").isEmpty());
        QCOMPARE(rpc("no/such/method").value("error").toObject().value("code").toInt(), -32601);
        QCOMPARE(rpc("tools/call", {{"name", "format_disk"}}).value("error").toObject().value("code").toInt(), -32602);
        const QJsonObject parse = QJsonDocument::fromJson(m_mcp->handleMessage("not json")).object();
        QCOMPARE(parse.value("error").toObject().value("code").toInt(), -32700);
        QVERIFY(rpc("ping").value("result").isObject());
    }
    void toolListIsCompleteAndEveryToolHasASchema()
    {
        const QJsonArray tools = rpc("tools/list").value("result").toObject().value("tools").toArray();
        QSet<QString> names;
        for (const QJsonValue& t : tools) {
            const QJsonObject o = t.toObject();
            names.insert(o.value("name").toString());
            QVERIFY(!o.value("description").toString().isEmpty());
            QCOMPARE(o.value("inputSchema").toObject().value("type").toString(), QString("object"));
        }
        for (const char* expected : {"get_project_info", "list_tracks", "list_media", "list_clips", "import_media", "add_clip", "move_clip", "trim_clip", "split_at",
                                     "delete_clips", "add_subtitle", "add_marker", "set_playhead", "undo", "redo", "save_project", "open_project", "create_project",
                                     "get_ai_status", "detect_silence", "apply_silence_cuts", "transcribe", "apply_captions"})
            QVERIFY2(names.contains(QLatin1String(expected)), expected);
        // nothing that runs programs or deletes files
        for (const QString& n : names)
            QVERIFY2(!n.contains("shell") && !n.contains("exec") && !n.contains("delete_file"), qPrintable(n));
    }

    // ---- tools on a real project --------------------------------------------------------------------------------------
    void toolsNeedAProjectFirst()
    {
        QVERIFY(!tool("get_project_info").isError);
        QCOMPARE(tool("get_project_info").json.value("open").toBool(), false);
        QVERIFY(tool("list_clips").isError);
        QVERIFY(tool("add_clip", {{"media_id", "x"}, {"start_frame", 0}}).isError);
    }
    void editingFlowWithRealMediaAndUndo()
    {
        QVERIFY(!tool("create_project", {{"name", "McpTest"}, {"folder", p("projects")}, {"width", 640}, {"height", 360}}).isError);
        const QJsonObject info = tool("get_project_info").json;
        QCOMPARE(info.value("open").toBool(), true);
        QCOMPARE(info.value("name").toString(), QString("McpTest"));
        QCOMPARE(info.value("width").toInt(), 640);

        QVERIFY(testmedia::makeTwoColorVideo(p("clip.mp4"), "red", "blue", true, "320x240", 30, 2));      // 4 s with sound
        // wrong arguments are reported, not guessed
        QVERIFY(tool("import_media", {{"path", "relative.mp4"}}).isError);
        QVERIFY(tool("import_media", {{"path", p("missing.mp4")}}).isError);
        QVERIFY(tool("import_media", {{"path", 5}}).isError);
        const Result imported = tool("import_media", {{"path", p("clip.mp4")}});
        QVERIFY2(!imported.isError, qPrintable(imported.text));
        const QString mediaId = imported.json.value("mediaId").toString();
        QVERIFY(!mediaId.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(m_pm->media()->isReady(mediaId), 15000);
        m_pm->media()->waitForIdle();
        const QJsonArray media = tool("list_media").json.value("media").toArray();
        QCOMPARE(media.size(), 1);
        QCOMPARE(media.at(0).toObject().value("id").toString(), mediaId);
        QVERIFY(std::abs(media.at(0).toObject().value("durationSeconds").toDouble() - 4.0) < 0.3);

        const Result added = tool("add_clip", {{"media_id", mediaId}, {"start_frame", 0}});
        QVERIFY2(!added.isError, qPrintable(added.text));
        QCOMPARE(added.json.value("newClipIds").toArray().size(), 2);               // video + its audio
        QCOMPARE(tool("list_clips").json.value("clips").toArray().size(), 2);
        QVERIFY(!tool("list_tracks").json.value("tracks").toArray().isEmpty());

        // split in the middle: every clip under the frame is cut in two
        QVERIFY(!tool("split_at", {{"frame", 60}}).isError);
        const QJsonArray afterSplit = tool("list_clips").json.value("clips").toArray();
        QCOMPARE(afterSplit.size(), 4);
        QVERIFY(tool("get_project_info").json.value("canUndo").toBool());
        // Undo is exact: back to the two original clips
        QVERIFY(!tool("undo").isError);
        QCOMPARE(tool("list_clips").json.value("clips").toArray().size(), 2);
        QVERIFY(!tool("redo").isError);
        QCOMPARE(tool("list_clips").json.value("clips").toArray().size(), 4);

        // delete: unknown ids delete nothing, known ids go
        QVERIFY(tool("delete_clips", {{"clip_ids", QJsonArray{"nope"}}}).isError);
        QCOMPARE(tool("list_clips").json.value("clips").toArray().size(), 4);
        const QJsonObject last = afterSplit.at(0).toObject();
        QVERIFY(!tool("delete_clips", {{"clip_ids", QJsonArray{last.value("id").toString()}}}).isError);
        QVERIFY(tool("list_clips").json.value("clips").toArray().size() < 4);

        // subtitles and markers
        QVERIFY(tool("add_subtitle", {{"start_frame", 0}, {"duration_frames", 30}, {"text", ""}}).isError);
        const Result sub = tool("add_subtitle", {{"start_frame", 0}, {"duration_frames", 30}, {"text", QStringLiteral("สวัสดีครับ")}});
        QVERIFY2(!sub.isError, qPrintable(sub.text));
        QCOMPARE(m_pm->timeline()->subtitleCount(), 1);
        QVERIFY(!tool("add_marker", {{"frame", 10}, {"name", "here"}}).isError);
        QVERIFY(!tool("set_playhead", {{"frame", 45}}).isError);
        QCOMPARE(m_pm->playheadFrame(), qint64(45));
    }
    void aProjectWithUnsavedChangesIsNeverReplacedSilently()
    {
        QVERIFY(m_pm->dirty());
        const Result refused = tool("create_project", {{"name", "Other"}, {"folder", p("projects")}});
        QVERIFY(refused.isError);
        QVERIFY(refused.text.contains("unsaved"));
        QCOMPARE(m_pm->name(), QString("McpTest"));
        QVERIFY(tool("open_project", {{"path", p("projects/McpTest/McpTest.dvproj")}}).isError);     // dirty -> refused too
        QVERIFY(!tool("save_project").isError);
        QVERIFY(!m_pm->dirty());
        // saved: opening is allowed, and only a real .dvproj with an absolute path
        QVERIFY(tool("open_project", {{"path", "McpTest.dvproj"}}).isError);
        QVERIFY(tool("open_project", {{"path", p("clip.mp4")}}).isError);
        QVERIFY(!tool("open_project", {{"path", p("projects/McpTest/McpTest.dvproj")}}).isError);
        QCOMPARE(tool("list_clips").json.value("clips").toArray().size() > 0, true);
    }
    void aiToolsRefuseWhileAiIsOffAndRunWhenOn()
    {
        QVERIFY(!m_ai->enabled());
        const Result off = tool("detect_silence");
        QVERIFY(off.isError);
        QVERIFY(off.text.contains("Enable AI"));
        QVERIFY(!tool("get_ai_status").isError);             // status is always readable
        QVERIFY(m_settings->set("aiEnabled", true));
        QVERIFY2(!tool("detect_silence", {{"threshold_db", -45}, {"min_silence_ms", 300}}).isError, "silence search should start");
        m_ai->waitForIdle(30000);
        const QJsonObject status = tool("get_ai_status").json;
        QCOMPARE(status.value("busy").toBool(), false);
        QVERIFY(status.contains("silenceCount"));
        QVERIFY(tool("detect_silence", {{"threshold_db", "loud"}}).isError);       // type errors are reported
        QVERIFY(tool("transcribe", {{"language", "klingon"}}).isError);
        QVERIFY(m_settings->set("aiEnabled", false));
    }

    // ---- HTTP -----------------------------------------------------------------------------------------------------------
    QByteArray http(quint16 port, const QByteArray& request)
    {
        QTcpSocket s;
        s.connectToHost(QHostAddress::LocalHost, port);
        if (!s.waitForConnected(3000))
            return "connect failed";
        s.write(request);
        QByteArray reply;
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < 5000 && s.state() != QAbstractSocket::UnconnectedState) {
            QTest::qWait(20);
            reply += s.readAll();
        }
        reply += s.readAll();
        return reply;
    }
    void httpRequiresTheTokenAndRefusesBrowsers()
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        const quint16 port = probe.serverPort();
        probe.close();
        QVERIFY(m_settings->set("mcpPort", int(port)));
        QVERIFY(m_settings->set("mcpEnabled", true));
        QVERIFY2(m_mcp->running(), qPrintable(m_mcp->error()));
        QCOMPARE(m_mcp->url(), QString("http://127.0.0.1:%1/mcp").arg(port));
        const QString token = m_mcp->token();
        QVERIFY(token.size() >= 32);
        QVERIFY(m_mcp->claudeCodeCommand().contains(token));
        QVERIFY(!m_mcp->codexConfig().contains("Bearer"));

        const QByteArray body = R"({"jsonrpc":"2.0","id":7,"method":"tools/list"})";
        auto post = [&](const QByteArray& extra, const QByteArray& path = "/mcp") {
            return http(port, "POST " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: application/json\r\n" + extra +
                                  "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
        };
        QVERIFY2(post("").startsWith("HTTP/1.1 401"), "no token");
        QVERIFY2(post("Authorization: Bearer wrong\r\n").startsWith("HTTP/1.1 401"), "wrong token");
        const QByteArray good = post("Authorization: Bearer " + token.toUtf8() + "\r\n");
        QVERIFY2(good.startsWith("HTTP/1.1 200"), good.constData());
        QVERIFY(good.contains("\"get_project_info\""));
        QVERIFY2(post("Authorization: Bearer " + token.toUtf8() + "\r\nOrigin: http://evil.example\r\n").startsWith("HTTP/1.1 403"), "browser origin");
        QVERIFY2(post("Authorization: Bearer " + token.toUtf8() + "\r\n", "/other").startsWith("HTTP/1.1 404"), "path");
        const QByteArray get = http(port, "GET /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer " + token.toUtf8() + "\r\n\r\n");
        QVERIFY2(get.startsWith("HTTP/1.1 405"), "GET is not supported");
        const QByteArray notice = R"({"jsonrpc":"2.0","method":"notifications/initialized"})";
        const QByteArray note = http(port, "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer " + token.toUtf8() + "\r\nContent-Length: " +
                                               QByteArray::number(notice.size()) + "\r\n\r\n" + notice);
        QVERIFY2(note.startsWith("HTTP/1.1 202"), note.constData());

        // a new token cuts the old one off at once
        const QString old = token;
        const QString fresh = m_mcp->regenerateToken();
        QVERIFY(fresh != old);
        QVERIFY2(post("Authorization: Bearer " + old.toUtf8() + "\r\n").startsWith("HTTP/1.1 401"), "old token");
        QVERIFY2(post("Authorization: Bearer " + fresh.toUtf8() + "\r\n").startsWith("HTTP/1.1 200"), "new token");

        // switched off: the port is closed
        QVERIFY(m_settings->set("mcpEnabled", false));
        QVERIFY(!m_mcp->running());
        QCOMPARE(http(port, "GET / HTTP/1.1\r\n\r\n"), QByteArray("connect failed"));
    }
};

QTEST_MAIN(TstMcp)
#include "tst_mcp.moc"
