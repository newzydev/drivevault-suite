#include "FilterParser.h"
#include "ListManager.h"
#include "RuleSet.h"

#include <QElapsedTimer>
#include <QFile>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace dv::br::ab;

namespace {

RuleSet makeSet(const QStringList& lines, quint32* unsupported = nullptr)
{
    RuleSet set;
    for (const QString& l : lines) {
        NetRule r;
        const ParseKind k = parseLine(l, r);
        if (k == ParseKind::Network)
            set.add(r);
        else if (k == ParseKind::Unsupported && unsupported)
            ++*unsupported;
    }
    set.finalize();
    return set;
}

bool blocked(const RuleSet& set, const QString& url, const QString& source = QString(), quint16 type = Script)
{
    return set.match(url, source, type, 0xFFFFFFFFu).blocked;
}

void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

// a tiny HTTP server on this computer: answers every request with the given status and body
class FakeHttp : public QTcpServer {
public:
    int status = 200;
    QByteArray body;
    int requests = 0;
    FakeHttp() { listen(QHostAddress::LocalHost); connect(this, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket* s = nextPendingConnection()) {
            connect(s, &QTcpSocket::readyRead, s, [this, s] {
                s->readAll();
                ++requests;
                s->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                s->disconnectFromHost();
            });
        }
    }); }
    QString url(const QString& path) const { return QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path); }
};

} // namespace

