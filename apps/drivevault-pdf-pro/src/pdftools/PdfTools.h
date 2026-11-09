#pragma once

#include "SplitPlan.h"

#include <QList>
#include <QMarginsF>
#include <QSizeF>
#include <QString>
#include <QStringList>

#include <functional>

namespace dp::tools {

// One page of the file that is going to be written: a page of a source file, or a blank page.
struct PageSpec {
    QString file;                 // source file (ignored for a blank page)
    QString password;             // the open password of the source, when it has one
    int page = 0;                 // 0-based page of the source
    int rotation = 0;             // quarter turns clockwise added to the page's own rotation (any integer)
    QMarginsF crop;               // points cut off each side as the page is SHOWN (after rotation); all zero = no crop
    bool blank = false;
    QSizeF blankSize = QSizeF(595.276, 841.89);       // points, for a blank page (A4)
};

struct WriteResult {
    bool ok = false;
    bool cancelled = false;
    // "" | "password" | "file" | "format" | "range" | "exists" | "write" | "unknown"
    QString error;
    QString detail;               // the library's own message for "format" / "write" / "unknown"
    QString errorFile;            // the source that caused a "password" / "file" / "format" error
    // Things in the sources that the new file does not carry, as "code" or "code:count":
    //   bookmarks  - a source has bookmarks (they are not copied)
    //   forms      - a source has form fields (they may not work in the new file)
    //   encrypted  - a source is protected (the new file is not)
    //   linksRemoved:<n> - links to pages that are not in the new file were removed
    QStringList warnings;
    int pages = 0;
    qint64 bytes = 0;
};

// Called with (pages done, total); return false to cancel.
using Progress = std::function<bool(int done, int total)>;

// Writes `pages` (in this order) as a new PDF. The output file is written next to its final name and then renamed, so a failure never leaves a
// half written file; an existing `outFile` is refused ("exists") unless `overwrite`. The sources are never changed.
WriteResult writePages(const QList<PageSpec>& pages, const QString& outFile, const Progress& progress = {}, bool overwrite = false);

// The number of pages of a file (0 on failure: `error` says why: "password", "file", "format").
int pageCountOf(const QString& file, const QString& password, QString* error = nullptr);

// Splits `file` into parts that each stay under `maxBytes` (as far as one page allows: a single page bigger than the limit is a part of its own).
// The sizes are measured by really writing the parts, so the plan is exact. Empty list on failure (`error` as in WriteResult).
QList<SplitPart> planSplitBySize(const QString& file, const QString& password, qint64 maxBytes, const Progress& progress, QString* error);

} // namespace dp::tools
