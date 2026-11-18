#pragma once

#include <QString>
#include <QVariantList>

namespace dm::ai {

// The colours of the inline parts of an answer (QML passes the colours of Theme.qml: "#rrggbb").
struct MarkdownStyle {
    QString codeBg = QStringLiteral("#20232B");        // inline code
    QString text = QStringLiteral("#F8FAFC");
    QString link = QStringLiteral("#F59E0B");
    int fontPx = 15;                                   // the size of the body text (inline code is a little smaller)
};

// Markdown -> a list of BLOCKS that the page draws as its own pieces (a code box with a copy button, a table, a list row ...). Every block is a map:
//   type "p"      {html}                          a paragraph (a single line break stays a line break: <br>)
//   type "h"      {level 1..6, html}              a heading
//   type "li"     {depth 0.., marker, html}       one row of a bullet / numbered list (marker "•" "◦" "▪" or "1." ..)
//   type "code"   {lang, text}                    a fenced code block (plain text; also an unclosed one: the answer is shown while it is still being written)
//   type "table"  {head [html], aligns [left|center|right], rows [[html]]}
//   type "hr"     {}
// and "quote" (0 = not quoted, 1.. = the depth of > marks). `html` is Qt rich text with only <b> <i> <s> <a href> <br> and styled <span>: bold / italic / strike /
// inline code / links (only http:// and https:// become links). Everything the model wrote is escaped: no tag of the model ever reaches the page. Pure, tested.
QVariantList markdownBlocks(const QString& markdown, const MarkdownStyle& style = {});

} // namespace dm::ai
