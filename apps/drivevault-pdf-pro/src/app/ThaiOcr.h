#pragma once

#include "pdftools/PdfTools.h"

#include <atomic>

namespace dp::pdf { class PdfDocument; }

namespace dp {

bool thaiOcrAvailable();
QString saveSearchableCopy(const pdf::PdfDocument& doc, const QList<int>& pages, const QString& destination,
                           const std::atomic_bool& cancel, const tools::Progress& progress);

} // namespace dp
