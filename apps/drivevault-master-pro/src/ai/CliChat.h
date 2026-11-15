#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QProcess;

namespace dm::ai {

// Works through the official command line program the user already has and is signed in to: Claude Code (`claude`) or Codex (`codex`). The program does
// the talking to the AI; this class only starts it with FIXED options, hands the question over on its standard input (never on the command line: nothing the
// user typed is ever parsed by a shell), and reads its JSON event stream. Nothing here asks for or stores a password or a key.
class CliChat : public QObject {
    Q_OBJECT
public:
    enum class Tool { Claude, Codex };

    struct Request {
        Tool tool = Tool::Claude;
        QString prompt;
        QString workDir;                  // the folder the program works in (the agent) or an empty temporary folder (questions)
        bool allowEdits = false;          // false: it can only read; true: it may also edit files in workDir
        QString sessionId;                // Claude: continue this conversation
        QString executable;               // default: found on PATH
    };

    explicit CliChat(QObject* parent = nullptr);
    ~CliChat() override;

    static QString executableFor(Tool tool);                 // "" when not installed
    static QString versionOf(const QString& executable);     // "" when it does not answer (blocks up to 8 s: call it from a button, not on start-up)
    static QString toolName(Tool tool);

    // "" when started, otherwise: noProgram | noFolder | startFailed
    QString start(const Request& request);
    void cancel();
    bool running() const { return m_process != nullptr; }

    // Pure, tested: the options of each program and the reading of one line of its event stream.
    static QStringList arguments(const Request& request);
    struct Event {
        QString text;                     // a piece of the answer
        QString activity;                 // "Read: notes.txt" - what the agent does
        QString sessionId;
        QString error;
        bool done = false;
        bool textIsWhole = false;         // true: `text` is a complete message (not a piece)
        bool newBlock = false;            // a new block of text starts: a break goes between it and the text before
    };
    static QList<Event> readLine(Tool tool, const QByteArray& line, bool* streamedText);

signals:
    void piece(const QString& text);
    void activity(const QString& text);
    void sessionId(const QString& id);
    void finished(bool ok, const QString& code, const QString& detail);    // code: "" | cancelled | failed | startFailed

private:
    void onOutput();
    void onFinished(int exitCode);

    QProcess* m_process = nullptr;
    Tool m_tool = Tool::Claude;
    QByteArray m_buffer;
    QByteArray m_stderr;
    QString m_error;
    bool m_streamed = false;
    bool m_sawText = false;
    bool m_cancelled = false;
    bool m_done = false;
};

} // namespace dm::ai
