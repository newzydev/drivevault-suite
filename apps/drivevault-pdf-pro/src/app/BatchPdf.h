#pragma once

#include "pdftools/PdfTools.h"

#include <QVariantList>
#include <QVariantMap>

namespace dp {

QVariantList batchPdf(const QString& source, const QString& destination, const QVariantMap& options,
                      const tools::Progress& progress, QString& error);

} // namespace dp
