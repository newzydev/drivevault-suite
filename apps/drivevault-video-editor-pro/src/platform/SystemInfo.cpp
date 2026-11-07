#include "SystemInfo.h"

#include "core/AppPaths.h"
#include "dv/core/Version.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSysInfo>
#include <QUrl>

namespace dv {

QString SystemInfo::productName() const { return QString::fromLatin1(version::kProductName); }
QString SystemInfo::version() const { return QString::fromLatin1(version::kVersion); }
QString SystemInfo::buildNumber() const { return QString::fromLatin1(version::kBuildNumber); }

QString SystemInfo::operatingSystem() const
{
    return QSysInfo::prettyProductName() + QStringLiteral(" (") + QSysInfo::kernelVersion() + QLatin1Char(')');
}

QString SystemInfo::architecture() const { return QSysInfo::currentCpuArchitecture(); }
QString SystemInfo::qtVersion() const { return QString::fromLatin1(qVersion()); }

QString SystemInfo::compiler() const
{
#if defined(_MSC_VER)
    return QStringLiteral("MSVC %1").arg(_MSC_VER);
#elif defined(__clang__)
    return QStringLiteral("Clang %1.%2").arg(__clang_major__).arg(__clang_minor__);
#elif defined(__GNUC__)
    return QStringLiteral("GCC %1.%2").arg(__GNUC__).arg(__GNUC_MINOR__);
#else
    return QStringLiteral("unknown");
#endif
}

QString SystemInfo::dataDirectory() const { return QDir::toNativeSeparators(paths::dataDir()); }
QString SystemInfo::logDirectory() const { return QDir::toNativeSeparators(paths::logsDir()); }
QString SystemInfo::defaultCacheDirectory() const { return QDir::toNativeSeparators(paths::defaultCacheDir()); }
QString SystemInfo::defaultExportDirectory() const { return QDir::toNativeSeparators(paths::defaultExportDir()); }

bool SystemInfo::showInFolder(const QString& path) const
{
#ifdef Q_OS_WIN
    return QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
#else
    return QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
}

bool SystemInfo::openFile(const QString& path) const
{
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

QString SystemInfo::documentPath(const QString& fileName) const
{
    if (fileName.isEmpty() || fileName.contains(QLatin1Char('/')) || fileName.contains(QLatin1Char('\\')))
        return {};
    QStringList dirs;
    const QString app = QCoreApplication::applicationDirPath();
    dirs << app + QStringLiteral("/docs") << app + QStringLiteral("/licenses");
    QDir d(app);
    for (int i = 0; i < 6; ++i) {
        dirs << d.filePath(QStringLiteral("docs"));
        if (!d.cdUp())
            break;
    }
    for (const QString& dir : std::as_const(dirs)) {
        const QString p = QDir(dir).filePath(fileName);
        if (QFileInfo::exists(p))
            return QDir::cleanPath(p);
    }
    return {};
}

bool SystemInfo::openFolder(const QString& path) const
{
    paths::ensureDir(path);
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

} // namespace dv
