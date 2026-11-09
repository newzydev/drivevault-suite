#pragma once

#include "pdftools/PdfTools.h"

#include <QColor>
#include <QRectF>

namespace dp::pdf { class PdfDocument; }

namespace dp {

struct RedactionMark {
    int page;
    QRectF rect;
};

// Creates an image-only, sanitized copy. Empty on success. Never replaces an existing file.
QString redactPdf(const pdf::PdfDocument& source, const QString& path, const QList<RedactionMark>& marks,
                  const QColor& color, const QString& label, const tools::Progress& progress);

} // namespace dp
