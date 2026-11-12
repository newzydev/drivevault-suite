// DocumentController: tabs, error codes, remembered views, recent files, language, search, selection, outline, links, annotations, printing.
#include "DocumentController.h"
#include "PageRange.h"
#include "TestPdf.h"
#include "pdfengine/PdfiumRuntime.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

using dp::DocumentController;

class TstController : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

    QString write(const QString& name, const QByteArray& data)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() ? path : QString();
    }
    static bool waitSearch(DocumentController& c) { return QTest::qWaitFor([&] { return !c.searching(); }, 10000); }
    static QVariantMap rectOf(DocumentController& c, const QString& word, int page)
    {
        c.search(word, false, false, 0);
        waitSearch(c);
        for (const QVariant& v : c.searchRects(page))
            return v.toMap();
        return {};
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }
    void cleanupTestCase() { dp::pdf::shutdownPdfium(); }

    // ---- tabs --------------------------------------------------------------------------------------------------------------

    void openingFilesMakesTabsAndTheSameFileIsNotOpenedTwice()
    {
        DocumentController c(m_dir.filePath("data1"));
        QVERIFY(!c.isOpen());
        QCOMPARE(c.pageCount(), 0);
        QSignalSpy changed(&c, &DocumentController::documentChanged);
        const QString a = write("a.pdf", testpdf::make(4));
        QCOMPARE(c.open(a), QString());
        QVERIFY(c.isOpen());
        QCOMPARE(c.pageCount(), 4);
        QCOMPARE(c.fileName(), QStringLiteral("a.pdf"));
        QCOMPARE(c.pageWidthPt(0), 612.0);
        QCOMPARE(c.pageHeightPt(0), 792.0);
        QCOMPARE(changed.count(), 1);
        const int idA = c.docId();
        QVERIFY(c.documentFor(idA));
        QVERIFY(!c.documentFor(idA + 7));

        const QString b = write("b.pdf", testpdf::make(2));
        QCOMPARE(c.open(b), QString());
        QCOMPARE(c.tabs().size(), 2);
        QCOMPARE(c.currentTab(), 1);
        QVERIFY(c.docId() != idA);
        QCOMPARE(c.pageCount(), 2);
        QVERIFY(c.documentFor(idA));                    // the first file is still open in its tab

        QCOMPARE(c.open(a), QString());                 // already open: its tab becomes the current one
        QCOMPARE(c.tabs().size(), 2);
        QCOMPARE(c.currentTab(), 0);
        QCOMPARE(c.docId(), idA);
        QCOMPARE(c.tabs().at(1).toMap().value("title").toString(), QStringLiteral("b.pdf"));

        c.nextTab(1);
        QCOMPARE(c.currentTab(), 1);
        c.nextTab(1);                                   // wraps
        QCOMPARE(c.currentTab(), 0);
        c.nextTab(-1);
        QCOMPARE(c.currentTab(), 1);

        c.closeTab(0);                                  // closing another tab keeps the current file
        QCOMPARE(c.tabs().size(), 1);
        QCOMPARE(c.fileName(), QStringLiteral("b.pdf"));
        QVERIFY(!c.documentFor(idA));
        c.close();
        QVERIFY(!c.isOpen());
        QCOMPARE(c.tabs().size(), 0);
        QCOMPARE(c.pageWidthPt(0), 0.0);
        c.close();                                      // nothing to close: no crash
    }

    void aFileUrlIsAccepted()
    {
        DocumentController c(m_dir.filePath("data2"));
        const QString path = write("url.pdf", testpdf::make(1));
        QCOMPARE(c.open(QUrl::fromLocalFile(path).toString()), QString());
        QCOMPARE(c.fileName(), QStringLiteral("url.pdf"));
    }

    void failuresGiveACodeAndKeepTheOpenDocument()
    {
        DocumentController c(m_dir.filePath("data3"));
        QCOMPARE(c.open(write("ok.pdf", testpdf::make(3))), QString());
        QCOMPARE(c.open(write("bad.pdf", "not a pdf")), QStringLiteral("format"));
        QCOMPARE(c.open(m_dir.filePath("none.pdf")), QStringLiteral("file"));
        QVERIFY(c.isOpen());                            // the file that was open stays open
        QCOMPARE(c.fileName(), QStringLiteral("ok.pdf"));
        QCOMPARE(c.tabs().size(), 1);
        QVERIFY(!c.recentFiles().contains(QDir::toNativeSeparators(m_dir.filePath("bad.pdf"))));
    }

    void recentFilesAreNewestFirstUniqueLimitedAndPersisted()
    {
        const QString data = m_dir.filePath("data4");
        QStringList paths;
        {
            DocumentController c(data);
            for (int i = 0; i < 12; ++i) {
                const QString p = write(QString("r%1.pdf").arg(i), testpdf::make(1));
                paths.append(QDir::toNativeSeparators(p));
                QCOMPARE(c.open(p), QString());
            }
            QCOMPARE(c.recentFiles().size(), 10);
            QCOMPARE(c.recentFiles().first(), paths.last());
            QCOMPARE(c.open(paths.at(5)), QString());          // opening again moves it to the top, no duplicate
            QCOMPARE(c.recentFiles().first(), paths.at(5));
            QCOMPARE(c.recentFiles().count(paths.at(5)), 1);
            c.removeRecent(paths.at(11));
            QVERIFY(!c.recentFiles().contains(paths.at(11)));
        }
        DocumentController again(data);
        QCOMPARE(again.recentFiles().size(), 9);
        QCOMPARE(again.recentFiles().first(), paths.at(5));
    }

    void theLanguageAndTheDarkViewArePersistedAndOnlyThaiOrEnglishIsAccepted()
    {
        const QString data = m_dir.filePath("data5");
        {
            DocumentController c(data);
            QVERIFY(c.language() == "th" || c.language() == "en");
            QVERIFY(!c.darkPages());
            QVERIFY(!c.lightTheme());
            QSignalSpy spy(&c, &DocumentController::languageChanged);
            c.setLanguage("th");
            c.setLanguage("fr");                              // rejected
            QCOMPARE(c.language(), QStringLiteral("th"));
            c.setLanguage("en");
            QCOMPARE(c.language(), QStringLiteral("en"));
            QVERIFY(spy.count() >= 1);
            c.setDarkPages(true);
            c.setLightTheme(true);
        }
        DocumentController again(data);
        QCOMPARE(again.language(), QStringLiteral("en"));
        QVERIFY(again.darkPages());
        QVERIFY(again.lightTheme());
    }

    // ---- view: rotation and the remembered position ----------------------------------------------------------------------

    void rotationTurnsTheShownSizeAndIsRememberedWithTheFileView()
    {
        const QString data = m_dir.filePath("data6");
        const QString path = write("view.pdf", testpdf::make(5));
        {
            DocumentController c(data);
            QCOMPARE(c.open(path), QString());
            QVERIFY(c.viewState().isEmpty());                      // a file never seen
            QSignalSpy rot(&c, &DocumentController::rotationChanged);
            c.rotateView(1);
            QCOMPARE(c.rotation(), 1);
            QCOMPARE(c.pageWidthPt(0), 792.0);
            QCOMPARE(c.pageHeightPt(0), 612.0);
            QVERIFY(rot.count() >= 1);
            c.rotateView(-2);                                      // 1 - 2 = -1 = 3
            QCOMPARE(c.rotation(), 3);
            c.rotateView(1);
            QCOMPARE(c.rotation(), 0);
            c.rotateView(1);
            c.saveViewState({{"page", 3}, {"zoom", 1.5}, {"zoomMode", "custom"}, {"viewMode", "twoPage"}});
            QCOMPARE(c.viewState().value("page").toInt(), 3);
        }
        DocumentController again(data);                            // a new run of the program
        QCOMPARE(again.open(path), QString());
        const QVariantMap v = again.viewState();
        QCOMPARE(v.value("page").toInt(), 3);
        QCOMPARE(v.value("zoom").toDouble(), 1.5);
        QCOMPARE(v.value("zoomMode").toString(), QStringLiteral("custom"));
        QCOMPARE(v.value("viewMode").toString(), QStringLiteral("twoPage"));
        QCOMPARE(again.rotation(), 1);
        QCOMPARE(again.pageWidthPt(0), 792.0);
    }

    // ---- search ----------------------------------------------------------------------------------------------------------

    void searchFindsHitsStepsThroughThemAndHighlightsTheCurrentOne()
    {
        DocumentController c(m_dir.filePath("data7"));
        QCOMPARE(c.open(write("search.pdf", testpdf::makeFeatureDoc())), QString());
        QSignalSpy target(&c, &DocumentController::searchTarget);
        c.search("search", false, false, 0);
        QVERIFY(waitSearch(c));
        QCOMPARE(c.searchCount(), 3);
        QCOMPARE(c.searchCurrent(), 0);
        QCOMPARE(c.searchProgress(), 1.0);
        QVERIFY(target.count() >= 1);
        QCOMPARE(target.first().at(0).toInt(), 0);                 // the first hit is on page 1
        const QVariantList rects = c.searchRects(0);
        QCOMPARE(rects.size(), 3);
        int current = 0;
        for (const QVariant& v : rects)
            current += v.toMap().value("current").toBool() ? 1 : 0;
        QCOMPARE(current, 1);
        QVERIFY(c.searchRects(1).isEmpty());

        c.searchStep(1);
        QCOMPARE(c.searchCurrent(), 1);
        c.searchStep(1);
        QCOMPARE(c.searchCurrent(), 2);
        c.searchStep(1);                                           // wraps
        QCOMPARE(c.searchCurrent(), 0);
        c.searchStep(-1);
        QCOMPARE(c.searchCurrent(), 2);

        c.search("Second", false, false, 0);
        QVERIFY(waitSearch(c));
        QCOMPARE(c.searchCount(), 1);
        QCOMPARE(c.searchRects(1).size(), 1);
        QVERIFY(c.searchRects(0).isEmpty());

        c.search("search", true, false, 0);                        // match case
        QVERIFY(waitSearch(c));
        QCOMPARE(c.searchCount(), 2);
        c.search("search", false, false, 1);                        // starts on page 2: nothing there, so the first hit is shown
        QVERIFY(waitSearch(c));
        QCOMPARE(c.searchCount(), 3);
        QCOMPARE(c.searchCurrent(), 0);

        c.search("no such words", false, false, 0);
        QVERIFY(waitSearch(c));
        QCOMPARE(c.searchCount(), 0);
        QCOMPARE(c.searchCurrent(), -1);
        c.searchStep(1);                                           // nothing to step through: no crash
        c.search("", false, false, 0);
        QCOMPARE(c.searchCount(), 0);
        c.search("search", false, false, 0);
        QVERIFY(waitSearch(c));
        c.clearSearch();
        QCOMPARE(c.searchCount(), 0);
        QVERIFY(c.searchRects(0).isEmpty());
    }

    void aSearchIsTiedToItsTabAndFollowsTheRotation()
    {
        DocumentController c(m_dir.filePath("data8"));
        QCOMPARE(c.open(write("s1.pdf", testpdf::makeFeatureDoc())), QString());
        c.search("search", false, false, 0);
        QVERIFY(waitSearch(c));
        const double upright = c.searchRects(0).first().toMap().value("x").toDouble();
        c.rotateView(1);                                           // the same words are looked for again in the turned page
        QVERIFY(waitSearch(c));
        QCOMPARE(c.searchCount(), 3);
        QVERIFY(c.searchRects(0).first().toMap().value("x").toDouble() > 600);
        QVERIFY(upright < 200);
        QCOMPARE(c.open(write("s2.pdf", testpdf::make(2))), QString());      // a new tab: no search of the other file
        QCOMPARE(c.searchCount(), 0);
        QVERIFY(c.searchRects(0).isEmpty());
    }

    // ---- selection and copy ----------------------------------------------------------------------------------------------

    void draggingSelectsTextAndCopyPutsItOnTheClipboard()
    {
        DocumentController c(m_dir.filePath("data9"));
        QCOMPARE(c.open(write("sel.pdf", testpdf::makeFeatureDoc())), QString());
        const QVariantMap hello = rectOf(c, "Hello", 0);
        const QVariantMap world = rectOf(c, "world", 0);
        QVERIFY(!hello.isEmpty() && !world.isEmpty());
        const double y = hello.value("y").toDouble() + hello.value("h").toDouble() / 2;
        c.clearSearch();

        QVERIFY(!c.hasSelection());
        c.selectStart(0, hello.value("x").toDouble() + 1, y);
        QVERIFY(!c.hasSelection());                                // a press alone selects nothing
        QCOMPARE(c.textCaret().value("page").toInt(), 0);
        QVERIFY(c.textCaret().value("h").toDouble() > 0);
        c.selectMove(0, world.value("x").toDouble() + world.value("w").toDouble() - 1, y);
        QVERIFY(c.hasSelection());
        QCOMPARE(c.selectedText().trimmed(), QStringLiteral("Hello search world"));
        QCOMPARE(c.selectionRects(0).size(), 1);
        QVERIFY(c.selectionRects(1).isEmpty());
        QVERIFY(c.copySelection());
        QCOMPARE(QGuiApplication::clipboard()->text().trimmed(), QStringLiteral("Hello search world"));

        c.selectMove(0, hello.value("x").toDouble() + 3, y);       // dragging back shrinks it to the first letters
        QVERIFY(c.selectedText().size() < 8);
        c.clearSelection();
        QVERIFY(!c.hasSelection());
        QVERIFY(c.textCaret().isEmpty());
        QVERIFY(c.selectedText().isEmpty());
        QVERIFY(!c.copySelection());

        // across pages
        const QVariantMap second = rectOf(c, "Second", 1);
        c.clearSearch();
        c.selectStart(0, hello.value("x").toDouble() + 1, y);
        c.selectMove(1, second.value("x").toDouble() + second.value("w").toDouble() - 1, second.value("y").toDouble() + 6);
        const QString across = c.selectedText();
        QVERIFY2(across.startsWith("Hello") && across.contains("search again") && across.contains("Second"), qPrintable(across));
        QVERIFY(!c.selectionRects(0).isEmpty() && !c.selectionRects(1).isEmpty());

        c.selectAll();
        QCOMPARE(c.selectionRects(0).size(), 2);
        QVERIFY(c.selectedText().contains("Third page"));
        QVERIFY(c.selectedText().contains("Hello"));
        QVERIFY(!c.selectionRects(2).isEmpty());
        c.rotateView(1);                                           // turning the view drops the selection
        QVERIFY(!c.hasSelection());
        c.selectStart(0, 5, 5);                                    // on empty space: nothing to select
        c.selectMove(0, 6, 6);
        QVERIFY(c.selectedText().size() <= 1);
    }

    // ---- outline, links, annotations -------------------------------------------------------------------------------------

    void outlineLinksAndAnnotationsAreAvailableToTheUi()
    {
        DocumentController c(m_dir.filePath("data10"));
        QCOMPARE(c.open(write("struct.pdf", testpdf::makeFeatureDoc())), QString());
        const QVariantList o = c.outline();
        QCOMPARE(o.size(), 3);
        QCOMPARE(o.at(1).toMap().value("title").toString(), QStringLiteral("Chapter 2"));
        QCOMPARE(o.at(2).toMap().value("level").toInt(), 1);
        QCOMPARE(o.at(2).toMap().value("page").toInt(), 2);

        QCOMPARE(c.linkAt(0, 150, 270).value("kind").toString(), QStringLiteral("page"));
        QCOMPARE(c.linkAt(0, 150, 270).value("page").toInt(), 1);
        QCOMPARE(c.linkAt(0, 150, 330).value("kind").toString(), QStringLiteral("uri"));
        QCOMPARE(c.linkAt(0, 150, 330).value("uri").toString(), QStringLiteral("https://example.com/"));
        QCOMPARE(c.linkAt(0, 450, 450).value("kind").toString(), QStringLiteral("none"));
        // a link may open a web page or a mail program, nothing else
        QVERIFY(!c.openUri("file:///C:/Windows/System32/calc.exe"));
        QVERIFY(!c.openUri("javascript:alert(1)"));
        QVERIFY(!c.openUri("ftp://example.com/x"));
        QVERIFY(!c.openUri("C:\\Windows\\System32\\cmd.exe"));
        QVERIFY(!c.openUri("not a uri at all"));

        QVERIFY(c.annotationItems().isEmpty());
        c.requestAnnotations();
        QVERIFY(QTest::qWaitFor([&] { return !c.annotationsLoading(); }, 10000));
        QCOMPARE(c.annotationItems().size(), 2);
        QCOMPARE(c.annotationItems().at(0).toMap().value("text").toString(), QStringLiteral("Remember this"));
        QCOMPARE(c.annotationItems().at(1).toMap().value("type").toString(), QStringLiteral("Highlight"));
        c.switchTab(0);                                            // same tab: the list stays
        QCOMPARE(c.annotationItems().size(), 2);
        QCOMPARE(c.open(write("struct2.pdf", testpdf::make(1))), QString());
        QVERIFY(c.annotationItems().isEmpty());                    // belongs to the other tab
    }

    // ---- printing --------------------------------------------------------------------------------------------------------

    void pageRangesAreParsedStrictly()
    {
        using dp::parsePageRanges;
        QCOMPARE(parsePageRanges("1-3,5", 10), (QList<int>{0, 1, 2, 4}));
        QCOMPARE(parsePageRanges(" 2 , 4 - 5 ", 10), (QList<int>{1, 3, 4}));
        QCOMPARE(parsePageRanges("8-", 10), (QList<int>{7, 8, 9}));
        QCOMPARE(parsePageRanges("-3", 10), (QList<int>{0, 1, 2}));
        QCOMPARE(parsePageRanges("3,1,3", 10), (QList<int>{2, 0}));          // the order is kept, no duplicates
        QCOMPARE(parsePageRanges("2-99", 5), (QList<int>{1, 2, 3, 4}));      // the end may go past the last page
        QVERIFY(parsePageRanges("", 10).isEmpty());
        QVERIFY(parsePageRanges("0", 10).isEmpty());
        QVERIFY(parsePageRanges("5-3", 10).isEmpty());
        QVERIFY(parsePageRanges("abc", 10).isEmpty());
        QVERIFY(parsePageRanges("99", 5).isEmpty());                         // a page that does not exist is an error
        QVERIFY(parsePageRanges("1-", 0).isEmpty());
        QVERIFY(parsePageRanges("-", 5).isEmpty());
    }

    void printingToAFileWritesTheChosenPages()
    {
        DocumentController c(m_dir.filePath("data11"));
        QCOMPARE(c.startPrint({}), QStringLiteral("noDocument"));
        QCOMPARE(c.open(write("print.pdf", testpdf::make(6))), QString());
        QCOMPARE(c.startPrint({{"pages", "9"}}), QStringLiteral("badRange"));
        QCOMPARE(c.startPrint({{"pages", "x"}}), QStringLiteral("badRange"));

        const QString out = m_dir.filePath("printed.pdf");
        QSignalSpy finished(&c, &DocumentController::printFinished);
        QCOMPARE(c.startPrint({{"pages", "2-3,6"}, {"outputFile", out}}), QString());
        QVERIFY(c.printing());
        QCOMPARE(c.startPrint({{"pages", "all"}, {"outputFile", m_dir.filePath("other.pdf")}}), QStringLiteral("busy"));
        QVERIFY(QTest::qWaitFor([&] { return finished.count() > 0; }, 60000));
        QCOMPARE(finished.first().at(0).toBool(), true);
        QVERIFY(!c.printing());
        QCOMPARE(c.printProgress(), 1.0);

        auto printed = dp::pdf::PdfDocument::open(out, {}, nullptr);
        QVERIFY(printed);
        QCOMPARE(printed->pageCount(), 3);
        const QImage page = printed->render(0, 0.5);
        int dark = 0;
        for (int y = 0; y < page.height(); ++y)
            for (int x = 0; x < page.width(); ++x)
                if (qGray(page.pixel(x, y)) < 110)
                    ++dark;
        QVERIFY2(dark > 100, qPrintable(QString("only %1 dark pixels: the page was printed blank").arg(dark)));

        // the current page only
        finished.clear();
        QCOMPARE(c.startPrint({{"pages", "current"}, {"currentPage", 4}, {"outputFile", m_dir.filePath("one.pdf")}}), QString());
        QVERIFY(QTest::qWaitFor([&] { return finished.count() > 0; }, 60000));
        auto one = dp::pdf::PdfDocument::open(m_dir.filePath("one.pdf"), {}, nullptr);
        QVERIFY(one);
        QCOMPARE(one->pageCount(), 1);
        QCOMPARE(c.startPrint({{"pages", "current"}, {"currentPage", 40}}), QStringLiteral("badRange"));
    }
};

QTEST_MAIN(TstController)
#include "tst_controller.moc"
