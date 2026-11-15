#include "McpServer.h"

#include "ai/AiController.h"
#include "dv/core/Version.h"
#include "project/ProjectManager.h"
#include "settings/SettingsManager.h"

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>

namespace dv {

namespace {

constexpr int kMaxRequestBytes = 1024 * 1024;
const char* const kLatestProtocol = "2025-06-18";

QJsonObject textResult(const QString& text, bool isError = false)
{
    QJsonObject o;
    o["content"] = QJsonArray{QJsonObject{{"type", "text"}, {"text", text}}};
    if (isError)
        o["isError"] = true;
    return o;
}

QJsonObject jsonResult(const QJsonObject& value)
{
    return textResult(QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Compact)));
}

QJsonObject fail(const QString& message) { return textResult(message, true); }

// Reads typed arguments and remembers what was missing or of the wrong type.
struct Args {
    QJsonObject o;
    QStringList problems;

    bool has(const char* key) const { return o.contains(QLatin1String(key)) && !o.value(QLatin1String(key)).isNull(); }
    qint64 integer(const char* key, bool required = true, qint64 fallback = 0)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (!v.isDouble()) {
            if (required || has(key))
                problems << QStringLiteral("'%1' must be a number").arg(QLatin1String(key));
            return fallback;
        }
        return qint64(v.toDouble());
    }
    double number(const char* key, double fallback)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isDouble())
            return v.toDouble();
        if (has(key))
            problems << QStringLiteral("'%1' must be a number").arg(QLatin1String(key));
        return fallback;
    }
    QString string(const char* key, bool required = true, const QString& fallback = {})
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (!v.isString()) {
            if (required || has(key))
                problems << QStringLiteral("'%1' must be a string").arg(QLatin1String(key));
            return fallback;
        }
        return v.toString();
    }
    bool boolean(const char* key, bool fallback)
    {
        const QJsonValue v = o.value(QLatin1String(key));
        if (v.isBool())
            return v.toBool();
        if (has(key))
            problems << QStringLiteral("'%1' must be true or false").arg(QLatin1String(key));
        return fallback;
    }
    QStringList strings(const char* key)
    {
        QStringList out;
        const QJsonValue v = o.value(QLatin1String(key));
        if (!v.isArray()) {
            problems << QStringLiteral("'%1' must be a list of strings").arg(QLatin1String(key));
            return out;
        }
        for (const QJsonValue& item : v.toArray()) {
            if (!item.isString()) {
                problems << QStringLiteral("'%1' must be a list of strings").arg(QLatin1String(key));
                return {};
            }
            out << item.toString();
        }
        return out;
    }
    QString report() const { return problems.join(QStringLiteral("; ")); }
};

QJsonObject prop(const char* type, const QString& description)
{
    return QJsonObject{{"type", QLatin1String(type)}, {"description", description}};
}

QJsonObject clipJson(const ClipData& c, double fps)
{
    QJsonObject o;
    o["id"] = c.id;
    o["trackId"] = c.trackId;
    o["mediaId"] = c.mediaId;
    o["type"] = c.type;
    o["startFrame"] = double(c.startFrame);
    o["durationFrames"] = double(c.durationFrames);
    o["endFrame"] = double(c.endFrame());
    o["sourceInFrame"] = double(c.sourceInFrame);
    o["startSeconds"] = double(c.startFrame) / fps;
    o["durationSeconds"] = double(c.durationFrames) / fps;
    if (!c.linkId.isEmpty())
        o["linkId"] = c.linkId;
    return o;
}

} // namespace

McpServer::McpServer(ProjectManager* project, AiController* ai, SettingsManager* settings, QObject* parent)
    : QObject(parent)
    , m_project(project)
    , m_ai(ai)
    , m_settings(settings)
{
    registerTools();
    connect(m_settings, &SettingsManager::valueChanged, this, [this](const QString& key, const QVariant&) {
        if (key == QLatin1String("mcpEnabled") || key == QLatin1String("mcpPort"))
            applySettings();
    });
}

McpServer::~McpServer()
{
    stop();
}

bool McpServer::running() const { return m_server && m_server->isListening(); }
int McpServer::port() const { return running() ? int(m_server->serverPort()) : m_settings->get(QStringLiteral("mcpPort")).toInt(); }
QString McpServer::url() const { return QStringLiteral("http://127.0.0.1:%1/mcp").arg(port()); }

QString McpServer::claudeCodeCommand() const
{
    return QStringLiteral("claude mcp add --transport http drivevault-video-editor %1 --header \"Authorization: Bearer %2\"").arg(url(), m_token);
}

