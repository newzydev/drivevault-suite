#pragma once

#include <QList>
#include <QString>

namespace dp {

// Parses a page range text such as "1-3, 5, 8-" (1-based, "8-" = to the last page, "-3" = from the first) into a list of 0-based page
// numbers in the order they are written, without duplicates, clamped to [1, pageCount]. An empty list means the text is not valid.
QList<int> parsePageRanges(const QString& text, int pageCount);

} // namespace dp
