#include "WebTools.h"

#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QUrlQuery>

namespace dm::ai {

namespace {

QJsonObject prop(const char* type, const QString& description)
{
    return QJsonObject{{"type", QLatin1String(type)}, {"description", description}};
}

QJsonObject tool(const char* name, const QString& description, const QJsonObject& properties, const QStringList& required)
{
    QJsonArray req;
    for (const QString& r : required)
        req.append(r);
    return QJsonObject{{"type", "function"},
                       {"function", QJsonObject{{"name", QLatin1String(name)}, {"description", description},
                                                {"parameters", QJsonObject{{"type", "object"}, {"properties", properties}, {"required", req}}}}}};
}

QString decodeEntities(const QString& html)
{
    return QTextDocumentFragment::fromHtml(html).toPlainText();
}

// like decodeEntities, but keeps the line breaks of the text (an HTML parser would fold them into spaces)
QString decodeKeepingLines(const QString& text)
{
    static const QRegularExpression entity(QStringLiteral("&(#x[0-9a-fA-F]+|#[0-9]+|[A-Za-z]+);"));
    QString out;
    int last = 0;
    for (auto it = entity.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        last = m.capturedEnd();
        const QString name = m.captured(1);
        QString rep;
        if (name == QLatin1String("amp")) rep = QStringLiteral("&");
        else if (name == QLatin1String("lt")) rep = QStringLiteral("<");
        else if (name == QLatin1String("gt")) rep = QStringLiteral(">");
        else if (name == QLatin1String("quot")) rep = QStringLiteral("\"");
        else if (name == QLatin1String("apos")) rep = QStringLiteral("'");
        else if (name == QLatin1String("nbsp")) rep = QStringLiteral(" ");
        else if (name.startsWith(QLatin1Char('#'))) {
            bool ok = false;
            const uint cp = name.startsWith(QLatin1String("#x")) ? name.mid(2).toUInt(&ok, 16) : name.mid(1).toUInt(&ok, 10);
            if (ok && cp > 0 && cp < 0x110000)
                rep = QString::fromUcs4(reinterpret_cast<const char32_t*>(&cp), 1);
        } else {
            rep = QTextDocumentFragment::fromHtml(m.captured(0)).toPlainText();      // a named entity of the rest (&middot; ...)
        }
        out += rep.isEmpty() ? m.captured(0) : rep;
    }
    return out + text.mid(last);
}

QString stripTags(const QString& s)
{
    static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
    QString t = s;
    t.remove(tags);
    return decodeEntities(t).simplified();
}

} // namespace

WebTools::WebTools(QNetworkAccessManager* net, QObject* parent)
    : QObject(parent)
    , m_net(net)
{
}

WebTools::~WebTools() { cancel(); }

QJsonArray WebTools::specs()
{
    QJsonArray a;
    a.append(tool("web_search", QStringLiteral("Searches the web and returns the top results (title, address, short text). Use it for current or unknown facts."),
                  QJsonObject{{"query", prop("string", QStringLiteral("the search words"))}}, {QStringLiteral("query")}));
    a.append(tool("fetch_page", QStringLiteral("Opens one web page (http or https address) and returns its text. Use it to read a result of web_search."),
                  QJsonObject{{"url", prop("string", QStringLiteral("the address of the page"))}}, {QStringLiteral("url")}));
    return a;
}

bool WebTools::handles(const QString& tool) { return tool == QLatin1String("web_search") || tool == QLatin1String("fetch_page"); }

QString WebTools::describe(const QString& tool, const QJsonObject& args)
{
    if (tool == QLatin1String("web_search"))
        return QStringLiteral("Web search: ") + args.value(QLatin1String("query")).toString().simplified().left(100);
    return QStringLiteral("Open page: ") + args.value(QLatin1String("url")).toString().left(120);
}

// ---- pure parts ---------------------------------------------------------------------------------------------------------------

QList<WebTools::SearchHit> WebTools::parseSearch(const QString& html, int maxHits)
{
    QList<SearchHit> hits;
    static const QRegularExpression block(QStringLiteral("class=\"result__a\"[^>]*href=\"([^\"]+)\"[^>]*>(.*?)</a>"),
                                          QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression snippet(QStringLiteral("class=\"result__snippet\"[^>]*>(.*?)</a>"), QRegularExpression::DotMatchesEverythingOption);
    QList<QRegularExpressionMatch> snippets;
    for (auto it = snippet.globalMatch(html); it.hasNext();)
        snippets << it.next();
    int i = 0;
    for (auto it = block.globalMatch(html); it.hasNext() && hits.size() < maxHits; ++i) {
        const QRegularExpressionMatch m = it.next();
        QString href = decodeEntities(m.captured(1));
        if (href.startsWith(QLatin1String("//")))
            href = QStringLiteral("https:") + href;
        const QUrl u(href);
        QString target = href;
        if (u.host().endsWith(QLatin1String("duckduckgo.com"))) {
            if (u.path() != QLatin1String("/l/"))
                continue;                                              // an advertisement or an internal link of the search page
            target = QUrlQuery(u).queryItemValue(QStringLiteral("uddg"), QUrl::FullyDecoded);       // the real address is in "uddg"
        }
        const QUrl real(target);
        if ((real.scheme() != QLatin1String("http") && real.scheme() != QLatin1String("https")) || real.host().isEmpty())
            continue;                                                  // an advertisement or an internal link
        SearchHit h;
        h.title = stripTags(m.captured(2));
        h.url = real.toString();
        if (i < snippets.size())
            h.snippet = stripTags(snippets.at(i).captured(1)).left(300);
        if (!h.title.isEmpty())
            hits << h;
    }
    return hits;
}

QString WebTools::formatHits(const QList<SearchHit>& hits)
{
    if (hits.isEmpty())
        return QStringLiteral("No results.");
    QStringList lines;
    int n = 0;
    for (const SearchHit& h : hits)
        lines << QStringLiteral("%1. %2\n   %3\n   %4").arg(++n).arg(h.title, h.url, h.snippet);
    return lines.join(QStringLiteral("\n\n"));
}

QString WebTools::htmlToText(const QString& html, int maxChars)
{
    QString t = html;
    static const QRegularExpression blocks(QStringLiteral("<(script|style|noscript|svg|head|nav|footer|form|iframe)\\b.*?</\\1\\s*>"),
                                           QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression comments(QStringLiteral("<!--.*?-->"), QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression breaks(QStringLiteral("<(br|/p|/div|/li|/tr|/h[1-6]|/section|/article|/table)\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tags(QStringLiteral("<[^>]*>"));
    t.remove(comments);
    t.remove(blocks);
    t.replace(breaks, QStringLiteral("\n"));
    t.remove(tags);
    t = decodeKeepingLines(t);
    QStringList lines;
    for (const QString& line : t.split(QLatin1Char('\n'))) {
        const QString s = line.simplified();
        if (!s.isEmpty())
            lines << s;
    }
    QString out = lines.join(QLatin1Char('\n'));
    if (out.size() > maxChars)
        out = out.left(maxChars) + QStringLiteral("\n... (the page is longer; only the beginning is shown)");
    return out;
}

bool WebTools::addressIsPublic(const QHostAddress& address)
{
    QHostAddress a = address;
    bool ok = false;
    if (a.protocol() == QAbstractSocket::IPv6Protocol) {
        const quint32 v4 = a.toIPv4Address(&ok);                           // ::ffff:a.b.c.d (an IPv4 address inside an IPv6 one)
        if (ok)
            a = QHostAddress(v4);
    }
    if (a.isNull() || a.isLoopback() || a.isMulticast())
        return false;
    static const QList<QPair<QHostAddress, int>> blocked = {
        QHostAddress::parseSubnet(QStringLiteral("0.0.0.0/8")), QHostAddress::parseSubnet(QStringLiteral("10.0.0.0/8")),
        QHostAddress::parseSubnet(QStringLiteral("100.64.0.0/10")), QHostAddress::parseSubnet(QStringLiteral("127.0.0.0/8")),
        QHostAddress::parseSubnet(QStringLiteral("169.254.0.0/16")), QHostAddress::parseSubnet(QStringLiteral("172.16.0.0/12")),
        QHostAddress::parseSubnet(QStringLiteral("192.0.0.0/24")), QHostAddress::parseSubnet(QStringLiteral("192.168.0.0/16")),
        QHostAddress::parseSubnet(QStringLiteral("198.18.0.0/15")), QHostAddress::parseSubnet(QStringLiteral("224.0.0.0/4")),
        QHostAddress::parseSubnet(QStringLiteral("240.0.0.0/4")), QHostAddress::parseSubnet(QStringLiteral("::/128")),
        QHostAddress::parseSubnet(QStringLiteral("::1/128")), QHostAddress::parseSubnet(QStringLiteral("fc00::/7")),
        QHostAddress::parseSubnet(QStringLiteral("fe80::/10")), QHostAddress::parseSubnet(QStringLiteral("ff00::/8")),
    };
    for (const auto& net : blocked)
        if (a.isInSubnet(net))
            return false;
    return true;
}

QString WebTools::checkUrl(const QUrl& url)
{
    if (!url.isValid() || url.host().isEmpty())
        return QStringLiteral("not a valid address");
    if (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))
        return QStringLiteral("only http and https addresses can be opened");
    if (!url.userInfo().isEmpty())
        return QStringLiteral("addresses with a user name or password are not opened");
    if (url.port() != -1 && url.port() != 80 && url.port() != 443)
        return QStringLiteral("only the normal web ports are used");
    const QString host = url.host().toLower();
    if (host == QLatin1String("localhost") || host.endsWith(QLatin1String(".localhost")) || host.endsWith(QLatin1String(".local")) || host.endsWith(QLatin1String(".internal"))
        || !host.contains(QLatin1Char('.')) && QHostAddress(host).isNull())
        return QStringLiteral("this computer and the local network are not reachable from here");
    const QHostAddress literal(host);
    if (!literal.isNull() && !addressIsPublic(literal))
        return QStringLiteral("this computer and the local network are not reachable from here");
    return {};
}

// ---- the work ------------------------------------------------------------------------------------------------------------------

void WebTools::finish(const QString& text, bool error)
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    auto done = std::move(m_done);
    m_done = nullptr;
    if (done)
        done(text, error);
}

void WebTools::cancel()
{
    ++m_token;
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_done = nullptr;
}

void WebTools::run(const QString& tool, const QJsonObject& args, std::function<void(const QString&, bool)> done)
{
    cancel();
    m_done = std::move(done);
    if (tool == QLatin1String("web_search")) {
        const QString q = args.value(QLatin1String("query")).toString().trimmed();
        if (q.isEmpty()) {
            finish(QStringLiteral("query is empty"), true);
            return;
        }
        search(q.left(300));
    } else if (tool == QLatin1String("fetch_page")) {
        const QUrl url(args.value(QLatin1String("url")).toString().trimmed());
        const QString why = checkUrl(url);
        if (!why.isEmpty()) {
            finish(why, true);
            return;
        }
        fetch(url, 0);
    } else {
        finish(QStringLiteral("unknown tool"), true);
    }
}

void WebTools::search(const QString& query)
{
    QUrl url(QStringLiteral("https://html.duckduckgo.com/html/"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query);
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36");
    req.setTransferTimeout(20000);
    const int token = m_token;
    m_reply = m_net->get(req);
    connect(m_reply, &QNetworkReply::finished, this, [this, token] {
        if (token != m_token || !m_reply)
            return;
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->read(2 * 1024 * 1024);
        const bool failed = reply->error() != QNetworkReply::NoError && status == 0;
        reply->deleteLater();
        if (failed || status >= 400) {
            finish(QStringLiteral("the search service could not be reached (%1)").arg(status == 0 ? QStringLiteral("no connection") : QString::number(status)), true);
            return;
        }
        finish(formatHits(parseSearch(QString::fromUtf8(data))), false);
    });
}

void WebTools::fetch(const QUrl& url, int hop)
{
    if (hop > 3) {
        finish(QStringLiteral("too many redirects"), true);
        return;
    }
    const int token = m_token;
    // the address must be public: look the name up first and refuse a private answer
    QHostInfo::lookupHost(url.host(), this, [this, token, url, hop](const QHostInfo& info) {
        if (token != m_token)
            return;
        if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
            finish(QStringLiteral("the address could not be found"), true);
            return;
        }
        for (const QHostAddress& a : info.addresses()) {
            if (!addressIsPublic(a)) {
                finish(QStringLiteral("this computer and the local network are not reachable from here"), true);
                return;
            }
        }
        QNetworkRequest req(url);
        req.setRawHeader("User-Agent", "DriveVaultMasterPro (local AI assistant; reads one page for the user)");
        req.setRawHeader("Accept", "text/html,text/plain,application/json;q=0.9,*/*;q=0.1");
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
        req.setTransferTimeout(20000);
        m_reply = m_net->get(req);
        auto* body = new QByteArray;                                    // freed with the reply
        connect(m_reply, &QObject::destroyed, m_reply, [body] { delete body; });
        connect(m_reply, &QNetworkReply::readyRead, this, [this, token, body] {
            if (token != m_token || !m_reply)
                return;
            body->append(m_reply->readAll());
            if (body->size() > kMaxPageBytes)
                m_reply->abort();                                       // enough: the rest is not needed (handled as finished below)
        });
        connect(m_reply, &QNetworkReply::finished, this, [this, token, hop, body, url] {
            if (token != m_token || !m_reply)
                return;
            QNetworkReply* reply = m_reply;
            m_reply = nullptr;
            body->append(reply->readAll());
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
            const QVariant location = reply->header(QNetworkRequest::LocationHeader);
            const QNetworkReply::NetworkError error = reply->error();
            const QString errorText = reply->errorString();
            const QByteArray bytes = *body;
            reply->deleteLater();
            if (status >= 300 && status < 400 && location.isValid()) {
                const QUrl next = url.resolved(location.toUrl());
                const QString why = checkUrl(next);
                if (!why.isEmpty()) {
                    finish(QStringLiteral("the page redirects to an address that is not allowed (%1)").arg(why), true);
                    return;
                }
                fetch(next, hop + 1);
                return;
            }
            if (status >= 400 || (status == 0 && error != QNetworkReply::NoError)) {
                finish(QStringLiteral("the page could not be opened (%1)").arg(status == 0 ? QStringLiteral("no connection: ") + errorText : QString::number(status)), true);
                return;
            }
            if (!type.isEmpty() && !type.contains(QLatin1String("html")) && !type.contains(QLatin1String("text")) && !type.contains(QLatin1String("json"))
                && !type.contains(QLatin1String("xml"))) {
                finish(QStringLiteral("this address is not a text page (%1)").arg(type.left(40)), true);
                return;
            }
            const QString raw = QString::fromUtf8(bytes);
            const QString text = type.contains(QLatin1String("html")) || raw.trimmed().startsWith(QLatin1Char('<')) ? htmlToText(raw, kMaxPageChars) : raw.left(kMaxPageChars);
            finish(text.isEmpty() ? QStringLiteral("(the page has no text)") : text, false);
        });
    });
}

} // namespace dm::ai
