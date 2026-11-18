#include "MarkdownHtml.h"

#include <QRegularExpression>
#include <QStringList>
#include <QVariantMap>
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

QVariantMap block(const QString& type, int quote)
{
    return QVariantMap{{"type", type}, {"quote", quote}};
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

void convert(const QStringList& lines, const MarkdownStyle& st, int quote, QVariantList& out);

// the rows of one list (items and what continues them); `at` is moved past it
void listRows(const QStringList& lines, int& at, const MarkdownStyle& st, int quote, QVariantList& out)
{
    struct Level { int indent; bool ordered; int counter; };
    QVector<Level> stack;
    int last = -1;                                              // the index in `out` of the last row (a continuation line is added to it)
    while (at < lines.size()) {
        const QRegularExpressionMatch m = itemRx().match(lines.at(at));
        if (!m.hasMatch()) {
            const QString& l = lines.at(at);
            if (l.trimmed().isEmpty()) {                        // a blank line only ends the list when no item follows
                int k = at + 1;
                while (k < lines.size() && lines.at(k).trimmed().isEmpty())
                    ++k;
                if (k < lines.size() && itemRx().match(lines.at(k)).hasMatch()) {
                    at = k;
                    continue;
                }
                break;
            }
            if (last >= 0 && !stack.isEmpty() && indentOf(l) >= stack.last().indent + 2 && !startsBlock(l, QString())) {
                QVariantMap row = out.at(last).toMap();
                row["html"] = row.value("html").toString() + QStringLiteral("<br>") + inlineHtml(l.trimmed(), st);
                out[last] = row;
                ++at;
                continue;
            }
            break;
        }
        const int indent = indentOf(m.captured(1));
        const QString marker = m.captured(2);
        const bool ordered = marker.at(0).isDigit();
        while (!stack.isEmpty() && indent < stack.last().indent)
            stack.removeLast();
        if (!stack.isEmpty() && indent == stack.last().indent && ordered != stack.last().ordered)
            stack.removeLast();
        if (stack.isEmpty() || indent > stack.last().indent + 1)
            stack.append({indent, ordered, ordered ? marker.left(marker.size() - 1).toInt() : 0});
        else if (ordered)
            ++stack.last().counter;
        const int depth = int(stack.size()) - 1;
        QVariantMap row = block(QStringLiteral("li"), quote);
        row["depth"] = depth;
        row["marker"] = ordered ? QString::number(stack.last().counter) + QLatin1Char('.') : depth == 0 ? QStringLiteral("•") : depth == 1 ? QStringLiteral("◦") : QStringLiteral("▪");
        row["html"] = inlineHtml(m.captured(3).trimmed(), st);
        out.append(row);
        last = int(out.size()) - 1;
        ++at;
    }
}

void convert(const QStringList& lines, const MarkdownStyle& st, int quote, QVariantList& out)
{
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
            QVariantMap b = block(QStringLiteral("code"), quote);
            b["lang"] = f.captured(2);
            b["text"] = code.join(QLatin1Char('\n'));
            out.append(b);
            continue;
        }
        if (const QRegularExpressionMatch h = headingRx().match(line); h.hasMatch()) {
            QVariantMap b = block(QStringLiteral("h"), quote);
            b["level"] = int(h.captured(1).size());
            b["html"] = inlineHtml(h.captured(2), st);
            out.append(b);
            ++i;
            continue;
        }
        if (ruleRx().match(line).hasMatch()) {
            out.append(block(QStringLiteral("hr"), quote));
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
            convert(inner, st, quote + 1, out);
            continue;
        }
        if (line.contains(QLatin1Char('|')) && tableSepRx().match(next).hasMatch() && next.contains(QLatin1Char('-'))) {
            const QVector<QString> head = splitRow(line);
            QVariantList heads, aligns, rows;
            for (const QString& c : head)
                heads << inlineHtml(c, st);
            for (const QString& c : splitRow(next))
                aligns << (c.startsWith(QLatin1Char(':')) && c.endsWith(QLatin1Char(':')) ? QStringLiteral("center") : c.endsWith(QLatin1Char(':')) ? QStringLiteral("right") : QStringLiteral("left"));
            while (aligns.size() < heads.size())
                aligns << QStringLiteral("left");
            i += 2;
            while (i < n && lines.at(i).contains(QLatin1Char('|')) && !lines.at(i).trimmed().isEmpty()) {
                const QVector<QString> cells = splitRow(lines.at(i));
                QVariantList row;
                for (int c = 0; c < head.size(); ++c)
                    row << (c < cells.size() ? inlineHtml(cells.at(c), st) : QString());
                rows << QVariant(row);
                ++i;
            }
            QVariantMap b = block(QStringLiteral("table"), quote);
            b["head"] = heads;
            b["aligns"] = aligns;
            b["rows"] = rows;
            out.append(b);
            continue;
        }
        if (itemRx().match(line).hasMatch()) {
            listRows(lines, i, st, quote, out);
            continue;
        }
        // a paragraph: lines up to a blank line or the start of another block
        QStringList parts;
        while (i < n && !lines.at(i).trimmed().isEmpty()) {
            if (!parts.isEmpty() && startsBlock(lines.at(i), i + 1 < n ? lines.at(i + 1) : QString()))
                break;
            parts << inlineHtml(lines.at(i++).trimmed(), st);
        }
        QVariantMap b = block(QStringLiteral("p"), quote);
        b["html"] = parts.join(QStringLiteral("<br>"));
        out.append(b);
    }
}

} // namespace

QVariantList markdownBlocks(const QString& markdown, const MarkdownStyle& style)
{
    QString text = markdown;
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    QVariantList out;
    convert(text.split(QLatin1Char('\n')), style, 0, out);
    return out;
}

} // namespace dm::ai
