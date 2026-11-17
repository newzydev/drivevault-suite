#pragma once

#include "CommandRunner.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

namespace dm::ai {

// The commands of the agent page as TASKS. A command is a task while it runs and stays in the list afterwards (finished, failed or stopped). A foreground command is
// waited for by the agent (`start` calls `done` when it ends); a BACKGROUND command goes on while the agent works on: `done` is called at once, the output can be
// read at any time (output) and the user or the agent can stop it (stop). Every command was allowed by the user before it got here (LocalAgent asks every time) and
// all of them die with this program (a job object, see CommandRunner). At most kMaxRunning run at the same time.
class CommandPool : public QObject {
    Q_OBJECT
public:
    explicit CommandPool(QObject* parent = nullptr);
    ~CommandPool() override;

    static constexpr int kMaxRunning = 4;
    static constexpr int kKeepTasks = 40;                              // the newest finished ones that stay in the list

    // The id of the task, or 0 when it cannot start (kMaxRunning commands already run).
    int start(const QString& folder, const QString& command, int seconds, bool background, const CommandRunner::Done& done);
    bool stop(int id);                                                 // kills a running task; false when there is no such running task
    bool known(int id) const;
    QString output(int id) const;                                      // what the task has written so far
    // [{id, title, command, state: running | done | error | stopped, startedMs, endedMs, note, background}], the newest first
    QVariantList list() const;
    int runningCount() const;
    QVariantMap info(int id) const;

signals:
    void changed();                                                    // a task started, ended or was stopped
    void finished(int id);                                             // a task ended (any way)

private:
    struct Task {
        int id = 0;
        QString command, folder, state, note;
        qint64 startedMs = 0, endedMs = 0;
        bool background = false;
        CommandRunner* runner = nullptr;
    };
    Task* find(int id);
    const Task* find(int id) const;
    void prune();

    QList<Task*> m_tasks;
    int m_nextId = 0;
};

} // namespace dm::ai
