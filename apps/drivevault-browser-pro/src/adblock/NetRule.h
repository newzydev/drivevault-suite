#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

// A network filter of the Adblock Plus / uBlock Origin syntax, parsed. No WebEngine here.
namespace dv::br::ab {

enum ResType : quint16 {
    Document = 1, Subdocument = 2, Stylesheet = 4, Script = 8, Image = 16, Font = 32, Object = 64, Media = 128,
    Xhr = 256, Ping = 512, Websocket = 1024, Other = 2048
};
constexpr quint16 kAllTypes = 0x0FFF;
constexpr quint16 kDefaultTypes = kAllTypes & ~Document;       // no type option: everything except the page itself

struct NetRule {
    QString pattern;                  // lower case (unless matchCase), wildcards "*" and separators "^" kept
    QStringList parts;                // pattern split at "*"
    bool hostAnchor = false;          // "||"
    bool startAnchor = false;         // leading "|"
    bool endAnchor = false;           // trailing "|"
    bool exception = false;           // "@@"
    bool important = false;           // $important
    bool matchCase = false;
    qint8 thirdParty = -1;            // -1 any, 1 only third-party requests, 0 only first-party requests
    quint16 types = kDefaultTypes;
    bool typesGiven = false;          // the filter named types itself
    QStringList domainsInc, domainsExc;       // $domain= (the page the request comes from), suffix match
    QString redirect;                 // $redirect=name (stand-in resource, Phase 3)
    QString hostKey;                  // "||example.com^" -> "example.com" (index key); empty when not usable
    quint8 cosmeticOff = 0;           // exception filters only: 1 = $generichide, 2 = $elemhide (no network meaning)
    quint8 list = 0;                  // which list it came from
    QString raw;                      // the original line
};

enum class ParseKind { Network, Cosmetic, CosmeticSite, Comment, Unsupported };       // CosmeticSite: "@@||site^$elemhide" / "$generichide"

} // namespace dv::br::ab
