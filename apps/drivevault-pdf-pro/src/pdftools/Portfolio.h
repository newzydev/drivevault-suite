#pragma once

#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

namespace dp::tools {

// A PDF portfolio: a PDF whose files are embedded (any type) and that asks the reader to show them as a list (/Collection, details view).
// It opens in Acrobat and other readers as a portfolio; readers without portfolio support show the cover page, which lists the files.
// The source files are only read. Returns "" or an error code:
//   "noFiles" | "pdfSuffix" | "exists" | "fileRead:<name>" (a file cannot be read or is empty) | "tooLarge" (more than 1 GiB in total) | "cancelled" | "write" | "unknown".
// `progress(done, total)` is called after each file.
QString createPortfolio(const QString& outFile, const QString& title, const QStringList& files, bool overwrite, const std::atomic_bool& cancel,
                        const std::function<void(int, int)>& progress = {});

} // namespace dp::tools
