#pragma once

#include "pdftools/PdfTools.h"

#include <atomic>

namespace dp::pdf { class PdfDocument; }

namespace dp {

struct ThaiOcrReport {
    int recognizedPages = 0;
    int skippedTextPages = 0;
    int unrecognizedPages = 0;
};

bool thaiOcrAvailable();
QString saveSearchableCopy(const pdf::PdfDocument& doc, const QList<int>& pages, const QString& destination,
                           const std::atomic_bool& cancel, const tools::Progress& progress, ThaiOcrReport* report = nullptr);

} // namespace dp
