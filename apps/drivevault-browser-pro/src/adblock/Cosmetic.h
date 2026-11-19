#pragma once

#include "NetRule.h"

#include <QHash>
#include <QSet>
#include <QStringList>
#include <QVector>

// Filters that act inside the page: elements to hide (## and #$#), scriptlets (##+js(...)) and exceptions (#@#). No WebEngine here.
namespace dv::br::ab {

enum class CosmeticKind { Hide, Style, Scriptlet };

struct CosmeticRule {
    CosmeticKind kind = CosmeticKind::Hide;
    bool exception = false;
    QString selector;                 // Hide: the CSS selector; Style: the whole "selector { declarations }" text
    QString name;                     // Scriptlet: its canonical name
    QStringList args;                 // Scriptlet: the arguments
    QStringList inc, exc;             // domains the rule is for / not for ("foo.*" = every country of foo)
    quint8 list = 0;
    QString raw;
};

struct ScriptletCall {
    QString name;
    QStringList args;
};

// What one page gets
struct PageCosmetics {
    QStringList selectors;            // hide: specific and the complex generic ones
    QStringList styles;               // injected style rules
    QVector<ScriptletCall> scriptlets;
    QStringList genericOff;           // generic ".name" / "#name" rules that an exception switched off on this page
    bool noGeneric = false;           // $generichide: no generic hiding on this page
    bool noCosmetic = false;          // $elemhide: nothing is hidden on this page
    bool empty() const { return selectors.isEmpty() && styles.isEmpty() && scriptlets.isEmpty(); }
};

// canonical scriptlet names (the ones this program implements) and the short names the lists use for them
QString canonicalScriptlet(const QString& nameOrAlias);          // "" when this program has no such scriptlet
QStringList implementedScriptlets();

// One cosmetic line. Returns Cosmetic or Unsupported (procedural selectors, HTML filters, unknown scriptlets, remote CSS ...).
ParseKind parseCosmetic(const QString& line, CosmeticRule& out);

class CosmeticStore {
public:
    void add(const CosmeticRule& rule);
    void finalize();

    PageCosmetics forPage(const QString& host, quint32 enabledMask, bool noGeneric = false) const;      // noGeneric: the page has $generichide
    // the names of the generic rules of the lists that are on: classes ("foo" for ".foo") and ids; one JSON-safe string each, names separated by a space
    QString genericClasses(quint32 enabledMask) const;
    QString genericIds(quint32 enabledMask) const;
    int ruleCount() const { return int(m_rules.size()); }
    int scriptletCount() const;

    static bool domainMatches(const QString& host, const QString& domain);       // "a.b.com" matches "b.com" and "*.a.b.*" style entities

private:
    QVector<CosmeticRule> m_rules;
    QHash<QString, QVector<int>> m_byDomain;          // specific rules by their domain
    QHash<QString, QVector<int>> m_byEntity;          // "foo.*" rules by "foo"
    QVector<int> m_genericComplex;                    // no domain (or only excluded ones), not a plain .class / #id
    QVector<int> m_genericNames;                      // plain ".class" / "#id", no domain
    QVector<int> m_genericExceptions;                 // "#@#" with no domain
    QVector<int> m_exceptions;                        // "#@#" with domains
    QVector<int> m_genericScriptlets;
};

} // namespace dv::br::ab
