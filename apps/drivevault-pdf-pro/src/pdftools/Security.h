#pragma once

#include <QString>

namespace dp::tools {

struct Protection {
    QString userPassword;
    QString ownerPassword;
    bool print = true;
    bool copy = true;
    bool modify = true;
    bool annotate = true;
};

// Empty on success. Both operations write a new file and refuse an existing destination.
QString protectPdf(const QString& source, const QString& destination, const Protection& options);
QString unlockPdf(const QString& source, const QString& destination, const QString& password);

} // namespace dp::tools
