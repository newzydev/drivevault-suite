#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

class QTcpServer;
class QTcpSocket;

namespace dv {

class AiController;
class ProjectManager;
class SettingsManager;

// A Model Context Protocol server (Streamable HTTP, JSON-RPC 2.0) built into the running program, so that Claude Code, Codex or any other MCP client can
// work on the open project: list media and clips, edit the timeline, run the offline AI helpers. It is OFF until the user switches it on in Settings,
// listens on 127.0.0.1 only and needs the private token as a Bearer header. Every call runs on the UI thread through the same models as the UI, so every
// edit is one undo step and shows up on the timeline at once. Nothing here deletes files, runs programs or reaches the network.
class McpServer : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY statusChanged FINAL)
    Q_PROPERTY(int port READ port NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString token READ token NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString url READ url NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString error READ error NOTIFY statusChanged FINAL)             // why it could not start, "" otherwise
    Q_PROPERTY(QString claudeCodeCommand READ claudeCodeCommand NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString codexConfig READ codexConfig NOTIFY statusChanged FINAL)

public:
    McpServer(ProjectManager* project, AiController* ai, SettingsManager* settings, QObject* parent = nullptr);
    ~McpServer() override;

    bool running() const;
    int port() const;
    QString token() const { return m_token; }
    QString url() const;
    QString error() const { return m_error; }
    QString claudeCodeCommand() const;
    QString codexConfig() const;

    // Starts, restarts or stops the listener according to the settings (mcpEnabled, mcpPort, mcpToken). Creates the token when there is none.
    void applySettings();
    // A new private token (the old one stops working at once). Returns it.
    Q_INVOKABLE QString regenerateToken();
    // Puts text (the connect command / config) on the clipboard.
    Q_INVOKABLE void copyText(const QString& text) const;

    // The protocol itself, without any network: one JSON-RPC message in, the response out (empty for a notification). Used by the HTTP layer and by tests.
    QByteArray handleMessage(const QByteArray& body);

signals:
    void statusChanged();
    // The AI is about to run a tool: the UI shows a virtual mouse cursor that moves to `target` ("timeline" | "media" | "ai" | "monitor" | "none")
    // with a caption saying what happens. `frame` / `trackRow` place it on the timeline (-1 = not specific). Only emitted when the setting mcpCursor is on.
    void agentAction(const QString& target, const QString& label, qint64 frame, int trackRow);

private:
    using ToolFn = std::function<QJsonObject(const QJsonObject&)>;
    struct Tool {
        QString name;
        QString description;
        QJsonObject schema;
        bool readOnly = false;
        ToolFn run;
    };

    void registerTools();
    void addTool(const QString& name, const QString& description, const QJsonObject& properties, const QStringList& required, bool readOnly, ToolFn run);
    QJsonObject callTool(const QString& name, const QJsonObject& args);
    QJsonObject handleRequest(const QJsonObject& request, bool* isNotification);
    int announce(const QString& name, const QJsonObject& args);

    void onNewConnection();
    void onReadyRead(QTcpSocket* socket);
    void respond(QTcpSocket* socket, int status, const QByteArray& body, const QByteArray& contentType = "application/json", const QByteArray& extraHeaders = {});
    bool tokenMatches(const QByteArray& bearer) const;
    void stop();

    ProjectManager* m_project;
    AiController* m_ai;
    SettingsManager* m_settings;
    QTcpServer* m_server = nullptr;
    QString m_token;
    QString m_error;
    QList<Tool> m_tools;
    QHash<QTcpSocket*, QByteArray> m_buffers;
};

} // namespace dv
