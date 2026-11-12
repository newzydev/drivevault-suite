#pragma once

#include "Forms.h"

#include <QMap>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace dp::tools {

// Form data as XFDF (the XML format Acrobat and other programs use to exchange the values of a form): field names with dots are nested
// (<field name="a"><field name="b">), a check box is "Yes" / "Off", a radio group is the name of the chosen option.

// Writes the values of `fields`; `overrides` (name -> text / option / bool) replace the value the file has. Returns "" or "write".
QString writeXfdf(const QString& outFile, const QList<FormField>& fields, const QMap<QString, QVariant>& overrides);

struct FormDataRead {
    bool ok = false;
    QString error;                          // "" | "file" | "format"
    QMap<QString, QVariant> values;         // only fields that exist, can be changed and accept the value (text / option name / bool)
    QStringList unknown;                    // names in the data that this form does not have
    QStringList skipped;                    // "name: reason", reason = readonly | option | unsupported
};

// Reads an XFDF file and matches it against the fields of the open form.
FormDataRead readXfdf(const QString& file, const QList<FormField>& fields);

} // namespace dp::tools
