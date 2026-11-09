#pragma once

#include <QList>
#include <QPair>
#include <QString>

namespace dp::tools {

// One output file of a split: the pages (0-based, in order) and a short text for the file name.
struct SplitPart {
    QList<int> pages;
    QString suffix;
};

// "1-3, 5, 8-": every comma separated part becomes one file. Empty list (ok = false) when the text is not valid for the page count.
QList<SplitPart> splitByRanges(const QString& text, int pageCount, bool* ok = nullptr);
// Every `n` pages one file (the last one may be shorter).
QList<SplitPart> splitEvery(int n, int pageCount);
// One file per top level bookmark: from its page to the page before the next one. Pages before the first bookmark form their own part.
// `topLevel` = (title, 0-based page) of the level 0 entries; entries without a page are ignored, several on one page make one part.
QList<SplitPart> splitByBookmarks(const QList<QPair<QString, int>>& topLevel, int pageCount);

// "1-3,5" (1-based) for a list of 0-based pages, runs of consecutive pages are joined.
QString rangeLabel(const QList<int>& pages);
// A text that is safe as part of a Windows file name (no \ / : * ? " < > |, no control characters, trimmed, not too long).
QString safeFileName(const QString& text);
// <dir>/<stem>-<suffix>.pdf, or with " (2)", " (3)" ... when it exists already. Never returns an existing path.
QString uniqueOutputPath(const QString& dir, const QString& stem, const QString& suffix);

} // namespace dp::tools
