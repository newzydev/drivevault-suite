#include "DocumentSearch.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace dp::assistant {

namespace {

bool isThaiLetter(QChar c) { return c.unicode() >= 0x0E01 && c.unicode() <= 0x0E5B; }
bool isLatinOrDigit(QChar c) { return (c.unicode() < 128 && c.isLetterOrNumber()) || (c.unicode() >= 0x0E50 && c.unicode() <= 0x0E59); }

// Question words that every question has and no document is about.
const QSet<QString>& thaiStopWords()
{
    static const QSet<QString> words = {
        QStringLiteral("อะไร"), QStringLiteral("ใคร"), QStringLiteral("ที่ไหน"), QStringLiteral("เมื่อไหร่"), QStringLiteral("เมื่อไร"), QStringLiteral("เท่าไหร่"),
        QStringLiteral("เท่าไร"), QStringLiteral("อย่างไร"), QStringLiteral("ทำไม"), QStringLiteral("หรือไม่"), QStringLiteral("หรือเปล่า"), QStringLiteral("ไหม"),
        QStringLiteral("เกี่ยวกับ"), QStringLiteral("เอกสารนี้"), QStringLiteral("เอกสารฉบับนี้"), QStringLiteral("ในเอกสาร"), QStringLiteral("บอกหน่อย"),
        QStringLiteral("ช่วยบอก"), QStringLiteral("ช่วย"), QStringLiteral("หน่อย"), QStringLiteral("ครับ"), QStringLiteral("ค่ะ"), QStringLiteral("คะ"),
        QStringLiteral("คือ"), QStringLiteral("มีอะไร"), QStringLiteral("บ้าง"),
    };
    return words;
}

const QSet<QString>& englishStopWords()
{
    static const QSet<QString> words = {QStringLiteral("the"), QStringLiteral("a"), QStringLiteral("an"), QStringLiteral("of"), QStringLiteral("is"),
                                        QStringLiteral("are"), QStringLiteral("was"), QStringLiteral("what"), QStringLiteral("who"), QStringLiteral("when"),
                                        QStringLiteral("where"), QStringLiteral("which"), QStringLiteral("how"), QStringLiteral("why"), QStringLiteral("does"),
                                        QStringLiteral("do"), QStringLiteral("in"), QStringLiteral("on"), QStringLiteral("to"), QStringLiteral("and"),
                                        QStringLiteral("or"), QStringLiteral("this"), QStringLiteral("that"), QStringLiteral("document"), QStringLiteral("about"),
                                        QStringLiteral("me"), QStringLiteral("tell"), QStringLiteral("please")};
    return words;
}

} // namespace

QList<Chunk> chunkPages(const QStringList& pageTexts, int maxChars)
{
    QList<Chunk> chunks;
    maxChars = std::max(120, maxChars);
    for (int page = 0; page < pageTexts.size(); ++page) {
        QString current;
        const auto flush = [&] {
            const QString text = current.trimmed();
            if (!text.isEmpty())
                chunks.append({page, text});
            current.clear();
        };
        const QStringList lines = pageTexts.at(page).split(QLatin1Char('\n'));
        for (QString line : lines) {
            line = line.trimmed();
            if (line.isEmpty())
                continue;
            while (line.size() > maxChars) {                       // one very long line: cut it
                if (!current.isEmpty())
                    flush();
                current = line.left(maxChars);
                flush();
                line = line.mid(maxChars);
            }
            if (!current.isEmpty() && current.size() + 1 + line.size() > maxChars)
                flush();
            current += (current.isEmpty() ? QString() : QStringLiteral("\n")) + line;
        }
        flush();
    }
    return chunks;
}

QStringList searchTerms(const QString& input)
{
    const QString text = input.toLower();
    QStringList terms;
    for (qsizetype i = 0; i < text.size();) {
        const QChar c = text.at(i);
        if (isThaiLetter(c)) {
            qsizetype end = i;
            while (end < text.size() && isThaiLetter(text.at(end)))
                ++end;
            const QString run = text.mid(i, end - i);
            for (qsizetype k = 0; k + 2 <= run.size(); ++k) {
                terms << run.mid(k, 2);
                if (k + 3 <= run.size())
                    terms << run.mid(k, 3);
            }
            i = end;
        } else if (isLatinOrDigit(c)) {
            qsizetype end = i;
            while (end < text.size() && isLatinOrDigit(text.at(end)))
                ++end;
            const QString word = text.mid(i, end - i);
            if (word.size() >= 2 || word.at(0).isDigit())
                terms << word;
            i = end;
        } else {
            ++i;
        }
    }
    return terms;
}

QList<int> bestChunks(const QList<Chunk>& chunks, const QString& question, int maxChars, int maxCount)
{
    if (chunks.isEmpty())
        return {};
    // the question without its question words
    QString query = question;
    for (const QString& word : thaiStopWords())
        query.replace(word, QStringLiteral(" "));
    QStringList queryTerms;
    for (const QString& term : searchTerms(query))
        if (!englishStopWords().contains(term))
            queryTerms << term;
    const QSet<QString> unique(queryTerms.begin(), queryTerms.end());

    // BM25
    const int count = int(chunks.size());
    QVector<QHash<QString, int>> frequency(count);
    QVector<int> length(count, 0);
    QHash<QString, int> documents;
    double totalLength = 0;
    for (int i = 0; i < count; ++i) {
        const QStringList terms = searchTerms(chunks.at(i).text);
        length[i] = int(terms.size());
        totalLength += terms.size();
        for (const QString& term : terms)
            ++frequency[i][term];
        for (auto it = frequency[i].cbegin(); it != frequency[i].cend(); ++it)
            if (unique.contains(it.key()))
                ++documents[it.key()];
    }
    const double averageLength = std::max(1.0, totalLength / count);
    constexpr double k1 = 1.2, b = 0.75;
    QVector<double> score(count, 0.0);
    for (const QString& term : unique) {
        const int df = documents.value(term, 0);
        if (df == 0)
            continue;
        const double idf = std::log(1.0 + (count - df + 0.5) / (df + 0.5));
        for (int i = 0; i < count; ++i) {
            const int tf = frequency[i].value(term, 0);
            if (tf == 0)
                continue;
            score[i] += idf * (tf * (k1 + 1)) / (tf + k1 * (1 - b + b * length[i] / averageLength));
        }
    }

    QList<int> order;
    for (int i = 0; i < count; ++i)
        order << i;
    const bool anyHit = std::any_of(score.cbegin(), score.cend(), [](double s) { return s > 0; });
    if (anyHit)
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return score[a] > score[b]; });      // else: document order = the start first
    // a piece must be at least a third as good as the best one: weak overlaps would only dilute what the model reads
    const double best = anyHit ? score[order.first()] : 0.0;
    QList<int> chosen;
    int chars = 0;
    for (const int i : std::as_const(order)) {
        if (anyHit && score[i] < 0.33 * best)
            break;
        if (chosen.size() >= maxCount || (!chosen.isEmpty() && chars + chunks.at(i).text.size() > maxChars))
            continue;
        chosen << i;
        chars += int(chunks.at(i).text.size());
    }
    std::sort(chosen.begin(), chosen.end());      // document order
    return chosen;
}

} // namespace dp::assistant
