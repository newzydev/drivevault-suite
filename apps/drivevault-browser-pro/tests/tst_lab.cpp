#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

// The "lab": pages on this computer that imitate what real pages do (hide-able boxes, scripts a blocker must tame, ad libraries a page tests for), opened in the REAL
// program with Chromium. Each case writes a filter list, starts the exe with --probe and reads what the page recorded in window.__res.
namespace {

struct Page {
    QByteArray type, body;
};

class LabServer : public QTcpServer {
public:
    QMap<QString, Page> files;
    QMap<QString, int> hits;
    LabServer()
    {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* s = nextPendingConnection())
                connect(s, &QTcpSocket::readyRead, s, [this, s] {
                    const QString path = QString::fromLatin1(s->readAll().split(' ').value(1)).section(QLatin1Char('?'), 0, 0);
                    ++hits[path];
                    const Page p = files.value(path);
                    const int status = files.contains(path) ? 200 : 404;
                    s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Type: " + (p.type.isEmpty() ? QByteArray("text/plain") : p.type) + "\r\nContent-Length: "
                             + QByteArray::number(p.body.size()) + "\r\nConnection: close\r\n\r\n" + p.body);
                    s->disconnectFromHost();
                });
        });
    }
    QString url(const QString& path) const { return QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path); }
};

struct Result {
    QJsonObject res;
    int blocked = 0;
    bool ok = false;
};

Result runCase(LabServer& server, const QByteArray& list, const QByteArray& settings, const QStringList& extra, const QString& path)
{
    Result out;
    QTemporaryDir dir;
    const QString data = dir.filePath(QStringLiteral("data"));
    QDir().mkpath(data + QStringLiteral("/filters"));
    QFile l(data + QStringLiteral("/filters/easylist.txt"));
    if (!l.open(QIODevice::WriteOnly))
        return out;
    l.write(list);
    l.close();
    QFile s(data + QStringLiteral("/settings.json"));
    if (!s.open(QIODevice::WriteOnly))
        return out;
    s.write(settings);
    s.close();
    const QString report = dir.filePath(QStringLiteral("probe.json"));
    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("DV_NO_AUDIO"), QStringLiteral("1"));
    p.setProcessEnvironment(env);
    p.start(QStringLiteral(BROWSER_EXE_PATH), QStringList{QStringLiteral("--data-dir"), data, QStringLiteral("--lang"), QStringLiteral("en"), QStringLiteral("--probe"), report} + extra + QStringList{server.url(path)});
    QElapsedTimer waited;
    waited.start();
    while (p.state() != QProcess::NotRunning && waited.elapsed() < 90000)
        QTest::qWait(50);                                  // the server of this test must keep answering while the page loads
    if (p.state() != QProcess::NotRunning) {
        p.kill();
        return out;
    }
    QFile r(report);
    if (!r.open(QIODevice::ReadOnly))
        return out;
    const QJsonObject o = QJsonDocument::fromJson(r.readAll()).array().at(0).toObject();
    out.res = QJsonDocument::fromJson(o.value(QStringLiteral("detail")).toString().toUtf8()).object();
    out.blocked = o.value(QStringLiteral("blocked")).toInt();
    out.ok = true;
    return out;
}

const QByteArray kCosmeticList =
    "[Adblock Plus 2.0]\n###adbox\n##.promo-x\n127.0.0.1##.special\n##.kept\n127.0.0.1#@#.kept\n##div[data-ad=\"1\"]\n127.0.0.1#$#.styled { display: none !important }\n";

const QByteArray kScriptletList =
    "[Adblock Plus 2.0]\n"
    "127.0.0.1##+js(aopr, adbDetect)\n127.0.0.1##+js(set, window.canRunAds, true)\n127.0.0.1##+js(json-prune, ads)\n127.0.0.1##+js(nostif, TIMER_BLOCK_ME)\n"
    "127.0.0.1##+js(no-fetch-if, /api/track)\n127.0.0.1##+js(no-xhr-if, /api/xhr-track)\n127.0.0.1##+js(aeld, click, tracking)\n127.0.0.1##+js(acs, secretVar, evil)\n"
    "127.0.0.1##+js(set-local-storage-item, consent, accepted)\n127.0.0.1##+js(ra, data-ad, #ra)\n127.0.0.1##+js(rc, adclass, #rc)\n127.0.0.1##+js(aopw, writeGuard)\n";

