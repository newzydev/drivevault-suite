// The real QML UI offscreen with real mouse clicks and key presses: open, pages, zoom, tabs, sidebar (thumbnails / bookmarks / comments),
// search, selecting and copying text, links, two page view, rotation, dark pages, print dialog, language, password dialog.
// Fails on any QML warning.
#include "DocumentController.h"
#include "PageImageProvider.h"
#include "TestPdf.h"
#include "pdfengine/PdfiumRuntime.h"

#include <QClipboard>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtQml/qqml.h>
#include <QtTest>

#include <functional>

namespace {
QStringList g_qmlProblems;
QtMessageHandler g_previous = nullptr;
void collect(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const bool isQml = QByteArray(context.category).startsWith("qml") || message.contains(QLatin1String(".qml"));
    if ((type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) && isQml)
        g_qmlProblems << message;
    if (g_previous)
        g_previous(type, context, message);
}
} // namespace

class TstUiFlow : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    dp::DocumentController* m_doc = nullptr;
    QQmlApplicationEngine* m_engine = nullptr;
    QQuickWindow* m_window = nullptr;
    QString m_sixPath;
    QString m_featurePath;

    QString write(const QString& name, const QByteArray& data)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() ? path : QString();
    }
    static QQuickItem* search(QQuickItem* parent, const QString& objectName)
    {
        if (!parent)
            return nullptr;
        if (parent->objectName() == objectName)
            return parent;
        for (QQuickItem* child : parent->childItems())
            if (QQuickItem* found = search(child, objectName))
                return found;
        return nullptr;
    }
    // The delegates of a ListView / Repeater are children in the item tree only (not QObject children), so walk the items.
    QQuickItem* find(const QString& objectName) const
    {
        return m_window ? search(m_window->contentItem(), objectName) : nullptr;
    }
    bool visible(const QString& objectName) const
    {
        QQuickItem* i = find(objectName);
        return i && i->isVisible();
    }
    void clickItem(QQuickItem* item, Qt::MouseButton button = Qt::LeftButton)
    {
        const QPointF p = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        QTest::mouseClick(m_window, button, {}, p.toPoint());
        QTest::qWait(60);
    }
    bool click(const QString& objectName)
    {
        QQuickItem* item = find(objectName);
        if (!item || !item->isVisible())
            return false;
        clickItem(item);
        return true;
    }
    bool waitFor(const std::function<bool()>& condition, int ms = 8000)
    {
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < ms) {
            if (condition())
                return true;
            QTest::qWait(30);
        }
        return condition();
    }
    QString text(const QString& objectName) const
    {
        QQuickItem* item = find(objectName);
        return item ? item->property("text").toString() : QString();
    }
    bool imageReady(const QString& objectName) const
    {
        QQuickItem* i = find(objectName);
        return i && i->property("status").toInt() == 1;           // Image.Ready
    }
    bool typeInto(const QString& fieldName, const char* chars, bool enter = true)
    {
        QQuickItem* field = find(fieldName);
        if (!field)
            return false;
        clickItem(field);
        QTest::keyClick(m_window, Qt::Key_A, Qt::ControlModifier);
        for (const char* c = chars; *c; ++c)
            QTest::keyClick(m_window, *c);
        if (enter)
            QTest::keyClick(m_window, Qt::Key_Return);
        QTest::qWait(300);
        return true;
    }
    // a place on a page (points from its top left, as shown) as a point in the window
    QPoint pagePoint(int page, double xPt, double yPt) const
    {
        QQuickItem* img = find(QStringLiteral("pageImage_%1").arg(page));
        if (!img)
            return {};
        const double scale = img->width() / m_doc->pageWidthPt(page);
        return img->mapToScene(QPointF(xPt * scale, yPt * scale)).toPoint();
    }
    void openAndWait(const QString& path, const QString& expectedName)
    {
        QCOMPARE(m_doc->open(path), QString());
        QTRY_VERIFY_WITH_TIMEOUT(m_doc->fileName() == expectedName && find("pages") != nullptr, 8000);
        QTRY_VERIFY_WITH_TIMEOUT(imageReady("pageImage_0"), 8000);
    }
    // every test that needs a known start closes all tabs, forgets the remembered views and opens the file afresh
    void closeAllTabs()
    {
        while (m_doc->tabs().size() > 0)
            m_doc->closeTab(0);
        QTRY_VERIFY_WITH_TIMEOUT(visible("homeOpen"), 8000);
    }
    void forgetViews()
    {
        QSettings s(m_dir.filePath("data/settings.ini"), QSettings::IniFormat);
        s.remove(QStringLiteral("views"));
        s.remove(QStringLiteral("viewOrder"));
        s.sync();
    }
    void fresh(const QString& path, const QString& name)
    {
        closeAllTabs();
        forgetViews();
        openAndWait(path, name);
    }
    QRectF wordRect(const QString& word, int page)
    {
        m_doc->search(word, false, false, 0);
        QTest::qWaitFor([&] { return !m_doc->searching(); }, 10000);
        QRectF r;
        for (const QVariant& v : m_doc->searchRects(page)) {
            const QVariantMap m = v.toMap();
            r = QRectF(m.value("x").toDouble(), m.value("y").toDouble(), m.value("w").toDouble(), m.value("h").toDouble());
            break;
        }
        m_doc->clearSearch();
        return r;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        g_previous = qInstallMessageHandler(collect);
        QQuickStyle::setStyle(QStringLiteral("Basic"));
        m_doc = new dp::DocumentController(m_dir.filePath("data"));
        m_doc->setLanguage("en");
        qmlRegisterSingletonInstance("DriveVault.PdfPro.Backend", 1, 0, "Doc", m_doc);
        m_engine = new QQmlApplicationEngine;
        m_engine->addImageProvider(QStringLiteral("pdf"), new dp::PageImageProvider(m_doc));
        m_engine->loadFromModule("DriveVault.PdfPro.UI", "Main");
        QVERIFY(!m_engine->rootObjects().isEmpty());
        m_window = qobject_cast<QQuickWindow*>(m_engine->rootObjects().constFirst());
        QVERIFY(m_window);
        m_window->resize(1240, 800);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        m_sixPath = write("six.pdf", testpdf::make(6));
        m_featurePath = write("feature.pdf", testpdf::makeFeatureDoc());
    }
    void cleanupTestCase()
    {
        m_doc->close();
        delete m_engine;
        delete m_doc;
        dp::pdf::shutdownPdfium();
        qInstallMessageHandler(g_previous);
    }

    void startsOnTheHomeScreenWithTheOpenButton()
    {
        QVERIFY(visible("homeOpen"));
        QVERIFY(!find("pages"));
    }

    void openingAFileShowsTheViewerAndRendersThePages()
    {
        openAndWait(m_sixPath, "six.pdf");
        QCOMPARE(text("pageField"), QStringLiteral("1"));
        QQuickItem* image = find("pageImage_0");
        QVERIFY(image->width() > 300 && image->height() > image->width());     // a portrait page
        QVERIFY(visible("thumbnails"));
        QVERIFY(visible("tab_0"));
    }

    void thePageButtonsAndTheFieldMoveThroughThePages()
    {
        QVERIFY(click("nextPage"));
        QTRY_COMPARE_WITH_TIMEOUT(text("pageField"), QStringLiteral("2"), 5000);
        QVERIFY(click("nextPage"));
        QTRY_COMPARE_WITH_TIMEOUT(text("pageField"), QStringLiteral("3"), 5000);
        QVERIFY(click("prevPage"));
        QTRY_COMPARE_WITH_TIMEOUT(text("pageField"), QStringLiteral("2"), 5000);
        QVERIFY(typeInto("pageField", "5"));
        QVERIFY(waitFor([&] { return imageReady("pageImage_4"); }));            // page 5 is rendered when it comes into view
        QTRY_COMPARE_WITH_TIMEOUT(text("pageField"), QStringLiteral("5"), 5000);
    }

    void zoomButtonsChangeTheZoomAndFitModesWork()
    {
        QVERIFY(click("fitWidth"));
        const int fit = text("zoomLabel").chopped(1).toInt();
        QVERIFY(fit > 20 && fit < 400);
        QVERIFY(click("zoomIn"));
        const int zoomed = text("zoomLabel").chopped(1).toInt();
        QVERIFY2(zoomed > fit, qPrintable(QString("%1 -> %2").arg(fit).arg(zoomed)));
        QVERIFY(click("zoomOut"));
        QVERIFY(click("zoomOut"));
        QVERIFY(text("zoomLabel").chopped(1).toInt() < zoomed);
        QVERIFY(click("zoomLabel"));                                             // the percentage button = actual size
        QCOMPARE(text("zoomLabel"), QStringLiteral("100%"));
        QVERIFY(click("fitPage"));
        QVERIFY(text("zoomLabel").chopped(1).toInt() <= fit);                    // a whole page fits in no more than the width does
        QVERIFY(waitFor([&] { return imageReady("pageImage_4"); }));
        QVERIFY(click("fitWidth"));
    }

    void theSidePanelCanBeHiddenAndShowsTheThumbnails()
    {
        QVERIFY(visible("sidePanel"));
        QVERIFY(visible("thumbnails"));
        QVERIFY(click("toggleSidebar"));
        QVERIFY(!visible("sidePanel"));
        QVERIFY(click("toggleSidebar"));
        QVERIFY(visible("sidePanel"));
    }

    void theLanguageSwitchChangesTheTexts()
    {
        QCOMPARE(text("toolbarOpen"), QStringLiteral("Open PDF…"));
        QVERIFY(click("langTh"));
        QCOMPARE(m_doc->language(), QStringLiteral("th"));
        QCOMPARE(text("toolbarOpen"), QString::fromUtf8("เปิดไฟล์ PDF…"));
        QVERIFY(click("langEn"));
        QCOMPARE(text("toolbarOpen"), QStringLiteral("Open PDF…"));
    }

    void tabsShowTheOpenFilesAndSwitchAndClose()
    {
        openAndWait(m_featurePath, "feature.pdf");
        QCOMPARE(m_doc->tabs().size(), 2);
        QVERIFY(visible("tab_0") && visible("tab_1"));
        const int featureId = m_doc->docId();
        QVERIFY(click("tab_0"));
        QTRY_COMPARE_WITH_TIMEOUT(m_doc->fileName(), QStringLiteral("six.pdf"), 5000);
        QVERIFY(m_doc->docId() != featureId);
        QTRY_VERIFY_WITH_TIMEOUT(imageReady("pageImage_0"), 8000);
        QVERIFY(click("tab_1"));
        QTRY_COMPARE_WITH_TIMEOUT(m_doc->fileName(), QStringLiteral("feature.pdf"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(imageReady("pageImage_0"), 8000);
        // the same file again does not make a third tab
        QCOMPARE(m_doc->open(m_sixPath), QString());
        QCOMPARE(m_doc->tabs().size(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(m_doc->fileName(), QStringLiteral("six.pdf"), 5000);
        QVERIFY(click("tabClose_0"));                                            // close the six page file: the other stays
        QTRY_COMPARE_WITH_TIMEOUT(m_doc->tabs().size(), 1, 5000);
        QCOMPARE(m_doc->fileName(), QStringLiteral("feature.pdf"));
        QVERIFY(!visible("tab_1"));
        QTRY_VERIFY_WITH_TIMEOUT(imageReady("pageImage_0"), 8000);
    }

    void theBookmarkAndCommentPanelsListTheStructureAndJump()
    {
        fresh(m_featurePath, "feature.pdf");
        QVERIFY(click("sideOutline"));
        QVERIFY(visible("outline_0") && visible("outline_1") && visible("outline_2"));
        QVERIFY(!visible("noOutline"));
        QVERIFY(click("outline_1"));                                             // "Chapter 2" -> page 2
        QTRY_COMPARE_WITH_TIMEOUT(text("pageField"), QStringLiteral("2"), 5000);
        QVERIFY(click("outline_2"));                                             // "Section 2.1" -> page 3
        QTRY_COMPARE_WITH_TIMEOUT(text("pageField"), QStringLiteral("3"), 5000);

        QVERIFY(click("sideComments"));
        QTRY_VERIFY_WITH_TIMEOUT(visible("comment_0") && visible("comment_1"), 8000);
        QVERIFY(!visible("noComments"));
        QVERIFY(click("comment_0"));                                             // the comment is on page 1
        QTRY_COMPARE_WITH_TIMEOUT(text("pageField"), QStringLiteral("1"), 5000);
        QVERIFY(click("sideThumbs"));
        QVERIFY(visible("thumbnails"));
    }

    void searchHighlightsHitsAndStepsThroughThem()
    {
        fresh(m_featurePath, "feature.pdf");
        QVERIFY(!visible("searchBar"));
        QVERIFY(click("findButton"));
        QVERIFY(visible("searchBar"));
        QVERIFY(typeInto("searchField", "search"));
        QTRY_COMPARE_WITH_TIMEOUT(text("searchCount"), QStringLiteral("1 of 3"), 8000);
        QCOMPARE(m_doc->searchRects(0).size(), 3);
        // the three hits are drawn on page 1 (the page view keeps them as a property)
        QQuickItem* page = find("pageImage_0")->parentItem();
        QTRY_COMPARE_WITH_TIMEOUT(page->property("searchRects").toList().size(), 3, 5000);
        QVERIFY(click("searchNext"));
        QTRY_COMPARE_WITH_TIMEOUT(text("searchCount"), QStringLiteral("2 of 3"), 5000);
        QVERIFY(click("searchPrev"));
        QVERIFY(click("searchPrev"));
        QTRY_COMPARE_WITH_TIMEOUT(text("searchCount"), QStringLiteral("3 of 3"), 5000);      // wraps to the last
        QVERIFY(click("searchCase"));                                            // match case: "SEARCH" is not "search"
        QTRY_COMPARE_WITH_TIMEOUT(text("searchCount"), QStringLiteral("1 of 2"), 8000);
        QVERIFY(typeInto("searchField", "zzz"));
        QTRY_COMPARE_WITH_TIMEOUT(text("searchCount"), QStringLiteral("No matches"), 8000);
        QVERIFY(click("searchCase"));
        QVERIFY(click("searchClose"));
        QVERIFY(!visible("searchBar"));
        QCOMPARE(m_doc->searchCount(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(page->property("searchRects").toList().size(), 0, 5000);
    }

    void draggingWithTheMouseSelectsTextAndCtrlCCopiesIt()
    {
        fresh(m_featurePath, "feature.pdf");
        const QRectF hello = wordRect("Hello", 0);
        const QRectF world = wordRect("world", 0);
        QVERIFY(!hello.isEmpty() && !world.isEmpty());
        const double y = hello.center().y();
        const QPoint from = pagePoint(0, hello.left() + 1, y);
        const QPoint to = pagePoint(0, world.right() - 1, y);
        QTest::mousePress(m_window, Qt::LeftButton, {}, from);
        for (int i = 1; i <= 8; ++i)
            QTest::mouseMove(m_window, from + (to - from) * i / 8);
        QTest::mouseRelease(m_window, Qt::LeftButton, {}, to);
        QTRY_VERIFY_WITH_TIMEOUT(m_doc->hasSelection(), 5000);
        QCOMPARE(m_doc->selectedText().trimmed(), QStringLiteral("Hello search world"));
        QQuickItem* page = find("pageImage_0")->parentItem();
        QTRY_VERIFY_WITH_TIMEOUT(!page->property("selectionRects").toList().isEmpty(), 5000);   // the selection is drawn
        QGuiApplication::clipboard()->clear();
        QTest::keyClick(m_window, Qt::Key_C, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(QGuiApplication::clipboard()->text().trimmed(), QStringLiteral("Hello search world"), 5000);
        // a click on an empty place clears it
        QTest::mouseClick(m_window, Qt::LeftButton, {}, pagePoint(0, 400, 200));
        QTRY_VERIFY2_WITH_TIMEOUT(!m_doc->hasSelection(), "a click on an empty place did not clear the selection", 5000);
        QTRY_VERIFY_WITH_TIMEOUT(page->property("selectionRects").toList().isEmpty(), 5000);
        // Ctrl+A selects everything, Escape clears it
        QTest::keyClick(m_window, Qt::Key_A, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(m_doc->hasSelection(), 5000);
        QVERIFY(m_doc->selectedText().contains("Third page"));
        QTest::keyClick(m_window, Qt::Key_Escape);
        QTRY_VERIFY2_WITH_TIMEOUT(!m_doc->hasSelection(), "Escape did not clear the selection", 5000);
    }

    void clickingALinkGoesToItsPageOrAsksBeforeOpeningAnAddress()
    {
        fresh(m_featurePath, "feature.pdf");
        QTest::mouseClick(m_window, Qt::LeftButton, {}, pagePoint(0, 150, 270));       // the link to page 2
        QTRY_COMPARE_WITH_TIMEOUT(text("pageField"), QStringLiteral("2"), 5000);
        QVERIFY(typeInto("pageField", "1"));
        QTRY_COMPARE_WITH_TIMEOUT(text("pageField"), QStringLiteral("1"), 5000);
        QTest::mouseClick(m_window, Qt::LeftButton, {}, pagePoint(0, 150, 330));       // the web address: a question first
        QTRY_VERIFY_WITH_TIMEOUT(visible("linkUri"), 5000);
        QCOMPARE(text("linkUri"), QStringLiteral("https://example.com/"));
        QVERIFY(click("linkCancel"));
        QTRY_VERIFY_WITH_TIMEOUT(!visible("linkUri"), 5000);
    }

    void twoPageViewPutsTwoPagesSideBySide()
    {
        fresh(m_sixPath, "six.pdf");
        QVERIFY(click("fitWidth"));
        const int single = text("zoomLabel").chopped(1).toInt();
        QVERIFY(click("viewMode"));
        QTRY_VERIFY_WITH_TIMEOUT(imageReady("pageImage_1"), 8000);
        QQuickItem* p0 = find("pageImage_0");
        QQuickItem* p1 = find("pageImage_1");
        const QPointF a = p0->mapToScene(QPointF(0, 0));
        const QPointF b = p1->mapToScene(QPointF(0, 0));
        QVERIFY2(b.x() > a.x() + p0->width() - 2 && qAbs(a.y() - b.y()) < 2, qPrintable(QString("%1,%2 / %3,%4").arg(a.x()).arg(a.y()).arg(b.x()).arg(b.y())));
        QVERIFY(text("zoomLabel").chopped(1).toInt() < single);                  // fit width of two pages is smaller
        QVERIFY(click("nextPage"));
        QTRY_COMPARE_WITH_TIMEOUT(text("pageField"), QStringLiteral("2"), 5000);
        QVERIFY(click("viewMode"));                                              // back to one page per row
        QTRY_VERIFY_WITH_TIMEOUT(text("zoomLabel").chopped(1).toInt() == single, 5000);
    }

    void rotatingTheViewTurnsThePagesAndDarkPagesInvertThem()
    {
        fresh(m_sixPath, "six.pdf");
        QQuickItem* image = find("pageImage_0");
        QVERIFY(image && image->height() > image->width());
        QVERIFY(click("rotate"));
        QCOMPARE(m_doc->rotation(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(find("pageImage_0") && find("pageImage_0")->width() > find("pageImage_0")->height(), 5000);   // now landscape
        QTRY_VERIFY_WITH_TIMEOUT(imageReady("pageImage_0"), 8000);
        QVERIFY(click("rotate"));
        QVERIFY(click("rotate"));
        QVERIFY(click("rotate"));
        QCOMPARE(m_doc->rotation(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(find("pageImage_0")->height() > find("pageImage_0")->width(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(imageReady("pageImage_0"), 8000);

        // dark pages: the white sheet turns dark (looked at in a picture of the window)
        auto brightnessAtPage = [&] {
            const QImage shot = m_window->grabWindow();
            const QPoint p = pagePoint(0, 400, 650);               // an empty place of the page
            return qGray(shot.pixel(qBound(0, p.x(), shot.width() - 1), qBound(0, p.y(), shot.height() - 1)));
        };
        QTRY_VERIFY_WITH_TIMEOUT(brightnessAtPage() > 200, 8000);
        QVERIFY(click("darkPages"));
        QVERIFY(m_doc->darkPages());
        QTRY_VERIFY_WITH_TIMEOUT(brightnessAtPage() < 60, 8000);
        QVERIFY(click("darkPages"));
        QTRY_VERIFY_WITH_TIMEOUT(brightnessAtPage() > 200, 8000);
    }

    void thePrintDialogChecksThePageRange()
    {
        fresh(m_sixPath, "six.pdf");
        QVERIFY(click("printButton"));
        QObject* dialog = m_window->findChild<QObject*>("printDialog");
        QVERIFY(dialog);
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 5000);
        QVERIFY(visible("printStart"));
        QVERIFY(visible("printRangeField"));
        QQuickItem* field = find("printRangeField");
        field->setProperty("text", "99");                                        // the file has 6 pages
        QVERIFY(QMetaObject::invokeMethod(dialog, "start"));                     // the same function the Print button calls
        QTRY_VERIFY_WITH_TIMEOUT(visible("printError"), 5000);
        QVERIFY2(text("printError").contains("not valid"), qPrintable(text("printError")));
        QVERIFY(!m_doc->printing());
        QVERIFY(click("printCancel"));
        QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("opened").toBool(), 5000);
    }

    void theViewOfAFileIsRememberedWhenItIsClosedAndOpenedAgain()
    {
        fresh(m_sixPath, "six.pdf");
        QVERIFY(typeInto("pageField", "4"));
        QTRY_COMPARE_WITH_TIMEOUT(text("pageField"), QStringLiteral("4"), 5000);
        QVERIFY(click("viewMode"));                                              // two pages per row
        QTest::qWait(150);
        QVERIFY(click("tabClose_0"));                                            // closing saves the view
        QTRY_VERIFY_WITH_TIMEOUT(visible("homeOpen"), 5000);
        openAndWait(m_sixPath, "six.pdf");
        QTRY_COMPARE_WITH_TIMEOUT(text("pageField"), QStringLiteral("4"), 8000);   // page 4 is back (the second page of the spread 3-4)
        QCOMPARE(m_doc->viewState().value("viewMode").toString(), QStringLiteral("twoPage"));
        QTRY_VERIFY_WITH_TIMEOUT(imageReady("pageImage_3"), 8000);
        QVERIFY(click("viewMode"));
        QTest::qWait(150);
        QVERIFY(click("tabClose_0"));
        QTRY_VERIFY_WITH_TIMEOUT(visible("homeOpen"), 5000);
        QVERIFY(find("recent_0"));
        QVERIFY(click("recent_0"));
        QTRY_VERIFY_WITH_TIMEOUT(m_doc->isOpen() && find("pages") != nullptr, 8000);
        m_doc->close();
        QTRY_VERIFY_WITH_TIMEOUT(visible("homeOpen"), 5000);
    }

    void fullScreenCanBeToggled()
    {
        QVERIFY(QMetaObject::invokeMethod(m_window, "toggleFullScreen"));
        QTRY_COMPARE_WITH_TIMEOUT(m_window->visibility(), QWindow::FullScreen, 5000);
        QVERIFY(QMetaObject::invokeMethod(m_window, "toggleFullScreen"));
        QTRY_VERIFY_WITH_TIMEOUT(m_window->visibility() != QWindow::FullScreen, 5000);
    }

    void anEncryptedFileAsksForThePassword()
    {
        const QFileInfo qpdf(QStringLiteral(DP_TEST_QPDF));
        if (!qpdf.exists())
            QSKIP("qpdf.exe is not in .tools\\qpdf");
        const QString plain = write("plain.pdf", testpdf::make(2));
        const QString locked = m_dir.filePath("locked.pdf");
        QProcess p;
        p.start(qpdf.absoluteFilePath(), {"--encrypt", "open-secret", "owner-secret", "256", "--", plain, locked});
        QVERIFY(p.waitForFinished(30000));
        QCOMPARE(p.exitCode(), 0);

        closeAllTabs();
        // the same path the UI takes: the window function opens the dialog when the engine says "password"
        QVERIFY(QMetaObject::invokeMethod(m_window, "openPath", Q_ARG(QVariant, locked), Q_ARG(QVariant, QVariant())));
        QVERIFY(waitFor([&] { QQuickItem* f = find("passwordField"); return f && f->isVisible(); }));
        QVERIFY(!m_doc->isOpen());

        QVERIFY(typeInto("passwordField", "wrong"));
        QVERIFY(!m_doc->isOpen());
        QVERIFY(find("passwordField")->isVisible());        // still asking after a wrong password
        QVERIFY(typeInto("passwordField", "open-secret"));
        QVERIFY(waitFor([&] { return m_doc->isOpen(); }));
        QCOMPARE(m_doc->pageCount(), 2);
        QVERIFY(waitFor([&] { QQuickItem* f = find("passwordField"); return !f || !f->isVisible(); }));
        m_doc->close();
    }

    void anInvalidFileShowsAMessageAndStaysOnHome()
    {
        closeAllTabs();
        QVERIFY(QMetaObject::invokeMethod(m_window, "openPath", Q_ARG(QVariant, write("junk.pdf", "this is not a pdf")), Q_ARG(QVariant, QVariant())));
        QVERIFY(waitFor([&] { QQuickItem* b = find("noticeBanner"); return b && b->isVisible(); }));
        QVERIFY(!m_doc->isOpen());
    }

    void noQmlWarningsWereEmitted()
    {
        QVERIFY2(g_qmlProblems.isEmpty(), qPrintable(g_qmlProblems.join("\n")));
    }
};

QTEST_MAIN(TstUiFlow)
#include "tst_ui_flow.moc"
