#include <QDir>
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
};

QTEST_MAIN(TstLab)
#include "tst_lab.moc"