const QByteArray kRedirectList =
    "[Adblock Plus 2.0]\n/lib/adtag.js$script,redirect=noopjs\n/ads.js\n/adsbygoogle.js\n/plain-blocked.js\n/px.gif$image,redirect=1x1.gif\n";

void fillPages(LabServer& s)
{
    s.files[QStringLiteral("/cosmetic.html")] = {"text/html", R"HTML(<!doctype html><meta charset=utf-8><title>Cosmetic</title><body>
<div id="adbox">adbox</div><div id="t_class" class="promo-x">class</div><div id="t_spec" class="special">specific</div><div id="t_kept" class="kept">kept</div>
<div id="t_cx" data-ad="1">complex</div><div id="t_style" class="styled">style</div><div id="t_plain">plain</div>
<script>
setTimeout(function () { var d = document.createElement('div'); d.id = 't_dyn'; d.className = 'promo-x'; d.textContent = 'dynamic'; document.body.appendChild(d); }, 100);
setTimeout(function () {
  var r = {}; ['adbox', 't_class', 't_spec', 't_kept', 't_cx', 't_style', 't_plain', 't_dyn'].forEach(function (id) { r[id] = getComputedStyle(document.getElementById(id)).display === 'none'; });
  window.__res = r;
}, 900);
</script>)HTML"};
    s.files[QStringLiteral("/scriptlet.html")] = {"text/html", R"HTML(<!doctype html><meta charset=utf-8><title>Scriptlets</title>
<script>
window.__res = {};
try { var v = window.adbDetect; __res.aopr = 'read ok'; } catch (e) { __res.aopr = 'aborted'; }
__res.set = String(window.canRunAds);
__res.json = JSON.stringify(JSON.parse('{"ads":1,"keep":2}'));
setTimeout(function () { __res.timer = 'ran'; /* TIMER_BLOCK_ME */ }, 30);
setTimeout(function () { __res.timerOk = 'ran'; }, 30);
fetch('/api/track').then(function (r) { return r.text(); }).then(function (t) { __res.fetchTrack = 'fake:' + t.length; });
fetch('/api/other').then(function (r) { return r.text(); }).then(function (t) { __res.fetchOther = 'real:' + t; });
var x = new XMLHttpRequest(); x.open('GET', '/api/xhr-track'); x.onload = function () { __res.xhr = 'load:' + x.status + ':' + x.responseText.length; }; x.send();
document.addEventListener('click', function tracking() { __res.aeldTracking = 'fired'; });
document.addEventListener('click', function fine() { __res.aeldOk = 'fired'; });
window.secretVar = 5;
try { var a = window.secretVar; __res.acsPlain = 'read ok'; } catch (e) { __res.acsPlain = 'aborted'; }
</script>
<script>/* evil */ try { var b = window.secretVar; __res.acsEvil = 'read ok'; } catch (e) { __res.acsEvil = 'aborted'; }</script>
<script>try { window.writeGuard = 1; __res.aopw = 'written'; } catch (e) { __res.aopw = 'aborted'; }</script>
<body><div id="ra" data-ad="1">ra</div><div id="rc" class="adclass keep">rc</div>
<script>
document.body.click();
setTimeout(function () {
  __res.storage = String(localStorage.getItem('consent'));
  __res.ra = document.getElementById('ra').hasAttribute('data-ad') ? 'kept' : 'removed';
  __res.rc = document.getElementById('rc').classList.contains('adclass') ? 'kept' : 'removed';
  __res.rcKeep = document.getElementById('rc').classList.contains('keep');
}, 1000);
</script>)HTML"};
    s.files[QStringLiteral("/api/track")] = {"text/plain", "REAL-TRACK-ANSWER"};
    s.files[QStringLiteral("/api/other")] = {"text/plain", "yes"};
    s.files[QStringLiteral("/api/xhr-track")] = {"text/plain", "REAL-XHR-ANSWER"};
    s.files[QStringLiteral("/redirect.html")] = {"text/html", R"HTML(<!doctype html><meta charset=utf-8><title>Redirect</title>
<script>window.__res = {};</script>
<script src="/lib/adtag.js" onload="__res.adtag='loaded'" onerror="__res.adtag='failed'"></script>
<script src="/ads.js" onload="__res.adsjs='loaded:'+window.canRunAds" onerror="__res.adsjs='failed'"></script>
<script src="/adsbygoogle.js" onload="__res.abg='loaded:'+(window.adsbygoogle && window.adsbygoogle.loaded)" onerror="__res.abg='failed'"></script>
<script src="/plain-blocked.js" onload="__res.plain='loaded'" onerror="__res.plain='failed'"></script>
<img src="/px.gif" onload="__res.img='loaded'" onerror="__res.img='failed'">)HTML"};
    s.files[QStringLiteral("/lib/adtag.js")] = {"application/javascript", "window.REAL_ADTAG = 1;"};
    s.files[QStringLiteral("/ads.js")] = {"application/javascript", "window.canRunAds = 'real';"};
    s.files[QStringLiteral("/adsbygoogle.js")] = {"application/javascript", "window.REAL_ABG = 1;"};
    s.files[QStringLiteral("/plain-blocked.js")] = {"application/javascript", "window.REAL_PLAIN = 1;"};
    s.files[QStringLiteral("/px.gif")] = {"image/gif", QByteArray::fromBase64("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7")};
    s.files[QStringLiteral("/pip.html")] = {"text/html", R"HTML(<!doctype html><meta charset=utf-8><title>Video page</title>
<style>body{margin:20px;font-family:sans-serif} video{width:320px;height:180px;background:#222}</style>
<body><h1>A page with a video</h1><div id="other">Some text of the page that must be hidden while the video is popped out.</div>
<video id="v" muted autoplay playsinline></video>
<canvas id="c" width="320" height="180" style="display:none"></canvas>
<script>
var c = document.getElementById('c'), x = c.getContext('2d'), n = 0;
setInterval(function () { n++; x.fillStyle = 'hsl(' + (n * 7 % 360) + ',70%,45%)'; x.fillRect(0, 0, 320, 180); x.fillStyle = '#fff'; x.font = '40px sans-serif'; x.fillText('frame ' + n, 60, 100); }, 40);
var v = document.getElementById('v'); window.__video = v;
v.srcObject = c.captureStream(25);
v.play().catch(function () {});
</script>)HTML"};
    s.files[QStringLiteral("/detect.html")] = {"text/html", R"HTML(<!doctype html><meta charset=utf-8><title>Detect</title>
<style>body{margin:0;font-family:sans-serif} #article{padding:20px} .tiny-bait{width:1px;height:1px;position:absolute;left:-9999px}</style>
<script>
window.__res = {};
function makeBait(cls) {
  var b = document.createElement('div'); b.className = cls;
  b.style.cssText = 'width: 1px !important; height: 1px !important; position: absolute !important; left: -10000px !important; top: -1000px !important;';
  document.body.appendChild(b); return b;
}
function looksBlocked(b) { var cs = getComputedStyle(b); return b.offsetParent === null || b.offsetHeight === 0 || b.offsetWidth === 0 || cs.display === 'none' || cs.visibility === 'hidden'; }
</script>
<script src="/ads.js" onload="__res.script = (window.canRunAds === true) ? 'clear' : 'detected'" onerror="__res.script = 'detected'"></script>
<body>
<div id="article"><p id="para">Our review of the best ad blocker extensions for 2026.</p></div>
<div id="realad" class="text-ad">Buy our product now!</div>
<div id="cookie" style="position:fixed;left:0;right:0;bottom:0;height:60px;background:#222;color:#fff;z-index:1000">We use cookies to improve your experience.</div>
<div id="news" style="position:fixed;top:0;left:0;right:0;bottom:0;z-index:99998;background:rgba(0,0,0,.5);color:#fff;display:flex;align-items:center;justify-content:center"><div>Subscribe to our newsletter</div></div>
<script>
// (a) a bait box that is looked at a little later (as BlockAdBlock does)
var bait = makeBait('pub_300x250 pub_300x250m pub_728x90 text-ad textAd text_ad text_ads text-ads text-ad-links');
setTimeout(function () { __res.bait = looksBlocked(bait) ? 'detected' : 'clear'; }, 400);
// (a2) a bait in normal flow that holds only a space, (a3) a bait made tiny by a style sheet instead of an inline style
var bait2 = document.createElement('div'); bait2.className = 'ad-banner'; bait2.innerHTML = '&nbsp;'; document.body.appendChild(bait2);
var bait3 = document.createElement('div'); bait3.className = 'tiny-bait'; document.body.appendChild(bait3);
setTimeout(function () { __res.bait2 = looksBlocked(bait2) ? 'detected' : 'clear'; __res.bait3 = looksBlocked(bait3) ? 'detected' : 'clear'; }, 400);
// (b) a detector library that the page defines itself
(function () {
  var FuckAdBlock = function () { this._d = []; this._n = []; };
  FuckAdBlock.prototype.onDetected = function (f) { this._d.push(f); return this; };
  FuckAdBlock.prototype.onNotDetected = function (f) { this._n.push(f); return this; };
  FuckAdBlock.prototype.check = function () { var self = this, b = makeBait('adsbox'); setTimeout(function () { (looksBlocked(b) ? self._d : self._n).forEach(function (f) { f(); }); }, 150); };
  window.FuckAdBlock = FuckAdBlock;
  if (typeof window.fuckAdBlock === 'undefined') window.fuckAdBlock = new FuckAdBlock();
  fuckAdBlock.onDetected(function () { __res.lib = 'detected'; });
  fuckAdBlock.onNotDetected(function () { __res.lib = 'clear'; });
  fuckAdBlock.check();
})();
// (c) a fetch to an ad address that must "succeed"
fetch('https://pagead2.googlesyndication.com/pagead/js/adsbygoogle.js', { method: 'HEAD', mode: 'no-cors' }).then(function () { __res.fetch = 'clear'; }).catch(function () { __res.fetch = 'detected'; });
// (d) a picture from an ad address
var im = new Image(); im.onload = function () { __res.img = 'clear'; }; im.onerror = function () { __res.img = 'detected'; }; im.src = 'https://ads.tracker.test/banner.gif';
// (e) a wall that appears later and locks the page
setTimeout(function () {
  var o = document.createElement('div'); o.id = 'wall';
  o.style.cssText = 'position:fixed;top:0;left:0;right:0;bottom:0;z-index:99999;background:rgba(0,0,0,.85);color:#fff;display:flex;align-items:center;justify-content:center';
  o.innerHTML = '<div><h2>Ad blocker detected</h2><p>Please disable your ad blocker to continue.</p><button>I have disabled it</button></div>';
  document.body.appendChild(o);
  document.body.style.overflow = 'hidden';
  document.documentElement.style.overflow = 'hidden';
}, 300);
setTimeout(function () {
  var gone = function (id) { var e = document.getElementById(id); return !e || !document.body.contains(e) || getComputedStyle(e).display === 'none'; };
  __res.wallGone = gone('wall');
  __res.scrollLocked = getComputedStyle(document.body).overflow === 'hidden' || getComputedStyle(document.documentElement).overflow === 'hidden';
  __res.cookieKept = !gone('cookie');
  __res.newsKept = !gone('news');
  __res.paraKept = !gone('para');
  __res.realAdHidden = gone('realad');
}, 1300);
</script>)HTML"};
}

bool is(const QJsonObject& o, const char* key, bool value) { return o.value(QLatin1String(key)).toBool(!value) == value; }
QString str(const QJsonObject& o, const char* key) { return o.value(QLatin1String(key)).toString(); }

} // namespace

