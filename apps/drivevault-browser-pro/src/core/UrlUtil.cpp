#include "UrlUtil.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace dv::br {

QList<SearchEngine> searchEngines()
{
    return {
        {QStringLiteral("google"), QStringLiteral("Google"), QStringLiteral("https://www.google.com/search?q=%s")},
        {QStringLiteral("duckduckgo"), QStringLiteral("DuckDuckGo"), QStringLiteral("https://duckduckgo.com/?q=%s")},
        {QStringLiteral("bing"), QStringLiteral("Bing"), QStringLiteral("https://www.bing.com/search?q=%s")},
        {QStringLiteral("brave"), QStringLiteral("Brave Search"), QStringLiteral("https://search.brave.com/search?q=%s")},
    };
}

QString defaultSearchEngineId() { return searchEngines().constFirst().id; }

QUrl searchUrl(const QString& engineId, const QString& words)
{
    QString templ;
    for (const SearchEngine& e : searchEngines())
        if (e.id == engineId)
            templ = e.urlTemplate;
    if (templ.isEmpty())
        templ = searchEngines().constFirst().urlTemplate;
    return QUrl(templ.replace(QStringLiteral("%s"), QString::fromLatin1(QUrl::toPercentEncoding(words.trimmed()))));
}

QUrl resolveInput(const QString& input, const QString& engineId)
{
    const QString in = input.trimmed();
    if (in.isEmpty())
        return QUrl(QStringLiteral("about:blank"));

    static const QRegularExpression drivePath(QStringLiteral(R"(^[A-Za-z]:[\\/])"));
    if (drivePath.match(in).hasMatch())
        return QUrl::fromLocalFile(in);
    if (in.startsWith(QLatin1String("view-source:"), Qt::CaseInsensitive))
        return QUrl(in);

    const bool hasSpace = in.contains(QLatin1Char(' ')) || in.contains(QLatin1Char('\t'));
    static const QRegularExpression hostLike(
        QStringLiteral(R"(^(localhost|(\d{1,3}\.){3}\d{1,3}|([\p{L}\p{N}-]+\.)+\p{L}{2,})(:\d{1,5})?([/?#].*)?$)"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
    if (!hasSpace && hostLike.match(in).hasMatch()) {
        static const QRegularExpression local(QStringLiteral(R"(^(localhost|(\d{1,3}\.){3}\d{1,3})([:/?#]|$))"), QRegularExpression::CaseInsensitiveOption);
        return QUrl((local.match(in).hasMatch() ? QStringLiteral("http://") : QStringLiteral("https://")) + in);
    }
    static const QRegularExpression withScheme(QStringLiteral(R"(^(https?|file|ftp|about):)"), QRegularExpression::CaseInsensitiveOption);
    if (!hasSpace && withScheme.match(in).hasMatch())
        return QUrl(in);
    return searchUrl(engineId, in);
}

QString hostOf(const QString& url)
{
    return QUrl(url).host();
}

bool isGoogleSignInPage(const QUrl& url)
{
    if (url.scheme() != QLatin1String("https") || url.host() != QLatin1String("accounts.google.com") || url.port() != -1)
        return false;
    const QString path = url.path();
    static const QRegularExpression entry(QStringLiteral(R"(^/(servicelogin|accountchooser|signin|v3/signin)(/|$))"), QRegularExpression::CaseInsensitiveOption);
    return entry.match(path).hasMatch();
}

QString prettyUrl(const QString& url)
{
    if (url.isEmpty() || url == QLatin1String("about:blank"))
        return {};
    QString s = url;
    static const QRegularExpression scheme(QStringLiteral(R"(^https?://)"), QRegularExpression::CaseInsensitiveOption);
    s.remove(scheme);
    if (s.startsWith(QLatin1String("www."), Qt::CaseInsensitive))
        s.remove(0, 4);
    if (s.endsWith(QLatin1Char('/')) && s.count(QLatin1Char('/')) == 1)
        s.chop(1);
    return s;
}

QString safeFileName(const QString& name)
{
    QString s = name.trimmed();
    for (QChar& c : s)
        if (QStringLiteral("<>:\"/\\|?*").contains(c) || c.unicode() < 32)
            c = QLatin1Char('_');
    while (s.endsWith(QLatin1Char('.')) || s.endsWith(QLatin1Char(' ')))
        s.chop(1);
    if (s.isEmpty())
        s = QStringLiteral("download");
    static const QSet<QString> reserved = {QStringLiteral("CON"), QStringLiteral("PRN"), QStringLiteral("AUX"), QStringLiteral("NUL"),
        QStringLiteral("COM1"), QStringLiteral("COM2"), QStringLiteral("COM3"), QStringLiteral("COM4"), QStringLiteral("LPT1"), QStringLiteral("LPT2"), QStringLiteral("LPT3")};
    if (reserved.contains(QFileInfo(s).completeBaseName().toUpper()))
        s.prepend(QLatin1Char('_'));
    return s;
}

QString uniqueFileName(const QString& dir, const QString& name)
{
    const QString safe = safeFileName(name);
    const QDir d(dir);
    if (!d.exists(safe))
        return safe;
    const QFileInfo fi(safe);
    const QString stem = fi.completeBaseName();
    const QString ext = fi.suffix().isEmpty() ? QString() : QLatin1Char('.') + fi.suffix();
    for (int i = 1; i < 10000; ++i) {
        const QString candidate = QStringLiteral("%1 (%2)%3").arg(stem).arg(i).arg(ext);
        if (!d.exists(candidate))
            return candidate;
    }
    return safe;
}

bool riskyFileName(const QString& name)
{
    static const QSet<QString> risky = {QStringLiteral("exe"), QStringLiteral("msi"), QStringLiteral("bat"), QStringLiteral("cmd"), QStringLiteral("ps1"),
        QStringLiteral("js"), QStringLiteral("jse"), QStringLiteral("vbs"), QStringLiteral("vbe"), QStringLiteral("wsf"), QStringLiteral("scr"), QStringLiteral("lnk"),
        QStringLiteral("com"), QStringLiteral("cpl"), QStringLiteral("hta"), QStringLiteral("jar"), QStringLiteral("reg"), QStringLiteral("dll"),
        QStringLiteral("msix"), QStringLiteral("appx"), QStringLiteral("apk")};
    return risky.contains(QFileInfo(name).suffix().toLower());
}

} // namespace dv::br
