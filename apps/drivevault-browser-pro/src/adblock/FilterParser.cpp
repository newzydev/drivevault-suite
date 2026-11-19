#include "FilterParser.h"

#include <QHash>
#include <QRegularExpression>

namespace dv::br::ab {

namespace {

const QHash<QString, quint16>& typeNames()
{
    static const QHash<QString, quint16> names = {
        {QStringLiteral("script"), Script}, {QStringLiteral("image"), Image}, {QStringLiteral("stylesheet"), Stylesheet}, {QStringLiteral("css"), Stylesheet},
        {QStringLiteral("subdocument"), Subdocument}, {QStringLiteral("frame"), Subdocument}, {QStringLiteral("xmlhttprequest"), Xhr}, {QStringLiteral("xhr"), Xhr},
        {QStringLiteral("media"), Media}, {QStringLiteral("font"), Font}, {QStringLiteral("object"), Object}, {QStringLiteral("ping"), Ping},
        {QStringLiteral("websocket"), Websocket}, {QStringLiteral("other"), Other}, {QStringLiteral("document"), Document}, {QStringLiteral("doc"), Document},
    };
    return names;
}

// options that change what a filter does in a way this program does not implement: the whole line is skipped
bool unsupportedOption(const QString& name)
{
    static const QStringList bad = {
        QStringLiteral("removeparam"), QStringLiteral("queryprune"), QStringLiteral("csp"), QStringLiteral("rewrite"), QStringLiteral("replace"), QStringLiteral("header"),
        QStringLiteral("permissions"), QStringLiteral("cname"), QStringLiteral("uritransform"), QStringLiteral("urlskip"), QStringLiteral("popup"), QStringLiteral("popunder"),
        QStringLiteral("specifichide"), QStringLiteral("shide"),
        QStringLiteral("genericblock"), QStringLiteral("inline-script"), QStringLiteral("inline-font"), QStringLiteral("webrtc"), QStringLiteral("badfilter"),
        QStringLiteral("app"), QStringLiteral("method"), QStringLiteral("to"), QStringLiteral("from"), QStringLiteral("denyallow"), QStringLiteral("strict1p"), QStringLiteral("strict3p"),
        QStringLiteral("empty"), QStringLiteral("mp4"), QStringLiteral("redirect-rule"), QStringLiteral("xhr-only"), QStringLiteral("all-except"),
    };
    return bad.contains(name);
}

bool isHostChar(QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('.') || c == QLatin1Char('-'); }

} // namespace

ParseKind parseLine(const QString& rawLine, NetRule& rule)
{
    const QString line = rawLine.trimmed();
    if (line.isEmpty() || line.startsWith(QLatin1Char('!')) || line.startsWith(QLatin1Char('[')))
        return ParseKind::Comment;
    // cosmetic: element hiding, scriptlets, style injection
    if (line.contains(QLatin1String("##")) || line.contains(QLatin1String("#@#")) || line.contains(QLatin1String("#?#")) || line.contains(QLatin1String("#$#"))
        || line.contains(QLatin1String("#%#")) || line.contains(QLatin1String("#@$#")) || line.contains(QLatin1String("#@%#")))
        return ParseKind::Cosmetic;

    NetRule r;
    r.raw = line;
    QString body = line;
    if (body.startsWith(QLatin1String("@@"))) {
        r.exception = true;
        body.remove(0, 2);
    }

    // options: after the last '$' when the rest looks like a comma separated option list
    QString optionText;
    const int dollar = body.lastIndexOf(QLatin1Char('$'));
    if (dollar >= 0) {
        static const QRegularExpression optList(QStringLiteral(R"(^~?[A-Za-z0-9_-]+(=[^,]*)?(,~?[A-Za-z0-9_-]+(=[^,]*)?)*$)"));
        const QString tail = body.mid(dollar + 1);
        if (optList.match(tail).hasMatch()) {
            optionText = tail;
            body.truncate(dollar);
        }
    }

    // regular expression filters /.../ are not supported
    if (body.size() >= 2 && body.startsWith(QLatin1Char('/')) && body.endsWith(QLatin1Char('/')))
        return ParseKind::Unsupported;

    quint16 plus = 0, minus = 0;
    if (!optionText.isEmpty()) {
        for (const QString& optRaw : optionText.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            QString opt = optRaw;
            const bool neg = opt.startsWith(QLatin1Char('~'));
            if (neg)
                opt.remove(0, 1);
            QString value;
            const int eq = opt.indexOf(QLatin1Char('='));
            if (eq >= 0) {
                value = opt.mid(eq + 1);
                opt.truncate(eq);
            }
            opt = opt.toLower();
            if (unsupportedOption(opt))
                return ParseKind::Unsupported;
            if (typeNames().contains(opt)) {
                (neg ? minus : plus) |= typeNames().value(opt);
            } else if (opt == QLatin1String("all")) {
                plus |= kAllTypes;
            } else if (opt == QLatin1String("third-party") || opt == QLatin1String("3p")) {
                r.thirdParty = neg ? 0 : 1;
            } else if (opt == QLatin1String("first-party") || opt == QLatin1String("1p")) {
                r.thirdParty = neg ? 1 : 0;
            } else if (opt == QLatin1String("elemhide") || opt == QLatin1String("ehide")) {
                r.cosmeticOff = 2;
            } else if (opt == QLatin1String("generichide") || opt == QLatin1String("ghide")) {
                if (r.cosmeticOff == 0)
                    r.cosmeticOff = 1;
            } else if (opt == QLatin1String("important")) {
                r.important = true;
            } else if (opt == QLatin1String("match-case")) {
                r.matchCase = true;
            } else if (opt == QLatin1String("domain") || opt == QLatin1String("from")) {
                for (const QString& d : value.split(QLatin1Char('|'), Qt::SkipEmptyParts)) {
                    if (d.startsWith(QLatin1Char('~')))
                        r.domainsExc << d.mid(1).toLower();
                    else
                        r.domainsInc << d.toLower();
                }
            } else if (opt == QLatin1String("redirect")) {
                r.redirect = value;
            } else if (opt == QLatin1String("object-subrequest") || opt == QLatin1String("subdocument-only") || opt == QLatin1String("collapse") || opt == QLatin1String("empty")) {
                // old names that change nothing we do
            } else {
                return ParseKind::Unsupported;                      // an option we do not know: do not guess
            }
        }
    }
    if (plus) {
        r.types = plus & ~minus;
        r.typesGiven = true;
    } else if (minus) {
        r.types = kDefaultTypes & ~minus;
        r.typesGiven = true;
    }

    // pattern
    if (body.startsWith(QLatin1String("||"))) {
        r.hostAnchor = true;
        body.remove(0, 2);
    } else if (body.startsWith(QLatin1Char('|'))) {
        r.startAnchor = true;
        body.remove(0, 1);
    }
    if (body.endsWith(QLatin1Char('|'))) {
        r.endAnchor = true;
        body.chop(1);
    }
    if (!r.matchCase)
        body = body.toLower();
    if (body.isEmpty() || body == QLatin1String("*")) {
        // "$third-party" alone, "||" alone ...: too broad to be a real filter
        if (r.domainsInc.isEmpty() && r.types == kDefaultTypes)
            return ParseKind::Unsupported;
    }
    r.pattern = body;
    r.parts = body.split(QLatin1Char('*'));
    if (r.parts.size() > 1 && r.parts.first().isEmpty()) {       // "*foo": not anchored at the start
        r.startAnchor = false;
        r.hostAnchor = false;
    }
    if (r.hostAnchor) {
        int n = 0;
        while (n < body.size() && isHostChar(body.at(n)))
            ++n;
        const bool endsCleanly = n == body.size() || body.at(n) == QLatin1Char('^') || body.at(n) == QLatin1Char('/') || body.at(n) == QLatin1Char(':') || body.at(n) == QLatin1Char('?');
        if (n > 0 && endsCleanly && !body.left(n).endsWith(QLatin1Char('.')) && !body.left(n).endsWith(QLatin1Char('-')))
            r.hostKey = body.left(n);
    }
    if (r.cosmeticOff != 0) {
        if (!r.exception)
            return ParseKind::Unsupported;               // meaningful only as an exception
        r.types = Document;
        r.typesGiven = true;
        rule = r;
        return ParseKind::CosmeticSite;
    }
    rule = r;
    return ParseKind::Network;
}

} // namespace dv::br::ab
