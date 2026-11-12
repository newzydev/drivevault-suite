#pragma once

#include <QPointF>
#include <QString>

namespace dp::pdf { class PdfDocument; }

namespace dp {

QString replaceTextRun(const pdf::PdfDocument& doc, int page, QPointF point, const QString& original,
                       const QString& replacement, const QString& destination, bool remove = false);

} // namespace dp
