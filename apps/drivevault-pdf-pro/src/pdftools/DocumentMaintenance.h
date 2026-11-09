#pragma once

#include <QStringList>
#include <QVariantMap>

namespace dp::tools {

QVariantMap inspectDocument(const QString& source, const QString& password);
// mode: metadata, clean, repair. New files only. Repair enables qpdf's recovery.
QString maintainDocument(const QString& source, const QString& password, const QString& destination,
                         const QString& mode, const QVariantMap& fields = {}, const QStringList& removeAttachments = {});

} // namespace dp::tools
