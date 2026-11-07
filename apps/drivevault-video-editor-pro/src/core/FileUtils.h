#pragma once

#include <QByteArray>
#include <QString>

namespace dv::fileutils {

// Atomically replaces `path` with `data` (write to a temporary file, then rename).
// When `keepBackup` is true the previous file is preserved as "<path>.bak".
bool writeAtomic(const QString& path, const QByteArray& data, QString* error = nullptr, bool keepBackup = false);

// Turns a user supplied name into something safe to use as a Windows file name.
// Returns an empty string when nothing usable remains.
QString sanitizeFileName(const QString& name);

} // namespace dv::fileutils
