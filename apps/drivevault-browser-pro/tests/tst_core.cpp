#include "BookmarkStore.h"
#include "BrowserSettings.h"
#include "HistoryStore.h"
#include "UrlUtil.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QVariantMap>

using namespace dv::br;

class TstCore : public QObject {
    Q_OBJECT
private slots:
    // ---- address bar -------------------------------------------------------------------------------------------------
    void typedTextBecomesAnAddress_data()
    {
        QTest::addColumn<QString>("typed");
        QTest::addColumn<QString>("expected");
        QTest::newRow("empty") << "" << "about:blank";
        QTest::newRow("domain") << "example.com" << "https://example.com";
        QTest::newRow("domain path") << "example.com/a/b?x=1" << "https://example.com/a/b?x=1";
        QTest::newRow("subdomain port") << "dev.example.co.th:8443/x" << "https://dev.example.co.th:8443/x";
        QTest::newRow("localhost") << "localhost" << "http://localhost";
        QTest::newRow("localhost port") << "localhost:3000/app" << "http://localhost:3000/app";
        QTest::newRow("ip") << "192.168.1.10:8080" << "http://192.168.1.10:8080";
        QTest::newRow("full https") << "https://example.com/x" << "https://example.com/x";
        QTest::newRow("full http") << "http://localhost:5173/" << "http://localhost:5173/";
        QTest::newRow("about") << "about:blank" << "about:blank";
        QTest::newRow("trim") << "  example.com  " << "https://example.com";
        QTest::newRow("search words") << "hello world" << "https://www.google.com/search?q=hello%20world";
        QTest::newRow("single word") << "weather" << "https://www.google.com/search?q=weather";
        QTest::newRow("thai words") << QString::fromUtf8("ข่าววันนี้") << "https://www.google.com/search?q=%E0%B8%82%E0%B9%88%E0%B8%B2%E0%B8%A7%E0%B8%A7%E0%B8%B1%E0%B8%99%E0%B8%99%E0%B8%B5%E0%B9%89";
        QTest::newRow("words with a dot") << "what is node.js really" << "https://www.google.com/search?q=what%20is%20node.js%20really";
        QTest::newRow("view-source") << "view-source:https://example.com" << "view-source:https://example.com";
    }
    void typedTextBecomesAnAddress()
    {
        QFETCH(QString, typed);
        QFETCH(QString, expected);
        QCOMPARE(resolveInput(typed, {}).toString(QUrl::FullyEncoded), expected);
    }
    void aWindowsPathBecomesAFileAddress()
    {
        QCOMPARE(resolveInput(QStringLiteral("C:\\Users\\me\\page.html"), {}).scheme(), QStringLiteral("file"));
        QVERIFY(resolveInput(QStringLiteral("C:/x/y.html"), {}).toString().startsWith(QLatin1String("file:///C:/x/y.html")));
    }
    void theChosenSearchEngineIsUsed()
    {
        QCOMPARE(resolveInput(QStringLiteral("cats"), QStringLiteral("bing")), QUrl(QStringLiteral("https://www.bing.com/search?q=cats")));
        QCOMPARE(resolveInput(QStringLiteral("cats"), QStringLiteral("unknown-engine")), QUrl(QStringLiteral("https://www.google.com/search?q=cats")));
    }
    void anAddressIsShownShort()
    {
        QCOMPARE(prettyUrl(QStringLiteral("https://www.example.com/")), QStringLiteral("example.com"));
        QCOMPARE(prettyUrl(QStringLiteral("https://example.com/a/b")), QStringLiteral("example.com/a/b"));
        QCOMPARE(prettyUrl(QStringLiteral("about:blank")), QString());
        QCOMPARE(prettyUrl(QStringLiteral("file:///C:/x.html")), QStringLiteral("file:///C:/x.html"));
        QCOMPARE(hostOf(QStringLiteral("https://dev.example.com:8080/x")), QStringLiteral("dev.example.com"));
    }

