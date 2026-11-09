#include "PageRange.h"

#include <QSet>
#include <QStringList>

namespace dp {

QList<int> parsePageRanges(const QString& text, int pageCount)
{
    QList<int> pages;
    QSet<int> seen;
    if (pageCount <= 0)
        return pages;
    const QStringList parts = text.split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return pages;
    for (const QString& raw : parts) {
        const QString part = raw.trimmed();
        if (part.isEmpty())
            return {};
        int from = 0, to = 0;
        const int dash = part.indexOf(QLatin1Char('-'));
        bool ok = true;
        if (dash < 0) {
            from = to = part.toInt(&ok);
        } else {
            const QString a = part.left(dash).trimmed();
            const QString b = part.mid(dash + 1).trimmed();
            if (a.isEmpty() && b.isEmpty())
                return {};
            from = a.isEmpty() ? 1 : a.toInt(&ok);
            if (ok)
                to = b.isEmpty() ? pageCount : b.toInt(&ok);
        }
        if (!ok || from < 1 || to < 1 || from > to)
            return {};
        if (from > pageCount)
            return {};                   // a page that does not exist is an error, not "the last page"
        to = qMin(to, pageCount);
        for (int p = from; p <= to; ++p)
            if (!seen.contains(p - 1)) {
                seen.insert(p - 1);
                pages.append(p - 1);
            }
    }
    return pages;
}

} // namespace dp
