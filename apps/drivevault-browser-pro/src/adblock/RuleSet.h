#pragma once

#include "NetRule.h"

#include <QHash>
#include <QVector>
#include <functional>

namespace dv::br::ab {

// All network filters of the loaded lists, indexed so that a request is checked against a few candidates only:
//   host index   "||example.com^"  -> looked up with every suffix of the request's host name
//   token index  the rarest-looking whole word of the pattern -> looked up with every word of the address
//   fallback     filters with no usable word (checked every time; kept small)
// Built once (add + finalize), then only read: match() may be called from any thread.
class RuleSet {
public:
    struct Decision {
        bool blocked = false;
        const NetRule* rule = nullptr;          // the blocking filter
        const NetRule* exception = nullptr;     // the exception that let it through
    };

    void add(const NetRule& rule);
    void finalize();

    // url / sourceUrl: full addresses; type: one ResType bit; enabledMask: bit n = list n is on
    Decision match(const QString& url, const QString& sourceUrl, quint16 type, quint32 enabledMask) const;
    bool siteExempt(const QString& sourceUrl, quint32 enabledMask) const;        // "@@||site^$document"

    int ruleCount() const { return int(m_rules.size()); }
    int ruleCountOfList(int list) const { return list >= 0 && list < m_perList.size() ? m_perList.at(list) : 0; }

    static QString hostOf(const QString& lowerUrl, int* hostStart = nullptr);
    static QString registrableDomain(const QString& host);

private:
    struct Index {
        QHash<QString, QVector<int>> hosts;
        QHash<QString, QVector<int>> tokens;
        QVector<int> fallback;
    };
    struct Request {
        QString url, lower, host, sourceHost;
        int hostStart = 0;
        quint16 type = 0;
        bool thirdParty = false;
    };

    static QString bestToken(const NetRule& r);
    bool ruleMatches(const NetRule& r, const Request& q) const;
    static bool patternMatches(const NetRule& r, const QString& url, const Request& q);
    static bool matchHere(const QString& url, int pos, const QString& part, int* end);
    void candidates(const Index& idx, const Request& q, const std::function<bool(int)>& visit) const;

    QVector<NetRule> m_rules;
    QVector<int> m_perList;
    Index m_block, m_allow;
    QVector<int> m_siteExempt;               // exception filters with $document
};

} // namespace dv::br::ab