class TstLab : public QObject {
    Q_OBJECT
private slots:
    void hidingInTheRealEngine()
    {
        struct Case { const char* name; QByteArray list, settings; QStringList extra; bool generic, specific; };
        const QList<Case> cases = {
            {"normal window", kCosmeticList, "{}", {}, true, true},
            {"private window", kCosmeticList, "{}", {QStringLiteral("--private")}, true, true},
            {"$generichide", kCosmeticList + "@@||127.0.0.1^$generichide\n", "{}", {}, false, true},
            {"$elemhide", kCosmeticList + "@@||127.0.0.1^$elemhide\n", "{}", {}, false, false},
            {"cosmetic switched off", kCosmeticList, "{\"cosmeticEnabled\":false}", {}, false, false},
            {"paused on the site", kCosmeticList, "{\"adblockPaused\":[\"127.0.0.1\"]}", {}, false, false},
            {"blocker switched off", kCosmeticList, "{\"adblockEnabled\":false}", {}, false, false},
        };
        for (const Case& c : cases) {
            LabServer server;
            QVERIFY(server.isListening());
            fillPages(server);
            const Result r = runCase(server, c.list, c.settings, c.extra, QStringLiteral("/cosmetic.html"));
            QVERIFY2(r.ok, c.name);
            qInfo().noquote() << "COSMETIC" << c.name << QJsonDocument(r.res).toJson(QJsonDocument::Compact);
            QVERIFY2(is(r.res, "t_plain", false), c.name);                                         // nobody listed it
            QVERIFY2(is(r.res, "t_kept", false), c.name);                                          // the exception keeps it
            QVERIFY2(is(r.res, "adbox", c.generic), c.name);                                       // generic id
            QVERIFY2(is(r.res, "t_class", c.generic), c.name);                                     // generic class
            QVERIFY2(is(r.res, "t_cx", c.generic), c.name);                                        // generic complex selector
            QVERIFY2(is(r.res, "t_dyn", c.generic), c.name);                                       // added by a script after the page loaded
            QVERIFY2(is(r.res, "t_spec", c.specific), c.name);                                     // rule for this site
            QVERIFY2(is(r.res, "t_style", c.specific), c.name);                                    // injected style of this site
        }
    }

