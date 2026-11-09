#include "SplitPlan.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QStringList>

#include <algorithm>

namespace dp::tools {

namespace {
// parses one "a-b" / "a" / "a-" / "-b" part; false when invalid
bool parsePart(const QString& raw, int pageCount, int& from, int& to)
{
    const QString part = raw.trimmed();
    if (part.isEmpty())
        return false;
    bool ok = true;
    const int dash = part.indexOf(QLatin1Char('-'));
    if (dash < 0) {
        from = to = part.toInt(&ok);
    } else {
        const QString a = part.left(dash).trimmed();
        const QString b = part.mid(dash + 1).trimmed();
        if (a.isEmpty() && b.isEmpty())
            return false;
        from = a.isEmpty() ? 1 : a.toInt(&ok);
        if (ok)
            to = b.isEmpty() ? pageCount : b.toInt(&ok);
    }
    if (!ok || from < 1 || to < 1 || from > to || from > pageCount)
        return false;
    to = std::min(to, pageCount);
    return true;
}
} // namespace

QString rangeLabel(const QList<int>& pages)
{
    QStringList parts;
    for (int i = 0; i < pages.size();) {
        int j = i;
        while (j + 1 < pages.size() && pages.at(j + 1) == pages.at(j) + 1)
            ++j;
        parts << (j > i ? QStringLiteral("%1-%2").arg(pages.at(i) + 1).arg(pages.at(j) + 1) : QString::number(pages.at(i) + 1));
        i = j + 1;
    }
    return parts.join(QLatin1Char(','));
}

QList<SplitPart> splitByRanges(const QString& text, int pageCount, bool* ok)
{
    QList<SplitPart> parts;
    if (ok)
        *ok = false;
    if (pageCount <= 0)
        return parts;
    const QStringList texts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (texts.isEmpty())
        return parts;
    for (const QString& t : texts) {
        int from = 0, to = 0;
        if (!parsePart(t, pageCount, from, to))
            return {};
        SplitPart p;
        for (int page = from; page <= to; ++page)
            p.pages.append(page - 1);
        p.suffix = QStringLiteral("pages-") + rangeLabel(p.pages);
        parts.append(p);
    }
    if (ok)
        *ok = true;
    return parts;
}

QList<SplitPart> splitEvery(int n, int pageCount)
{
    QList<SplitPart> parts;
    if (n < 1 || pageCount < 1)
        return parts;
    for (int start = 0; start < pageCount; start += n) {
        SplitPart p;
        for (int page = start; page < std::min(pageCount, start + n); ++page)
            p.pages.append(page);
        p.suffix = QStringLiteral("pages-") + rangeLabel(p.pages);
        parts.append(p);
    }
    return parts;
}

QList<SplitPart> splitByBookmarks(const QList<QPair<QString, int>>& topLevel, int pageCount)
{
    QList<SplitPart> parts;
    if (pageCount < 1)
        return parts;
    // the first title of every page, in page order
    QMap<int, QString> starts;
    for (const auto& item : topLevel)
        if (item.second >= 0 && item.second < pageCount && !starts.contains(item.second))
            starts.insert(item.second, item.first);
    if (starts.isEmpty())
        return parts;
    QList<int> pagesStart = starts.keys();
    auto fill = [&](int from, int to, const QString& suffix) {
        SplitPart p;
        for (int page = from; page < to; ++page)
            p.pages.append(page);
        p.suffix = suffix;
        parts.append(p);
    };
    if (pagesStart.first() > 0)
        fill(0, pagesStart.first(), QStringLiteral("start"));
    for (int i = 0; i < pagesStart.size(); ++i) {
        const int to = i + 1 < pagesStart.size() ? pagesStart.at(i + 1) : pageCount;
        const QString title = safeFileName(starts.value(pagesStart.at(i)));
        fill(pagesStart.at(i), to, QStringLiteral("%1-%2").arg(parts.size() + 1, 2, 10, QLatin1Char('0')).arg(title.isEmpty() ? QStringLiteral("part") : title));
    }
    return parts;
}

QString safeFileName(const QString& text)
{
    QString out;
    for (const QChar c : text) {
        if (c.unicode() < 32 || QStringLiteral("\\/:*?\"<>|").contains(c))
            out += QLatin1Char('_');
        else
            out += c;
    }
    out = out.simplified();
    while (out.endsWith(QLatin1Char('.')) || out.endsWith(QLatin1Char(' ')))
        out.chop(1);
    if (out.size() > 60)
        out = out.left(60).trimmed();
    return out;
}

QString uniqueOutputPath(const QString& dir, const QString& stem, const QString& suffix)
{
    const QString base = QDir(dir).filePath(safeFileName(stem) + (suffix.isEmpty() ? QString() : QLatin1Char('-') + suffix));
    QString path = base + QStringLiteral(".pdf");
    for (int n = 2; QFileInfo::exists(path); ++n)
        path = QStringLiteral("%1 (%2).pdf").arg(base).arg(n);
    return path;
}

} // namespace dp::tools
