#include "MarkdownHtml.h"

#include <QRegularExpression>
#include <QStringList>
#include <QVector>

namespace dm::ai {

namespace {

QString esc(const QString& s)
{
    QString out = s;
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    out.replace(QLatin1Char('%'), QStringLiteral("&#37;"));          // (QString::arg would take a "%2" of the model's text for a placeholder)
    out.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    out.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return out;
}

const QString kMono = QStringLiteral("'Cascadia Mono', Consolas, 'Courier New', monospace");

// bold, italic, strike, inline code and links inside one run of text
QString inlineHtml(const QString& raw, const MarkdownStyle& st)
{
    // 1. inline code first: its content is not formatted any further
    QStringList codes;
    QString s;
    int i = 0;
    while (i < raw.size()) {
        if (raw.at(i) == QLatin1Char('`')) {
            int ticks = 0;
            while (i + ticks < raw.size() && raw.at(i + ticks) == QLatin1Char('`'))
                ++ticks;
            const QString fence(ticks, QLatin1Char('`'));
            const int close = raw.indexOf(fence, i + ticks);
            if (close > i + ticks) {
                codes << raw.mid(i + ticks, close - i - ticks).trimmed();
                s += QChar(0x01) + QString::number(codes.size() - 1) + QChar(0x02);
                i = close + ticks;
                continue;
            }
            s += raw.mid(i, ticks);
            i += ticks;
            continue;
        }
        s += raw.at(i);
        ++i;
    }
    // 2. escape what the model wrote
    s = esc(s);
    // 3. links [text](https://...)
    static const QRegularExpression link(QStringLiteral(R"(\[([^\]\n]+)\]\((https?://[^\s)]+)\))"));
    s.replace(link, QStringLiteral("<a href=\"\\2\"><span style=\"color:%1\">\\1</span></a>").arg(st.link));
    // 4. emphasis
    static const QRegularExpression bold(QStringLiteral(R"(\*\*(?=\S)(.+?)(?<=\S)\*\*)"));
    static const QRegularExpression bold2(QStringLiteral(R"((?<![\w_])__(?=\S)(.+?)(?<=\S)__(?![\w_]))"));
    static const QRegularExpression italic(QStringLiteral(R"((?<![\w*])\*(?=[^\s*])(.+?)(?<=[^\s*])\*(?![\w*]))"));
    static const QRegularExpression italic2(QStringLiteral(R"((?<![\w_])_(?=[^\s_])(.+?)(?<=[^\s_])_(?![\w_]))"));
    static const QRegularExpression strike(QStringLiteral(R"(~~(?=\S)(.+?)(?<=\S)~~)"));
    s.replace(bold, QStringLiteral("<b>\\1</b>"));
    s.replace(bold2, QStringLiteral("<b>\\1</b>"));
    s.replace(italic, QStringLiteral("<i>\\1</i>"));
    s.replace(italic2, QStringLiteral("<i>\\1</i>"));
    s.replace(strike, QStringLiteral("<s>\\1</s>"));
    // 5. put the code back
    for (int c = 0; c < codes.size(); ++c) {
        const QString span = QStringLiteral("<span style=\"font-family:%1; font-size:%2px; background-color:%3; color:%4\">&nbsp;%5&nbsp;</span>")
                                 .arg(kMono).arg(qMax(11, st.fontPx - 2)).arg(st.codeBg, st.text, esc(codes.at(c)));
        s.replace(QChar(0x01) + QString::number(c) + QChar(0x02), span);
    }
    return s;
}

QString paragraph(const QStringList& lines, const MarkdownStyle& st)
{
    QStringList parts;
    for (const QString& l : lines)
        parts << inlineHtml(l.trimmed(), st);
    return QStringLiteral("<p style=\"margin-top:0px; margin-bottom:12px; line-height:%1%\">%2</p>").arg(st.linePercent).arg(parts.join(QStringLiteral("<br>")));
}

QString codeBlock(const QStringList& lines, const QString& lang, const MarkdownStyle& st)
{
    const QString body = esc(lines.join(QLatin1Char('\n')));
    // one cell: the language (small, grey) above the code
    const QString head = lang.isEmpty() ? QString() : QStringLiteral("<p style=\"margin:0px 0px 4px 0px; font-size:%1px; color:%2\">%3</p>").arg(qMax(10, st.fontPx - 4)).arg(st.muted, esc(lang));
    // Qt paints the colour of a table (bgcolor on the <table>) behind everything including the padding of the cells, but the colour of a cell only behind its content
    return QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"12\" bgcolor=\"%1\"><tr><td>%2<pre style=\"margin:0px; white-space:pre-wrap; font-family:%3; font-size:%4px; color:%5\">%6</pre></td></tr></table>"
                          "<p style=\"margin:0px; font-size:6px\">&nbsp;</p>")
        .arg(st.codeBg, head, kMono).arg(qMax(11, st.fontPx - 2)).arg(st.text, body);
}

QVector<QString> splitRow(const QString& line)
{
    QString l = line.trimmed();
    if (l.startsWith(QLatin1Char('|')))
        l.remove(0, 1);
    if (l.endsWith(QLatin1Char('|')))
        l.chop(1);
    QVector<QString> cells;
    for (const QString& c : l.split(QLatin1Char('|')))
        cells.append(c.trimmed());
    return cells;
}

const QRegularExpression& fenceRx()
{
    static const QRegularExpression rx(QStringLiteral(R"(^\s*(```+|~~~+)\s*([^\s`]*)[^`]*$)"));
    return rx;
}
const QRegularExpression& headingRx()
{
    static const QRegularExpression rx(QStringLiteral(R"(^\s{0,3}(#{1,6})\s+(.*?)\s*#*\s*$)"));
    return rx;
}
const QRegularExpression& ruleRx()
{
    static const QRegularExpression rx(QStringLiteral(R"(^\s{0,3}([-*_])(\s*\1){2,}\s*$)"));
    return rx;
}
const QRegularExpression& itemRx()
{
    static const QRegularExpression rx(QStringLiteral(R"(^(\s*)([-*+]|\d{1,9}[.)])\s+(.*)$)"));
    return rx;
}
const QRegularExpression& quoteRx()
{
    static const QRegularExpression rx(QStringLiteral(R"(^\s{0,3}>\s?(.*)$)"));
    return rx;
}
const QRegularExpression& tableSepRx()
{
    static const QRegularExpression rx(QStringLiteral(R"(^\s*\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)*\|?\s*$)"));
    return rx;
}

int indentOf(const QString& s)
{
    int n = 0;
    for (const QChar c : s) {
        if (c == QLatin1Char(' '))
            ++n;
        else if (c == QLatin1Char('\t'))
            n += 4;
        else
            break;
    }
    return n;
}

bool startsBlock(const QString& line, const QString& next)
{
    return fenceRx().match(line).hasMatch() || headingRx().match(line).hasMatch() || ruleRx().match(line).hasMatch() || itemRx().match(line).hasMatch()
           || quoteRx().match(line).hasMatch() || (line.contains(QLatin1Char('|')) && tableSepRx().match(next).hasMatch() && next.contains(QLatin1Char('-')));
}

QString convert(const QStringList& lines, const MarkdownStyle& st);

QString listHtml(const QStringList& lines, int& at, const MarkdownStyle& st)
{
    struct Level { int indent; bool ordered; };
    QVector<Level> stack;
    QString out;
    bool liOpen = false;                                        // the <li> of the deepest level is still open (a nested list may go inside)
    auto closeLevel = [&] {
        if (liOpen)
            out += QStringLiteral("</li>");
        out += stack.last().ordered ? QStringLiteral("</ol>") : QStringLiteral("</ul>");
        stack.removeLast();
        liOpen = !stack.isEmpty();                              // the item of the parent that held the nested list
    };
    while (at < lines.size()) {
        const QRegularExpressionMatch m = itemRx().match(lines.at(at));
        if (!m.hasMatch()) {
            // a continuation of the last item: indented text right below it; a blank line only ends the list when no item follows
            const QString& l = lines.at(at);
            if (l.trimmed().isEmpty()) {
                int k = at + 1;
                while (k < lines.size() && lines.at(k).trimmed().isEmpty())
                    ++k;
                if (k < lines.size() && itemRx().match(lines.at(k)).hasMatch()) {
                    at = k;
                    continue;
                }
                break;
            }
            if (!stack.isEmpty() && indentOf(l) >= stack.last().indent + 2 && !startsBlock(l, QString())) {
                out += QStringLiteral("<br>") + inlineHtml(l.trimmed(), st);
                ++at;
                continue;
            }
            break;
        }
        const int indent = indentOf(m.captured(1));
        const QString marker = m.captured(2);
        const bool ordered = marker.at(0).isDigit();
        while (!stack.isEmpty() && indent < stack.last().indent)
            closeLevel();
        if (!stack.isEmpty() && indent == stack.last().indent && ordered != stack.last().ordered)
            closeLevel();
        if (stack.isEmpty() || indent > stack.last().indent + 1) {
            const int number = ordered ? marker.left(marker.size() - 1).toInt() : 0;
            const QString open = ordered ? QStringLiteral("<ol style=\"margin-top:0px; margin-bottom:%1px; margin-left:0px; -qt-list-indent:%3\"%2>")
                                               .arg(stack.isEmpty() ? 10 : 2).arg(number > 1 ? QStringLiteral(" start=\"%1\"").arg(number) : QString()).arg(stack.size() + 1)
                                         : QStringLiteral("<ul style=\"margin-top:0px; margin-bottom:%1px; margin-left:0px; -qt-list-indent:%2\">").arg(stack.isEmpty() ? 10 : 2).arg(stack.size() + 1);
            if (!stack.isEmpty() && !liOpen)
                out += QStringLiteral("<li>");
            out += open;
            stack.append({indent, ordered});
            liOpen = false;
        } else if (liOpen) {
            out += QStringLiteral("</li>");
        }
        out += QStringLiteral("<li style=\"margin-top:0px; margin-bottom:5px; line-height:%1%\">").arg(st.linePercent) + inlineHtml(m.captured(3).trimmed(), st);
        liOpen = true;
        ++at;
    }
    while (!stack.isEmpty())
        closeLevel();
    return out;
}

QString convert(const QStringList& lines, const MarkdownStyle& st)
{
    QString out;
    int i = 0;
    const int n = lines.size();
    while (i < n) {
        const QString& line = lines.at(i);
        if (line.trimmed().isEmpty()) {
            ++i;
            continue;
        }
        const QString next = i + 1 < n ? lines.at(i + 1) : QString();
        // fenced code (an unclosed fence runs to the end: the answer is still being written)
        if (const QRegularExpressionMatch f = fenceRx().match(line); f.hasMatch()) {
            const QString fence = f.captured(1);
            QStringList code;
            ++i;
            while (i < n && !(lines.at(i).trimmed().startsWith(fence.left(3)) && lines.at(i).trimmed().remove(fence.at(0)).trimmed().isEmpty()))
                code << lines.at(i++);
            if (i < n)
                ++i;                                             // the closing fence
            out += codeBlock(code, f.captured(2), st);
            continue;
        }
        if (const QRegularExpressionMatch h = headingRx().match(line); h.hasMatch()) {
            const int level = h.captured(1).size();
            const int size = level == 1 ? st.fontPx + 7 : level == 2 ? st.fontPx + 4 : level == 3 ? st.fontPx + 2 : st.fontPx;
            out += QStringLiteral("<p style=\"margin-top:%1px; margin-bottom:6px; font-size:%2px; font-weight:600; line-height:%3%\">%4</p>")
                       .arg(level <= 2 ? 22 : 14).arg(size).arg(st.linePercent - 15).arg(inlineHtml(h.captured(2), st));
            ++i;
            continue;
        }
        if (ruleRx().match(line).hasMatch()) {
            out += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\" style=\"margin-top:8px; margin-bottom:14px\"><tr><td bgcolor=\"%1\" height=\"1\"><span style=\"font-size:1px\">&nbsp;</span></td></tr></table>").arg(st.border);
            ++i;
            continue;
        }
        if (quoteRx().match(line).hasMatch()) {
            QStringList inner;
            while (i < n) {
                const QRegularExpressionMatch q = quoteRx().match(lines.at(i));
                if (!q.hasMatch())
                    break;
                inner << q.captured(1);
                ++i;
            }
            out += QStringLiteral("<table cellspacing=\"0\" cellpadding=\"0\" style=\"margin-top:2px; margin-bottom:10px\"><tr><td width=\"3\" bgcolor=\"%1\"></td><td width=\"12\"></td>"
                                  "<td><span style=\"color:%2\">%3</span></td></tr></table>").arg(st.border, st.muted, convert(inner, st));
            continue;
        }
        if (line.contains(QLatin1Char('|')) && tableSepRx().match(next).hasMatch() && next.contains(QLatin1Char('-'))) {
            const QVector<QString> head = splitRow(line);
            QVector<QString> aligns;
            for (const QString& c : splitRow(next))
                aligns.append(c.startsWith(QLatin1Char(':')) && c.endsWith(QLatin1Char(':')) ? QStringLiteral("center") : c.endsWith(QLatin1Char(':')) ? QStringLiteral("right") : QStringLiteral("left"));
            // thin lines the plain way: the table has the colour of the lines and a gap of 1 px between the cells, every cell has the colour of the page
            QString t = QStringLiteral("<table border=\"1\" bordercolor=\"%1\" cellspacing=\"0\" cellpadding=\"8\" style=\"border-collapse:collapse; border-color:%1\"><tr>").arg(st.border);
            for (int c = 0; c < head.size(); ++c)
                t += QStringLiteral("<th align=\"%1\">%2</th>").arg(c < aligns.size() ? aligns.at(c) : QStringLiteral("left"), inlineHtml(head.at(c), st));
            t += QStringLiteral("</tr>");
            i += 2;
            while (i < n && lines.at(i).contains(QLatin1Char('|')) && !lines.at(i).trimmed().isEmpty()) {
                const QVector<QString> cells = splitRow(lines.at(i));
                t += QStringLiteral("<tr>");
                for (int c = 0; c < head.size(); ++c)
                    t += QStringLiteral("<td align=\"%1\">%2</td>").arg(c < aligns.size() ? aligns.at(c) : QStringLiteral("left"), c < cells.size() ? inlineHtml(cells.at(c), st) : QString());
                t += QStringLiteral("</tr>");
                ++i;
            }
            out += t + QStringLiteral("</table><p style=\"margin:0px; font-size:8px\">&nbsp;</p>");
            continue;
        }
        if (itemRx().match(line).hasMatch()) {
            out += listHtml(lines, i, st);
            continue;
        }
        // a paragraph: lines up to a blank line or the start of another block
        QStringList para;
        while (i < n && !lines.at(i).trimmed().isEmpty()) {
            if (!para.isEmpty() && startsBlock(lines.at(i), i + 1 < n ? lines.at(i + 1) : QString()))
                break;
            para << lines.at(i++);
        }
        out += paragraph(para, st);
    }
    return out;
}

} // namespace

QString markdownToHtml(const QString& markdown, const MarkdownStyle& style)
{
    QString text = markdown;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return convert(text.split(QLatin1Char('\n')), style);
}

} // namespace dm::ai