    void scriptletsInTheRealEngine()
    {
        struct Case { const char* name; QByteArray settings; QStringList extra; bool on; };
        const QList<Case> cases = {
            {"normal window", "{}", {}, true},
            {"private window", "{}", {QStringLiteral("--private")}, true},
            {"cosmetic switched off", "{\"cosmeticEnabled\":false}", {}, false},
            {"paused on the site", "{\"adblockPaused\":[\"127.0.0.1\"]}", {}, false},
        };
        for (const Case& c : cases) {
            LabServer server;
            QVERIFY(server.isListening());
            fillPages(server);
            const Result r = runCase(server, kScriptletList, c.settings, c.extra, QStringLiteral("/scriptlet.html"));
            QVERIFY2(r.ok, c.name);
            qInfo().noquote() << "SCRIPTLET" << c.name << QJsonDocument(r.res).toJson(QJsonDocument::Compact) << "server hits" << server.hits;
            QCOMPARE(str(r.res, "timerOk"), QStringLiteral("ran"));                                // an ordinary timer always runs
            QCOMPARE(str(r.res, "fetchOther"), QStringLiteral("real:yes"));                        // an ordinary request always works
            QCOMPARE(str(r.res, "aeldOk"), QStringLiteral("fired"));
            QCOMPARE(str(r.res, "acsPlain"), QStringLiteral("read ok"));                           // a script without the needle may read the property
            QVERIFY2(is(r.res, "rcKeep", true), c.name);                                           // only the named class is removed
            if (c.on) {
                QCOMPARE(str(r.res, "aopr"), QStringLiteral("aborted"));
                QCOMPARE(str(r.res, "set"), QStringLiteral("true"));
                QCOMPARE(str(r.res, "json"), QStringLiteral("{\"keep\":2}"));
                QVERIFY2(!r.res.contains(QLatin1String("timer")), c.name);                          // that timer never ran
                QVERIFY2(str(r.res, "fetchTrack").startsWith(QLatin1String("fake:0")), c.name);      // a fake empty answer ...
                QCOMPARE(server.hits.value(QStringLiteral("/api/track")), 0);                       // ... and the server never saw the request
                QCOMPARE(str(r.res, "xhr"), QStringLiteral("load:200:0"));
                QCOMPARE(server.hits.value(QStringLiteral("/api/xhr-track")), 0);
                QVERIFY2(!r.res.contains(QLatin1String("aeldTracking")), c.name);                   // that listener was never added
                QCOMPARE(str(r.res, "acsEvil"), QStringLiteral("aborted"));
                QCOMPARE(str(r.res, "aopw"), QStringLiteral("aborted"));
                QCOMPARE(str(r.res, "storage"), QStringLiteral("accepted"));
                QCOMPARE(str(r.res, "ra"), QStringLiteral("removed"));
                QCOMPARE(str(r.res, "rc"), QStringLiteral("removed"));
            } else {
                QCOMPARE(str(r.res, "aopr"), QStringLiteral("read ok"));
                QCOMPARE(str(r.res, "set"), QStringLiteral("undefined"));
                QCOMPARE(str(r.res, "json"), QStringLiteral("{\"ads\":1,\"keep\":2}"));
                QCOMPARE(str(r.res, "timer"), QStringLiteral("ran"));
                QCOMPARE(str(r.res, "fetchTrack"), QStringLiteral("fake:%1").arg(QByteArray("REAL-TRACK-ANSWER").size()));
                QCOMPARE(server.hits.value(QStringLiteral("/api/track")), 1);
                QCOMPARE(str(r.res, "aeldTracking"), QStringLiteral("fired"));
                QCOMPARE(str(r.res, "acsEvil"), QStringLiteral("read ok"));
                QCOMPARE(str(r.res, "aopw"), QStringLiteral("written"));
                QCOMPARE(str(r.res, "storage"), QStringLiteral("null"));
                QCOMPARE(str(r.res, "ra"), QStringLiteral("kept"));
                QCOMPARE(str(r.res, "rc"), QStringLiteral("kept"));
            }
        }
    }

