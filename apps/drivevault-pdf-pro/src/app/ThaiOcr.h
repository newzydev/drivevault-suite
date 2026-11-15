#pragma once

#include "pdftools/PdfTools.h"

#include <QString>

#include <atomic>
#include <vector>

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

// Thai text rules of the OpenThai OCR model (pure functions, also used by the tests).
// One spelling per rendering: NFC, marks in one order, sara am composed, legacy glyph variants and zero-width characters removed, spaces collapsed.
QString canonicalThai(const QString& text);
// A lone W / w glued to Thai letters is a Thai pho phan; a Latin O or 0 inside Thai digits is a Thai zero.
QString fixThaiScripts(const QString& text);
// Share (0..1) of the Thai characters that break Thai spelling (a mark without a consonant, a leading vowel without a consonant after it).
double invalidThaiShare(const QString& text);
// Whether a recognised line is kept: confidence, length and spelling rules (stamps, seals and signatures read as short, unsure, ill-spelled strings).
bool keepOcrLine(const QString& text, double confidence);

} // namespace dp