QString McpServer::codexConfig() const
{
    // Codex reads the Bearer token from an environment variable, so the token never has to be written into the config file.
    return QStringLiteral("[mcp_servers.drivevault_video_editor]\nurl = \"%1\"\nbearer_token_env_var = \"DRIVEVAULT_MCP_TOKEN\"\n\n# PowerShell, once:  setx DRIVEVAULT_MCP_TOKEN \"%2\"")
        .arg(url(), m_token);
}

void McpServer::stop()
{
    if (m_server) {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    m_buffers.clear();
}

QString McpServer::regenerateToken()
{
    QByteArray raw(24, 0);
    QRandomGenerator* rng = QRandomGenerator::system();
    for (char& ch : raw)
        ch = char(rng->bounded(256));
    m_token = QString::fromLatin1(raw.toHex());
    m_settings->set(QStringLiteral("mcpToken"), m_token);
    emit statusChanged();
    return m_token;
}

void McpServer::copyText(const QString& text) const
{
    if (QClipboard* clipboard = QGuiApplication::clipboard())
        clipboard->setText(text);
}

void McpServer::applySettings()
{
    m_token = m_settings->get(QStringLiteral("mcpToken")).toString();
    if (m_token.size() < 32)
        regenerateToken();
    const bool wanted = m_settings->get(QStringLiteral("mcpEnabled")).toBool();
    const quint16 wantedPort = quint16(m_settings->get(QStringLiteral("mcpPort")).toInt());
    m_error.clear();
    if (m_server && (!wanted || !m_server->isListening() || m_server->serverPort() != wantedPort))
        stop();
    if (wanted && !m_server) {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, &McpServer::onNewConnection);
        if (!m_server->listen(QHostAddress::LocalHost, wantedPort)) {
            m_error = tr("Port %1 could not be opened: %2").arg(wantedPort).arg(m_server->errorString());
            stop();
        }
    }
    emit statusChanged();
}

// ---- HTTP (just enough of HTTP/1.1 for MCP clients: one POST per connection) ----------------------------------

void McpServer::onNewConnection()
{
    while (QTcpSocket* socket = m_server->nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
        QTimer::singleShot(15000, socket, [socket] {
            if (socket->state() != QAbstractSocket::UnconnectedState)
                socket->abort();
        });
    }
}

bool McpServer::tokenMatches(const QByteArray& bearer) const
{
    const QByteArray expected = m_token.toUtf8();
    if (expected.isEmpty() || bearer.size() != expected.size())
        return false;
    int diff = 0;
    for (int i = 0; i < expected.size(); ++i)
        diff |= bearer.at(i) ^ expected.at(i);
    return diff == 0;
}

void McpServer::respond(QTcpSocket* socket, int status, const QByteArray& body, const QByteArray& contentType, const QByteArray& extraHeaders)
{
    static const QHash<int, QByteArray> reasons = {{200, "OK"}, {202, "Accepted"}, {400, "Bad Request"}, {401, "Unauthorized"}, {403, "Forbidden"},
                                                   {404, "Not Found"}, {405, "Method Not Allowed"}, {413, "Payload Too Large"}};
    QByteArray out = "HTTP/1.1 " + QByteArray::number(status) + ' ' + reasons.value(status, "Error") + "\r\n";
    out += "Content-Type: " + contentType + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\nCache-Control: no-store\r\n" + extraHeaders + "\r\n";
    out += body;
    socket->write(out);
    socket->disconnectFromHost();
    m_buffers.remove(socket);
}

void McpServer::onReadyRead(QTcpSocket* socket)
{
    QByteArray& buf = m_buffers[socket];
    buf.append(socket->readAll());
    if (buf.size() > kMaxRequestBytes + 8192) {
        respond(socket, 413, "{}");
        return;
    }
    const int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return;
    const QList<QByteArray> lines = buf.left(headerEnd).split('\n');
    const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
    QHash<QByteArray, QByteArray> headers;
    for (int i = 1; i < lines.size(); ++i) {
        const int colon = lines.at(i).indexOf(':');
        if (colon > 0)
            headers.insert(lines.at(i).left(colon).trimmed().toLower(), lines.at(i).mid(colon + 1).trimmed());
    }
    const qint64 length = headers.value("content-length", "0").toLongLong();
    if (length < 0 || length > kMaxRequestBytes) {
        respond(socket, 413, "{}");
        return;
    }
    if (buf.size() < headerEnd + 4 + length)
        return;     // the body is still coming
    const QByteArray body = buf.mid(headerEnd + 4, int(length));
    const QByteArray method = requestLine.value(0);
    const QByteArray path = requestLine.value(1).split('?').value(0);

    // A browser page must never be able to drive the editor: native MCP clients send no Origin header.
    if (headers.contains("origin")) {
        respond(socket, 403, R"({"error":"browser requests are refused"})");
        return;
    }
    const QByteArray auth = headers.value("authorization");
    if (!auth.startsWith("Bearer ") || !tokenMatches(auth.mid(7).trimmed())) {
        respond(socket, 401, R"({"error":"missing or wrong token"})", "application/json", "WWW-Authenticate: Bearer\r\n");
        return;
    }
    if (path != "/mcp") {
        respond(socket, 404, R"({"error":"the MCP endpoint is /mcp"})");
        return;
    }
    if (method != "POST") {
        respond(socket, 405, R"({"error":"POST only"})", "application/json", "Allow: POST\r\n");
        return;
    }
    const QByteArray reply = handleMessage(body);
    if (reply.isEmpty())
        respond(socket, 202, {});
    else
        respond(socket, 200, reply);
}

