#include "CrashHandler.h"

#include "core/Logger.h"

#include <QDateTime>
#include <QString>

#include <cstdlib>
#include <exception>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace dv::crash {

namespace {
#ifdef Q_OS_WIN
LONG WINAPI unhandledFilter(EXCEPTION_POINTERS* info)
{
    const DWORD code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0;
    const void* address = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionAddress : nullptr;
    logging::writeRaw(QStringLiteral("%1 [FATAL] dv.crash: unhandled exception 0x%2 at %3")
                          .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")))
                          .arg(QString::number(code, 16).toUpper())
                          .arg(QString::number(reinterpret_cast<quintptr>(address), 16)));
    return EXCEPTION_CONTINUE_SEARCH; // let Windows terminate the process / show its dialog
}
#endif

void terminateHandler()
{
    logging::writeRaw(QStringLiteral("%1 [FATAL] dv.crash: std::terminate called")
                          .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))));
    std::abort();
}
} // namespace

void install()
{
    std::set_terminate(terminateHandler);
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(unhandledFilter);
#endif
}

} // namespace dv::crash
