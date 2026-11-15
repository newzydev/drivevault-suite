#pragma once

#include "DocumentSearch.h"

#include <QJsonArray>
#include <QList>
#include <QPair>
#include <QString>

namespace dp::assistant {

// True when more than a quarter of the letters of the text are Thai (the answer is then written in Thai).
bool isThai(const QString& text);

QString answerSystemPrompt(bool thai);
// partial: the text is one part of a longer document (the parts are summarised first, then the summaries are joined).
QString summarySystemPrompt(bool thai, bool partial);
QString joinSummariesSystemPrompt(bool thai);

// "[หน้า 3]\nline ...\n\n[หน้า 4]\n..." for the chosen pieces, with the page number (1-based) of each.
QString formatExcerpts(const QList<Chunk>& chunks, const QList<int>& chosen, bool thai);

// The chat request messages: the system prompt, the last few turns of the conversation (role, text), and the new user message.
QJsonArray chatMessages(const QString& system, const QList<QPair<QString, QString>>& history, const QString& user, int keepTurns = 6);

// Page numbers (1-based) an answer refers to: "(หน้า 3)", "หน้า 2, 5", "หน้า 4-6", "page 7", "pp. 2-3". Sorted, without duplicates.
QList<int> citedPages(const QString& answer, int pageCount);

// Takes a reasoning block ("<think>...</think>") out of a model reply, also when it is still open.
QString withoutReasoning(const QString& text);

} // namespace dp::assistant