// ---- JSON-RPC / MCP ---------------------------------------------------------------------------------------------------

QByteArray McpServer::handleMessage(const QByteArray& body)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        QJsonObject e{{"jsonrpc", "2.0"}, {"id", QJsonValue::Null}, {"error", QJsonObject{{"code", -32700}, {"message", "parse error: one JSON-RPC object per request"}}}};
        return QJsonDocument(e).toJson(QJsonDocument::Compact);
    }
    bool notification = false;
    const QJsonObject response = handleRequest(doc.object(), &notification);
    if (notification)
        return {};
    return QJsonDocument(response).toJson(QJsonDocument::Compact);
}

QJsonObject McpServer::handleRequest(const QJsonObject& request, bool* isNotification)
{
    const QString method = request.value("method").toString();
    const bool hasId = request.contains("id");
    *isNotification = !hasId;
    QJsonObject response{{"jsonrpc", "2.0"}, {"id", request.value("id")}};
    auto error = [&](int code, const QString& message) {
        response["error"] = QJsonObject{{"code", code}, {"message", message}};
        return response;
    };
    if (request.value("jsonrpc").toString() != QLatin1String("2.0") || method.isEmpty())
        return error(-32600, QStringLiteral("not a JSON-RPC 2.0 request"));
    if (!hasId)
        return {};    // notifications/initialized, notifications/cancelled ...

    const QJsonObject params = request.value("params").toObject();
    if (method == QLatin1String("initialize")) {
        static const QStringList supported = {"2025-06-18", "2025-03-26", "2024-11-05"};
        const QString asked = params.value("protocolVersion").toString();
        response["result"] = QJsonObject{
            {"protocolVersion", supported.contains(asked) ? asked : QString::fromLatin1(kLatestProtocol)},
            {"capabilities", QJsonObject{{"tools", QJsonObject{{"listChanged", false}}}}},
            {"serverInfo", QJsonObject{{"name", "drivevault-video-editor-pro"}, {"title", QString::fromLatin1(version::kProductName)}, {"version", QString::fromLatin1(version::kVersion)}}},
            {"instructions", "Works on the project that is open in DriveVault Video Editor Pro. Positions are frame numbers at the project frame rate "
                             "(get_project_info gives it). Every edit is one undo step; call undo to take it back. Start with get_project_info."}};
        return response;
    }
    if (method == QLatin1String("ping")) {
        response["result"] = QJsonObject{};
        return response;
    }
    if (method == QLatin1String("tools/list")) {
        QJsonArray list;
        for (const Tool& t : std::as_const(m_tools)) {
            list.append(QJsonObject{{"name", t.name}, {"description", t.description}, {"inputSchema", t.schema},
                                    {"annotations", QJsonObject{{"readOnlyHint", t.readOnly}}}});
        }
        response["result"] = QJsonObject{{"tools", list}};
        return response;
    }
    if (method == QLatin1String("tools/call")) {
        const QString name = params.value("name").toString();
        const bool known = std::any_of(m_tools.cbegin(), m_tools.cend(), [&](const Tool& t) { return t.name == name; });
        if (!known)
            return error(-32602, QStringLiteral("unknown tool '%1'").arg(name));
        response["result"] = callTool(name, params.value("arguments").toObject());
        return response;
    }
    return error(-32601, QStringLiteral("method not found: %1").arg(method));
}

QJsonObject McpServer::callTool(const QString& name, const QJsonObject& args)
{
    for (const Tool& t : std::as_const(m_tools)) {
        if (t.name == name)
            return t.run(args);
    }
    return fail(QStringLiteral("unknown tool"));
}

