#include "CommandRunner.h"

#include "ProcessUtil.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QTimer>

namespace dm::ai {

CommandRunner::CommandRunner(QObject* parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this] {
        m_timedOut = true;
        kill();
    });
}

CommandRunner::~CommandRunner()
{
    if (m_process) {
        m_done = nullptr;
        kill();
        m_process->disconnect(this);
        m_process->waitForFinished(2000);
    }
}

QString CommandRunner::cut(const QString& output)
{
    if (output.size() <= kMaxOutput)
        return output;
    const int head = 3000, tail = kMaxOutput - head;
    return output.left(head) + QStringLiteral("\n... (%1 characters left out) ...\n").arg(output.size() - head - tail) + output.right(tail);
}

bool CommandRunner::run(const QString& folder, const QString& command, int seconds, Done done)
{
    if (m_process)
        return false;
    m_output.clear();
    m_stopped = m_timedOut = false;
    m_done = std::move(done);
    seconds = qBound(1, seconds <= 0 ? kDefaultSeconds : seconds, kMaxSeconds);
    m_process = new QProcess(this);
    m_process->setWorkingDirectory(folder);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    const QString shell = QProcessEnvironment::systemEnvironment().value(QStringLiteral("ComSpec"), QStringLiteral("cmd.exe"));
    m_process->setProgram(shell);
    // chcp 65001: the program writes UTF-8, so Thai text in the output stays readable
    m_process->setNativeArguments(QStringLiteral("/d /s /c \"chcp 65001>nul & ") + command + QLatin1Char('"'));
    hideConsole(m_process);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] { if (m_process && m_output.size() < 200000) m_output += m_process->readAllStandardOutput(); });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            finish(QStringLiteral("the command could not be started"), true);
    });
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        if (!m_process)
            return;
        m_output += m_process->readAllStandardOutput();
        QString text = cut(QString::fromUtf8(m_output).trimmed());
        if (m_stopped) {
            finish(QStringLiteral("the command was stopped by the user"), true);
        } else if (m_timedOut) {
            finish(QStringLiteral("the command was stopped after the time limit (%1 s).\n%2").arg(m_timer->interval() / 1000).arg(text), true);
        } else {
            if (text.isEmpty())
                text = QStringLiteral("(no output)");
            finish(QStringLiteral("exit code %1\n%2").arg(code).arg(text), code != 0);
        }
    });
    m_process->start();
    if (m_process) {
        killWithThisProgram(m_process);
        m_process->closeWriteChannel();                                // nothing to type: a program that asks a question ends instead of waiting
        m_timer->start(seconds * 1000);
    }
    return true;
}

void CommandRunner::cancel()
{
    if (!m_process)
        return;
    m_stopped = true;
    kill();
}

// cmd.exe and everything it started
void CommandRunner::kill()
{
    if (!m_process)
        return;
    const qint64 pid = m_process->processId();
    if (pid > 0) {
        auto* tk = new QProcess(this);
        hideConsole(tk);
        connect(tk, &QProcess::finished, tk, &QObject::deleteLater);
        connect(tk, &QProcess::errorOccurred, tk, &QObject::deleteLater);
        tk->start(QStringLiteral("taskkill"), {QStringLiteral("/T"), QStringLiteral("/F"), QStringLiteral("/PID"), QString::number(pid)});
    }
    m_process->kill();
}

void CommandRunner::finish(const QString& text, bool error)
{
    if (!m_process)
        return;
    m_timer->stop();
    QProcess* p = m_process;
    m_process = nullptr;
    p->disconnect(this);
    p->deleteLater();
    Done done = std::move(m_done);
    m_done = nullptr;
    if (done)
        done(text, error);
}

} // namespace dm::ai