    void standInsInTheRealEngine()
    {
        {
            LabServer server;
            QVERIFY(server.isListening());
            fillPages(server);
            const Result r = runCase(server, kRedirectList, "{}", {}, QStringLiteral("/redirect.html"));
            QVERIFY(r.ok);
            qInfo().noquote() << "STAND-IN" << QJsonDocument(r.res).toJson(QJsonDocument::Compact) << "server hits" << server.hits;
            QCOMPARE(str(r.res, "adtag"), QStringLiteral("loaded"));                              // $redirect=noopjs: the script "loaded"
            QCOMPARE(str(r.res, "adsjs"), QStringLiteral("loaded:true"));                         // a bait script is answered by the stand-in: canRunAds is true
            QCOMPARE(str(r.res, "abg"), QStringLiteral("loaded:true"));                           // adsbygoogle.js: the global exists and says "loaded"
            QCOMPARE(str(r.res, "plain"), QStringLiteral("failed"));                              // no stand-in known: stopped
            QCOMPARE(str(r.res, "img"), QStringLiteral("loaded"));                                // $redirect=1x1.gif
            for (const char* path : {"/lib/adtag.js", "/ads.js", "/adsbygoogle.js", "/plain-blocked.js", "/px.gif"})
                QVERIFY2(server.hits.value(QString::fromLatin1(path)) == 0, path);                // none of them reached the server
        }
        {
            LabServer server;
            QVERIFY(server.isListening());
            fillPages(server);
            const Result r = runCase(server, kRedirectList, "{\"adblockEnabled\":false}", {}, QStringLiteral("/redirect.html"));
            QVERIFY(r.ok);
            QCOMPARE(str(r.res, "adsjs"), QStringLiteral("loaded:real"));                         // without the blocker the real files come
            QCOMPARE(str(r.res, "plain"), QStringLiteral("loaded"));
            QCOMPARE(server.hits.value(QStringLiteral("/ads.js")), 1);
        }
    }

