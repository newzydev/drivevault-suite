#include "FileUtils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

namespace dv::fileutils {

bool writeAtomic(const QString& path, const QByteArray& data, QString* error, bool keepBackup)
{
    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (error)
            *error = QStringLiteral("Cannot create directory: %1").arg(info.absolutePath());
        return false;
    }

    if (keepBackup && info.exists()) {
        const QString backup = path + QStringLiteral(".bak");
        QFile::remove(backup);
        QFile::copy(path, backup); // best effort; failure must not block saving
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (file.write(data) != data.size()) {
        if (error)
            *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

QString sanitizeFileName(const QString& name)
{
    static const QString kInvalid = QStringLiteral("<>:\"/\\|?*");
    static const QSet<QString> kReserved = {
        "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
        "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};

    QString out;
    out.reserve(name.size());
    for (const QChar c : name.trimmed()) {
        if (c.unicode() < 0x20 || kInvalid.contains(c))
            continue;
        out.append(c);
    }
    while (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char(' ')))
        out.chop(1);
    if (out.isEmpty() || kReserved.contains(out.section(QLatin1Char('.'), 0, 0).toUpper()))
        return {};
    if (out.size() > 120)
        out.truncate(120);
    return out;
}

} // namespace dv::fileutils
