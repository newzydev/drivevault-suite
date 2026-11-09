#pragma once

#include "pdfengine/PdfDocument.h"
#include "PdfTools.h"

#include <QColor>

namespace dp::tools {

// Writes a fresh PDF from allowlisted drawing operations. No original PDF object or stream is copied.
QString writeSelectiveRedaction(const pdf::PdfDocument& source, const QString& staged,
                                const QList<QList<QRectF>>& marks, const QColor& color, const Progress& progress);

} // namespace dp::tools