    // The detectors of plan section 4, each as a page on this computer, once with the defence on and once with it off (the control: it proves that the page really detects).
    void anAdBlockerDetectorFindsNothing()
    {
        const QByteArray list =
            "[Adblock Plus 2.0]\n##.pub_300x250\n##.text-ad\n##.adsbox\n##.ad-banner\n##.tiny-bait\n/ads.js\n||pagead2.googlesyndication.com^\n||ads.tracker.test^\n";
        for (int defend = 1; defend >= 0; --defend) {
            LabServer server;
            QVERIFY(server.isListening());
            fillPages(server);
            const Result r = runCase(server, list, defend ? "{}" : "{\"antiDetectEnabled\":false}", {}, QStringLiteral("/detect.html"));
            QVERIFY(r.ok);
            qInfo().noquote() << "DETECT defence" << (defend ? "ON " : "OFF") << QJsonDocument(r.res).toJson(QJsonDocument::Compact);
            QCOMPARE(str(r.res, "script"), QStringLiteral("clear"));          // the bait script (stand-in)
            QCOMPARE(str(r.res, "fetch"), QStringLiteral("clear"));           // fetch to an ad address
            QCOMPARE(str(r.res, "img"), QStringLiteral("clear"));             // picture from an ad address
            QVERIFY(is(r.res, "cookieKept", true));                           // a cookie notice is not a wall
            QVERIFY(is(r.res, "paraKept", true));                             // text about ad blockers in the page itself stays
            QVERIFY(is(r.res, "newsKept", true));                             // a full-window box that does not talk about ad blockers stays
            QVERIFY2(is(r.res, "realAdHidden", true), "a real ad (a box with content) is still hidden");
            if (defend) {
                QCOMPARE(str(r.res, "bait"), QStringLiteral("clear"));         // the bait box was left alone
                QCOMPARE(str(r.res, "bait2"), QStringLiteral("clear"));        // a bait that holds only a space
                QCOMPARE(str(r.res, "bait3"), QStringLiteral("clear"));        // a bait made tiny by a style sheet
                QCOMPARE(str(r.res, "lib"), QStringLiteral("clear"));          // the library says "no ad blocker"
                QVERIFY(is(r.res, "wallGone", true));                         // the wall is hidden ...
                QVERIFY(is(r.res, "scrollLocked", false));                    // ... and the page scrolls again
            } else {
                QCOMPARE(str(r.res, "bait"), QStringLiteral("detected"));      // control: without the defence the page does detect it
                QCOMPARE(str(r.res, "bait2"), QStringLiteral("detected"));
                QCOMPARE(str(r.res, "bait3"), QStringLiteral("detected"));
                QCOMPARE(str(r.res, "lib"), QStringLiteral("detected"));
                QVERIFY(is(r.res, "wallGone", false));
                QVERIFY(is(r.res, "scrollLocked", true));
            }
        }
    }

