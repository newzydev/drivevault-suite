#pragma once

#include <QList>
#include <QString>
#include <QStringList>

namespace dp::assistant {

// A piece of the document text for the AI assistant: a few lines of one page.
struct Chunk {
    int page = 0;        // 0-based
    QString text;
};

// Cuts the text of every page into pieces of at most `maxChars` characters (whole lines; a longer line is cut). Pieces never cross a page.
QList<Chunk> chunkPages(const QStringList& pageTexts, int maxChars = 900);

// What a text is searched by: lower-case words of Latin letters and digits, and the 2- and 3-letter groups of every run of Thai letters (Thai is written
// without spaces between words, so there are no words to split).
QStringList searchTerms(const QString& text);

// The pieces that answer a question best (BM25 over `searchTerms`, question words such as "อะไร" / "what" left out), in document order, at most
// `maxChars` characters in all and `maxCount` pieces. When nothing matches, the start of the document is returned. Indices into `chunks`.
QList<int> bestChunks(const QList<Chunk>& chunks, const QString& question, int maxChars = 5200, int maxCount = 8);

} // namespace dp::assistant
