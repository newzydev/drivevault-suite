#include "Cosmetic.h"

#include <QRegularExpression>

namespace dv::br::ab {

namespace {

const QHash<QString, QString>& aliasTable()
{
    // alias -> canonical name (only scriptlets this program has)
    static const QHash<QString, QString> t = {
        {QStringLiteral("set-constant"), QStringLiteral("set-constant")}, {QStringLiteral("set"), QStringLiteral("set-constant")},
        {QStringLiteral("abort-on-property-read"), QStringLiteral("abort-on-property-read")}, {QStringLiteral("aopr"), QStringLiteral("abort-on-property-read")},
        {QStringLiteral("abort-on-property-write"), QStringLiteral("abort-on-property-write")}, {QStringLiteral("aopw"), QStringLiteral("abort-on-property-write")},
        {QStringLiteral("abort-current-script"), QStringLiteral("abort-current-script")}, {QStringLiteral("acs"), QStringLiteral("abort-current-script")},
        {QStringLiteral("abort-current-inline-script"), QStringLiteral("abort-current-script")}, {QStringLiteral("acis"), QStringLiteral("abort-current-script")},
        {QStringLiteral("abort-on-stack-trace"), QStringLiteral("abort-on-stack-trace")}, {QStringLiteral("aost"), QStringLiteral("abort-on-stack-trace")},
        {QStringLiteral("no-xhr-if"), QStringLiteral("no-xhr-if")}, {QStringLiteral("prevent-xhr"), QStringLiteral("no-xhr-if")},
        {QStringLiteral("no-fetch-if"), QStringLiteral("no-fetch-if")}, {QStringLiteral("prevent-fetch"), QStringLiteral("no-fetch-if")},
        {QStringLiteral("no-setTimeout-if"), QStringLiteral("no-setTimeout-if")}, {QStringLiteral("nostif"), QStringLiteral("no-setTimeout-if")}, {QStringLiteral("setTimeout-defuser"), QStringLiteral("no-setTimeout-if")},
        {QStringLiteral("no-setInterval-if"), QStringLiteral("no-setInterval-if")}, {QStringLiteral("nosiif"), QStringLiteral("no-setInterval-if")}, {QStringLiteral("setInterval-defuser"), QStringLiteral("no-setInterval-if")},
        {QStringLiteral("addEventListener-defuser"), QStringLiteral("prevent-addEventListener")}, {QStringLiteral("prevent-addEventListener"), QStringLiteral("prevent-addEventListener")}, {QStringLiteral("aeld"), QStringLiteral("prevent-addEventListener")},
        {QStringLiteral("json-prune"), QStringLiteral("json-prune")},
        {QStringLiteral("remove-attr"), QStringLiteral("remove-attr")}, {QStringLiteral("ra"), QStringLiteral("remove-attr")},
        {QStringLiteral("remove-class"), QStringLiteral("remove-class")}, {QStringLiteral("rc"), QStringLiteral("remove-class")},
        {QStringLiteral("set-local-storage-item"), QStringLiteral("set-local-storage-item")},
        {QStringLiteral("set-session-storage-item"), QStringLiteral("set-session-storage-item")},
        {QStringLiteral("set-cookie"), QStringLiteral("set-cookie")}, {QStringLiteral("set-cookie-reload"), QStringLiteral("set-cookie-reload")},
        {QStringLiteral("cookie-remover"), QStringLiteral("cookie-remover")},
        {QStringLiteral("window.open-defuser"), QStringLiteral("no-window-open-if")}, {QStringLiteral("no-window-open-if"), QStringLiteral("no-window-open-if")}, {QStringLiteral("nowoif"), QStringLiteral("no-window-open-if")},
        {QStringLiteral("nowebrtc"), QStringLiteral("nowebrtc")},
        {QStringLiteral("adjust-setTimeout"), QStringLiteral("adjust-setTimeout")}, {QStringLiteral("nano-setTimeout-booster"), QStringLiteral("adjust-setTimeout")}, {QStringLiteral("nano-stb"), QStringLiteral("adjust-setTimeout")},
        {QStringLiteral("adjust-setInterval"), QStringLiteral("adjust-setInterval")}, {QStringLiteral("nano-setInterval-booster"), QStringLiteral("adjust-setInterval")}, {QStringLiteral("nano-sib"), QStringLiteral("adjust-setInterval")},
    };
    return t;
}

QStringList splitDomains(const QString& text, QStringList* inc, QStringList* exc, bool* bad)
{
    for (const QString& raw : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        QString d = raw.trimmed().toLower();
        const bool neg = d.startsWith(QLatin1Char('~'));
        if (neg)
            d.remove(0, 1);
        if (d.isEmpty() || d.startsWith(QLatin1Char('/')) || (d.contains(QLatin1Char('*')) && !d.endsWith(QLatin1String(".*"))) || d.contains(QLatin1Char(' '))) {
            *bad = true;                                    // regular expression domains and odd wildcards are not supported
            continue;
        }
        (neg ? exc : inc)->append(d);
    }
    return {};
}

// scriptlet arguments: "name, a, b" with "\," for a comma inside an argument
void splitScriptletArgs(const QString& text, QString* name, QStringList* args)
{
    QStringList parts;
    QString cur;
    for (int i = 0; i < text.size(); ++i) {
        if (text.at(i) == QLatin1Char('\\') && i + 1 < text.size() && text.at(i + 1) == QLatin1Char(',')) {
            cur += QLatin1Char(',');
            ++i;
        } else if (text.at(i) == QLatin1Char(',')) {
            parts << cur.trimmed();
            cur.clear();
        } else {
            cur += text.at(i);
        }
    }
    parts << cur.trimmed();
    *name = parts.value(0);
    parts.removeFirst();
    *args = parts;
}

bool isProceduralSelector(const QString& s)
{
    static const QRegularExpression procedural(QStringLiteral(R"(:-abp-|:has-text\(|:xpath\(|:matches-css|:matches-media|:matches-path|:matches-attr|:matches-property|:upward\(|:contains\(|:nth-ancestor\(|:remove\(\)|:style\(|:watch-attr|:min-text-length|:others\(|:if\(|:if-not\(|:not\(:has-text|^\^)"));
    return procedural.match(s).hasMatch();
}

} // namespace

QString canonicalScriptlet(const QString& nameOrAlias) { return aliasTable().value(nameOrAlias); }

QStringList implementedScriptlets()
{
    QSet<QString> names;
    for (auto it = aliasTable().cbegin(); it != aliasTable().cend(); ++it)
        names.insert(it.value());
    QStringList out(names.begin(), names.end());
    out.sort();
    return out;
}

ParseKind parseCosmetic(const QString& rawLine, CosmeticRule& out)
{
    const QString line = rawLine.trimmed();
    struct Sep { const char* text; bool exception; int kind; };       // kind 0 = ##, 1 = #$#, 2 = unsupported
    static const Sep seps[] = {{"#@$#", true, 1}, {"#@%#", true, 2}, {"#@?#", true, 2}, {"#$#", false, 1}, {"#%#", false, 2}, {"#?#", false, 2}, {"#@#", true, 0}, {"##", false, 0}};
    int at = -1, sepIndex = -1;
    for (int i = 0; i < int(sizeof(seps) / sizeof(seps[0])); ++i) {
        const int p = line.indexOf(QLatin1String(seps[i].text));
        if (p >= 0 && (at < 0 || p < at || (p == at && QString::fromLatin1(seps[i].text).size() > QString::fromLatin1(seps[sepIndex].text).size()))) {
            at = p;
            sepIndex = i;
        }
    }
    if (at < 0)
        return ParseKind::Unsupported;
    const Sep& sep = seps[sepIndex];
    if (sep.kind == 2)
        return ParseKind::Unsupported;
    CosmeticRule r;
    r.raw = line;
    r.exception = sep.exception;
    bool bad = false;
    splitDomains(line.left(at), &r.inc, &r.exc, &bad);
    if (bad)
        return ParseKind::Unsupported;
    const QString body = line.mid(at + int(qstrlen(sep.text))).trimmed();
    if (body.isEmpty())
        return ParseKind::Unsupported;

    if (sep.kind == 0 && body.startsWith(QLatin1String("+js(")) && body.endsWith(QLatin1Char(')'))) {
        QString name;
        splitScriptletArgs(body.mid(4, body.size() - 5), &name, &r.args);
        r.kind = CosmeticKind::Scriptlet;
        if (name.isEmpty() && r.exception) {               // "#@#+js()": every scriptlet is off for these domains
            r.name = QStringLiteral("*");
            out = r;
            return ParseKind::Cosmetic;
        }
        r.name = canonicalScriptlet(name);
        if (r.name.isEmpty())
            return ParseKind::Unsupported;                 // a scriptlet this program does not have: never pretend
        out = r;
        return ParseKind::Cosmetic;
    }
    if (sep.kind == 1) {
        if (body.contains(QLatin1Char('{')) && body.endsWith(QLatin1Char('}'))) {     // style injection: "selector { declarations }"
            if (body.contains(QLatin1String("url("), Qt::CaseInsensitive) || body.contains(QLatin1String("@import"), Qt::CaseInsensitive) || body.contains(QLatin1String("expression("), Qt::CaseInsensitive)
                || body.contains(QLatin1String("javascript:"), Qt::CaseInsensitive))
                return ParseKind::Unsupported;             // a style rule may not fetch anything
            const QString selectorPart = body.left(body.indexOf(QLatin1Char('{'))).trimmed();
            if (selectorPart.isEmpty() || isProceduralSelector(selectorPart) || body.contains(QLatin1Char('<')))
                return ParseKind::Unsupported;
            r.kind = CosmeticKind::Style;
            r.selector = body;
            out = r;
            return ParseKind::Cosmetic;
        }
        // ABP snippet: "name arg1 arg2 ; name2 ..." - only the ones that are the same as a scriptlet here; several in one line are not supported
        if (body.contains(QLatin1Char(';')))
            return ParseKind::Unsupported;
        QStringList words = body.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const QString name = canonicalScriptlet(words.value(0));
        if (name.isEmpty() || name == QLatin1String("set-constant"))
            return ParseKind::Unsupported;
        words.removeFirst();
        r.kind = CosmeticKind::Scriptlet;
        r.name = name;
        r.args = words;
        out = r;
        return ParseKind::Cosmetic;
    }
    // ordinary element hiding
    if (isProceduralSelector(body) || body.contains(QLatin1Char('{')) || body.contains(QLatin1Char('}')) || body.contains(QLatin1Char('<')) || body.contains(QLatin1String("/*")))
        return ParseKind::Unsupported;
    r.kind = CosmeticKind::Hide;
    r.selector = body;
    out = r;
    return ParseKind::Cosmetic;
}

bool CosmeticStore::domainMatches(const QString& host, const QString& domain)
{
    if (domain.endsWith(QLatin1String(".*"))) {
        const QString base = domain.left(domain.size() - 2);
        return host.startsWith(base + QLatin1Char('.')) || host.contains(QLatin1Char('.') + base + QLatin1Char('.'));
    }
    return host == domain || (host.size() > domain.size() && host.endsWith(domain) && host.at(host.size() - domain.size() - 1) == QLatin1Char('.'));
}

namespace {
bool isPlainName(const QString& selector)
{
    static const QRegularExpression plain(QStringLiteral(R"(^[.#][A-Za-z0-9_-]+$)"));
    return plain.match(selector).hasMatch();
}
}

void CosmeticStore::add(const CosmeticRule& rule)
{
    m_rules.append(rule);
}

void CosmeticStore::finalize()
{
    for (int i = 0; i < m_rules.size(); ++i) {
        const CosmeticRule& r = m_rules.at(i);
        if (r.exception) {
            (r.inc.isEmpty() ? m_genericExceptions : m_exceptions).append(i);
            continue;
        }
        if (r.inc.isEmpty()) {
            if (r.kind == CosmeticKind::Scriptlet)
                m_genericScriptlets.append(i);
            else if (r.kind == CosmeticKind::Hide && r.exc.isEmpty() && isPlainName(r.selector))
                m_genericNames.append(i);
            else
                m_genericComplex.append(i);
            continue;
        }
        for (const QString& d : r.inc) {
            if (d.endsWith(QLatin1String(".*")))
                m_byEntity[d.left(d.size() - 2)].append(i);
            else
                m_byDomain[d].append(i);
        }
    }
    m_rules.squeeze();
}

int CosmeticStore::scriptletCount() const
{
    int n = 0;
    for (const CosmeticRule& r : m_rules)
        if (r.kind == CosmeticKind::Scriptlet && !r.exception)
            ++n;
    return n;
}

PageCosmetics CosmeticStore::forPage(const QString& host, quint32 mask, bool noGeneric) const
{
    PageCosmetics out;
    // exceptions that apply to this page
    QSet<QString> offSelectors, offScriptlets;
    auto takeException = [&](const CosmeticRule& r) {
        if (r.kind == CosmeticKind::Scriptlet)
            offScriptlets.insert(r.name);
        else
            offSelectors.insert(r.selector);
    };
    for (int id : m_genericExceptions) {
        const CosmeticRule& r = m_rules.at(id);
        if (mask & (1u << r.list))
            takeException(r);
    }
    for (int id : m_exceptions) {
        const CosmeticRule& r = m_rules.at(id);
        if (!(mask & (1u << r.list)))
            continue;
        bool any = false;
        for (const QString& d : r.inc)
            any = any || domainMatches(host, d);
        bool excluded = false;
        for (const QString& d : r.exc)
            excluded = excluded || domainMatches(host, d);
        if (any && !excluded)
            takeException(r);
    }

    QSet<int> seen;
    auto consider = [&](int id) {
        if (seen.contains(id))
            return;
        seen.insert(id);
        const CosmeticRule& r = m_rules.at(id);
        if (!(mask & (1u << r.list)))
            return;
        for (const QString& d : r.exc)
            if (domainMatches(host, d))
                return;
        switch (r.kind) {
        case CosmeticKind::Hide:
            if (!offSelectors.contains(r.selector))
                out.selectors.append(r.selector);
            break;
        case CosmeticKind::Style:
            out.styles.append(r.selector);
            break;
        case CosmeticKind::Scriptlet:
            if (!offScriptlets.contains(QStringLiteral("*")) && !offScriptlets.contains(r.name))
                out.scriptlets.append({r.name, r.args});
            break;
        }
    };
    // specific rules: every suffix of the host name; entities: every label
    const QStringList labels = host.split(QLatin1Char('.'), Qt::SkipEmptyParts);
    for (int i = 0; i < labels.size(); ++i) {
        const QString suffix = labels.mid(i).join(QLatin1Char('.'));
        const auto it = m_byDomain.constFind(suffix);
        if (it != m_byDomain.constEnd())
            for (int id : it.value())
                consider(id);
        const auto en = m_byEntity.constFind(labels.at(i));
        if (en != m_byEntity.constEnd())
            for (int id : en.value()) {
                bool matches = false;
                for (const QString& d : m_rules.at(id).inc)
                    matches = matches || domainMatches(host, d);
                if (matches)
                    consider(id);
            }
    }
    if (!noGeneric)
    for (int id : m_genericComplex)
        consider(id);
    for (int id : m_genericScriptlets)
        consider(id);
    for (const QString& s : offSelectors)
        if (isPlainName(s))
            out.genericOff.append(s);
    return out;
}

QString CosmeticStore::genericClasses(quint32 mask) const
{
    QStringList names;
    for (int id : m_genericNames) {
        const CosmeticRule& r = m_rules.at(id);
        if ((mask & (1u << r.list)) && r.selector.startsWith(QLatin1Char('.')))
            names << r.selector.mid(1);
    }
    return names.join(QLatin1Char(' '));
}

QString CosmeticStore::genericIds(quint32 mask) const
{
    QStringList names;
    for (int id : m_genericNames) {
        const CosmeticRule& r = m_rules.at(id);
        if ((mask & (1u << r.list)) && r.selector.startsWith(QLatin1Char('#')))
            names << r.selector.mid(1);
    }
    return names.join(QLatin1Char(' '));
}

} // namespace dv::br::ab
