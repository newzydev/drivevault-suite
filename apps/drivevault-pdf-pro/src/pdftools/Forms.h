#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace dp::tools {

// A field of an AcroForm (the fill-in form of a PDF).
struct FormField {
    QString name;                 // fully qualified name
    QString type;                 // "text" | "checkbox" | "radio" | "choice" | "button" | "signature"
    QString value;                // text / choice: the value; radio: the chosen option ("" = none)
    bool checked = false;         // checkbox
    QStringList options;          // choice: the list; radio: the option names
    bool readOnly = false;
    bool multiline = false;
    int page = -1;                // 0-based page of its first widget, -1 unknown
    int maxLength = 0;            // 0 = unlimited
};

struct FormRead {
    bool ok = false;
    QString error;                // "" | "password" | "file" | "format" | "unknown"
    bool hasForm = false;
    QList<FormField> fields;
    bool xfa = false;             // the file also has an XFA form (dynamic XML forms): only the classic AcroForm fields are shown
};

FormRead readFormFields(const QString& file, const QString& password);

struct FormFill {
    bool ok = false;
    QString error;                // "" | "password" | "file" | "format" | "exists" | "write" | "unknown" | "noForm"
    QString detail;
    int filled = 0;
    QStringList skipped;          // "name: reason", reason = unicode | option | readonly | unknown | unsupported
    QStringList warnings;         // "encrypted"
    qint64 bytes = 0;
};

// Writes a copy of `file` with the values filled in (name -> text, option name or bool). Appearance streams are made so that every viewer shows
// the values. Text fields take Latin-1 characters only (the standard form fonts have no others). The source is never changed.
FormFill fillForm(const QString& file, const QString& password, const QString& outFile, const QMap<QString, QVariant>& values, bool overwrite);

} // namespace dp::tools
