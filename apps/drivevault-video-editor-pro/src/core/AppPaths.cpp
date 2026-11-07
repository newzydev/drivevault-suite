#include "AppPaths.h"

#include <QDir>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

namespace dv::paths {

namespace {
QMutex g_mutex;
QString g_overrideRoot;

QString overrideRoot()
{
    QMutexLocker lock(&g_mutex);
    return g_overrideRoot;
}

QString userDir(QStandardPaths::StandardLocation location)
{
    const QString base = QStandardPaths::writableLocation(location);
    return base.isEmpty() ? QDir::homePath() : base;
}
} // namespace

void setOverrideRoot(const QString& root)
{
    QMutexLocker lock(&g_mutex);
    g_overrideRoot = root.isEmpty() ? QString() : QDir::cleanPath(root);
}

QString dataDir()
{
    if (const QString root = overrideRoot(); !root.isEmpty())
        return root + QStringLiteral("/data");
    // GenericDataLocation is %LOCALAPPDATA% on Windows: one folder holds settings, logs, autosave and cache.
    return userDir(QStandardPaths::GenericDataLocation) + QStringLiteral("/DriveVault/VideoEditorPro");
}

QString logsDir() { return dataDir() + QStringLiteral("/logs"); }
QString autosaveDir() { return dataDir() + QStringLiteral("/autosave"); }

QString defaultCacheDir() { return dataDir() + QStringLiteral("/cache"); }

QString settingsFile() { return dataDir() + QStringLiteral("/settings.json"); }
QString recentFile() { return dataDir() + QStringLiteral("/recent-projects.json"); }

QString defaultProjectsDir()
{
    if (const QString root = overrideRoot(); !root.isEmpty())
        return root + QStringLiteral("/projects");
    return userDir(QStandardPaths::DocumentsLocation) + QStringLiteral("/DriveVault Projects");
}

QString defaultExportDir()
{
    if (const QString root = overrideRoot(); !root.isEmpty())
        return root + QStringLiteral("/exports");
    return userDir(QStandardPaths::MoviesLocation) + QStringLiteral("/DriveVault Exports");
}

bool ensureDir(const QString& path)
{
    return !path.isEmpty() && QDir().mkpath(path);
}

} // namespace dv::paths