void McpServer::addTool(const QString& name, const QString& description, const QJsonObject& properties, const QStringList& required, bool readOnly, ToolFn run)
{
    Tool t;
    t.name = name;
    t.description = description;
    t.readOnly = readOnly;
    QJsonArray req;
    for (const QString& r : required)
        req.append(r);
    t.schema = QJsonObject{{"type", "object"}, {"properties", properties}, {"required", req}};
    t.run = std::move(run);
    m_tools.append(t);
}

// ---- the tools ------------------------------------------------------------------------------------------------------

void McpServer::registerTools()
{
    auto needProject = [this]() -> QString {
        return m_project->isOpen() ? QString() : QStringLiteral("No project is open. Use open_project or create_project first.");
    };
    auto fps = [this] { return std::max(1.0, m_project->frameRate().toDouble()); };
    const QJsonObject none;

    addTool("get_project_info", "State of the open project: name, size, frame rate, length, playhead, clip and track counts, undo state.", none, {}, true,
            [this, fps](const QJsonObject&) {
                if (!m_project->isOpen())
                    return jsonResult({{"open", false}});
                TimelineModel* tl = m_project->timeline();
                return jsonResult({{"open", true}, {"name", m_project->name()}, {"path", m_project->filePath()}, {"unsavedChanges", m_project->dirty()},
                                   {"width", m_project->width()}, {"height", m_project->height()}, {"frameRate", m_project->frameRateLabel()},
                                   {"framesPerSecond", fps()}, {"sampleRate", m_project->sampleRate()},
                                   {"durationFrames", double(tl->durationFrames())}, {"durationSeconds", double(tl->durationFrames()) / fps()},
                                   {"playheadFrame", double(m_project->playheadFrame())}, {"clipCount", tl->clipCount()}, {"trackCount", tl->trackCount()},
                                   {"mediaCount", m_project->media()->count()}, {"canUndo", tl->canUndo()}, {"undoText", tl->undoText()},
                                   {"canRedo", tl->canRedo()}, {"redoText", tl->redoText()}});
            });

    addTool("list_tracks", "The tracks of the timeline, top to bottom: id (V1, A1, S1 ...), kind, muted, locked, visible.", none, {}, true,
            [this, needProject](const QJsonObject&) {
                if (const QString e = needProject(); !e.isEmpty())
                    return fail(e);
                QJsonArray list;
                int index = 0;
                for (const TrackInfo& t : m_project->timeline()->tracks()) {
                    list.append(QJsonObject{{"index", index++}, {"id", t.id}, {"name", t.name}, {"kind", t.kind}, {"muted", t.muted}, {"locked", t.locked}, {"visible", t.visible}});
                }
                return jsonResult({{"tracks", list}});
            });

    addTool("list_media", "The media imported in the project (id, name, path, kind, status, duration). Use the id with add_clip.", none, {}, true,
            [this, needProject](const QJsonObject&) {
                if (const QString e = needProject(); !e.isEmpty())
                    return fail(e);
                MediaLibrary* lib = m_project->media();
                QJsonArray list;
                for (int row = 0; row < lib->rowCount(); ++row) {
                    const QString id = lib->data(lib->index(row), MediaLibrary::IdRole).toString();
                    QJsonObject item = QJsonObject::fromVariantMap(lib->describe(id));
                    item["id"] = id;
                    item["durationSeconds"] = double(lib->data(lib->index(row), MediaLibrary::DurationRole).toLongLong()) / 1.0e6;
                    list.append(item);
                }
                return jsonResult({{"media", list}});
            });

    addTool("list_clips", "The clips on the timeline with their track, start frame and length. Titles and subtitles are included with their text. Optional: only one track, a limit.",
            QJsonObject{{"track_id", prop("string", "only clips of this track, for example V1")}, {"limit", prop("integer", "at most this many clips (default 500)")}}, {}, true,
            [this, needProject, fps](const QJsonObject& in) {
                if (const QString e = needProject(); !e.isEmpty())
                    return fail(e);
                Args a{in, {}};
                const QString track = a.string("track_id", false);
                const int limit = int(std::clamp<qint64>(a.integer("limit", false, 500), 1, 5000));
                if (!a.problems.isEmpty())
                    return fail(a.report());
                TimelineModel* tl = m_project->timeline();
                QList<ClipData> clips = tl->clips();
                std::sort(clips.begin(), clips.end(), [](const ClipData& x, const ClipData& y) { return x.trackId == y.trackId ? x.startFrame < y.startFrame : x.trackId < y.trackId; });
                QJsonArray list;
                for (const ClipData& c : std::as_const(clips)) {
                    if (!track.isEmpty() && c.trackId != track)
                        continue;
                    if (list.size() >= limit)
                        break;
                    QJsonObject o = clipJson(c, fps());
                    if (c.isTitle())
                        o["title"] = QJsonObject::fromVariantMap(tl->titleInfo(c.id));
                    list.append(o);
                }
                return jsonResult({{"clips", list}, {"truncated", list.size() >= limit}});
            });

    addTool("import_media", "Imports a video, audio or image file (absolute path) into the project. It is analysed in the background; check list_media until its status is ready.",
            QJsonObject{{"path", prop("string", "absolute path of an existing media file")}}, {"path"}, false,
            [this, needProject](const QJsonObject& in) {
                if (const QString e = needProject(); !e.isEmpty())
                    return fail(e);
                Args a{in, {}};
                const QString path = a.string("path");
                if (!a.problems.isEmpty())
                    return fail(a.report());
                const QFileInfo info(path);
                if (!info.isAbsolute() || !info.isFile())
                    return fail(QStringLiteral("'%1' is not an existing file with an absolute path.").arg(path));
                const QString id = m_project->media()->importFile(info.absoluteFilePath());
                if (id.isEmpty())
                    return fail(QStringLiteral("This file could not be imported (unreadable or not a media file)."));
                return jsonResult({{"mediaId", id}, {"note", "Imported in the background. Check list_media for status ready."}});
            });

    addTool("add_clip", "Puts a media item on the timeline. A video with sound becomes a linked video + audio pair. A busy position is skipped to the first free one.",
            QJsonObject{{"media_id", prop("string", "id from list_media")}, {"start_frame", prop("integer", "timeline position in frames")},
                        {"track_index", prop("integer", "track index from list_tracks, or -1 / omitted for the automatic track")}},
            {"media_id", "start_frame"}, false,
            [this, needProject](const QJsonObject& in) {
                if (const QString e = needProject(); !e.isEmpty())
                    return fail(e);
                Args a{in, {}};
                const QString media = a.string("media_id");
                const qint64 start = a.integer("start_frame");
                const int track = int(a.integer("track_index", false, -1));
                if (!a.problems.isEmpty())
                    return fail(a.report());
                if (start < 0)
                    return fail(QStringLiteral("'start_frame' cannot be negative."));
                TimelineModel* tl = m_project->timeline();
                QSet<QString> before;
                for (const ClipData& c : tl->clips())
                    before.insert(c.id);
                const QString err = tl->addMedia(media, track, start);
                if (!err.isEmpty())
                    return fail(err);
                QJsonArray added;
                for (const ClipData& c : tl->clips()) {
                    if (!before.contains(c.id))
                        added.append(c.id);
                }
                return jsonResult({{"ok", true}, {"newClipIds", added}});
            });

    addTool("move_clip", "Moves a clip (with its linked audio / video) to a new start frame, optionally to another track.",
            QJsonObject{{"clip_id", prop("string", "clip id")}, {"start_frame", prop("integer", "new start in frames")}, {"track_index", prop("integer", "new track index (default: stay on the same track)")}},
            {"clip_id", "start_frame"}, false,
            [this, needProject](const QJsonObject& in) {
                if (const QString e = needProject(); !e.isEmpty())
                    return fail(e);
                Args a{in, {}};
                const QString id = a.string("clip_id");
                const qint64 start = a.integer("start_frame");
                if (!a.problems.isEmpty())
                    return fail(a.report());
                TimelineModel* tl = m_project->timeline();
                const ClipData* clip = tl->findClip(id);
                if (!clip)
                    return fail(QStringLiteral("There is no clip '%1'.").arg(id));
                const int track = a.has("track_index") ? int(a.integer("track_index")) : tl->trackIndexOf(clip->trackId);
                const QString err = tl->moveClip(id, start, track);
                return err.isEmpty() ? textResult(QStringLiteral("Moved.")) : fail(err);
            });

    addTool("trim_clip", "Changes where a clip starts and / or ends on the timeline (frames). Give at least one of start_frame, end_frame.",
            QJsonObject{{"clip_id", prop("string", "clip id")}, {"start_frame", prop("integer", "new first frame")}, {"end_frame", prop("integer", "new end frame (exclusive)")}},
            {"clip_id"}, false,
            [this, needProject](const QJsonObject& in) {
                if (const QString e = needProject(); !e.isEmpty())
                    return fail(e);
                Args a{in, {}};
                const QString id = a.string("clip_id");
                const bool hasStart = a.has("start_frame"), hasEnd = a.has("end_frame");
                const qint64 start = a.integer("start_frame", false), end = a.integer("end_frame", false);
                if (!a.problems.isEmpty())
                    return fail(a.report());
                if (!hasStart && !hasEnd)
                    return fail(QStringLiteral("Give start_frame, end_frame or both."));
                TimelineModel* tl = m_project->timeline();
                if (!tl->findClip(id))
                    return fail(QStringLiteral("There is no clip '%1'.").arg(id));
                if (hasEnd) {
                    const QString err = tl->trimEnd(id, end);
                    if (!err.isEmpty())
                        return fail(err);
                }
                if (hasStart) {
                    const QString err = tl->trimStart(id, start);
                    if (!err.isEmpty())
                        return fail(err);
                }
                return textResult(QStringLiteral("Trimmed."));
            });

    addTool("split_at", "Splits every unlocked clip under the given frame in two.", QJsonObject{{"frame", prop("integer", "timeline frame")}}, {"frame"}, false,
            [this, needProject](const QJsonObject& in) {
                if (const QString e = needProject(); !e.isEmpty())
                    return fail(e);
                Args a{in, {}};
                const qint64 frame = a.integer("frame");
                if (!a.problems.isEmpty())
                    return fail(a.report());
                m_project->timeline()->clearSelection();
                const QString err = m_project->timeline()->splitAt(frame);
                return err.isEmpty() ? textResult(QStringLiteral("Split.")) : fail(err);
            });

    addTool("delete_clips", "Deletes clips (and their linked partners). With ripple=true the gap is closed. Undo brings them back.",
            QJsonObject{{"clip_ids", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"description", "ids of the clips to delete"}}},
                        {"ripple", prop("boolean", "close the gap (default false)")}},
            {"clip_ids"}, false,
            [this, needProject](const QJsonObject& in) {
                if (const QString e = needProject(); !e.isEmpty())
                    return fail(e);
                Args a{in, {}};
                const QStringList ids = a.strings("clip_ids");
                const bool ripple = a.boolean("ripple", false);
                if (!a.problems.isEmpty())
                    return fail(a.report());
                if (ids.isEmpty() || ids.size() > 500)
                    return fail(QStringLiteral("Give between 1 and 500 clip ids."));
                TimelineModel* tl = m_project->timeline();
                for (const QString& id : ids) {
                    if (!tl->findClip(id))
                        return fail(QStringLiteral("There is no clip '%1'. Nothing was deleted.").arg(id));
                }
                tl->clearSelection();
                for (const QString& id : ids)
                    tl->select(id, true);
                const QString err = ripple ? tl->rippleDeleteSelected() : tl->deleteSelected();
                tl->clearSelection();
                return err.isEmpty() ? textResult(QStringLiteral("Deleted.")) : fail(err);
            });

    addTool("add_subtitle", "Adds one subtitle (text on the subtitle track) at a position and length in frames.",
            QJsonObject{{"start_frame", prop("integer", "first frame")}, {"duration_frames", prop("integer", "length in frames")}, {"text", prop("string", "subtitle text (up to 1000 characters)")}},
            {"start_frame", "duration_frames", "text"}, false,
            [this, needProject](const QJsonObject& in) {
                if (const QString e = needProject(); !e.isEmpty())
                    return fail(e);
                Args a{in, {}};
                const qint64 start = a.integer("start_frame"), length = a.integer("duration_frames");
                const QString text = a.string("text");
                if (!a.problems.isEmpty())
                    return fail(a.report());
                if (start < 0 || length < 1 || text.isEmpty() || text.size() > 1000)
                    return fail(QStringLiteral("start_frame must be >= 0, duration_frames >= 1 and text 1 to 1000 characters."));
                bool hasTrack = false;                      // like "add captions": the subtitle track is created when there is none
                for (const TrackInfo& t : m_project->timeline()->tracks())
                    hasTrack = hasTrack || t.kind == QLatin1String("subtitle");
                if (!hasTrack) {
                    if (const QString trackError = m_project->addTrack(QStringLiteral("subtitle")); !trackError.isEmpty())
                        return fail(trackError);
                }
                const QString err = m_project->timeline()->addSubtitle(start, length, text);
                return err.isEmpty() ? textResult(QStringLiteral("Subtitle added.")) : fail(err);
            });

    addTool("add_marker", "Adds a marker on the timeline (markers are not part of undo).", QJsonObject{{"frame", prop("integer", "timeline frame")}, {"name", prop("string", "optional name")}}, {"frame"}, false,
            [this, needProject](const QJsonObject& in) {
                if (const QString e = needProject(); !e.isEmpty())
                    return fail(e);
                Args a{in, {}};
                const qint64 frame = a.integer("frame");
                const QString name = a.string("name", false);
                if (!a.problems.isEmpty())
                    return fail(a.report());
                const QString markerId = m_project->timeline()->addMarker(frame, name);      // returns the new marker's id (markers are not on the undo stack)
                return jsonResult({{"ok", true}, {"markerId", markerId}});
            });

    addTool("set_playhead", "Moves the playhead of the editor to a frame.", QJsonObject{{"frame", prop("integer", "timeline frame")}}, {"frame"}, false,
            [this, needProject](const QJsonObject& in) {
                if (const QString e = needProject(); !e.isEmpty())
                    return fail(e);
                Args a{in, {}};
                const qint64 frame = a.integer("frame");
                if (!a.problems.isEmpty())
                    return fail(a.report());
                m_project->setPlayheadFrame(std::max<qint64>(0, frame));
                return textResult(QStringLiteral("Playhead at frame %1.").arg(m_project->playheadFrame()));
            });

    addTool("undo", "Undoes the last edit (the same Undo as in the editor). Returns what was undone.", none, {}, false,
            [this, needProject](const QJsonObject&) {
                if (const QString e = needProject(); !e.isEmpty())
                    return fail(e);
                TimelineModel* tl = m_project->timeline();
                if (!tl->canUndo())
                    return fail(QStringLiteral("There is nothing to undo."));
                const QString what = tl->undoText();
                tl->undo();
                return textResult(QStringLiteral("Undone: %1").arg(what));
            });

    addTool("redo", "Redoes the edit that was undone last.", none, {}, false,
            [this, needProject](const QJsonObject&) {
                if (const QString e = needProject(); !e.isEmpty())
                    return fail(e);
                TimelineModel* tl = m_project->timeline();
                if (!tl->canRedo())
                    return fail(QStringLiteral("There is nothing to redo."));
                const QString what = tl->redoText();
                tl->redo();
                return textResult(QStringLiteral("Redone: %1").arg(what));
            });

    addTool("save_project", "Saves the open project to its .dvproj file.", none, {}, false,
            [this, needProject](const QJsonObject&) {
                if (const QString e = needProject(); !e.isEmpty())
                    return fail(e);
                const QString err = m_project->save();
                return err.isEmpty() ? textResult(QStringLiteral("Saved to %1").arg(m_project->filePath())) : fail(err);
            });

    addTool("open_project", "Opens a .dvproj file (absolute path). Refuses to replace a project with unsaved changes unless discard_unsaved is true.",
            QJsonObject{{"path", prop("string", "absolute path of a .dvproj file")}, {"discard_unsaved", prop("boolean", "true to drop unsaved changes of the open project")}}, {"path"}, false,
            [this](const QJsonObject& in) {
                Args a{in, {}};
                const QString path = a.string("path");
                const bool discard = a.boolean("discard_unsaved", false);
                if (!a.problems.isEmpty())
                    return fail(a.report());
                if (m_project->isOpen() && m_project->dirty() && !discard)
                    return fail(QStringLiteral("The open project has unsaved changes. Call save_project first, or pass discard_unsaved=true to drop them."));
                const QFileInfo info(path);
                if (!info.isAbsolute() || !info.isFile() || info.suffix().compare(QLatin1String("dvproj"), Qt::CaseInsensitive) != 0)
                    return fail(QStringLiteral("'%1' is not an existing .dvproj file with an absolute path.").arg(path));
                const QString err = m_project->openProject(info.absoluteFilePath());
                return err.isEmpty() ? textResult(QStringLiteral("Opened %1").arg(m_project->name())) : fail(err);
            });

    addTool("create_project", "Creates a new project in <folder>/<name>/<name>.dvproj and opens it. Refuses to replace a project with unsaved changes unless discard_unsaved is true.",
            QJsonObject{{"name", prop("string", "project name")}, {"folder", prop("string", "parent folder (default: the default project location of the program)")},
                        {"width", prop("integer", "default 1920")}, {"height", prop("integer", "default 1080")},
                        {"frame_rate", prop("string", "24, 25, 30, 60 or 29.97 (default 30)")}, {"sample_rate", prop("integer", "default 48000")},
                        {"discard_unsaved", prop("boolean", "true to drop unsaved changes of the open project")}},
            {"name"}, false,
            [this](const QJsonObject& in) {
                Args a{in, {}};
                const QString name = a.string("name");
                QString folder = a.string("folder", false);
                const int width = int(a.integer("width", false, 1920)), height = int(a.integer("height", false, 1080));
                const QString rate = a.string("frame_rate", false, QStringLiteral("30"));
                const int sampleRate = int(a.integer("sample_rate", false, 48000));
                const bool discard = a.boolean("discard_unsaved", false);
                if (!a.problems.isEmpty())
                    return fail(a.report());
                if (m_project->isOpen() && m_project->dirty() && !discard)
                    return fail(QStringLiteral("The open project has unsaved changes. Call save_project first, or pass discard_unsaved=true to drop them."));
                if (const QString nameError = ProjectManager::validateName(name); !nameError.isEmpty())
                    return fail(nameError);
                if (folder.isEmpty())
                    folder = ProjectManager::defaultLocation(m_settings->get(QStringLiteral("defaultProjectLocation")).toString());
                if (!QFileInfo(folder).isAbsolute())
                    return fail(QStringLiteral("'folder' must be an absolute path."));
                const QString err = m_project->createProject(name, folder, width, height, rate, sampleRate);
                return err.isEmpty() ? textResult(QStringLiteral("Created %1").arg(m_project->filePath())) : fail(err);
            });

    // ---- offline AI helpers (the same services as the AI panel; apply_* is the separate confirmation step) ----
    auto needAi = [this]() -> QString {
        return m_ai->enabled() ? QString() : QStringLiteral("AI features are switched off. Turn on 'Enable AI features' in Settings > AI.");
    };

    addTool("get_ai_status", "State of the AI helper: busy, task, progress, message, speech engine status, and the results waiting to be applied (silent passages, captions).", none, {}, true,
            [this](const QJsonObject&) {
                return jsonResult({{"enabled", m_ai->enabled()}, {"busy", m_ai->busy()}, {"task", m_ai->task()}, {"progress", m_ai->progress()}, {"message", m_ai->message()},
                                   {"speechEngine", m_ai->whisperStatus()},
                                   {"silences", QJsonArray::fromVariantList(m_ai->silences().mid(0, 200))}, {"silenceCount", int(m_ai->silences().size())},
                                   {"captions", QJsonArray::fromVariantList(m_ai->captions().mid(0, 200))}, {"captionCount", int(m_ai->captions().size())}});
            });

    addTool("detect_silence", "Starts a search for silent passages in the timeline audio. It runs in the background; poll get_ai_status. Nothing is cut until apply_silence_cuts.",
            QJsonObject{{"threshold_db", prop("number", "level below which it is silent, -80 to -10 (default -40)")}, {"min_silence_ms", prop("integer", "shortest silence, 100 to 10000 (default 500)")}}, {}, false,
            [this, needProject, needAi](const QJsonObject& in) {
                if (const QString e = needProject(); !e.isEmpty())
                    return fail(e);
                if (const QString e = needAi(); !e.isEmpty())
                    return fail(e);
                Args a{in, {}};
                const double db = a.number("threshold_db", -40);
                const int ms = int(a.integer("min_silence_ms", false, 500));
                if (!a.problems.isEmpty())
                    return fail(a.report());
                const QString err = m_ai->detectSilence(db, ms);
                return err.isEmpty() ? textResult(QStringLiteral("Started. Poll get_ai_status until busy is false.")) : fail(err);
            });

    addTool("apply_silence_cuts", "Removes the silent passages found by detect_silence and closes the gaps (one undo step).", none, {}, false,
            [this, needProject, needAi](const QJsonObject&) {
                if (const QString e = needProject(); !e.isEmpty())
                    return fail(e);
                if (const QString e = needAi(); !e.isEmpty())
                    return fail(e);
                const QString err = m_ai->applySilenceCuts();
                return err.isEmpty() ? textResult(QStringLiteral("Silent passages removed. undo brings them back.")) : fail(err);
            });

    addTool("transcribe", "Starts speech-to-text of the timeline audio (offline). It runs in the background; poll get_ai_status. Captions can be wrong. Nothing is added until apply_captions.",
            QJsonObject{{"language", prop("string", "th, en or auto (default auto)")}}, {}, false,
            [this, needProject, needAi](const QJsonObject& in) {
                if (const QString e = needProject(); !e.isEmpty())
                    return fail(e);
                if (const QString e = needAi(); !e.isEmpty())
                    return fail(e);
                Args a{in, {}};
                const QString language = a.string("language", false, QStringLiteral("auto"));
                if (!a.problems.isEmpty())
                    return fail(a.report());
                if (!QSet<QString>{"th", "en", "auto"}.contains(language))
                    return fail(QStringLiteral("language must be th, en or auto."));
                const QString err = m_ai->transcribe(language);
                return err.isEmpty() ? textResult(QStringLiteral("Started. Poll get_ai_status until busy is false.")) : fail(err);
            });

    addTool("apply_captions", "Adds the transcribed phrases as editable subtitles (one undo step).", none, {}, false,
            [this, needProject, needAi](const QJsonObject&) {
                if (const QString e = needProject(); !e.isEmpty())
                    return fail(e);
                if (const QString e = needAi(); !e.isEmpty())
                    return fail(e);
                const QString err = m_ai->applyCaptions();
                return err.isEmpty() ? textResult(QStringLiteral("Captions added.")) : fail(err);
            });
}

} // namespace dv
