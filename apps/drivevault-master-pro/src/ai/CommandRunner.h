#pragma once

#include <QObject>
#include <QString>

#include <functional>

class QProcess;
class QTimer;

namespace dm::ai {

// Runs ONE command line for the agent (tool run_command) with cmd.exe in the working folder: no window, no input, the output (both streams) is collected, cut
// to a size the model can read, and handed back with the exit code. It only runs after the user pressed Allow on the command (LocalAgent asks every time, in
// every mode that offers the tool). A command that runs longer than the time limit, or is stopped by the user, is killed with everything it started.
// It is NOT limited to the working folder: it starts there, but a command can reach whatever the user can reach - that is why the user always sees it first.
class CommandRunner : public QObject {
    Q_OBJECT
public:
    explicit CommandRunner(QObject* parent = nullptr);
    ~CommandRunner() override;

    static constexpr int kDefaultSeconds = 60;
    static constexpr int kMaxSeconds = 300;
    static constexpr int kMaxOutput = 8000;                            // characters handed back to the model

    using Done = std::function<void(const QString& text, bool error)>;
    // false (and `done` not called) when one is already running
    bool run(const QString& folder, const QString& command, int seconds, Done done);
    void cancel();                                                     // kills the command; `done` is called with "stopped"
    bool running() const { return m_process != nullptr; }

    static QString cut(const QString& output);                         // pure, tested: the beginning and the end when it is too long

private:
    void kill();
    void finish(const QString& text, bool error);

    QProcess* m_process = nullptr;
    QTimer* m_timer = nullptr;
    QByteArray m_output;
    Done m_done;
    bool m_stopped = false;
    bool m_timedOut = false;
};

} // namespace dm::ai
