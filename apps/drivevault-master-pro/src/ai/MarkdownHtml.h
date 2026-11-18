#pragma once

#include <QString>

namespace dm::ai {

// The colours and sizes of the answers of the AI (QML passes the colours of Theme.qml: "#rrggbb").
struct MarkdownStyle {
    QString text = QStringLiteral("#F8FAFC");
    QString muted = QStringLiteral("#A1A1AA");
    QString codeBg = QStringLiteral("#20232B");        // code blocks and inline code
    QString codeHeadBg = QStringLiteral("#2B2F39");    // (kept for callers; the language is now a line inside the code block)
    QString bg = QStringLiteral("#101114");            // the colour of the page behind the answer (the cells of a table)
    QString border = QStringLiteral("#343844");        // table lines, rules, the bar of a quote
    QString link = QStringLiteral("#F59E0B");
    int fontPx = 15;                                   // the size of the body text
    int linePercent = 110;                             // line height of the body, in percent of the font's own line (a Thai font already has room for its marks)
};

// Markdown -> the HTML subset that Qt Quick text understands (TextEdit.RichText): headings, paragraphs (a single line break stays a line break),
// bold / italic / strike / inline code / links (only https:// and http:// become links), bullet and numbered lists (nested), quotes, rules, GFM tables
// and fenced code blocks (also an unclosed one: the answer is shown while it is still being written). Everything the model wrote is escaped: no tag of the
// model ever reaches the output. Pure, tested in tst_ai.
QString markdownToHtml(const QString& markdown, const MarkdownStyle& style = {});

} // namespace dm::ai
