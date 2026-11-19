#include "StandIns.h"

#include "NetRule.h"

#include <QHash>
#include <QStringList>

namespace dv::br::ab {

namespace {

const char* const kNoop = "(function(){})();";
const char* const kGtm =
    "(function(){var w=window;var dl=w.dataLayer=w.dataLayer||[];w.google_tag_manager=w.google_tag_manager||{};"
    "w.gtag=w.gtag||function(){dl.push(arguments)};"
    "function cb(e){try{if(e&&typeof e.eventCallback==='function')e.eventCallback()}catch(_){}}"
    "for(var i=0;i<dl.length;i++)cb(dl[i]);"
    "dl.push=function(){for(var i=0;i<arguments.length;i++)cb(arguments[i]);return Array.prototype.push.apply(dl,arguments)};})();";
const char* const kGa =
    "(function(){var w=window;var t=function(){return{get:function(){return''},set:function(){},send:function(){},ga:function(){}}};"
    "var ga=w.ga=function(){var a=arguments,c=a[a.length-1];if(typeof c==='function'){try{c(t())}catch(_){}}};"
    "ga.create=t;ga.getByName=t;ga.getAll=function(){return[t()]};ga.remove=function(){};ga.loaded=true;w.GoogleAnalyticsObject='ga';"
    "w._gaq=w._gaq||[];w._gaq.push=function(f){if(typeof f==='function'){try{f()}catch(_){}}return 1};"
    "w._gat={_getTracker:t,_createTracker:t,_anonymizeIp:function(){}};})();";
const char* const kAdsByGoogle =
    "(function(){var a=window.adsbygoogle=window.adsbygoogle||[];a.loaded=true;a.push=function(){return 1};})();";
const char* const kGpt =
    "(function(){var w=window,g=w.googletag=w.googletag||{};g.apiReady=true;g.pubadsReady=true;"
    "var f=function(){return P};var P=new Proxy(f,{get:function(t,k){if(k===Symbol.toPrimitive)return function(){return''};if(k==='then')return undefined;"
    "if(k==='getSlotElementId'||k==='getAdUnitPath'||k==='getName')return function(){return''};return f},apply:function(){return P}});"
    "var cmd=g.cmd=g.cmd||[];var run=function(x){try{if(typeof x==='function')x()}catch(_){}};for(var i=0;i<cmd.length;i++)run(cmd[i]);"
    "cmd.push=function(x){run(x);return 1};"
    "g.pubads=function(){return P};g.defineSlot=function(){return P};g.defineOutOfPageSlot=function(){return P};g.companionAds=function(){return P};"
    "g.content=function(){return P};g.sizeMapping=function(){return P};g.enableServices=function(){};g.display=function(){};g.destroySlots=function(){return true};"
    "g.getVersion=function(){return''};})();";
const char* const kFbq =
    "(function(){var w=window;if(w.fbq)return;var f=w.fbq=function(){};f.callMethod=f;f.queue=[];f.loaded=true;f.version='2.0';f.push=f;w._fbq=f;})();";
const char* const kChartbeat =
    "window.pSUPERFLY={activateInteraction:function(){},virtualPage:function(){}};";
const char* const kFingerprint =
    "(function(){var F=function(){};F.get=function(o,c){(typeof o==='function'?o:c)([])};F.getPromise=function(){return Promise.resolve([])};"
    "F.getV18=F.get;F.x64hash128=function(){return''};window.Fingerprint2=F;})();";
const char* const kInstream = "window.google_ad_status=1;";
// the libraries that sites use to ask "is an ad blocker running?": a stand-in that always answers "no"
const char* const kDetector =
    "(function(){var w=window;function D(){}var c=function(){return this};"
    "D.prototype={check:function(){return true},emitEvent:c,clearEvent:c,setOption:c,"
    "on:function(d,f){if(!d&&typeof f==='function')setTimeout(f,0);return this},onDetected:c,"
    "onNotDetected:function(f){if(typeof f==='function')setTimeout(f,0);return this}};"
    "w.FuckAdBlock=w.BlockAdBlock=w.SniffAdBlock=D;"
    "if(!w.fuckAdBlock)w.fuckAdBlock=new D();if(!w.blockAdBlock)w.blockAdBlock=new D();if(!w.sniffAdBlock)w.sniffAdBlock=new D();})();";
const char* const kCanRunAds = "window.canRunAds=true;window.isAdBlockActive=false;";
const char* const kPng1 = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNkYPhfDwAChwGA60e6kgAAAABJRU5ErkJggg==";
const char* const kGif1 = "R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7";

QHash<QString, StandIn> buildTable()
{
    QHash<QString, StandIn> t;
    auto js = [&](std::initializer_list<const char*> names, const char* body) {
        for (const char* n : names)
            t.insert(QString::fromLatin1(n), {QByteArray(body), "text/javascript"});
    };
    js({"noopjs", "noop.js"}, kNoop);
    js({"googletagmanager_gtm.js", "googletagmanager.com/gtm.js", "gtm.js"}, kGtm);
    js({"google-analytics_analytics.js", "google-analytics.com/analytics.js", "google-analytics_ga.js", "google-analytics.com/ga.js"}, kGa);
    js({"googlesyndication_adsbygoogle.js", "googlesyndication.com/adsbygoogle.js"}, kAdsByGoogle);
    js({"googletagservices_gpt.js", "googletagservices.com/gpt.js"}, kGpt);
    js({"fbevents.js", "facebook_fbevents.js"}, kFbq);
    js({"chartbeat.js"}, kChartbeat);
    js({"fingerprint2.js"}, kFingerprint);
    js({"doubleclick_instream_ad_status.js", "doubleclick.net/instream/ad_status.js"}, kInstream);
    js({"canrunads.js"}, kCanRunAds);
    js({"fuckadblock.js", "blockadblock.js"}, kDetector);
    t.insert(QStringLiteral("noopjson"), {"{}", "application/json"});
    t.insert(QStringLiteral("noop.json"), {"{}", "application/json"});
    t.insert(QStringLiteral("noop.txt"), {"", "text/plain"});
    t.insert(QStringLiteral("nooptext"), {"", "text/plain"});
    t.insert(QStringLiteral("empty"), {"", "text/plain"});
    t.insert(QStringLiteral("noopcss"), {"", "text/css"});
    t.insert(QStringLiteral("noop.css"), {"", "text/css"});
    const QByteArray frame = "<!DOCTYPE html><html><head><meta charset=\"utf-8\"></head><body></body></html>";
    t.insert(QStringLiteral("noopframe"), {frame, "text/html"});
    t.insert(QStringLiteral("noop.html"), {frame, "text/html"});
    const StandIn gif{QByteArray::fromBase64(kGif1), "image/gif"};
    t.insert(QStringLiteral("1x1.gif"), gif);
    t.insert(QStringLiteral("1x1-transparent.gif"), gif);
    const StandIn png{QByteArray::fromBase64(kPng1), "image/png"};         // a 1x1 picture serves for the "2x2" name as well: a transparent dot
    t.insert(QStringLiteral("2x2.png"), png);
    t.insert(QStringLiteral("2x2-transparent.png"), png);
    t.insert(QStringLiteral("3x2.png"), png);
    t.insert(QStringLiteral("32x32.png"), png);
    return t;
}

const QHash<QString, StandIn>& table()
{
    static const QHash<QString, StandIn> t = buildTable();
    return t;
}

} // namespace

bool standIn(const QString& name, StandIn* out)
{
    const auto it = table().constFind(name.trimmed().toLower());
    if (it == table().constEnd())
        return false;
    *out = it.value();
    return true;
}

QStringList standInNames()
{
    QStringList names = table().keys();
    names.sort();
    return names;
}

QString standInNameForUrl(const QString& url, quint16 type)
{
    if (!(type & (Script | Other | Xhr)))
        return {};                                          // scripts, and the fetch / XHR "is this ad address reachable?" tests
    const QString u = url.toLower();
    const QString noScheme = u.mid(u.indexOf(QLatin1String("://")) + 3);
    const QString path = noScheme.left(qMin(noScheme.indexOf(QLatin1Char('?')) < 0 ? noScheme.size() : noScheme.indexOf(QLatin1Char('?')), noScheme.size()));
    auto has = [&](const char* s) { return path.contains(QLatin1String(s)); };
    if (has("googlesyndication.com/pagead/js/adsbygoogle.js") || path.endsWith(QLatin1String("/adsbygoogle.js")))
        return QStringLiteral("googlesyndication_adsbygoogle.js");
    if (path.startsWith(QLatin1String("www.google-analytics.com/analytics.js")) || path.startsWith(QLatin1String("www.google-analytics.com/ga.js"))
        || path.startsWith(QLatin1String("ssl.google-analytics.com/ga.js")) || path.startsWith(QLatin1String("google-analytics.com/analytics.js")))
        return QStringLiteral("google-analytics_analytics.js");
    if (path.startsWith(QLatin1String("www.googletagmanager.com/gtm.js")) || path.startsWith(QLatin1String("www.googletagmanager.com/gtag/js")))
        return QStringLiteral("googletagmanager_gtm.js");
    if (has("/tag/js/gpt.js") && (has("googletagservices.com") || has("doubleclick.net")))
        return QStringLiteral("googletagservices_gpt.js");
    if (has("doubleclick.net/instream/ad_status.js"))
        return QStringLiteral("doubleclick_instream_ad_status.js");
    if (path.startsWith(QLatin1String("connect.facebook.net/")) && path.endsWith(QLatin1String("/fbevents.js")))
        return QStringLiteral("fbevents.js");
    // scripts that sites ship only to test whether an ad blocker is there: the file name says it
    for (const char* bait : {"/ads.js", "/adframe.js", "/advertisement.js", "/adsbox.js"})
        if (path.endsWith(QLatin1String(bait)))
            return QStringLiteral("canrunads.js");
    for (const char* lib : {"/fuckadblock.js", "/fuckadblock.min.js", "/blockadblock.js", "/blockadblock.min.js", "/sniffadblock.js"})
        if (path.endsWith(QLatin1String(lib)))
            return QStringLiteral("fuckadblock.js");
    return {};
}

} // namespace dv::br::ab
