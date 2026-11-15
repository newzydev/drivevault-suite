#include "ProcessUtil.h"

#include <QProcess>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace dm::ai {

#ifdef Q_OS_WIN
namespace {
HANDLE killOnCloseJob()
{
    static HANDLE job = [] {
        HANDLE h = CreateJobObjectW(nullptr, nullptr);
        if (h) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(h, JobObjectExtendedLimitInformation, &info, sizeof(info));
        }
        return h;
    }();
    return job;
}
} // namespace
#endif

void hideConsole(QProcess* process)
{
#ifdef Q_OS_WIN
    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#else
    Q_UNUSED(process)
#endif
}

void killWithThisProgram(QProcess* process)
{
#ifdef Q_OS_WIN
    if (HANDLE job = killOnCloseJob()) {
        if (HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(process->processId()))) {
            AssignProcessToJobObject(job, h);
            CloseHandle(h);
        }
    }
#else
    Q_UNUSED(process)
#endif
}

QString findOnPath(const QString& name)
{
    return QStandardPaths::findExecutable(name);
}

} // namespace dm::ai