    // ---- downloads -----------------------------------------------------------------------------------------------------
    void fileNamesAreMadeSafe()
    {
        QCOMPARE(safeFileName(QStringLiteral("a<b>c:d.txt")), QStringLiteral("a_b_c_d.txt"));
        QCOMPARE(safeFileName(QStringLiteral("  ")), QStringLiteral("download"));
        QCOMPARE(safeFileName(QStringLiteral("name. ")), QStringLiteral("name"));
        QCOMPARE(safeFileName(QStringLiteral("CON.txt")), QStringLiteral("_CON.txt"));
        QCOMPARE(safeFileName(QStringLiteral("a/b\\c.pdf")), QStringLiteral("a_b_c.pdf"));
    }
    void aDownloadNeverOverwritesAFile()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QCOMPARE(uniqueFileName(dir.path(), QStringLiteral("a.pdf")), QStringLiteral("a.pdf"));
        QFile(dir.filePath(QStringLiteral("a.pdf"))).open(QIODevice::WriteOnly);
        QCOMPARE(uniqueFileName(dir.path(), QStringLiteral("a.pdf")), QStringLiteral("a (1).pdf"));
        QFile(dir.filePath(QStringLiteral("a (1).pdf"))).open(QIODevice::WriteOnly);
        QCOMPARE(uniqueFileName(dir.path(), QStringLiteral("a.pdf")), QStringLiteral("a (2).pdf"));
        QFile(dir.filePath(QStringLiteral("noext"))).open(QIODevice::WriteOnly);
        QCOMPARE(uniqueFileName(dir.path(), QStringLiteral("noext")), QStringLiteral("noext (1)"));
    }
    void programFilesGetAWarning()
    {
        for (const char* n : {"setup.exe", "x.MSI", "run.bat", "a.ps1", "b.js", "c.lnk"})
            QVERIFY2(riskyFileName(QString::fromLatin1(n)), n);
        for (const char* n : {"a.pdf", "b.zip", "c.png", "noext"})
            QVERIFY2(!riskyFileName(QString::fromLatin1(n)), n);
    }

    // ---- bookmarks -----------------------------------------------------------------------------------------------------
    void bookmarksAreSavedAndLoaded()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("b.json"));
        QString id;
        {
            BookmarkStore s(file);
            QSignalSpy spy(&s, &BookmarkStore::changed);
            id = s.add(QStringLiteral("Example"), QStringLiteral("https://example.com/"));
            QVERIFY(!id.isEmpty());
            QCOMPARE(spy.count(), 1);
            QVERIFY(s.contains(QStringLiteral("https://example.com/")));
            QVERIFY2(s.add(QStringLiteral("again"), QStringLiteral("https://example.com/")).isEmpty(), "the same address is not added twice");
            QVERIFY(s.add(QStringLiteral("js"), QStringLiteral("javascript:alert(1)")).isEmpty());
            QVERIFY(s.add(QStringLiteral("data"), QStringLiteral("data:text/html,x")).isEmpty());
            s.add(QStringLiteral("Other one"), QStringLiteral("https://other.example/"), QStringLiteral("other"));
            QCOMPARE(s.count(), 2);
        }
        BookmarkStore again(file);
        QCOMPARE(again.count(), 2);
        const QVariantList items = again.items();
        QCOMPARE(items.at(0).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Example"));
        QCOMPARE(items.at(1).toMap().value(QStringLiteral("folder")).toString(), QStringLiteral("other"));
        again.rename(id, QStringLiteral("Renamed"));
        again.move(id, QStringLiteral("other"));
        again.removeUrl(QStringLiteral("https://other.example/"));
        BookmarkStore third(file);
        QCOMPARE(third.count(), 1);
        QCOMPARE(third.items().at(0).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Renamed"));
        third.remove(id);
        QCOMPARE(third.count(), 0);
    }
    void bookmarksExportAndImportAsHtml()
    {
        QTemporaryDir dir;
        BookmarkStore a(dir.filePath(QStringLiteral("a.json")));
        a.add(QStringLiteral("Tom & Jerry <fun>"), QStringLiteral("https://example.com/?a=1&b=2"), QStringLiteral("bar"));
        a.add(QStringLiteral("Thai ภาษาไทย"), QStringLiteral("https://example.org/"), QStringLiteral("other"));
        const QString html = dir.filePath(QStringLiteral("export.html"));
        QVERIFY(a.exportHtml(html));
        QFile f(html);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(QString::fromUtf8(f.readAll()).contains(QLatin1String("PERSONAL_TOOLBAR_FOLDER")));
        f.close();

        BookmarkStore b(dir.filePath(QStringLiteral("b.json")));
        QCOMPARE(b.importHtml(html), 2);
        QCOMPARE(b.importHtml(html), 0);                         // importing again adds nothing
        const QVariantList items = b.items();
        QCOMPARE(items.at(0).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Tom & Jerry <fun>"));
        QCOMPARE(items.at(0).toMap().value(QStringLiteral("url")).toString(), QStringLiteral("https://example.com/?a=1&b=2"));
        QCOMPARE(items.at(0).toMap().value(QStringLiteral("folder")).toString(), QStringLiteral("bar"));
        QCOMPARE(items.at(1).toMap().value(QStringLiteral("folder")).toString(), QStringLiteral("other"));
        QCOMPARE(b.importHtml(dir.filePath(QStringLiteral("missing.html"))), 0);
    }
    void aChromeStyleExportCanBeImported()
    {
        QTemporaryDir dir;
        const QString html = dir.filePath(QStringLiteral("chrome.html"));
        QFile f(html);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("<!DOCTYPE NETSCAPE-Bookmark-file-1>\n<DL><p>\n<DT><H3 ADD_DATE=\"1\" PERSONAL_TOOLBAR_FOLDER=\"true\">Bookmarks bar</H3>\n<DL><p>\n"
                "<DT><A HREF=\"https://github.com/\" ADD_DATE=\"2\" ICON=\"data:image/png;base64,AAAA\">GitHub</A>\n"
                "<DT><A HREF=\"javascript:void(0)\">bad</A>\n</DL><p>\n<DT><H3>Folder</H3>\n<DL><p>\n<DT><A HREF=\"https://netflix.com/\"><b>Net</b>flix</A>\n</DL><p>\n</DL><p>\n");
        f.close();
        BookmarkStore s(dir.filePath(QStringLiteral("s.json")));
        QCOMPARE(s.importHtml(QUrl::fromLocalFile(html).toString()), 2);        // a file: address works too
        QCOMPARE(s.items().at(0).toMap().value(QStringLiteral("folder")).toString(), QStringLiteral("bar"));
        QCOMPARE(s.items().at(1).toMap().value(QStringLiteral("folder")).toString(), QStringLiteral("other"));
        QCOMPARE(s.items().at(1).toMap().value(QStringLiteral("title")).toString(), QStringLiteral("Netflix"));
    }

    // ---- history -------------------------------------------------------------------------------------------------------
    void historyKeepsOnlyRealPagesAndSurvivesARestart()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("h.jsonl"));
        {
            HistoryStore h(file);
            h.add(QStringLiteral("https://a.example/"), QStringLiteral("A"));
            h.add(QStringLiteral("https://a.example/"), QStringLiteral("A again"));      // same page within a minute: once
            h.add(QStringLiteral("about:blank"), QStringLiteral("blank"));                // not a page
            h.add(QStringLiteral("javascript:1"), QStringLiteral("js"));
            h.add(QStringLiteral("https://b.example/x"), QStringLiteral("Bee"));
            QCOMPARE(h.count(), 2);
        }
        HistoryStore h(file);
        QCOMPARE(h.count(), 2);
        const QVariantList recent = h.recent(10);
        QCOMPARE(recent.at(0).toMap().value(QStringLiteral("url")).toString(), QStringLiteral("https://b.example/x"));      // newest first
        QCOMPARE(h.search(QStringLiteral("bee")).size(), 1);
        QCOMPARE(h.search(QStringLiteral("a.example")).size(), 1);
        QCOMPARE(h.search(QString()).size(), 2);
        h.remove(QStringLiteral("https://a.example/"));
        QCOMPARE(HistoryStore(file).count(), 1);
        h.clear();
        QCOMPARE(HistoryStore(file).count(), 0);
    }
    void historyTopSitesAndClearSince()
    {
        QTemporaryDir dir;
        HistoryStore h(dir.filePath(QStringLiteral("h.jsonl")));
        for (int i = 0; i < 3; ++i) {
            h.add(QStringLiteral("https://p%1.example/").arg(i), QStringLiteral("P%1").arg(i));
        }
        QCOMPARE(h.topSites(2).size(), 2);
        h.clearSince(0);
        QCOMPARE(h.count(), 0);
    }
    void historyTrimsToItsLimit()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("h.jsonl"));
        {
            QFile f(file);
            QVERIFY(f.open(QIODevice::WriteOnly));
            for (int i = 0; i < HistoryStore::kMaxEntries + 50; ++i)
                f.write(QStringLiteral("{\"u\":\"https://x.example/%1\",\"t\":\"t\",\"m\":%1}\n").arg(i + 1).toUtf8());
        }
        HistoryStore h(file);
        QCOMPARE(h.count(), int(HistoryStore::kMaxEntries));
        QCOMPARE(h.recent(1).at(0).toMap().value(QStringLiteral("url")).toString(), QStringLiteral("https://x.example/%1").arg(HistoryStore::kMaxEntries + 50));      // the file wrote numbers 1 .. kMax + 50
    }

    // ---- settings --------------------------------------------------------------------------------------------------------
    void settingsHaveSaneDefaultsAndAreSaved()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("s.json"));
        {
            BrowserSettings s(file);
            QCOMPARE(s.language(), QStringLiteral("th"));
            QCOMPARE(s.searchEngine(), QStringLiteral("google"));
            QVERIFY(s.showBookmarksBar());
            QVERIFY(s.blockNotifications());
            QCOMPARE(s.startup(), QStringLiteral("newtab"));
            QCOMPARE(s.homePage(), QString());
            QCOMPARE(s.zoomFor(QStringLiteral("a.example")), 1.0);
            QSignalSpy spy(&s, &BrowserSettings::changed);
            s.setLanguage(QStringLiteral("en"));
            s.setSearchEngine(QStringLiteral("bing"));
            s.setStartup(QStringLiteral("restore"));
            s.setBlockNotifications(false);
            s.setShowBookmarksBar(false);
            s.setHomePage(QStringLiteral("  https://example.com  "));
            s.setZoomFor(QStringLiteral("a.example"), 1.5);
            QCOMPARE(spy.count(), 7);
            s.setLanguage(QStringLiteral("en"));         // no change, no signal
            QCOMPARE(spy.count(), 7);
        }
        BrowserSettings s(file);
        QCOMPARE(s.language(), QStringLiteral("en"));
        QCOMPARE(s.searchEngine(), QStringLiteral("bing"));
        QCOMPARE(s.startup(), QStringLiteral("restore"));
        QVERIFY(!s.blockNotifications());
        QVERIFY(!s.showBookmarksBar());
        QCOMPARE(s.homePage(), QStringLiteral("https://example.com"));
        QCOMPARE(s.zoomFor(QStringLiteral("a.example")), 1.5);
        s.setZoomFor(QStringLiteral("a.example"), 1.0);
        QCOMPARE(BrowserSettings(file).zoomFor(QStringLiteral("a.example")), 1.0);
        s.setZoomFor(QStringLiteral("big.example"), 99.0);
        QCOMPARE(s.zoomFor(QStringLiteral("big.example")), 5.0);
        s.setSearchEngine(QStringLiteral("nonsense"));
        QCOMPARE(s.searchEngine(), QStringLiteral("google"));
    }
};

QTEST_APPLESS_MAIN(TstCore)
#include "tst_core.moc"