    // Picture in picture, in the real program: the tab goes into a small always-on-top window, the video fills it and keeps playing, then everything comes back.
    void pictureInPictureInTheRealEngine()
    {
        for (int privateMode = 0; privateMode <= 1; ++privateMode) {
            LabServer server;
            QVERIFY(server.isListening());
            fillPages(server);
            QTemporaryDir dir;
            const QString data = dir.filePath(QStringLiteral("data"));
            QDir().mkpath(data);
            const QString report = dir.filePath(QStringLiteral("pip.json"));
            QProcess p;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("DV_NO_AUDIO"), QStringLiteral("1"));
            p.setProcessEnvironment(env);
            QStringList args{QStringLiteral("--data-dir"), data, QStringLiteral("--lang"), QStringLiteral("en"), QStringLiteral("--pip-test"), report, QStringLiteral("--wait"), QStringLiteral("3500")};
            if (privateMode)
                args << QStringLiteral("--private");
            args << server.url(QStringLiteral("/pip.html"));
            p.start(QStringLiteral(BROWSER_EXE_PATH), args);
            QElapsedTimer waited;
            waited.start();
            while (p.state() != QProcess::NotRunning && waited.elapsed() < 90000)
                QTest::qWait(50);
            QVERIFY(p.state() == QProcess::NotRunning);
            QFile r(report);
            QVERIFY(r.open(QIODevice::ReadOnly));
            const QByteArray text = r.readAll();
            qInfo().noquote() << "PIP" << (privateMode ? "private" : "normal") << "exit" << p.exitCode() << "checks" << QJsonDocument::fromJson(text).array().size();
            QVERIFY2(p.exitCode() == 0, text.constData());
            QVERIFY2(!text.contains("\"ok\": false") && text.contains("\"ok\": true"), text.constData());
            QVERIFY(QJsonDocument::fromJson(text).array().size() >= 12);
        }
    }

    // Saving videos in the real program: the page loads a video file and an HLS playlist; the program notices both (not the pieces), shows the button, saves both into the
    // downloads list, and refuses a YouTube address. The HLS part needs FFmpeg (found like the program finds it) and is skipped without it.
    void videosOfAPageAreNoticedAndSaved()
    {
        QFile clip(QStringLiteral(TEST_DATA_DIR "/clip.webm"));
        QVERIFY2(clip.open(QIODevice::ReadOnly), "tests/data/clip.webm is missing");
        const QByteArray clipData = clip.readAll();
        QString ffmpeg = qEnvironmentVariable("DV_FFMPEG");
        if (ffmpeg.isEmpty() || !QFileInfo::exists(ffmpeg))
            ffmpeg = QFileInfo(QStringLiteral(DV_TOOLS_FFMPEG)).exists() ? QStringLiteral(DV_TOOLS_FFMPEG) : QString();

        LabServer server;
        QVERIFY(server.isListening());
        fillPages(server);
        server.files[QStringLiteral("/media/clip.webm")] = {"video/webm", clipData};
        QTemporaryDir hls;
        if (!ffmpeg.isEmpty()) {
            QProcess gen;
            gen.setWorkingDirectory(hls.path());
            gen.start(ffmpeg, {QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"), QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                               QStringLiteral("testsrc=size=160x90:rate=10:duration=3"), QStringLiteral("-c:v"), QStringLiteral("libopenh264"), QStringLiteral("-f"), QStringLiteral("hls"),
                               QStringLiteral("-hls_time"), QStringLiteral("1"), QStringLiteral("-hls_list_size"), QStringLiteral("0"), QStringLiteral("-hls_segment_filename"), QStringLiteral("seg%d.ts"),
                               QStringLiteral("index.m3u8")});
            QVERIFY(gen.waitForFinished(60000));
            for (const QString& name : QDir(hls.path()).entryList(QDir::Files)) {
                QFile f(hls.filePath(name));
                QVERIFY(f.open(QIODevice::ReadOnly));
                server.files[QStringLiteral("/media/live/") + name] = {name.endsWith(QLatin1String(".m3u8")) ? "application/vnd.apple.mpegurl" : "video/mp2t", f.readAll()};
            }
        } else {
            server.files[QStringLiteral("/media/live/index.m3u8")] = {"application/vnd.apple.mpegurl", "#EXTM3U\n#EXTINF:1,\nseg0.ts\n#EXT-X-ENDLIST\n"};      // noticed, but no FFmpeg to save it
        }
        server.files[QStringLiteral("/media.html")] = {"text/html", R"HTML(<!doctype html><meta charset=utf-8><title>Media</title>
<video src="/media/clip.webm" muted autoplay loop width="320"></video>
<script>fetch('/media/live/index.m3u8').then(function (r) { return r.text(); }).then(function (t) { window.__res = { m3u8: t.length }; });</script>)HTML"};

        QTemporaryDir dir;
        const QString data = dir.filePath(QStringLiteral("data"));
        QDir().mkpath(data);
        const QString report = dir.filePath(QStringLiteral("media.json"));
        QProcess p;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("DV_NO_AUDIO"), QStringLiteral("1"));
        p.setProcessEnvironment(env);
        p.start(QStringLiteral(BROWSER_EXE_PATH), {QStringLiteral("--data-dir"), data, QStringLiteral("--lang"), QStringLiteral("en"), QStringLiteral("--media-test"), report, QStringLiteral("--wait"), QStringLiteral("4000"),
                                                   server.url(QStringLiteral("/media.html"))});
        QElapsedTimer waited;
        waited.start();
        while (p.state() != QProcess::NotRunning && waited.elapsed() < 120000)
            QTest::qWait(50);
        QVERIFY(p.state() == QProcess::NotRunning);
        QFile r(report);
        QVERIFY(r.open(QIODevice::ReadOnly));
        const QByteArray text = r.readAll();
        qInfo().noquote() << "MEDIA exit" << p.exitCode() << "ffmpeg" << (ffmpeg.isEmpty() ? "no" : "yes") << "checks" << QJsonDocument::fromJson(text).array().size();
        QVERIFY2(p.exitCode() == 0, text.constData());
        QVERIFY2(!text.contains("\"ok\": false") && text.contains("\"ok\": true"), text.constData());
        QVERIFY(QDir(data + QStringLiteral("/downloads")).entryList(QStringList{QStringLiteral("clip*.webm")}, QDir::Files).size() == 1);
        if (!ffmpeg.isEmpty())
            QVERIFY(QDir(data + QStringLiteral("/downloads")).entryList(QStringList{QStringLiteral("*.mp4")}, QDir::Files).size() == 1);
        QVERIFY2(QDir(data + QStringLiteral("/downloads")).entryList(QStringList{QStringLiteral("*.part")}, QDir::Files).isEmpty(), "no half file is left");
    }
};

QTEST_MAIN(TstLab)
#include "tst_lab.moc"
