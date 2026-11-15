#include "CliChat.h"

#include "ProcessUtil.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

namespace dm::ai {

namespace {

QString shorten(QString s, int n = 120)
{
    s = s.simplified();
    return s.size() > n ? s.left(n - 1) + QStringLiteral("…") : s;
}

// "Read: C:\x\notes.txt": the most telling value of a tool call
QString describeTool(const QString& name, const QJsonObject& input)
{
    for (const char* key : {"file_path", "path", "pattern", "command", "url", "query", "description"}) {
        const QString v = input.value(QLatin1String(key)).toString();
        if (!v.isEmpty())
            return name + QStringLiteral(": ") + shorten(v);
    }
    return name;
}

} // namespace

CliChat::CliChat(QObject* parent)
    : QObject(parent)
{
}

CliChat::~CliChat()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(2000);
    }
}

QString CliChat::toolName(Tool tool) { return tool == Tool::Claude ? QStringLiteral("claude") : QStringLiteral("codex"); }

QString CliChat::executableFor(Tool tool) { return findOnPath(toolName(tool)); }

QString CliChat::versionOf(const QString& executable)
{
    if (executable.isEmpty())
        return {};
    QProcess p;
    hideConsole(&p);
    p.start(executable, {QStringLiteral("--version")});
    if (!p.waitForFinished(8000) || p.exitCode() != 0)
        return {};
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed().left(80);
}

QStringList CliChat::arguments(const Request& r)
{
    static const QRegularExpression id(QStringLiteral("^[A-Za-z0-9-]{8,64}$"));
    if (r.tool == Tool::Claude) {
        QStringList a{QStringLiteral("-p"), QStringLiteral("--output-format"), QStringLiteral("stream-json"), QStringLiteral("--verbose"),
                      QStringLiteral("--include-partial-messages"),
                      QStringLiteral("--restricted"),                           // no command / code tools; the file tools stay in the working folder
                      QStringLiteral("--permission-mode"), r.allowEdits ? QStringLiteral("acceptEdits") : QStringLiteral("plan")};
        if (id.match(r.sessionId).hasMatch())
            a << QStringLiteral("--resume") << r.sessionId;
        return a;
    }
    return {QStringLiteral("exec"), QStringLiteral("--json"), QStringLiteral("--skip-git-repo-check"), QStringLiteral("-s"),
            r.allowEdits ? QStringLiteral("workspace-write") : QStringLiteral("read-only"), QStringLiteral("-")};
}

QList<CliChat::Event> CliChat::readLine(Tool tool, const QByteArray& line, bool* streamedText)
{
    QList<Event> out;
    const QJsonObject o = QJsonDocument::fromJson(line.trimmed()).object();
    if (o.isEmpty())
        return out;
    const QString type = o.value(QLatin1String("type")).toString();
    if (tool == Tool::Claude) {
        if (type == QLatin1String("system")) {
            Event e;
            e.sessionId = o.value(QLatin1String("session_id")).toString();
            if (!e.sessionId.isEmpty())
                out << e;
        } else if (type == QLatin1String("stream_event")) {
            const QJsonObject ev = o.value(QLatin1String("event")).toObject();
            const QString et = ev.value(QLatin1String("type")).toString();
            if (et == QLatin1String("content_block_delta")) {
                const QJsonObject d = ev.value(QLatin1String("delta")).toObject();
                if (d.value(QLatin1String("type")).toString() == QLatin1String("text_delta")) {
                    Event e;
                    e.text = d.value(QLatin1String("text")).toString();
                    *streamedText = true;
                    out << e;
                }
            } else if (et == QLatin1String("content_block_start")
                       && ev.value(QLatin1String("content_block")).toObject().value(QLatin1String("type")).toString() == QLatin1String("text")) {
                Event e;
                e.newBlock = true;
                out << e;
            }
        } else if (type == QLatin1String("assistant")) {
            for (const QJsonValue& v : o.value(QLatin1String("message")).toObject().value(QLatin1String("content")).toArray()) {
                const QJsonObject block = v.toObject();
                const QString bt = block.value(QLatin1String("type")).toString();
                if (bt == QLatin1String("text") && !*streamedText) {
                    Event e;
                    e.text = block.value(QLatin1String("text")).toString();
                    e.textIsWhole = true;
                    out << e;
                } else if (bt == QLatin1String("tool_use")) {
                    Event e;
                    e.activity = describeTool(block.value(QLatin1String("name")).toString(), block.value(QLatin1String("input")).toObject());
                    out << e;
                }
            }
            *streamedText = false;
        } else if (type == QLatin1String("result")) {
            Event e;
            e.sessionId = o.value(QLatin1String("session_id")).toString();
            e.done = true;
            if (o.value(QLatin1String("is_error")).toBool())
                e.error = o.value(QLatin1String("result")).toString(QStringLiteral("failed"));
            out << e;
        }
        return out;
    }
    // Codex: `codex exec --json` writes one event per line
    if (type == QLatin1String("thread.started")) {
        Event e;
        e.sessionId = o.value(QLatin1String("thread_id")).toString();
        out << e;
    } else if (type == QLatin1String("item.started") || type == QLatin1String("item.completed")) {
        const QJsonObject item = o.value(QLatin1String("item")).toObject();
        const QString it = item.value(QLatin1String("type")).toString();
        const bool completed = type == QLatin1String("item.completed");
        Event e;
        if (it == QLatin1String("agent_message") && completed) {
            e.text = item.value(QLatin1String("text")).toString();
            e.textIsWhole = true;
            out << e;
        } else if (it == QLatin1String("command_execution") && !completed) {
            e.activity = QStringLiteral("Run: ") + shorten(item.value(QLatin1String("command")).toString());
            out << e;
        } else if (it == QLatin1String("file_change") && completed) {
            QStringList paths;
            for (const QJsonValue& c : item.value(QLatin1String("changes")).toArray())
                paths << c.toObject().value(QLatin1String("path")).toString();
            e.activity = QStringLiteral("Edit: ") + shorten(paths.join(QStringLiteral(", ")));
            out << e;
        } else if (it == QLatin1String("mcp_tool_call") && !completed) {
            e.activity = QStringLiteral("Tool: ") + shorten(item.value(QLatin1String("tool")).toString());
            out << e;
        }
    } else if (type == QLatin1String("turn.completed")) {
        Event e;
        e.done = true;
        out << e;
    } else if (type == QLatin1String("turn.failed") || type == QLatin1String("error")) {
        Event e;
        e.error = o.value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString(o.value(QLatin1String("message")).toString(QStringLiteral("failed")));
        e.done = type == QLatin1String("turn.failed");
        out << e;
    }
    return out;
}

