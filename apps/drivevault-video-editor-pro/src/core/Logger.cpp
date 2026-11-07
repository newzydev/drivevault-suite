#include "Logger.h"

#include "AppPaths.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfoList>
#include <QMutex>
#include <QMutexLocker>

#include <cstdio>
#include <cstdlib>

Q_LOGGING_CATEGORY(lcApp, "dv.app")
Q_LOGGING_CATEGORY(lcSettings, "dv.settings")
Q_LOGGING_CATEGORY(lcProject, "dv.project")
Q_LOGGING_CATEGORY(lcUi, "dv.ui")

namespace dv::logging {

namespace {
QMutex g_mutex;
QFile g_file;
QString g_path;
QtMessageHandler g_previous = nullptr;

const char* levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return "DEBUG";
    case QtInfoMsg: return "INFO ";
    case QtWarningMsg: return "WARN ";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg: return "FATAL";
    }
    return "?????";
}

void handler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const QString line = QStringLiteral("%1 [%2] %3: %4")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz")),
                                  QString::fromLatin1(levelName(type)),
                                  QString::fromLatin1(context.category ? context.category : "default"),
                                  message);
    {
        QMutexLocker lock(&g_mutex);
        if (g_file.isOpen()) {
            g_file.write(line.toUtf8());
            g_file.write("\n", 1);
            g_file.flush();
        }
    }
    std::fprintf(stderr, "%s\n", line.toLocal8Bit().constData());
    std::fflush(stderr);
    if (type == QtFatalMsg)
        std::abort();
}

void pruneOldLogs(int keepFiles)
{
    const QDir dir(paths::logsDir());
    const QFileInfoList files =
        dir.entryInfoList({QStringLiteral("drivevault-*.log")}, QDir::Files, QDir::Name | QDir::Reversed);
    for (int i = keepFiles; i < files.size(); ++i)
        QFile::remove(files.at(i).absoluteFilePath());
}
} // namespace

void init(int keepFiles)
{
    {
        QMutexLocker lock(&g_mutex);
        if (g_file.isOpen())
            return;

        paths::ensureDir(paths::logsDir());
        g_path = paths::logsDir()
                 + QStringLiteral("/drivevault-%1.log").arg(QDate::currentDate().toString(QStringLiteral("yyyyMMdd")));
        g_file.setFileName(g_path);
        if (!g_file.open(QIODevice::Append | QIODevice::Text))
            std::fprintf(stderr, "Cannot open log file %s\n", g_path.toLocal8Bit().constData());
    }
    pruneOldLogs(keepFiles);
    g_previous = qInstallMessageHandler(handler);
}

void shutdown()
{
    qInstallMessageHandler(g_previous);
    g_previous = nullptr;
    QMutexLocker lock(&g_mutex);
    if (g_file.isOpen())
        g_file.close();
}

QString currentLogFile()
{
    QMutexLocker lock(&g_mutex);
    return g_path;
}

void writeRaw(const QString& line)
{
    QMutexLocker lock(&g_mutex);
    if (g_file.isOpen()) {
        g_file.write(line.toUtf8());
        g_file.write("\n", 1);
        g_file.flush();
    }
}

} // namespace dv::logging
