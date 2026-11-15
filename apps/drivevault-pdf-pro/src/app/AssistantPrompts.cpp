#include "AssistantPrompts.h"

#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace dp::assistant {

bool isThai(const QString& text)
{
    int letters = 0, thai = 0;
    for (const QChar c : text) {
        if (!c.isLetter())
            continue;
        ++letters;
        thai += (c.unicode() >= 0x0E00 && c.unicode() <= 0x0E7F) ? 1 : 0;
    }
    return letters > 0 && thai * 4 > letters;
}

QString answerSystemPrompt(bool thai)
{
    if (thai)
        return QStringLiteral("คุณเป็นผู้ช่วยตอบคำถามเกี่ยวกับเอกสาร PDF ตอบเป็นภาษาไทยให้ชัดเจนและกระชับ ใช้เฉพาะข้อมูลในส่วนของเอกสารที่ให้มาเท่านั้น "
                              "ห้ามเดาหรือเพิ่มข้อมูลจากภายนอก ถ้าในส่วนของเอกสารที่ให้ไม่มีคำตอบ ให้บอกตรง ๆ ว่าไม่พบในเอกสาร "
                              "ทุกข้อเท็จจริงที่ตอบให้ระบุหมายเลขหน้าที่พบในรูปแบบ (หน้า N) ตัวเลขและชื่อให้คัดลอกตามเอกสารอย่างตรงตัว");
    return QStringLiteral("You answer questions about a PDF document. Answer clearly and briefly in the language of the question. Use only the document excerpts you are "
                          "given; never guess or add outside knowledge. If the excerpts do not contain the answer, say plainly that it is not in the document. "
                          "Give the page number of every fact in the form (page N). Copy numbers and names exactly as they are in the document.");
}

QString summarySystemPrompt(bool thai, bool partial)
{
    if (thai)
        return partial ? QStringLiteral("สรุปส่วนของเอกสารต่อไปนี้เป็นภาษาไทยอย่างกระชับ เก็บประเด็นหลัก ชื่อ วันที่ และตัวเลขสำคัญตามที่ปรากฏ ห้ามเพิ่มข้อมูลที่ไม่มีในข้อความ")
                       : QStringLiteral("สรุปเอกสารต่อไปนี้เป็นภาษาไทยอย่างกระชับ เริ่มด้วยหนึ่งประโยคว่าเอกสารนี้คืออะไร แล้วตามด้วยประเด็นสำคัญเป็นข้อ ๆ "
                                        "(ชื่อ วันที่ จำนวนเงิน และข้อสรุปตามที่ปรากฏ) ห้ามเพิ่มข้อมูลที่ไม่มีในข้อความ");
    return partial ? QStringLiteral("Summarise this part of a document briefly in English. Keep the main points, names, dates and key figures exactly as they appear. Add nothing that is not in the text.")
                   : QStringLiteral("Summarise the following document briefly. Start with one sentence saying what the document is, then give the key points as a list (names, "
                                    "dates, amounts and conclusions as they appear). Add nothing that is not in the text.");
}

QString joinSummariesSystemPrompt(bool thai)
{
    if (thai)
        return QStringLiteral("ข้างล่างเป็นสรุปของแต่ละส่วนของเอกสารฉบับเดียวกัน รวมเป็นสรุปเดียวเป็นภาษาไทยอย่างกระชับ เริ่มด้วยหนึ่งประโยคว่าเอกสารนี้คืออะไร "
                              "แล้วตามด้วยประเด็นสำคัญเป็นข้อ ๆ ห้ามเพิ่มข้อมูลที่ไม่มีในสรุปเหล่านี้");
    return QStringLiteral("Below are summaries of the parts of one document. Join them into a single brief summary: one sentence saying what the document is, then the key points as "
                          "a list. Add nothing that is not in these summaries.");
}

QString formatExcerpts(const QList<Chunk>& chunks, const QList<int>& chosen, bool thai)
{
    QString out;
    for (const int i : chosen) {
        if (i < 0 || i >= chunks.size())
            continue;
        if (!out.isEmpty())
            out += QStringLiteral("\n\n");
        out += (thai ? QStringLiteral("[หน้า %1]\n") : QStringLiteral("[page %1]\n")).arg(chunks.at(i).page + 1) + chunks.at(i).text;
    }
    return out;
}

QJsonArray chatMessages(const QString& system, const QList<QPair<QString, QString>>& history, const QString& user, int keepTurns)
{
    QJsonArray messages;
    messages.append(QJsonObject{{"role", "system"}, {"content", system}});
    const qsizetype from = std::max<qsizetype>(0, history.size() - keepTurns);
    for (qsizetype i = from; i < history.size(); ++i)
        messages.append(QJsonObject{{"role", history.at(i).first}, {"content", history.at(i).second}});
    messages.append(QJsonObject{{"role", "user"}, {"content", user}});
    return messages;
}

QList<int> citedPages(const QString& answer, int pageCount)
{
    static const QRegularExpression reference(QStringLiteral("(?:หน้า|page|pages|p\\.|pp\\.)\\s*(\\d+(?:\\s*(?:[,–\\-]|และ|and)\\s*\\d+)*)"),
                                              QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression range(QStringLiteral("^(\\d+)\\s*[–\\-]\\s*(\\d+)$"));
    static const QRegularExpression separators(QStringLiteral("\\s*(?:,|และ|and)\\s*"));
    QList<int> pages;
    auto it = reference.globalMatch(answer);
    while (it.hasNext()) {
        const QString list = it.next().captured(1);
        for (const QString& part : list.split(separators, Qt::SkipEmptyParts)) {
            const QRegularExpressionMatch span = range.match(part.trimmed());
            if (span.hasMatch()) {
                const int a = span.captured(1).toInt(), b = span.captured(2).toInt();
                for (int p = a; p <= b && p - a < 50; ++p)
                    pages << p;
            } else {
                pages << part.trimmed().toInt();
            }
        }
    }
    QList<int> valid;
    for (const int p : std::as_const(pages))
        if (p >= 1 && p <= pageCount && !valid.contains(p))
            valid << p;
    std::sort(valid.begin(), valid.end());
    return valid;
}

QString withoutReasoning(const QString& text)
{
    static const QRegularExpression closed(QStringLiteral("<think>.*?</think>\\s*"), QRegularExpression::DotMatchesEverythingOption);
    QString out = text;
    out.remove(closed);
    const qsizetype open = out.indexOf(QStringLiteral("<think>"));
    if (open >= 0)
        out.truncate(open);                // still thinking: nothing to show yet
    return out;
}

} // namespace dp::assistant
