#include "RuleSet.h"

#include <QSet>
#include <functional>

namespace dv::br::ab {

namespace {

bool isTokenChar(QChar c) { return c.isLetterOrNumber(); }

// the "separator" of the filter syntax: anything but a letter, a digit, "_", "-", "." or "%"
bool isSeparator(QChar c)
{
    return !(c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('-') || c == QLatin1Char('.') || c == QLatin1Char('%'));
}

bool hostMatchesDomain(const QString& host, const QString& domain)
{
    return host == domain || (host.size() > domain.size() && host.endsWith(domain) && host.at(host.size() - domain.size() - 1) == QLatin1Char('.'));
}

} // namespace

QString RuleSet::hostOf(const QString& lowerUrl, int* hostStart)
{
    const int scheme = lowerUrl.indexOf(QLatin1String("://"));
    int start = scheme < 0 ? 0 : scheme + 3;
    int end = start;
    while (end < lowerUrl.size() && lowerUrl.at(end) != QLatin1Char('/') && lowerUrl.at(end) != QLatin1Char('?') && lowerUrl.at(end) != QLatin1Char('#'))
        ++end;
    const int at = lowerUrl.lastIndexOf(QLatin1Char('@'), end - 1);
    if (at >= start)
        start = at + 1;
    int hostEnd = end;
    if (start < end && lowerUrl.at(start) == QLatin1Char('[')) {              // an IPv6 address: up to the closing bracket
        const int close = lowerUrl.indexOf(QLatin1Char(']'), start);
        if (close >= 0 && close < end)
            hostEnd = close + 1;
    } else {
        const int colon = lowerUrl.lastIndexOf(QLatin1Char(':'), end - 1);
        if (colon >= start)
            hostEnd = colon;
    }
    if (hostStart)
        *hostStart = start;
    return lowerUrl.mid(start, hostEnd - start);
}

QString RuleSet::registrableDomain(const QString& host)
{
    // An approximation without the public suffix list: the last two labels, three after a short second-level name under a two-letter country code (co.th, ac.th, com.au ...).
    const QStringList labels = host.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    if (labels.size() <= 2)
        return host;
    static const QSet<QString> second = {QStringLiteral("co"), QStringLiteral("com"), QStringLiteral("net"), QStringLiteral("org"), QStringLiteral("gov"), QStringLiteral("edu"),
                                         QStringLiteral("ac"), QStringLiteral("or"), QStringLiteral("go"), QStringLiteral("in"), QStringLiteral("mi"), QStringLiteral("ne")};
    const QString& tld = labels.at(labels.size() - 1);
    const QString& sld = labels.at(labels.size() - 2);
    const int keep = (tld.size() == 2 && second.contains(sld)) ? 3 : 2;
    return labels.mid(labels.size() - keep).join(QLatin1Char('.'));
}

QString RuleSet::bestToken(const NetRule& r)
{
    const QString& p = r.pattern;
    QString best;
    int i = 0;
    while (i < p.size()) {
        if (!isTokenChar(p.at(i))) {
            ++i;
            continue;
        }
        int j = i;
        while (j < p.size() && isTokenChar(p.at(j)))
            ++j;
        const bool leftOk = i > 0 ? p.at(i - 1) != QLatin1Char('*') : (r.startAnchor || r.hostAnchor);
        const bool rightOk = j < p.size() ? p.at(j) != QLatin1Char('*') : r.endAnchor;
        if (leftOk && rightOk && j - i > best.size() && j - i >= 2)
            best = p.mid(i, j - i);
        i = j;
    }
    return best.toLower();
}

void RuleSet::add(const NetRule& rule)
{
    m_rules.append(rule);
    if (rule.list >= m_perList.size())
        m_perList.resize(rule.list + 1);
    ++m_perList[rule.list];
}

void RuleSet::finalize()
{
    for (int i = 0; i < m_rules.size(); ++i) {
        const NetRule& r = m_rules.at(i);
        if (r.exception && (r.types & Document) && r.typesGiven && r.types == Document) {          // "@@||site^$document": the whole site is let through
            m_siteExempt.append(i);
            continue;
        }
        Index& idx = r.exception ? m_allow : m_block;
        if (!r.hostKey.isEmpty()) {
            idx.hosts[r.hostKey].append(i);
            continue;
        }
        const QString token = bestToken(r);
        if (!token.isEmpty())
            idx.tokens[token].append(i);
        else
            idx.fallback.append(i);
    }
    m_rules.squeeze();
}

bool RuleSet::matchHere(const QString& url, int pos, const QString& part, int* end)
{
    int u = pos;
    for (int k = 0; k < part.size(); ++k) {
        const QChar pc = part.at(k);
        if (pc == QLatin1Char('^')) {
            if (u == url.size()) {
                if (k != part.size() - 1)
                    return false;                                  // the end of the address only closes a pattern
                continue;
            }
            if (!isSeparator(url.at(u)))
                return false;
            ++u;
        } else {
            if (u >= url.size() || url.at(u) != pc)
                return false;
            ++u;
        }
    }
    *end = u;
    return true;
}

bool RuleSet::patternMatches(const NetRule& r, const QString& url, const Request& q)
{
    const QStringList& parts = r.parts;
    auto matchFrom = [&](int startPos, bool anchored) -> bool {
        int pos = startPos;
        for (int i = 0; i < parts.size(); ++i) {
            const QString& part = parts.at(i);
            const bool last = i == parts.size() - 1;
            if (part.isEmpty()) {
                if (last && r.endAnchor && i > 0)
                    return true;                                    // "...*|"
                continue;
            }
            const bool mustStartHere = i == 0 && anchored;
            int end = 0;
            if (mustStartHere) {
                if (!matchHere(url, pos, part, &end))
                    return false;
                if (last && r.endAnchor && end != url.size())
                    return false;
                pos = end;
                continue;
            }
            bool found = false;
            for (int s = pos; s <= url.size(); ++s) {
                if (matchHere(url, s, part, &end) && (!(last && r.endAnchor) || end == url.size())) {
                    found = true;
                    pos = end;
                    break;
                }
            }
            if (!found)
                return false;
        }
        return true;
    };

    if (r.hostAnchor) {
        // the pattern starts at the beginning of a label of the host name
        const int hs = q.hostStart;
        const int he = hs + q.host.size();
        for (int i = hs; i < he; ++i) {
            if (i != hs && url.at(i - 1) != QLatin1Char('.'))
                continue;
            if (matchFrom(i, true))
                return true;
        }
        return false;
    }
    if (r.startAnchor)
        return matchFrom(0, true);
    return matchFrom(0, false);
}

bool RuleSet::ruleMatches(const NetRule& r, const Request& q) const
{
    if (!(r.types & q.type))
        return false;
    if (r.thirdParty == 1 && !q.thirdParty)
        return false;
    if (r.thirdParty == 0 && q.thirdParty)
        return false;
    if (!r.domainsInc.isEmpty()) {
        bool any = false;
        for (const QString& d : r.domainsInc)
            any = any || (!q.sourceHost.isEmpty() && hostMatchesDomain(q.sourceHost, d));
        if (!any)
            return false;
    }
    for (const QString& d : r.domainsExc)
        if (!q.sourceHost.isEmpty() && hostMatchesDomain(q.sourceHost, d))
            return false;
    return patternMatches(r, r.matchCase ? q.url : q.lower, q);
}

void RuleSet::candidates(const Index& idx, const Request& q, const std::function<bool(int)>& visit) const
{
    // host index: every suffix of the host name at a label boundary
    int from = 0;
    while (from <= q.host.size()) {
        const QString suffix = q.host.mid(from);
        const auto it = idx.hosts.constFind(suffix);
        if (it != idx.hosts.constEnd())
            for (int id : it.value())
                if (visit(id))
                    return;
        const int dot = q.host.indexOf(QLatin1Char('.'), from);
        if (dot < 0)
            break;
        from = dot + 1;
    }
    // token index: every word of the address, once
    QSet<QString> seen;
    const QString& u = q.lower;
    int i = 0;
    while (i < u.size()) {
        if (!isTokenChar(u.at(i))) {
            ++i;
            continue;
        }
        int j = i;
        while (j < u.size() && isTokenChar(u.at(j)))
            ++j;
        const QString token = u.mid(i, j - i);
        i = j;
        if (token.size() < 2 || seen.contains(token))
            continue;
        seen.insert(token);
        const auto it = idx.tokens.constFind(token);
        if (it != idx.tokens.constEnd())
            for (int id : it.value())
                if (visit(id))
                    return;
    }
    for (int id : idx.fallback)
        if (visit(id))
            return;
}

RuleSet::Decision RuleSet::match(const QString& url, const QString& sourceUrl, quint16 type, quint32 enabledMask) const
{
    Request q;
    q.url = url;
    q.lower = url.toLower();
    q.host = hostOf(q.lower, &q.hostStart);
    q.sourceHost = sourceUrl.isEmpty() ? QString() : hostOf(sourceUrl.toLower());
    q.type = type;
    q.thirdParty = !q.sourceHost.isEmpty() && registrableDomain(q.host) != registrableDomain(q.sourceHost);

    Decision d;
    const NetRule* found = nullptr;
    candidates(m_block, q, [&](int id) {
        const NetRule& r = m_rules.at(id);
        if (!(enabledMask & (1u << r.list)) || !ruleMatches(r, q))
            return false;
        if (!found || (r.important && !found->important))
            found = &r;
        return found->important;              // an $important filter cannot be beaten: stop; otherwise keep looking for one
    });
    if (!found)
        return d;
    const NetRule* allow = nullptr;
    candidates(m_allow, q, [&](int id) {
        const NetRule& r = m_rules.at(id);
        if (!(enabledMask & (1u << r.list)) || !ruleMatches(r, q))
            return false;
        if (found->important && !r.important)
            return false;
        allow = &r;
        return true;
    });
    if (allow) {
        d.exception = allow;
        return d;
    }
    d.blocked = true;
    d.rule = found;
    return d;
}

bool RuleSet::siteExempt(const QString& sourceUrl, quint32 enabledMask) const
{
    if (m_siteExempt.isEmpty() || sourceUrl.isEmpty())
        return false;
    Request q;
    q.url = sourceUrl;
    q.lower = sourceUrl.toLower();
    q.host = hostOf(q.lower, &q.hostStart);
    q.type = Document;
    for (int id : m_siteExempt) {
        const NetRule& r = m_rules.at(id);
        if ((enabledMask & (1u << r.list)) && ruleMatches(r, q))
            return true;
    }
    return false;
}

} // namespace dv::br::ab
