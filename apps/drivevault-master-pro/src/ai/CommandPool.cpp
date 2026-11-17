#include "CommandPool.h"

#include <QDateTime>
#include <QVariantMap>

namespace dm::ai {

CommandPool::CommandPool(QObject* parent)
    : QObject(parent)
{
}

CommandPool::~CommandPool()
{
    for (Task* t : std::as_const(m_tasks)) {
        delete t->runner;                                              // kills a running command with everything it started
        delete t;
    }
}

CommandPool::Task* CommandPool::find(int id)
{
    for (Task* t : std::as_const(m_tasks))
        if (t->id == id)
            return t;
    return nullptr;
}

const CommandPool::Task* CommandPool::find(int id) const
{
    for (const Task* t : m_tasks)
        if (t->id == id)
            return t;
    return nullptr;
}

int CommandPool::runningCount() const
{
    int n = 0;
    for (const Task* t : m_tasks)
        if (t->state == QLatin1String("running"))
            ++n;
    return n;
}

bool CommandPool::known(int id) const { return find(id) != nullptr; }

// only the newest finished tasks stay
void CommandPool::prune()
{
    int finished = 0;
    for (int i = int(m_tasks.size()) - 1; i >= 0; --i) {
        if (m_tasks.at(i)->state == QLatin1String("running"))
            continue;
        if (++finished > kKeepTasks) {
            delete m_tasks.at(i)->runner;
            delete m_tasks.at(i);
            m_tasks.removeAt(i);
        }
    }
}

int CommandPool::start(const QString& folder, const QString& command, int seconds, bool background, const CommandRunner::Done& done)
{
    if (runningCount() >= kMaxRunning)
        return 0;
    auto* t = new Task;
    t->id = ++m_nextId;
    t->command = command;
    t->folder = folder;
    t->state = QStringLiteral("running");
    t->startedMs = QDateTime::currentMSecsSinceEpoch();
    t->background = background;
    t->runner = new CommandRunner(this);
    m_tasks.append(t);
    const int id = t->id;
    const bool started = t->runner->run(folder, command, background ? -1 : seconds, [this, id, background, done](const QString& text, bool error) {
        if (Task* task = find(id)) {
            task->endedMs = QDateTime::currentMSecsSinceEpoch();
            task->state = text.startsWith(QLatin1String("the command was stopped by the user")) ? QStringLiteral("stopped") : error ? QStringLiteral("error") : QStringLiteral("done");
            task->note = text.section(QLatin1Char('\n'), 0, 0).left(120);
        }
        emit changed();
        emit finished(id);
        if (!background && done)
            done(text, error);
    });
    if (!started) {
        m_tasks.removeOne(t);
        delete t->runner;
        delete t;
        return 0;
    }
    prune();
    emit changed();
    if (background && done)
        done(QStringLiteral("Started in the background as task %1. It goes on while you work. Read what it wrote with command_output (task_id %1), stop it with stop_command; "
                            "you are told when it ends.").arg(id),
             false);
    return id;
}

bool CommandPool::stop(int id)
{
    Task* t = find(id);
    if (!t || t->state != QLatin1String("running") || !t->runner)
        return false;
    t->runner->cancel();
    return true;
}

QString CommandPool::output(int id) const
{
    const Task* t = find(id);
    return t && t->runner ? t->runner->liveOutput() : QString();
}

QVariantMap CommandPool::info(int id) const
{
    const Task* t = find(id);
    if (!t)
        return {};
    return QVariantMap{{"id", t->id}, {"title", t->command.simplified().left(90)}, {"command", t->command}, {"state", t->state}, {"startedMs", double(t->startedMs)},
                       {"endedMs", double(t->endedMs)}, {"note", t->note}, {"background", t->background}};
}

QVariantList CommandPool::list() const
{
    QVariantList out;
    for (int i = int(m_tasks.size()) - 1; i >= 0; --i)
        out << info(m_tasks.at(i)->id);
    return out;
}

} // namespace dm::ai
