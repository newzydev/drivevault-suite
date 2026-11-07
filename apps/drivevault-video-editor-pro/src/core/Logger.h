#pragma once

#include <QLoggingCategory>
#include <QString>

Q_DECLARE_LOGGING_CATEGORY(lcApp)
Q_DECLARE_LOGGING_CATEGORY(lcSettings)
Q_DECLARE_LOGGING_CATEGORY(lcProject)
Q_DECLARE_LOGGING_CATEGORY(lcUi)

namespace dv::logging {

// Installs the file + console message handler. Safe to call more than once.
// Logs go to <logsDir>/drivevault-YYYYMMDD.log; at most `keepFiles` files are retained.
void init(int keepFiles = 7);
void shutdown();

QString currentLogFile();

// Appends a raw line to the active log (used by the crash handler).
void writeRaw(const QString& line);

} // namespace dv::logging