class TstAdblock : public QObject {
    Q_OBJECT
private slots:
    // ---- parser ----------------------------------------------------------------------------------------------------
    void linesAreClassified_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<int>("kind");
        QTest::newRow("comment") << "! Title: EasyList" << int(ParseKind::Comment);
        QTest::newRow("header") << "[Adblock Plus 2.0]" << int(ParseKind::Comment);
        QTest::newRow("empty") << "   " << int(ParseKind::Comment);
        QTest::newRow("host rule") << "||ads.example.com^" << int(ParseKind::Network);
        QTest::newRow("exception") << "@@||example.com/ok.js" << int(ParseKind::Network);
        QTest::newRow("with options") << "||x.com^$script,third-party,domain=a.com|~b.a.com" << int(ParseKind::Network);
        QTest::newRow("redirect") << "||ads.js^$script,redirect=noopjs" << int(ParseKind::Network);
        QTest::newRow("cosmetic") << "example.com##.banner" << int(ParseKind::Cosmetic);
        QTest::newRow("generic cosmetic") << "##.ad-box" << int(ParseKind::Cosmetic);
        QTest::newRow("cosmetic exception") << "example.com#@#.banner" << int(ParseKind::Cosmetic);
        QTest::newRow("scriptlet") << "example.com##+js(abort-on-property-read, x)" << int(ParseKind::Cosmetic);
        QTest::newRow("regex") << "/banner[0-9]+/" << int(ParseKind::Unsupported);
        QTest::newRow("removeparam") << "||x.com^$removeparam=utm" << int(ParseKind::Unsupported);
        QTest::newRow("csp") << "||x.com^$csp=script-src 'none'" << int(ParseKind::Unsupported);
        QTest::newRow("popup") << "||x.com^$popup" << int(ParseKind::Unsupported);
        QTest::newRow("unknown option") << "||x.com^$somethingnew" << int(ParseKind::Unsupported);
        QTest::newRow("too broad") << "$third-party" << int(ParseKind::Unsupported);
        QTest::newRow("dollar in url") << "/path$value/x" << int(ParseKind::Network);
    }
    void linesAreClassified()
    {
        QFETCH(QString, line);
        QFETCH(int, kind);
        NetRule r;
        QCOMPARE(int(parseLine(line, r)), kind);
    }
    void optionsAreParsed()
    {
        NetRule r;
        QCOMPARE(parseLine(QStringLiteral("@@||x.com^$script,~image,third-party,important,domain=a.com|~b.a.com,match-case"), r), ParseKind::Network);
        QVERIFY(r.exception && r.important && r.matchCase && r.hostAnchor);
        QCOMPARE(r.thirdParty, qint8(1));
        QCOMPARE(r.types, quint16(Script));
        QCOMPARE(r.domainsInc, QStringList{QStringLiteral("a.com")});
        QCOMPARE(r.domainsExc, QStringList{QStringLiteral("b.a.com")});
        QCOMPARE(r.hostKey, QStringLiteral("x.com"));
        QCOMPARE(parseLine(QStringLiteral("|http://a.b/c|"), r), ParseKind::Network);
        QVERIFY(r.startAnchor && r.endAnchor && !r.hostAnchor);
        QCOMPARE(parseLine(QStringLiteral("/ads/*/x$~script"), r), ParseKind::Network);
        QCOMPARE(r.types, quint16(kDefaultTypes & ~Script));
        QCOMPARE(parseLine(QStringLiteral("||a.com^$redirect=noopjs"), r), ParseKind::Network);
        QCOMPARE(r.redirect, QStringLiteral("noopjs"));
        QCOMPARE(parseLine(QStringLiteral("||Ads.Example.COM^"), r), ParseKind::Network);
        QCOMPARE(r.hostKey, QStringLiteral("ads.example.com"));
    }

    // ---- matching --------------------------------------------------------------------------------------------------
    void hostRulesMatchTheHostAndItsSubdomainsOnly()
    {
        const RuleSet s = makeSet({QStringLiteral("||ads.example.com^")});
        QVERIFY(blocked(s, QStringLiteral("http://ads.example.com/x.js")));
        QVERIFY(blocked(s, QStringLiteral("https://ADS.Example.com:8443/x.js?a=1")));
        QVERIFY(blocked(s, QStringLiteral("http://sub.ads.example.com/a")));
        QVERIFY(blocked(s, QStringLiteral("http://user:pw@ads.example.com/a")));
        QVERIFY(!blocked(s, QStringLiteral("http://example.com/ads/x.js")));
        QVERIFY(!blocked(s, QStringLiteral("http://notads.example.com/x.js")));
        QVERIFY(!blocked(s, QStringLiteral("http://ads.example.com.evil.com/x.js")));
        QVERIFY(!blocked(s, QStringLiteral("http://evil.com/?u=ads.example.com")));
    }
    void patternsWithWildcardsSeparatorsAndAnchors()
    {
        const RuleSet s = makeSet({QStringLiteral("/banner/*/img^"), QStringLiteral("|http://ad."), QStringLiteral(".swf|"), QStringLiteral("/adserver/*")});
        QVERIFY(blocked(s, QStringLiteral("http://x.com/banner/a/img?x=1")));
        QVERIFY(blocked(s, QStringLiteral("http://x.com/banner/a/b/img")));
        QVERIFY(!blocked(s, QStringLiteral("http://x.com/banner/a/imgs")));
        QVERIFY(blocked(s, QStringLiteral("http://ad.example.com/x")));
        QVERIFY(!blocked(s, QStringLiteral("https://ad.example.com/x")));
        QVERIFY(!blocked(s, QStringLiteral("http://x.com/?r=http://ad.example.com/")));
        QVERIFY(blocked(s, QStringLiteral("http://x.com/a/b.swf")));
        QVERIFY(!blocked(s, QStringLiteral("http://x.com/a/b.swf?x=1")));
        QVERIFY(blocked(s, QStringLiteral("http://x.com/adserver/foo/bar")));
        QVERIFY(!blocked(s, QStringLiteral("http://x.com/adservers/foo")));
    }
    void typesAndThirdPartyAreHonoured()
    {
        const RuleSet s = makeSet({QStringLiteral("||t.example^$script"), QStringLiteral("||pix.example^$image,third-party"), QStringLiteral("||own.example^$~script")});
        QVERIFY(blocked(s, QStringLiteral("http://t.example/a"), QString(), Script));
        QVERIFY(!blocked(s, QStringLiteral("http://t.example/a"), QString(), Image));
        QVERIFY(blocked(s, QStringLiteral("http://pix.example/a.gif"), QStringLiteral("http://news.test/"), Image));
        QVERIFY2(!blocked(s, QStringLiteral("http://pix.example/a.gif"), QStringLiteral("http://www.pix.example/"), Image), "same site is first party");
        QVERIFY(!blocked(s, QStringLiteral("http://pix.example/a.gif"), QString(), Image));       // typed address: no source, not third party
        QVERIFY(!blocked(s, QStringLiteral("http://own.example/a.js"), QString(), Script));
        QVERIFY(blocked(s, QStringLiteral("http://own.example/a.png"), QString(), Image));
        QVERIFY(!blocked(s, QStringLiteral("http://t.example/"), QString(), Document));          // the page itself is only blocked by $document
    }
    void domainOptionLimitsTheSite()
    {
        const RuleSet s = makeSet({QStringLiteral("||trk.example^$domain=a.test|~sub.a.test")});
        QVERIFY(blocked(s, QStringLiteral("http://trk.example/x"), QStringLiteral("http://a.test/page")));
        QVERIFY(blocked(s, QStringLiteral("http://trk.example/x"), QStringLiteral("http://www.a.test/page")));
        QVERIFY(!blocked(s, QStringLiteral("http://trk.example/x"), QStringLiteral("http://sub.a.test/page")));
        QVERIFY(!blocked(s, QStringLiteral("http://trk.example/x"), QStringLiteral("http://b.test/page")));
        QVERIFY(!blocked(s, QStringLiteral("http://trk.example/x"), QString()));
    }
    void exceptionsAndImportantFilters()
    {
        const RuleSet s = makeSet({QStringLiteral("||example.com/ads/"), QStringLiteral("@@||example.com/ads/ok.js"), QStringLiteral("||bad.test^$important"), QStringLiteral("@@||bad.test^"),
                                   QStringLiteral("||worse.test^$important"), QStringLiteral("@@||worse.test^$important")});
        QVERIFY(blocked(s, QStringLiteral("http://example.com/ads/a.js")));
        QVERIFY(!blocked(s, QStringLiteral("http://example.com/ads/ok.js")));
        QVERIFY2(blocked(s, QStringLiteral("http://bad.test/x")), "$important beats a normal exception");
        QVERIFY2(!blocked(s, QStringLiteral("http://worse.test/x")), "an $important exception beats it");
        const auto d = s.match(QStringLiteral("http://example.com/ads/ok.js"), QString(), Script, 0xFFFFFFFFu);
        QVERIFY(!d.blocked && d.exception && d.exception->raw == QLatin1String("@@||example.com/ads/ok.js"));
    }
    void aSiteCanBeLetThroughWithDocumentException()
    {
        const RuleSet s = makeSet({QStringLiteral("||ads.example^"), QStringLiteral("@@||friendly.test^$document")});
        const quint32 all = 0xFFFFFFFFu;
        QVERIFY(!s.siteExempt(QStringLiteral("http://news.test/"), all));
        QVERIFY(s.siteExempt(QStringLiteral("http://www.friendly.test/page"), all));
        QVERIFY(!s.siteExempt(QStringLiteral("http://notfriendly.test/"), all));
    }
    void matchCaseOnlyWhenAsked()
    {
        const RuleSet s = makeSet({QStringLiteral("/AdBanner.js$match-case"), QStringLiteral("/Other.JS")});
        QVERIFY(blocked(s, QStringLiteral("http://x.com/AdBanner.js")));
        QVERIFY(!blocked(s, QStringLiteral("http://x.com/adbanner.js")));
        QVERIFY(blocked(s, QStringLiteral("http://x.com/other.js")));
        QVERIFY(blocked(s, QStringLiteral("http://x.com/OTHER.js")));
    }
    void listsCanBeSwitchedOff()
    {
        RuleSet set;
        for (int list = 0; list < 2; ++list) {
            NetRule r;
            QVERIFY(parseLine(list == 0 ? QStringLiteral("||one.test^") : QStringLiteral("||two.test^"), r) == ParseKind::Network);
            r.list = quint8(list);
            set.add(r);
        }
        set.finalize();
        QVERIFY(set.match(QStringLiteral("http://one.test/"), QString(), Script, 0b01).blocked);
        QVERIFY(!set.match(QStringLiteral("http://two.test/"), QString(), Script, 0b01).blocked);
        QVERIFY(set.match(QStringLiteral("http://two.test/"), QString(), Script, 0b10).blocked);
        QVERIFY(!set.match(QStringLiteral("http://one.test/"), QString(), Script, 0).blocked);
        QCOMPARE(set.ruleCountOfList(0), 1);
    }
    void siteNamesAreReducedToTheRegistrableDomain()
    {
        QCOMPARE(RuleSet::registrableDomain(QStringLiteral("www.example.com")), QStringLiteral("example.com"));
        QCOMPARE(RuleSet::registrableDomain(QStringLiteral("a.b.example.co.th")), QStringLiteral("example.co.th"));
        QCOMPARE(RuleSet::registrableDomain(QStringLiteral("psu.ac.th")), QStringLiteral("psu.ac.th"));
        QCOMPARE(RuleSet::registrableDomain(QStringLiteral("localhost")), QStringLiteral("localhost"));
        QCOMPARE(RuleSet::hostOf(QStringLiteral("https://user@host.example:8080/a?b#c")), QStringLiteral("host.example"));
        QCOMPARE(RuleSet::hostOf(QStringLiteral("http://[::1]:80/x")), QStringLiteral("[::1]"));
    }

    // ---- the lists on disk ---------------------------------------------------------------------------------------------------
    void listsAreLoadedAndSwitched()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("data/filters/a.txt")), "[Adblock Plus 2.0]\n! Title\n||one.test^\n##.cosmetic\n/regex/\n||x.test^$removeparam\n");
        writeFile(dir.filePath(QStringLiteral("shipped/b.txt")), "! snapshot\n||two.test^\n");
        ListManager m(dir.filePath(QStringLiteral("data")), dir.filePath(QStringLiteral("shipped")));
        m.setCatalog({{QStringLiteral("a"), QStringLiteral("A"), QStringLiteral("ads"), QStringLiteral("https://x/a"), QStringLiteral("t")},
                      {QStringLiteral("b"), QStringLiteral("B"), QStringLiteral("tracking"), QStringLiteral("https://x/b"), QStringLiteral("t")},
                      {QStringLiteral("c"), QStringLiteral("C"), QStringLiteral("social"), QStringLiteral("https://x/c"), QStringLiteral("t")}});
        m.setEnabled({QStringLiteral("a"), QStringLiteral("b")});
        QSignalSpy spy(&m, &ListManager::reloaded);
        m.reload();
        QVERIFY(spy.wait(10000));
        const QVariantList st = m.status();
        QCOMPARE(st.at(0).toMap().value(QStringLiteral("rules")).toInt(), 1);
        QCOMPARE(st.at(0).toMap().value(QStringLiteral("cosmetic")).toInt(), 1);
        QCOMPARE(st.at(0).toMap().value(QStringLiteral("unsupported")).toInt(), 2);
        QVERIFY(st.at(0).toMap().value(QStringLiteral("present")).toBool());
        QVERIFY2(st.at(1).toMap().value(QStringLiteral("present")).toBool(), "a snapshot next to the program is used when the data folder has none");
        QVERIFY(!st.at(2).toMap().value(QStringLiteral("present")).toBool());
        QVERIFY(m.match(QStringLiteral("http://one.test/x"), QString(), Script).blocked);
        QCOMPARE(m.match(QStringLiteral("http://one.test/x"), QString(), Script).listId, QStringLiteral("a"));
        QVERIFY(m.match(QStringLiteral("http://two.test/x"), QString(), Script).blocked);
        m.setEnabled({QStringLiteral("b")});
        QVERIFY(!m.match(QStringLiteral("http://one.test/x"), QString(), Script).blocked);
        QVERIFY(m.match(QStringLiteral("http://two.test/x"), QString(), Script).blocked);
        m.setEnabled({});
        QVERIFY(!m.match(QStringLiteral("http://two.test/x"), QString(), Script).blocked);
        QCOMPARE(m.totalRules(), 0);
    }
    void updateDownloadsOnlyWhenAskedAndChecksTheAnswer()
    {
        QTemporaryDir dir;
        FakeHttp server;
        QVERIFY(server.isListening());
        ListManager m(dir.filePath(QStringLiteral("data")));
        m.setAllowInsecureForTests(true);
        m.setCatalog({{QStringLiteral("good"), QStringLiteral("Good"), QStringLiteral("ads"), server.url(QStringLiteral("/good.txt")), QStringLiteral("t")}});
        m.setEnabled({QStringLiteral("good")});
        QTest::qWait(300);
        QCOMPARE(server.requests, 0);                                  // nothing is read before the button

        server.body = "[Adblock Plus 2.0]\n! Title: Good\n||ads.test^\n||trk.test^\n";
        QSignalSpy done(&m, &ListManager::updateFinished);
        QSignalSpy reloaded(&m, &ListManager::reloaded);
        m.update();
        QVERIFY(done.wait(15000));
        QCOMPARE(m.updatedCount(), 1);
        QCOMPARE(m.failedCount(), 0);
        QVERIFY(reloaded.count() > 0 || reloaded.wait(10000));
        QVERIFY(m.match(QStringLiteral("http://ads.test/x"), QString(), Script).blocked);
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("data/filters/good.txt"))));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("data/filters/good.meta.json"))));

        // errors keep the old file
        server.status = 404;
        QSignalSpy done2(&m, &ListManager::updateFinished);
        m.update();
        QVERIFY(done2.wait(15000));
        QCOMPARE(m.failedCount(), 1);
        QVERIFY(m.failures().contains(QLatin1String("404")));
        server.status = 200;
        server.body = "<html>not a list</html>";
        QSignalSpy done3(&m, &ListManager::updateFinished);
        m.update();
        QVERIFY(done3.wait(15000));
        QCOMPARE(m.failedCount(), 1);
        QVERIFY(m.match(QStringLiteral("http://ads.test/x"), QString(), Script).blocked);           // still the good one
    }
    void updateRefusesAddressesThatAreNotHttps()
    {
        QTemporaryDir dir;
        ListManager m(dir.filePath(QStringLiteral("data")));
        m.setCatalog({{QStringLiteral("p"), QStringLiteral("P"), QStringLiteral("ads"), QStringLiteral("http://127.0.0.1:1/p.txt"), QStringLiteral("t")}});
        m.setEnabled({QStringLiteral("p")});
        QSignalSpy done(&m, &ListManager::updateFinished);
        m.update();
        QVERIFY(done.count() == 1 || done.wait(5000));
        QCOMPARE(m.failedCount(), 1);
    }

    // ---- speed (synthetic list: real lists are measured later and written down) -----------------------------------------------------
    void aBigListLoadsAndMatchesFast()
    {
        QTemporaryDir dir;
        QByteArray text = "[Adblock Plus 2.0]\n";
        auto* rng = QRandomGenerator::global();
        constexpr int kRules = 150000;
        for (int i = 0; i < kRules; ++i) {
            const int kind = i % 10;
            if (kind < 6)
                text += "||host" + QByteArray::number(i) + ".example" + QByteArray::number(i % 97) + ".test^\n";
            else if (kind < 8)
                text += "/ad" + QByteArray::number(i) + "/banner*.js\n";
            else if (kind == 8)
                text += "||trk" + QByteArray::number(i) + ".test^$third-party,script\n";
            else
                text += "@@||host" + QByteArray::number(i - 9) + ".example" + QByteArray::number((i - 9) % 97) + ".test/ok.js\n";
        }
        text += "##.cosmetic\n";
        writeFile(dir.filePath(QStringLiteral("data/filters/big.txt")), text);
        ListManager m(dir.filePath(QStringLiteral("data")));
        m.setCatalog({{QStringLiteral("big"), QStringLiteral("Big"), QStringLiteral("ads"), QStringLiteral("https://x/big"), QStringLiteral("t")}});
        m.setEnabled({QStringLiteral("big")});
        QSignalSpy spy(&m, &ListManager::reloaded);
        QElapsedTimer t;
        t.start();
        m.reload();
        QVERIFY(spy.wait(60000));
        const qint64 loadMs = t.elapsed();

        constexpr int kUrls = 30000;
        QStringList urls;
        int expectBlocked = 0;
        for (int i = 0; i < kUrls; ++i) {
            const int n = int(rng->bounded(kRules / 10)) * 10;               // n % 10 == 0 -> a host rule
            if (i % 3 == 0) {
                urls << QStringLiteral("https://cdn.host%1.example%2.test/lib/%3.js?v=%4").arg(n).arg(n % 97).arg(i).arg(rng->generate() % 1000);
                ++expectBlocked;
            } else {
                urls << QStringLiteral("https://www.site%1.org/static/app.%2.js?x=%3&y=zz").arg(i % 500).arg(i).arg(rng->generate() % 1000);
            }
        }
        int hits = 0;
        t.restart();
        for (const QString& u : urls)
            if (m.match(u, QStringLiteral("https://www.news.example/"), Script).blocked)
                ++hits;
        const double perMatchUs = double(t.nsecsElapsed()) / 1000.0 / kUrls;
        qInfo().noquote() << QStringLiteral("BENCH synthetic list: %1 rules, loaded in %2 ms, %3 us per request (%4 requests, %5 blocked)").arg(m.totalRules()).arg(loadMs).arg(perMatchUs, 0, 'f', 1).arg(kUrls).arg(hits);
        QVERIFY2(hits == expectBlocked, qPrintable(QStringLiteral("blocked %1, expected %2").arg(hits).arg(expectBlocked)));
        QVERIFY2(loadMs < 8000, "loading a 150,000 rule list took longer than 8 s");
        QVERIFY2(perMatchUs < 300.0, "a request took longer than 300 us on average");
    }
};

QTEST_MAIN(TstAdblock)
#include "tst_adblock.moc"
