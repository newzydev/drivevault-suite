#include "Cosmetic.h"
#include "FilterParser.h"
#include "ListManager.h"
#include "PageScript.h"
#include "StandIns.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace dv::br::ab;

namespace {
void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}
} // namespace

// Filters that act inside the page: hiding, scriptlets, exceptions, site switches, the script that carries them, and the stand-in files.
class TstCosmetic : public QObject {
    Q_OBJECT
private slots:
    void cosmeticLinesAreClassified_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<int>("kind");          // 0 = accepted, 1 = unsupported
        QTest::newRow("generic class") << "##.ad-box" << 0;
        QTest::newRow("generic id") << "###banner" << 0;
        QTest::newRow("specific") << "example.com,~sub.example.com##div.promo" << 0;
        QTest::newRow("entity") << "ticketmaster.*##.cookie" << 0;
        QTest::newRow("exception") << "example.com#@#.ad-box" << 0;
        QTest::newRow("has") << "example.com##div:has(> a[href*=\"/ad/\"])" << 0;
        QTest::newRow("scriptlet") << "example.com##+js(abort-on-property-read, adsbygoogle)" << 0;
        QTest::newRow("scriptlet alias") << "example.com##+js(aopr, x)" << 0;
        QTest::newRow("scriptlet escaped comma") << "example.com##+js(nostif, a\\,b)" << 0;
        QTest::newRow("scriptlet exception all") << "example.com#@#+js()" << 0;
        QTest::newRow("style injection") << "example.com#$#.sticky { position: static !important }" << 0;
        QTest::newRow("abp snippet") << "example.com#$#abort-on-property-read cookieman" << 0;
        QTest::newRow("unknown scriptlet") << "example.com##+js(some-new-thing, a)" << 1;
        QTest::newRow("procedural has-text") << "example.com##div:has-text(Advert)" << 1;
        QTest::newRow("procedural abp") << "example.com##div:-abp-has(.ad)" << 1;
        QTest::newRow("html filter") << "example.com##^script:has-text(ads)" << 1;
        QTest::newRow("extended") << "example.com#?#div:has(.ad)" << 1;
        QTest::newRow("style with url") << "example.com#$#.x { background: url(http://evil.test/a.png) }" << 1;
        QTest::newRow("regex domain") << "/ex.*/##.ad" << 1;
        QTest::newRow("snippet several") << "example.com#$#abort-on-property-read a; abort-on-property-write b" << 1;
    }
    void cosmeticLinesAreClassified()
    {
        QFETCH(QString, line);
        QFETCH(int, kind);
        CosmeticRule r;
        QCOMPARE(parseCosmetic(line, r) == ParseKind::Cosmetic ? 0 : 1, kind);
    }

    void scriptletArgumentsAreSplit()
    {
        CosmeticRule r;
        QCOMPARE(parseCosmetic(QStringLiteral("a.com##+js(acs, document.getElementsByTagName, gtm.js)"), r), ParseKind::Cosmetic);
        QCOMPARE(r.name, QStringLiteral("abort-current-script"));
        QCOMPARE(r.args, (QStringList{QStringLiteral("document.getElementsByTagName"), QStringLiteral("gtm.js")}));
        QCOMPARE(parseCosmetic(QStringLiteral("a.com##+js(nostif, a\\,b, 1000)"), r), ParseKind::Cosmetic);
        QCOMPARE(r.args, (QStringList{QStringLiteral("a,b"), QStringLiteral("1000")}));
        QCOMPARE(parseCosmetic(QStringLiteral("a.com#$#aopr cookieman"), r), ParseKind::Cosmetic);
        QCOMPARE(r.name, QStringLiteral("abort-on-property-read"));
        QCOMPARE(r.args, QStringList{QStringLiteral("cookieman")});
        QCOMPARE(canonicalScriptlet(QStringLiteral("set")), QStringLiteral("set-constant"));
        QVERIFY(canonicalScriptlet(QStringLiteral("nope")).isEmpty());
        QVERIFY(implementedScriptlets().size() >= 15);
    }

    void aPageGetsItsOwnCosmeticRules()
    {
        CosmeticStore s;
        auto add = [&](const QString& line, int list = 0) {
            CosmeticRule r;
            QVERIFY2(parseCosmetic(line, r) == ParseKind::Cosmetic, qPrintable(line));
            r.list = quint8(list);
            s.add(r);
        };
        add(QStringLiteral("##.ad-box"));                                     // generic name
        add(QStringLiteral("###top-banner"));
        add(QStringLiteral("##div[data-ad]"));                                // generic complex
        add(QStringLiteral("news.test##.promo"));
        add(QStringLiteral("news.test,shop.test##.newsletter"));
        add(QStringLiteral("~shop.test##.everywhere-but-shop"));
        add(QStringLiteral("ticketmaster.*##.tm-only"));
        add(QStringLiteral("news.test#@#.promo"));                            // exception for the specific rule
        add(QStringLiteral("shop.test#@#.ad-box"));                           // exception for a generic name
        add(QStringLiteral("news.test##+js(set, window.x, true)"));
        add(QStringLiteral("shop.test#@#+js()"));
        add(QStringLiteral("shop.test##+js(set, window.y, false)"));
        add(QStringLiteral("~skip.test##.from-list-1"), 1);                  // a rule of the second list
        s.finalize();
        const quint32 all = 0xFFFFFFFFu;

        const PageCosmetics news = s.forPage(QStringLiteral("www.news.test"), all);
        QVERIFY2(!news.selectors.contains(QStringLiteral(".promo")), "the exception removes it");
        QVERIFY(news.selectors.contains(QStringLiteral(".newsletter")));
        QVERIFY(news.selectors.contains(QStringLiteral("div[data-ad]")));
        QVERIFY(news.selectors.contains(QStringLiteral(".everywhere-but-shop")));
        QVERIFY(news.selectors.contains(QStringLiteral(".from-list-1")));
        QVERIFY(!news.selectors.contains(QStringLiteral(".ad-box")));         // generic names are not in the page list: the page script finds them
        QCOMPARE(news.scriptlets.size(), 1);
        QCOMPARE(news.scriptlets.at(0).name, QStringLiteral("set-constant"));

        const PageCosmetics shop = s.forPage(QStringLiteral("shop.test"), all);
        QVERIFY(shop.selectors.contains(QStringLiteral(".newsletter")));
        QVERIFY(!shop.selectors.contains(QStringLiteral(".everywhere-but-shop")));
        QVERIFY(shop.genericOff.contains(QStringLiteral(".ad-box")));
        QCOMPARE(shop.scriptlets.size(), 0);                                  // "#@#+js()" switched them all off

        QVERIFY(s.forPage(QStringLiteral("www.ticketmaster.co.uk"), all).selectors.contains(QStringLiteral(".tm-only")));
        QVERIFY(s.forPage(QStringLiteral("ticketmaster.com"), all).selectors.contains(QStringLiteral(".tm-only")));
        QVERIFY(!s.forPage(QStringLiteral("notticketmaster.com"), all).selectors.contains(QStringLiteral(".tm-only")));
        QVERIFY(!s.forPage(QStringLiteral("example.org"), all).selectors.contains(QStringLiteral(".promo")));
        QVERIFY(!s.forPage(QStringLiteral("example.org"), 0b01).selectors.contains(QStringLiteral(".from-list-1")));
        QVERIFY(s.genericClasses(all).split(QLatin1Char(' ')).contains(QStringLiteral("ad-box")));
        QVERIFY(s.genericIds(all).split(QLatin1Char(' ')).contains(QStringLiteral("top-banner")));
        QVERIFY(!s.genericClasses(0).contains(QLatin1String("ad-box")));
    }

    void elemhideAndGenerichideAreSiteSwitches()
    {
        NetRule r;
        QCOMPARE(parseLine(QStringLiteral("@@||friendly.test^$generichide"), r), ParseKind::CosmeticSite);
        QCOMPARE(r.cosmeticOff, quint8(1));
        QCOMPARE(parseLine(QStringLiteral("@@||quiet.test^$elemhide"), r), ParseKind::CosmeticSite);
        QCOMPARE(r.cosmeticOff, quint8(2));
        QCOMPARE(parseLine(QStringLiteral("||x.test^$elemhide"), r), ParseKind::Unsupported);       // only an exception means something
    }

    void aPageScriptCarriesTheRules()
    {
        PageCosmetics p;
        p.selectors << QStringLiteral(".a") << QStringLiteral("div[data-x=\"1\"]");
        p.styles << QStringLiteral(".s { display: none !important }");
        p.scriptlets.append({QStringLiteral("set-constant"), {QStringLiteral("window.x"), QStringLiteral("true")}});
        p.genericOff << QStringLiteral(".off");
        const QString js = buildPageScript(QStringLiteral("/*LIB*/"), p, QStringLiteral("ad-box promo"), QStringLiteral("top"));
        QVERIFY(js.startsWith(QLatin1String("(function(){var CFG={")));
        QVERIFY(js.contains(QLatin1String("/*LIB*/")));
        QVERIFY(js.contains(QLatin1String("\"cls\":\"ad-box promo\"")));
        QVERIFY(js.contains(QLatin1String("\"gen\":true")));
        QVERIFY(js.contains(QLatin1String("[\"set-constant\",[\"window.x\",\"true\"]]")));
        QVERIFY(js.contains(QLatin1String("div[data-x=\\\"1\\\"]")));
        p.noGeneric = true;
        QVERIFY(buildPageScript(QStringLiteral("L"), p, QStringLiteral("a"), QStringLiteral("b")).contains(QLatin1String("\"gen\":false")));
        p.noCosmetic = true;
        const QString none = buildPageScript(QStringLiteral("L"), p, QStringLiteral("a"), QStringLiteral("b"));
        QVERIFY2(none.contains(QLatin1String("\"sel\":[]")) && none.contains(QLatin1String("set-constant")), "$elemhide keeps the scriptlets but hides nothing");
        QVERIFY(buildPageScript(QStringLiteral("L"), PageCosmetics(), QString(), QString()).isEmpty());
    }

    void standInsAreHarmlessAndKnown()
    {
        StandIn s;
        QVERIFY(standIn(QStringLiteral("noopjs"), &s) && s.mime == "text/javascript");
        QVERIFY(standIn(QStringLiteral("NOOP.JS"), &s));
        QVERIFY(standIn(QStringLiteral("1x1.gif"), &s) && s.mime == "image/gif" && s.body.startsWith("GIF8"));
        QVERIFY(standIn(QStringLiteral("2x2.png"), &s) && s.body.startsWith("\x89PNG"));
        QVERIFY(standIn(QStringLiteral("googletagmanager_gtm.js"), &s) && s.body.contains("dataLayer"));
        QVERIFY(standIn(QStringLiteral("googlesyndication_adsbygoogle.js"), &s) && s.body.contains("adsbygoogle"));
        QVERIFY(!standIn(QStringLiteral("does-not-exist.js"), &s));
        for (const QString& name : standInNames()) {
            QVERIFY(standIn(name, &s));
            QVERIFY2(!s.body.contains("http://") && !s.body.contains("https://") && !s.body.contains("XMLHttpRequest") && !s.body.contains("fetch("), qPrintable(name));      // a stand-in never calls out
        }
        QCOMPARE(standInNameForUrl(QStringLiteral("https://pagead2.googlesyndication.com/pagead/js/adsbygoogle.js?client=ca-pub-1"), Script), QStringLiteral("googlesyndication_adsbygoogle.js"));
        QCOMPARE(standInNameForUrl(QStringLiteral("https://www.google-analytics.com/analytics.js"), Script), QStringLiteral("google-analytics_analytics.js"));
        QCOMPARE(standInNameForUrl(QStringLiteral("https://www.googletagmanager.com/gtm.js?id=GTM-X"), Script), QStringLiteral("googletagmanager_gtm.js"));
        QCOMPARE(standInNameForUrl(QStringLiteral("https://securepubads.g.doubleclick.net/tag/js/gpt.js"), Script), QStringLiteral("googletagservices_gpt.js"));
        QCOMPARE(standInNameForUrl(QStringLiteral("https://connect.facebook.net/en_US/fbevents.js"), Script), QStringLiteral("fbevents.js"));
        QCOMPARE(standInNameForUrl(QStringLiteral("https://site.test/js/ads.js"), Script), QStringLiteral("canrunads.js"));
        QVERIFY(standInNameForUrl(QStringLiteral("https://site.test/js/app.js"), Script).isEmpty());
        QVERIFY(standInNameForUrl(QStringLiteral("https://www.google-analytics.com/analytics.js"), Image).isEmpty());          // only scripts
    }

    void cosmeticRulesComeThroughTheListManager()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("data/filters/a.txt")),
                  "[Adblock Plus 2.0]\n##.ad-box\nnews.test##.promo\nnews.test##+js(aopr, x)\nnews.test##div:has-text(x)\n@@||quiet.test^$elemhide\n@@||calm.test^$generichide\n||trk.test^\n");
        ListManager m(dir.filePath(QStringLiteral("data")));
        m.setCatalog({{QStringLiteral("a"), QStringLiteral("A"), QStringLiteral("ads"), QStringLiteral("https://x/a"), QStringLiteral("t")}});
        m.setEnabled({QStringLiteral("a")});
        QSignalSpy spy(&m, &ListManager::reloaded);
        m.reload();
        QVERIFY(spy.wait(10000));
        const QVariantMap st = m.status().at(0).toMap();
        QCOMPARE(st.value(QStringLiteral("rules")).toInt(), 1);
        QCOMPARE(st.value(QStringLiteral("cosmetic")).toInt(), 2);
        QCOMPARE(st.value(QStringLiteral("scriptlets")).toInt(), 1);
        QCOMPARE(st.value(QStringLiteral("unsupported")).toInt(), 1);
        const PageCosmetics p = m.cosmeticsFor(QStringLiteral("https://www.news.test/a/b"));
        QVERIFY(p.selectors.contains(QStringLiteral(".promo")) && p.scriptlets.size() == 1 && !p.noGeneric && !p.noCosmetic);
        QVERIFY(m.cosmeticsFor(QStringLiteral("https://quiet.test/")).noCosmetic);
        QVERIFY(m.cosmeticsFor(QStringLiteral("https://quiet.test/")).noGeneric);
        QVERIFY(m.cosmeticsFor(QStringLiteral("https://calm.test/")).noGeneric);
        QVERIFY(!m.cosmeticsFor(QStringLiteral("https://calm.test/")).noCosmetic);
        QVERIFY(m.cosmeticsFor(QStringLiteral("file:///C:/x.html")).empty());                        // only web pages
        const QString lib = QStringLiteral("/*LIB*/");
        QVERIFY(m.pageScriptFor(lib, QStringLiteral("https://www.news.test/")).contains(QLatin1String("\"cls\":\"ad-box\"")));
        QVERIFY(!m.pageScriptFor(lib, QStringLiteral("https://calm.test/")).contains(QLatin1String("\"cls\"")));        // $generichide: the generic names are not sent
        QVERIFY(m.pageScriptFor(lib, QStringLiteral("ftp://x/")).isEmpty());
        QVERIFY(m.match(QStringLiteral("https://trk.test/x"), QString(), Script).blocked);
        m.setEnabled({});
        QVERIFY(m.pageScriptFor(lib, QStringLiteral("https://www.news.test/")).isEmpty());
        QCOMPARE(m.cosmeticRuleCount(), 0);
    }

    // ---- the real lists (opt in: DV_TEST_FILTER_DATA) ---------------------------------------------------------------------------------------
    void realListsGiveCosmeticRules()
    {
        const QString data = qEnvironmentVariable("DV_TEST_FILTER_DATA");
        if (data.isEmpty())
            QSKIP("set DV_TEST_FILTER_DATA to a data folder with downloaded lists");
        ListManager m(data);
        QStringList ids;
        for (const ListDef& d : m.catalog())
            ids << d.id;
        m.setEnabled(ids);
        QSignalSpy spy(&m, &ListManager::reloaded);
        QElapsedTimer t;
        t.start();
        m.reload();
        QVERIFY(spy.wait(60000));
        const qint64 loadMs = t.elapsed();
        int cosmetic = 0, scriptlets = 0, unsupported = 0;
        for (const QVariant& v : m.status()) {
            cosmetic += v.toMap().value(QStringLiteral("cosmetic")).toInt();
            scriptlets += v.toMap().value(QStringLiteral("scriptlets")).toInt();
            unsupported += v.toMap().value(QStringLiteral("unsupported")).toInt();
        }
        const QString lib = QStringLiteral("/*LIB*/");
        t.restart();
        int total = 0, with = 0;
        qint64 bytes = 0;
        for (const char* url : {"https://www.nytimes.com/", "https://www.bbc.com/news", "https://edition.cnn.com/", "https://www.espn.com/", "https://www.reddit.com/",
                                "https://www.twitch.tv/", "https://www.imdb.com/", "https://stackoverflow.com/questions", "https://www.msn.com/", "https://sanook.com/"}) {
            const QString js = m.pageScriptFor(lib, QString::fromLatin1(url));
            ++total;
            if (!js.isEmpty())
                ++with;
            bytes += js.size();
        }
        const double perPageMs = double(t.nsecsElapsed()) / 1e6 / total;
        qInfo().noquote() << QStringLiteral("COSMETIC real lists: loaded in %1 ms; %2 page rules + %3 scriptlets accepted, %4 lines unsupported (network + page); "
                                            "page script for 10 sites: %5 ms each, %6 KB each on average, %7 of 10 non-empty")
                                 .arg(loadMs).arg(cosmetic).arg(scriptlets).arg(unsupported).arg(perPageMs, 0, 'f', 2).arg(bytes / total / 1024).arg(with);
        QVERIFY(cosmetic > 50000);
        QVERIFY(scriptlets > 10);
        QVERIFY2(perPageMs < 20.0, "composing the script of a page took longer than 20 ms");
        QVERIFY(!m.cosmeticsFor(QStringLiteral("https://www.espn.com/")).scriptlets.isEmpty());       // EasyPrivacy has set-constant for espn.com
    }
};

QTEST_MAIN(TstCosmetic)
#include "tst_cosmetic.moc"
