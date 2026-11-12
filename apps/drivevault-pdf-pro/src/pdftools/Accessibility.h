#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace dp::tools {

// One line of the accessibility check.
struct AccessCheck {
    // "tagged" | "figureAlt" | "language" | "title" | "displayTitle" | "bookmarks" | "fieldTips" | "security" | "pdfa"
    QString id;
    // "pass" | "fail" | "warn" (worth fixing, not an error) | "info" | "na" (does not apply)
    QString status;
    int count = 0;                // figureAlt: figures without a description; fieldTips: fields without a tool tip; pdfa: the part (1, 2, 3 ...)
    QString detail;               // title: the title; language: the tag
};

struct AccessReport {
    bool ok = false;
    QString error;                // "" | "password" | "file" | "format" | "unknown"
    int pages = 0;
    QList<AccessCheck> checks;
};

// A quick check of the things a screen reader and the PDF/UA rules start from. It reads the structure of the file with qpdf; it is NOT a full PDF/UA
// validation (reading order, table structure, colour contrast and so on are not judged) and does not prove that the file is accessible.
AccessReport checkAccessibility(const QString& file, const QString& password);

struct AccessFix {
    bool ok = false;
    QString error;                // "" | "password" | "file" | "format" | "exists" | "write" | "nothing" | "unknown"
    QString detail;
    QStringList warnings;         // "encrypted": the copy keeps the protection of the source
};

// Writes a copy with the document title and / or the language set (and "show the title in the title bar" switched on). Empty = leave as it is.
// The source is never changed; an existing `outFile` is refused unless `overwrite`.
AccessFix setTitleAndLanguage(const QString& file, const QString& password, const QString& outFile, const QString& title, const QString& language, bool overwrite);

} // namespace dp::tools