QString CliChat::start(const Request& r)
{
    if (m_process)
        return QStringLiteral("busy");
    const QString exe = r.executable.isEmpty() ? executableFor(r.tool) : r.executable;
    if (exe.isEmpty() || !QFileInfo::exists(exe))
        return QStringLiteral("noProgram");
    if (!QFileInfo(r.workDir).isDir())
        return QStringLiteral("noFolder");
    m_tool = r.tool;
    m_buffer.clear();
    m_stderr.clear();
    m_error.clear();
    m_streamed = false;
    m_sawText = false;
    m_cancelled = false;
    m_done = false;

    m_process = new QProcess(this);
    hideConsole(m_process);
    m_process->setProgram(exe);
    m_process->setArguments(arguments(r));
    m_process->setWorkingDirectory(r.workDir);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &CliChat::onOutput);
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        m_stderr += m_process->readAllStandardError();
        if (m_stderr.size() > 8192)
            m_stderr = m_stderr.right(4096);
    });
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) { onFinished(code); });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            const QString why = m_process->errorString();
            m_process->deleteLater();
            m_process = nullptr;
            emit finished(false, QStringLiteral("startFailed"), why);
        }
    });
    m_process->start();
    if (!m_process->waitForStarted(8000))
        return {};                                           // errorOccurred reports it
    killWithThisProgram(m_process);
    m_process->write(r.prompt.toUtf8());                     // the question goes in on stdin, not on the command line
    m_process->closeWriteChannel();
    return {};
}

void CliChat::cancel()
{
    if (!m_process)
        return;
    m_cancelled = true;
    m_process->kill();
}

void CliChat::onOutput()
{
    m_buffer += m_process->readAllStandardOutput();
    for (;;) {
        const int nl = m_buffer.indexOf('\n');
        if (nl < 0)
            break;
        const QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        for (const Event& e : readLine(m_tool, line, &m_streamed)) {
            if (!e.sessionId.isEmpty())
                emit sessionId(e.sessionId);
            if (!e.error.isEmpty())
                m_error = e.error;
            if (e.newBlock) {
                if (m_sawText)
                    emit piece(QStringLiteral("\n\n"));
                continue;
            }
            if (!e.activity.isEmpty())
                emit activity(e.activity);
            if (!e.text.isEmpty()) {
                if (e.textIsWhole && m_sawText)
                    emit piece(QStringLiteral("\n\n"));
                m_sawText = true;
                emit piece(e.text);
            }
            if (e.done)
                m_done = true;
        }
    }
}

void CliChat::onFinished(int exitCode)
{
    onOutput();
    QProcess* p = m_process;
    m_process = nullptr;
    if (p)
        p->deleteLater();
    if (m_cancelled) {
        emit finished(false, QStringLiteral("cancelled"), {});
        return;
    }
    if (!m_error.isEmpty() || exitCode != 0 || !m_done) {
        QString detail = m_error;
        if (detail.isEmpty())
            detail = QString::fromUtf8(m_stderr.right(600)).trimmed();
        emit finished(false, QStringLiteral("failed"), detail);
        return;
    }
    emit finished(true, {}, {});
}

} // namespace dm::ai
