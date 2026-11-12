// The real QML UI offscreen with real mouse clicks and key presses: open, pages, zoom, tabs, sidebar (thumbnails / bookmarks / comments),
// search, selecting and copying text, links, two page view, rotation, dark pages, print dialog, language, password dialog.
// Fails on any QML warning.
#include "DocumentController.h"
#include "PageImageProvider.h"
#include "Compressor.h"
#include "EditorModel.h"
#include "FormFiller.h"
#include "PdfComparer.h"
#include "OverlayImageProvider.h"
#include "PageListModel.h"
#include "TestPdf.h"
#include "pdftools/DocumentMaintenance.h"
#include "pdfengine/PdfiumRuntime.h"

#include <QClipboard>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
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
    dp::PageListModel* m_org = nullptr;
    dp::Compressor* m_cmp = nullptr;
    dp::EditorModel* m_edit = nullptr;
    dp::FormFiller* m_forms = nullptr;
    dp::PdfComparer* m_comparer = nullptr;
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
        QTest::qWait(350);                                            // the tool bar finishes its layout before a test clicks a button
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
        m_org = new dp::PageListModel(m_doc);
        m_cmp = new dp::Compressor(m_doc);
        m_edit = new dp::EditorModel(m_doc);
        m_forms = new dp::FormFiller(m_doc);
        m_comparer = new dp::PdfComparer(m_doc);
        qmlRegisterSingletonInstance("DriveVault.PdfPro.Backend", 1, 0, "Doc", m_doc);
        qmlRegisterSingletonInstance("DriveVault.PdfPro.Backend", 1, 0, "Organizer", m_org);
        qmlRegisterSingletonInstance("DriveVault.PdfPro.Backend", 1, 0, "Compressor", m_cmp);
        qmlRegisterSingletonInstance("DriveVault.PdfPro.Backend", 1, 0, "Editor", m_edit);
        qmlRegisterSingletonInstance("DriveVault.PdfPro.Backend", 1, 0, "Forms", m_forms);
        qmlRegisterSingletonInstance("DriveVault.PdfPro.Backend", 1, 0, "Comparer", m_comparer);
        m_engine = new QQmlApplicationEngine;
        m_engine->addImageProvider(QStringLiteral("pdf"), new dp::PageImageProvider(m_doc));
        m_engine->addImageProvider(QStringLiteral("ovl"), new dp::OverlayImageProvider(m_doc, m_edit));
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
        delete m_comparer;
        delete m_forms;
        delete m_edit;
        delete m_cmp;
        delete m_org;
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
        QCOMPARE(text("zoomLabel"), QStringLiteral("100%"));
        QVERIFY(!visible("sidePanel"));
        QVERIFY(visible("toolPanel"));
        QCOMPARE(find("zoomLabel")->property("variant").toString(), QStringLiteral("secondary"));
        const auto* pages = find("pages");
        const double pageCenter = image->mapToScene(QPointF(image->width() / 2, 0)).x();
        const double viewportCenter = pages->mapToScene(QPointF(pages->width() / 2, 0)).x();
        QVERIFY(qAbs(pageCenter - viewportCenter) < 2);
        QVERIFY(visible("tab_0"));
    }

    void homeKeepsTabsAndCommandSearchOpensARealTool()
    {
        m_doc->setLanguage(QStringLiteral("en"));
        QVERIFY(m_doc->isOpen());
        const int openTab = m_doc->currentTab();
        QVERIFY(click("homeTab"));
        QVERIFY(visible("homeOpen"));
        QVERIFY(!visible("pages"));
        QCOMPARE(m_doc->currentTab(), openTab);
        QTest::keyClick(m_window, Qt::Key_K, Qt::ControlModifier);
        QTRY_VERIFY(visible("toolSearchField"));
        QVERIFY(typeInto("toolSearchField", "OCR"));
        QTRY_VERIFY(visible("pages"));
        QTRY_VERIFY(visible("ocrSave"));
        QTest::keyClick(m_window, Qt::Key_Escape);
        QTRY_VERIFY(!visible("ocrSave"));
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
        QVERIFY(!visible("sidePanel"));
        QVERIFY(click("toggleSidebar"));
        QVERIFY(visible("sidePanel"));
        QVERIFY(visible("thumbnails"));
        QVERIFY(click("toggleSidebar"));
        QVERIFY(!visible("sidePanel"));
    }

    void titleBarTogglesAndDialogs()
    {
        QCOMPARE(text("toolbarOpen"), QStringLiteral("Open PDF…"));
        QVERIFY(find("languageToggle_en")->property("current").toBool());
        QVERIFY(click("languageToggle_th"));
        QCOMPARE(m_doc->language(), QStringLiteral("th"));
        QCOMPARE(text("toolbarOpen"), QString::fromUtf8("เปิดไฟล์ PDF…"));
        QVERIFY(find("languageToggle_th")->property("current").toBool());
        QVERIFY(click("languageToggle_en"));
        QCOMPARE(text("toolbarOpen"), QStringLiteral("Open PDF…"));

        const QColor darkBackground = m_window->color();
        QVERIFY(click("titleMenuButton"));
        QVERIFY(click("themeToggle"));
        QVERIFY(m_doc->lightTheme());
        QVERIFY(!m_doc->darkPages());
        QVERIFY(m_window->color().lightness() > darkBackground.lightness());

        QVERIFY(click("titleMenuButton"));
        QVERIFY(click("settingsButton"));
        QTRY_VERIFY(visible("settingsTheme"));
        QVERIFY(find("settingsTheme")->property("checked").toBool());
        QVERIFY(click("settingsDarkPages"));
        QVERIFY(m_doc->darkPages());
        QVERIFY(click("settingsTheme"));
        QVERIFY(!m_doc->lightTheme());
        QVERIFY(m_doc->darkPages());
        QCOMPARE(m_window->color(), darkBackground);
        QVERIFY(click("settingsDarkPages"));
        QVERIFY(!m_doc->darkPages());
        QVERIFY(click("settingsClose"));
        QTRY_VERIFY(!visible("settingsTheme"));

        QVERIFY(click("titleMenuButton"));
        QVERIFY(click("aboutButton"));
        QTRY_VERIFY(visible("aboutVersion"));
        QVERIFY(text("aboutVersion").contains(m_doc->version()));
        QVERIFY(text("aboutVersion").contains(m_doc->buildNumber()));
        QVERIFY(click("aboutClose"));
        QTRY_VERIFY(!visible("aboutVersion"));
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
        QVERIFY(click("toggleSidebar"));
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
        auto* toolbar = find("readerToolbar");
        auto* sidePanel = find("sidePanel");
        QVERIFY(toolbar && sidePanel);
        QVERIFY(toolbar->mapToScene(QPointF(0, toolbar->height())).y() <= sidePanel->mapToScene(QPointF()).y());
        QTest::mouseClick(m_window, Qt::LeftButton, {}, from);
        QTRY_VERIFY(visible("textCaret_0"));
        QTRY_COMPARE_WITH_TIMEOUT(find("textCaret_0")->opacity(), 0.0, 1500);
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::ShiftModifier, to);
        QCOMPARE(m_doc->selectedText().trimmed(), QStringLiteral("Hello search world"));
        QTest::mousePress(m_window, Qt::LeftButton, {}, from);
        for (int i = 1; i <= 8; ++i)
            QTest::mouseMove(m_window, from + (to - from) * i / 8);
        QTest::mouseRelease(m_window, Qt::LeftButton, {}, to);
        QTRY_VERIFY_WITH_TIMEOUT(m_doc->hasSelection(), 5000);
        QVERIFY(!visible("textCaret_0"));
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
        QTest::keyClick(m_window, Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE(m_doc->fileName(), QStringLiteral("six.pdf"));
        QTRY_COMPARE_WITH_TIMEOUT(text("pageField"), QStringLiteral("4"), 8000);   // page 4 is back (the second page of the spread 3-4)
        QCOMPARE(m_doc->viewState().value("viewMode").toString(), QStringLiteral("twoPage"));
        QTRY_VERIFY_WITH_TIMEOUT(imageReady("pageImage_3"), 8000);
        QVERIFY(click("viewMode"));
        QTest::qWait(150);
        QVERIFY(click("tabClose_0"));
        QTRY_VERIFY_WITH_TIMEOUT(visible("homeOpen"), 5000);
        QVERIFY(typeInto("recentSearch", "no-such-recent-file"));
        QTRY_VERIFY(!find("recent_0"));
        QVERIFY(typeInto("recentSearch", "SIX.PDF"));
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
        // Match the startup bridge: an empty command-line password must open the password prompt.
        QVERIFY(QMetaObject::invokeMethod(m_window, "openPath", Q_ARG(QVariant, locked), Q_ARG(QVariant, QVariant(QString()))));
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

        QVERIFY(QMetaObject::invokeMethod(m_window, "openPath", Q_ARG(QVariant, locked),
                                          Q_ARG(QVariant, QVariant(QStringLiteral("open-secret")))));
        QVERIFY(m_doc->isOpen());
        QCOMPARE(m_doc->pageCount(), 2);
        QVERIFY(!visible("passwordField"));
        m_doc->close();

        const QString invalid = write("queue-invalid.pdf", "not a PDF");
        const QVariantList batch{plain, invalid, locked, m_featurePath};
        QVERIFY(QMetaObject::invokeMethod(m_window, "openPaths", Q_ARG(QVariant, QVariant(batch))));
        QTRY_VERIFY(visible("passwordField"));
        QCOMPARE(m_doc->tabs().size(), 1);
        QCOMPARE(m_doc->fileName(), QStringLiteral("plain.pdf"));
        QVERIFY(click("passwordCancel"));
        QTRY_COMPARE(m_doc->tabs().size(), 2);
        QCOMPARE(m_doc->fileName(), QStringLiteral("feature.pdf"));
        QTRY_VERIFY(visible("noticeBanner"));
        closeAllTabs();
    }

    void anInvalidFileShowsAMessageAndStaysOnHome()
    {
        closeAllTabs();
        QVERIFY(QMetaObject::invokeMethod(m_window, "openPath", Q_ARG(QVariant, write("junk.pdf", "this is not a pdf")), Q_ARG(QVariant, QVariant())));
        QVERIFY(waitFor([&] { QQuickItem* b = find("noticeBanner"); return b && b->isVisible(); }));
        QVERIFY(!m_doc->isOpen());
    }

    void thePageOrganizerSelectsDeletesDragsAndSaves()
    {
        fresh(m_sixPath, "six.pdf");
        QVERIFY(click("organizeButton"));
        QObject* dialog = m_window->findChild<QObject*>("organizerDialog");
        QVERIFY(dialog);
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(m_org->count(), 6, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(find("orgPage_5") != nullptr, 5000);
        QVERIFY(!find("orgDelete")->isEnabled());                                // nothing selected

        QVERIFY(click("orgPage_1"));
        QVERIFY(find("orgDelete")->isEnabled());
        QVERIFY(text("orgCount").contains("1"));
        QVERIFY(click("orgDelete"));
        QTRY_COMPARE_WITH_TIMEOUT(m_org->count(), 5, 3000);
        QVERIFY(click("orgUndo"));
        QTRY_COMPARE_WITH_TIMEOUT(m_org->count(), 6, 3000);
        QVERIFY(m_org->canRedo());

        // drag the first page onto the fourth place
        QQuickItem* from = find("orgPage_0");
        QQuickItem* to = find("orgPage_3");
        QVERIFY(from && to);
        const QPoint a = from->mapToScene(QPointF(from->width() / 2, from->height() / 2)).toPoint();
        const QPoint b = to->mapToScene(QPointF(to->width() / 2, to->height() / 2)).toPoint();
        QTest::mousePress(m_window, Qt::LeftButton, {}, a);
        for (int i = 1; i <= 12; ++i) {
            QTest::mouseMove(m_window, a + (b - a) * i / 12);
            QTest::qWait(25);
        }
        QTest::mouseRelease(m_window, Qt::LeftButton, {}, b);
        QTRY_COMPARE_WITH_TIMEOUT(m_org->data(m_org->index(3), dp::PageListModel::PageRole).toInt(), 0, 3000);
        QVERIFY(m_org->modified());

        // turning a page changes its shown size
        QVERIFY(click("orgPage_0"));
        QVERIFY(click("orgRotateRight"));
        QCOMPARE(m_org->data(m_org->index(0), dp::PageListModel::RotationRole).toInt(), 1);

        // save through the same function the file dialog calls
        const QString out = m_dir.filePath("organized-ui.pdf");
        QFile::remove(out);
        QVERIFY(QMetaObject::invokeMethod(dialog, "saveTo", Q_ARG(QVariant, out), Q_ARG(QVariant, QVariantList()), Q_ARG(QVariant, false)));
        QTRY_VERIFY_WITH_TIMEOUT(visible("orgResult"), 10000);
        QVERIFY(QFileInfo::exists(out));
        // again: the file exists, so the dialog asks before replacing it
        QVERIFY(QMetaObject::invokeMethod(dialog, "saveTo", Q_ARG(QVariant, out), Q_ARG(QVariant, QVariantList()), Q_ARG(QVariant, false)));
        QTRY_VERIFY_WITH_TIMEOUT(visible("orgReplaceYes"), 3000);
        QVERIFY(click("orgReplaceNo"));
        QVERIFY(!visible("orgReplaceYes"));

        QVERIFY(click("orgClose"));                                              // saved: closes without asking
        QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("opened").toBool(), 5000);
        QCOMPARE(m_org->count(), 0);
        m_doc->close();
    }

    void unsavedChangesAreNotLostWithoutAsking()
    {
        fresh(m_sixPath, "six.pdf");
        QVERIFY(click("organizeButton"));
        QObject* dialog = m_window->findChild<QObject*>("organizerDialog");
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool() && find("orgPage_0") != nullptr, 5000);
        QVERIFY(click("orgPage_0"));
        QVERIFY(click("orgDelete"));
        QTRY_COMPARE_WITH_TIMEOUT(m_org->count(), 5, 3000);
        QVERIFY(click("orgClose"));
        QTRY_VERIFY_WITH_TIMEOUT(visible("orgDiscardYes"), 3000);
        QVERIFY(dialog->property("opened").toBool());
        QVERIFY(click("orgDiscardNo"));
        QVERIFY(click("orgClose"));
        QTRY_VERIFY_WITH_TIMEOUT(visible("orgDiscardYes"), 3000);
        QVERIFY(click("orgDiscardYes"));
        QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("opened").toBool(), 5000);
        m_doc->close();
    }

    void theSplitDialogShowsThePreviewAndWritesTheFiles()
    {
        fresh(m_sixPath, "six.pdf");
        QVERIFY(click("splitButton"));
        QObject* dialog = m_window->findChild<QObject*>("splitDialog");
        QVERIFY(dialog);
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 5000);
        QVERIFY(!find("splitStart")->isEnabled());                               // ranges mode, nothing typed
        QVERIFY(typeInto("splitRangeField", "1-2, 5", false));
        QTRY_COMPARE_WITH_TIMEOUT(dialog->property("preview").toList().size(), 2, 3000);
        QVERIFY(find("splitStart")->isEnabled());
        QVERIFY(!find("splitBookmarks")->isEnabled());                            // no bookmarks in this file
        QVERIFY(typeInto("splitRangeField", "9", false));                        // page 9 does not exist in 6 pages
        QTRY_COMPARE_WITH_TIMEOUT(dialog->property("preview").toList().size(), 0, 3000);
        QVERIFY(!find("splitStart")->isEnabled());
        QVERIFY(typeInto("splitRangeField", "2-3, 6", false));
        QTRY_COMPARE_WITH_TIMEOUT(dialog->property("preview").toList().size(), 2, 3000);

        const QString outDir = m_dir.filePath("ui-split");
        find("splitFolderField")->setProperty("text", outDir);
        QVERIFY(click("splitStart"));
        QTRY_VERIFY_WITH_TIMEOUT(visible("splitDone"), 15000);
        QCOMPARE(QDir(outDir).entryList({"*.pdf"}).size(), 2);
        QVERIFY(click("splitCancel"));                                           // "Close" when idle
        QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("opened").toBool(), 5000);
        m_doc->close();
    }

    void theCompressDialogShowsRealNumbersComparesAPageAndSaves()
    {
        const QString src = write("photo.pdf", testpdf::makePhotoDoc());
        fresh(src, "photo.pdf");
        QVERIFY(click("compressButton"));
        QObject* dialog = m_window->findChild<QObject*>("compressDialog");
        QVERIFY(dialog);
        if (!waitFor([&] { return dialog->property("opened").toBool(); }, 1500))
            QVERIFY(click("compressButton"));                       // late in a long run the first click can be eaten by a tool tip that is still closing
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 5000);
        QVERIFY(visible("compressStart"));
        QVERIFY(!visible("compressSave"));                                       // nothing to save before a run
        QCOMPARE(find("compressDpi")->property("value").toInt(), 150);           // "Balanced" is the default
        QVERIFY(click("level_high"));
        QCOMPARE(find("compressDpi")->property("value").toInt(), 110);
        QVERIFY(click("level_medium"));
        QVERIFY(click("compressStart"));
        QTRY_VERIFY_WITH_TIMEOUT(m_cmp->hasResult(), 30000);
        QTRY_VERIFY_WITH_TIMEOUT(visible("compressSummary") && visible("compressSave"), 5000);
        QVERIFY2(text("compressSummary").contains("→") && text("compressSummary").contains("−"), qPrintable(text("compressSummary")));
        QTRY_VERIFY_WITH_TIMEOUT(imageReady("compare_before") && imageReady("compare_after"), 10000);

        QVERIFY(!QFileInfo::exists(m_dir.filePath("photo-compressed.pdf")));    // nothing is saved until the user says so
        const QString out = m_dir.filePath("photo-compressed.pdf");
        QVERIFY(QMetaObject::invokeMethod(dialog, "save", Q_ARG(QVariant, out), Q_ARG(QVariant, false)));
        QTRY_VERIFY_WITH_TIMEOUT(visible("compressSaved"), 5000);
        QVERIFY(QFileInfo::exists(out));
        QVERIFY(QFileInfo(out).size() < QFileInfo(src).size() / 2);
        QVERIFY(QMetaObject::invokeMethod(dialog, "save", Q_ARG(QVariant, out), Q_ARG(QVariant, false)));
        QTRY_VERIFY_WITH_TIMEOUT(visible("compressReplaceYes"), 3000);          // asks before replacing
        QVERIFY(click("compressReplaceNo"));
        QVERIFY(click("compressClose"));
        QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("opened").toBool(), 5000);
        QVERIFY(!m_cmp->hasResult());
        m_doc->close();
    }

    void theEditBarDrawsSelectsMovesTypesAndSavesANewFile()
    {
        fresh(m_sixPath, "six.pdf");
        QVERIFY(!visible("editBar"));
        QVERIFY(click("editButton"));
        QTRY_VERIFY_WITH_TIMEOUT(visible("editBar"), 3000);
        QVERIFY(visible("tool_rect") && visible("editSave"));
        QVERIFY(!find("editSave")->isEnabled());                                  // nothing to save yet
        QVERIFY(click("fitPage"));                                                // keep all drag targets inside the test window

        // draw a rectangle with the mouse
        QVERIFY(click("tool_rect"));
        QCOMPARE(m_edit->tool(), QStringLiteral("rect"));
        QQuickItem* page = find("pageImage_0");
        QVERIFY(page);
        const double scale = page->width() / m_doc->pageWidthPt(0);                // pixels per point
        auto pt = [&](double x, double y) { return page->mapToScene(QPointF(x * scale, y * scale)).toPoint(); };
        QTest::mousePress(m_window, Qt::LeftButton, {}, pt(100, 200));
        for (int i = 1; i <= 8; ++i) {
            QTest::mouseMove(m_window, pt(100 + 25 * i, 200 + 10 * i));
            QTest::qWait(20);
        }
        QTest::mouseRelease(m_window, Qt::LeftButton, {}, pt(300, 280));
        QTRY_COMPARE_WITH_TIMEOUT(m_edit->count(), 1, 3000);
        QVERIFY(m_edit->selectedId() > 0);
        QVariantMap info = m_edit->selectedInfo();
        QVERIFY(std::abs(info.value("x").toDouble() - 100) < 4 && std::abs(info.value("w").toDouble() - 200) < 4);
        QTRY_VERIFY_WITH_TIMEOUT(imageReady("overlay_0"), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(find("editSave")->isEnabled(), 3000);

        // select it again and move it with the mouse
        QVERIFY(click("tool_select"));
        QTest::mousePress(m_window, Qt::LeftButton, {}, pt(200, 240));
        for (int i = 1; i <= 6; ++i) {
            QTest::mouseMove(m_window, pt(200 + 10 * i, 240 + 5 * i));
            QTest::qWait(20);
        }
        QTest::mouseRelease(m_window, Qt::LeftButton, {}, pt(260, 270));
        QTRY_VERIFY_WITH_TIMEOUT(m_edit->selectedInfo().value("x").toDouble() > 140, 3000);
        const double movedX = m_edit->selectedInfo().value("x").toDouble();
        QVERIFY(click("editUndo"));                                                // the move was one step
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(m_edit->itemInfo(info.value("id").toInt()).value("x").toDouble() - 100) < 4, 3000);
        QVERIFY(click("editRedo"));
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(m_edit->itemInfo(info.value("id").toInt()).value("x").toDouble() - movedX) < 1, 3000);

        // the Delete key removes the selected item; undo brings it back
        QTest::keyClick(m_window, Qt::Key_Delete);
        QTRY_COMPARE_WITH_TIMEOUT(m_edit->count(), 0, 3000);
        QVERIFY(click("editUndo"));
        QTRY_COMPARE_WITH_TIMEOUT(m_edit->count(), 1, 3000);

        // type text: a drag makes the box, the field opens, the letters go into it
        QVERIFY(click("tool_text"));
        QTest::mousePress(m_window, Qt::LeftButton, {}, pt(100, 40));
        for (int i = 1; i <= 6; ++i) {
            QTest::mouseMove(m_window, pt(100 + 40 * i, 40 + 6 * i));
            QTest::qWait(20);
        }
        QTest::mouseRelease(m_window, Qt::LeftButton, {}, pt(340, 76));
        QTRY_VERIFY_WITH_TIMEOUT(visible("editField"), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(find("editField")->hasActiveFocus(), 3000);
        for (const char* c = "Typed text"; *c; ++c)
            QTest::keyClick(m_window, *c);
        QTRY_COMPARE_WITH_TIMEOUT(m_edit->selectedInfo().value("text").toString(), QStringLiteral("Typed text"), 3000);
        QTest::keyClick(m_window, Qt::Key_Escape);                                 // finishes the typing
        QTRY_COMPARE_WITH_TIMEOUT(m_edit->editingId(), 0, 3000);
        QCOMPARE(m_edit->count(), 2);

        // save through the same function the file dialog calls
        const QString out = m_dir.filePath("edited-ui.pdf");
        QFile::remove(out);
        QQuickItem* bar = find("editBar");
        QVERIFY(QMetaObject::invokeMethod(bar, "save", Q_ARG(QVariant, out), Q_ARG(QVariant, false)));
        QTRY_VERIFY_WITH_TIMEOUT(visible("editOpenResult"), 15000);
        QVERIFY(QFileInfo::exists(out));
        auto saved = dp::pdf::PdfDocument::open(out, {}, nullptr);
        QVERIFY(saved);
        QVERIFY(saved->pageText(0).contains("Typed text"));
        const QImage img = saved->render(0, 1.0, nullptr, 0);
        const QVariantMap now = m_edit->itemInfo(info.value("id").toInt());
        QVERIFY(QColor(img.pixel(int(now.value("x").toDouble() + now.value("w").toDouble() / 2), int(now.value("y").toDouble()))).lightness() < 100);   // its outline
        QVERIFY(!m_edit->modified());
        saved.reset();

        // closing the tab with unsaved edits asks first
        m_edit->select(info.value("id").toInt());
        QVERIFY(click("editDelete"));                                              // a change after the save: unsaved again
        QTRY_VERIFY_WITH_TIMEOUT(m_edit->modified(), 3000);
        QVERIFY(click("tabClose_0"));
        QTRY_VERIFY_WITH_TIMEOUT(visible("confirmYes"), 3000);
        QVERIFY(m_doc->isOpen());
        QVERIFY(click("confirmCancel"));
        QTRY_VERIFY_WITH_TIMEOUT(!visible("confirmYes"), 3000);
        QVERIFY(m_doc->isOpen());
        QVERIFY(click("editClose"));
        QVERIFY(!m_edit->active());
        m_edit->discardEdits();
        m_doc->close();
    }

    void theFormDialogListsTheFieldsFillsThemAndSavesACopy()
    {
        fresh(write("form.pdf", testpdf::makeFormDoc()), "form.pdf");
        // the reader draws the (empty) fields without errors; then the dialog
        QVERIFY(click("formButton"));
        QObject* dialog = m_window->findChild<QObject*>("formDialog");
        QVERIFY(dialog);
        QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(visible("formField_name") && visible("formField_agree") && visible("formField_country"), 8000);
        QVERIFY(!find("formSave")->isEnabled());                                 // nothing typed yet
        QVERIFY(!find("formField_locked")->isEnabled());                         // read-only
        QVERIFY(typeInto("formField_name", "Somchai", false));
        QVERIFY(click("formField_agree"));
        QTRY_VERIFY_WITH_TIMEOUT(find("formSave")->isEnabled(), 3000);
        const QString out = m_dir.filePath("form-filled.pdf");
        QFile::remove(out);
        QVERIFY(QMetaObject::invokeMethod(dialog, "save", Q_ARG(QVariant, out), Q_ARG(QVariant, false)));
        QTRY_VERIFY_WITH_TIMEOUT(visible("formOpenResult"), 10000);
        QVERIFY(text("formMessage").contains("2"));
        auto filled = dp::pdf::PdfDocument::open(out, {}, nullptr);
        QVERIFY(filled);
        const QImage img = filled->render(0, 1.0, nullptr, 0);
        int ink = 0;
        for (int y = 112; y < 138; ++y)
            for (int x = 76; x < 296; ++x)
                if (QColor(img.pixel(x, y)).lightness() < 110)
                    ++ink;
        QVERIFY2(ink > 40, qPrintable(QString::number(ink)));
        filled.reset();
        QVERIFY(click("formClose"));
        QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("opened").toBool(), 5000);
        m_doc->close();
    }

    void outputMenuExportsSelectedPagesAndRequiresUnlockConsent()
    {
        fresh(m_sixPath, "six.pdf");
        QVERIFY(click("outputButton"));
        QTRY_VERIFY(visible("exportImagesAction"));
        QVERIFY(click("exportImagesAction"));
        auto* images = m_window->findChild<QObject*>("imageExportDialog");
        QVERIFY(images);
        QTRY_VERIFY(images->property("opened").toBool());
        QCOMPARE(images->property("count").toInt(), 6);
        find("imagePageMode")->setProperty("currentIndex", 2);
        QVERIFY(click("imageSave"));
        QCOMPARE(images->property("error").toString(), QStringLiteral("range"));
        QVERIFY(typeInto("imageRange", "3", false));
        QCOMPARE(images->property("count").toInt(), 1);
        const auto png = m_dir.filePath("ui-page.png");
        QVERIFY(QMetaObject::invokeMethod(images, "save", Q_ARG(QVariant, png)));
        QTRY_COMPARE(images->property("result").toString(), png);
        QCOMPARE(QImage(png).size(), QSize(1224, 1584));
        QVERIFY(click("imageClose"));
        QTRY_VERIFY(!images->property("opened").toBool());

        QVERIFY(click("outputButton"));
        QTRY_VERIFY(visible("protectPdfAction"));
        QVERIFY(click("protectPdfAction"));
        auto* security = m_window->findChild<QObject*>("securityDialog");
        QVERIFY(security);
        QTRY_VERIFY(security->property("opened").toBool());
        QVERIFY(typeInto("securityPassword", "owner", false));
        QVERIFY(click("securitySave"));
        QCOMPARE(security->property("error").toString(), QStringLiteral("passwordMismatch"));
        const auto protectedFile = m_dir.filePath("ui-protected.pdf");
        QVERIFY(QMetaObject::invokeMethod(security, "save", Q_ARG(QVariant, protectedFile)));
        QTRY_COMPARE(security->property("result").toString(), protectedFile);
        QCOMPARE(text("securityPassword"), QString());
        QVERIFY(click("securityClose"));
        QTRY_VERIFY(!security->property("opened").toBool());

        fresh(protectedFile, "ui-protected.pdf");
        QVERIFY(click("outputButton"));
        QTRY_VERIFY(visible("unlockPdfAction"));
        QVERIFY(click("unlockPdfAction"));
        QTRY_VERIFY(security->property("opened").toBool());
        QVERIFY(typeInto("securityPassword", "owner", false));
        QVERIFY(!find("securitySave")->isEnabled());
        QVERIFY(click("securityConsent"));
        QVERIFY(find("securitySave")->isEnabled());
        const auto unlocked = m_dir.filePath("ui-unlocked.pdf");
        QVERIFY(QMetaObject::invokeMethod(security, "save", Q_ARG(QVariant, unlocked)));
        QTRY_COMPARE(security->property("result").toString(), unlocked);
        QCOMPARE(text("securityPassword"), QString());
        QVERIFY(click("securityClose"));
        QTRY_VERIFY(!security->property("opened").toBool());
    }

    void redactionReviewsDrawnAndFoundMarksBeforeSaving()
    {
        const QString name = "redaction-ui.pdf";
        const QList<QSizeF> sizes(6, QSizeF(612, 792));
        fresh(write(name, testpdf::make(sizes, {"First page", "Page 2", "name@example.org"})), name);
        QVERIFY(click("outputButton"));
        QTRY_VERIFY(visible("redactAction"));
        QVERIFY(click("redactAction"));
        auto* bar = find("redactionBar");
        QVERIFY(bar && bar->isVisible());
        QVERIFY(!find("redactApply")->isEnabled());
        const QPoint start = pagePoint(0, 65, 65);
        const QPoint end = pagePoint(0, 195, 120);
        QTest::mousePress(m_window, Qt::LeftButton, {}, start);
        QTest::mouseMove(m_window, end, 50);
        QTest::mouseRelease(m_window, Qt::LeftButton, {}, end);
        QTRY_COMPARE(bar->property("marks").value<QJSValue>().property("length").toInt(), 1);
        QVERIFY(visible("redactionMark_0"));
        QVERIFY(typeInto("redactQuery", "Page 2", false));
        QVERIFY(click("redactScan"));
        QTRY_COMPARE(bar->property("marks").value<QJSValue>().property("length").toInt(), 2);
        find("redactSearchMode")->setProperty("currentIndex", 4);
        QVERIFY(click("redactScan"));
        QTRY_COMPARE(bar->property("marks").value<QJSValue>().property("length").toInt(), 3);
        QVERIFY(click("redactRemove"));
        QCOMPARE(bar->property("marks").value<QJSValue>().property("length").toInt(), 2);
        QVERIFY(click("redactApply"));
        QTRY_VERIFY(visible("confirmCancel"));
        QVERIFY(click("confirmCancel"));
        const auto output = m_dir.filePath("redacted-ui.pdf");
        QVERIFY(QMetaObject::invokeMethod(bar, "save", Q_ARG(QVariant, output)));
        QTRY_COMPARE_WITH_TIMEOUT(bar->property("result").toString(), output, 12000);
        const auto result = dp::pdf::PdfDocument::open(output, {}, nullptr);
        QVERIFY(result);
        QCOMPARE(result->pageCount(), 6);
        QCOMPARE(result->charCount(1), 0);
        QVERIFY(result->pageText(2).contains("name@example.org"));
        QVERIFY(click("redactClose"));
        QVERIFY(!bar->isVisible());
    }

    void maintenanceAndBatchAreAvailableFromTheReaderAndHome()
    {
        fresh(m_featurePath, "feature.pdf");
        QVERIFY(click("outputButton"));
        QTRY_VERIFY(visible("maintenanceAction"));
        QVERIFY(click("maintenanceAction"));
        auto* maintenance = m_window->findChild<QObject*>("maintenanceDialog");
        QVERIFY(maintenance);
        QTRY_VERIFY(maintenance->property("loaded").toBool());
        QVERIFY(text("maintenanceDocumentSummary").contains(QStringLiteral("3 pages")));
        QVERIFY(text("maintenanceDocumentSummary").contains(QStringLiteral("Unencrypted")));
        QVERIFY(text("maintenanceFontSummary").contains(QStringLiteral("Helvetica")));
        QVERIFY(typeInto("metadataTitle", "Draft", false));
        QVERIFY(find("maintenanceSave")->isEnabled());
        const auto edited = m_dir.filePath("metadata-ui.pdf");
        QVERIFY(QMetaObject::invokeMethod(maintenance, "save", Q_ARG(QVariant, edited)));
        QTRY_COMPARE(maintenance->property("result").toString(), edited);
        QCOMPARE(dp::tools::inspectDocument(edited, {}).value("Title").toString(), QStringLiteral("Draft"));
        QVERIFY(click("maintenanceClose"));
        QTRY_VERIFY(!maintenance->property("opened").toBool());
        closeAllTabs();
        QVERIFY(click("homeRepair"));
        QTRY_VERIFY(maintenance->property("opened").toBool());
        QVERIFY(maintenance->property("repair").toBool());
        QVERIFY(click("maintenanceClose"));
        QTRY_VERIFY(!maintenance->property("opened").toBool());
        QVERIFY(click("homeBatch"));
        auto* batch = m_window->findChild<QObject*>("batchDialog");
        QVERIFY(batch);
        QTRY_VERIFY(batch->property("opened").toBool());
        const auto in = m_dir.filePath("batch-ui-in");
        const auto out = m_dir.filePath("batch-ui-out");
        QVERIFY(QDir().mkdir(in));
        QVERIFY(QDir().mkdir(out));
        QVERIFY(QFile::copy(m_sixPath, QDir(in).filePath("source.pdf")));
        batch->setProperty("input", in);
        batch->setProperty("output", out);
        QVERIFY(click("batchStart"));
        QTRY_VERIFY(find("batchReport") && find("batchReport")->property("count").toInt() == 1);
        QVERIFY(click("batchClose"));
        QTRY_VERIFY(!batch->property("opened").toBool());
    }

    void homeToolsReflowAndFitThaiOcrTitle()
    {
        closeAllTabs();
        const QSize originalSize = m_window->size();
        m_doc->setLanguage(QStringLiteral("th"));
        m_window->resize(960, 600);
        QTRY_VERIFY(find("recommended_thaiOcr"));
        auto* card = find("recommended_thaiOcr");
        auto* title = search(card, "toolTitle");
        auto* description = search(card, "toolDescription");
        auto* merge = find("homeMerge");
        QVERIFY(title && description && merge);
        const auto top = [](QQuickItem* item) { return item->mapToScene(QPointF()).y(); };
        QTRY_VERIFY(top(find("homeOpen")) < top(card));
        QTRY_VERIFY(top(merge) > top(card));
        m_window->resize(1480, 780);
        QTRY_COMPARE(top(merge), top(card));
        QTRY_VERIFY(!title->property("truncated").toBool());
        for (auto* label : {title, description}) {
            QCOMPARE(label->property("lineCount").toInt(), 1);
            QVERIFY(label->mapToItem(card, QPointF(0, label->height())).y() <= card->height());
        }
        m_window->resize(originalSize);
        m_doc->setLanguage(QStringLiteral("en"));
    }

    void removingExistingTextWritesANewCopy()
    {
        fresh(m_sixPath, "six.pdf");
        const QRectF word = wordRect("Page", 0);
        QVERIFY(!word.isEmpty());
        QVERIFY(QMetaObject::invokeMethod(m_window, "runTool", Q_ARG(QVariant, QVariant("editExisting"))));
        QTest::qWait(60);
        QTest::mouseClick(m_window, Qt::LeftButton, {}, pagePoint(0, word.center().x(), word.center().y()));
        QObject* dialog = m_window->findChild<QObject*>("replaceTextDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(click("removeTextRun"));
        QVERIFY(!find("replacementText")->isEnabled());
        const QString out = m_dir.filePath("removed-run.pdf");
        QVERIFY(QMetaObject::invokeMethod(dialog, "save", Q_ARG(QVariant, QVariant(out))));
        QTRY_COMPARE(dialog->property("result").toString(), out);
        const auto result = dp::pdf::PdfDocument::open(out, {}, nullptr);
        QVERIFY(result);
        QCOMPARE(result->pageCount(), 6);
        QVERIFY(result->pageText(0).trimmed().isEmpty());
        QVERIFY(result->pageText(1).contains(QStringLiteral("Page 2 of 6")));
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        closeAllTabs();
    }

    void createPdfCommandsWriteAndOpenRealDocuments()
    {
        closeAllTabs();
        const QString blank = m_dir.filePath("created-blank.pdf");
        const QString pictures = m_dir.filePath("created-images.pdf");
        const QString red = m_dir.filePath("create-red.png");
        const QString blue = m_dir.filePath("create-blue.png");
        QImage image(80, 50, QImage::Format_RGB32);
        image.fill(Qt::red);
        QVERIFY(image.save(red));
        image.fill(Qt::blue);
        QVERIFY(image.save(blue));

        QVERIFY(QMetaObject::invokeMethod(m_window, "runTool", Q_ARG(QVariant, QVariant("createBlankPdf"))));
        QObject* dialog = m_window->findChild<QObject*>("createPdfDialog");
        QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        QVERIFY(find("createOrientation")->setProperty("currentIndex", 1));
        QVERIFY(QMetaObject::invokeMethod(dialog, "save", Q_ARG(QVariant, QVariant(blank))));
        QTRY_COMPARE(m_doc->fileName(), QStringLiteral("created-blank.pdf"));
        QCOMPARE(m_doc->pageCount(), 1);
        QVERIFY(m_doc->pageWidthPt(0) > m_doc->pageHeightPt(0));

        QVERIFY(QMetaObject::invokeMethod(m_window, "runTool", Q_ARG(QVariant, QVariant("createFromImages"))));
        QTRY_VERIFY(dialog->property("opened").toBool());
        const QVariantList sources{red, blue};
        QVERIFY(QMetaObject::invokeMethod(dialog, "addImages", Q_ARG(QVariant, QVariant(sources))));
        QTRY_VERIFY(visible("createMoveUp_1"));
        QVERIFY(click("createMoveUp_1"));
        QVERIFY(click("createRotate_0"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "save", Q_ARG(QVariant, QVariant(pictures))));
        QTRY_COMPARE(m_doc->fileName(), QStringLiteral("created-images.pdf"));
        QCOMPARE(m_doc->pageCount(), 2);
        const auto pdf = dp::pdf::PdfDocument::open(pictures, {}, nullptr);
        QVERIFY(pdf);
        const auto first = pdf->render(0, 1);
        const auto second = pdf->render(1, 1);
        const QColor firstColor = first.pixelColor(first.rect().center());
        const QColor secondColor = second.pixelColor(second.rect().center());
        QVERIFY(firstColor.blue() > 240 && firstColor.red() < 20);
        QVERIFY(secondColor.red() > 240 && secondColor.blue() < 20);
        QCOMPARE(first.pixelColor(75, first.height() / 2), QColor(Qt::white)); // rotated image is narrow on landscape paper
        QVERIFY(second.pixelColor(75, second.height() / 2).blue() < 20);
        closeAllTabs();
    }

    void windowRestoresMaximizedThenNormalSizeOnReopen()
    {
        closeAllTabs();
        QQmlComponent component(m_engine);
        component.loadFromModule("DriveVault.PdfPro.UI", "Main");
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        const QSize normalSize(960, 650);
        const auto settingsPath = m_dir.filePath("data/settings.ini");
        const auto clickCaption = [](QQuickWindow* window, const QString& name) {
            auto* button = search(window->contentItem(), name);
            if (!button || !button->isVisible())
                return false;
            const QPointF center(button->width() / 2, button->height() / 2);
            QTest::mouseClick(window, Qt::LeftButton, {}, button->mapToScene(center).toPoint());
            return true;
        };

        {
            const std::unique_ptr<QObject> instance(component.create());
            auto* window = qobject_cast<QQuickWindow*>(instance.get());
            QVERIFY(window);
            window->resize(normalSize);
            QTRY_COMPARE(window->property("normalGeometry").toRect().size(), normalSize);
            QVERIFY(clickCaption(window, "winMaximize"));
            QTRY_COMPARE(window->visibility(), QWindow::Maximized);
            QVERIFY(clickCaption(window, "winClose"));
            QTRY_VERIFY(!window->isVisible());
        }
        {
            QSettings settings(settingsPath, QSettings::IniFormat);
            QVERIFY(settings.value("window/maximized").toBool());
            QCOMPARE(settings.value("window/normalGeometry").toRect().size(), normalSize);
        }
        {
            const std::unique_ptr<QObject> instance(component.create());
            auto* window = qobject_cast<QQuickWindow*>(instance.get());
            QVERIFY(window);
            QTRY_COMPARE(window->visibility(), QWindow::Maximized);
            QVERIFY(clickCaption(window, "winMaximize"));
            QTRY_COMPARE(window->visibility(), QWindow::Windowed);
            QCOMPARE(window->size(), normalSize);
            QVERIFY(clickCaption(window, "winMinimize"));
            QTRY_COMPARE(window->visibility(), QWindow::Minimized);
            QVERIFY(window->close());
        }
        {
            const std::unique_ptr<QObject> instance(component.create());
            auto* window = qobject_cast<QQuickWindow*>(instance.get());
            QVERIFY(window);
            QTRY_COMPARE(window->visibility(), QWindow::Windowed);
            QCOMPARE(window->size(), normalSize);
            QVERIFY(window->close());
        }
    }

    void aLocalWorkspaceReopensTheSavedTabs()
    {
        fresh(m_sixPath, "six.pdf");
        QCOMPARE(m_doc->open(m_featurePath), QString());
        const QString workspace = m_dir.filePath("tabs.dvworkspace");
        QVERIFY(QMetaObject::invokeMethod(m_window, "saveWorkspaceFile", Q_ARG(QVariant, QVariant(workspace))));
        QVERIFY(QFileInfo::exists(workspace));
        QCOMPARE(m_doc->saveWorkspace(workspace), QStringLiteral("exists"));
        closeAllTabs();
        QVERIFY(QMetaObject::invokeMethod(m_window, "openWorkspaceFile", Q_ARG(QVariant, QVariant(workspace))));
        QTRY_COMPARE(m_doc->tabs().size(), 2);
        QCOMPARE(m_doc->tabs().first().toMap().value("title").toString(), QStringLiteral("six.pdf"));
        QCOMPARE(m_doc->fileName(), QStringLiteral("feature.pdf"));
        const QJsonDocument invalidDocument(QJsonObject{
            {"version", 1}, {"files", QJsonArray{QStringLiteral("https://example.invalid/sample.pdf")}}
        });
        const QString invalid = write("invalid.dvworkspace", invalidDocument.toJson());
        QCOMPARE(m_doc->readWorkspace(invalid).value("error").toString(), QStringLiteral("workspaceFormat"));
        closeAllTabs();
    }

    void diagnosticsAreLocalAndDoNotOpenDocumentsOrOverwriteReports()
    {
        const QString path = m_dir.filePath("diagnostics.json");
        const QString profile = m_dir.filePath("diagnostics-profile");
        QProcess process;
        const auto exe = QCoreApplication::applicationDirPath() + "/DriveVaultPdfPro.exe";
        const QStringList arguments{"--data-dir", profile, "--diagnostics-json", path, m_sixPath};
        process.start(exe, arguments);
        QVERIFY(process.waitForFinished(15000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QCOMPARE(process.exitCode(), 0);
        QVERIFY(!QFileInfo::exists(profile));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray original = file.readAll();
        file.close();
        const auto json = QJsonDocument::fromJson(original).object();
        QCOMPARE(json.value("version").toString(), m_doc->version());
        QVERIFY(!json.value("build").toString().isEmpty());
        QVERIFY(json.value("thaiOcrFilesPresent").isBool());
        QCOMPARE(json.size(), 7);
        QVERIFY(!original.contains("six.pdf"));
        process.start(exe, arguments);
        QVERIFY(process.waitForFinished(15000));
        QCOMPARE(process.exitCode(), 3);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
    }

    void noQmlWarningsWereEmitted()
    {
        QVERIFY2(g_qmlProblems.isEmpty(), qPrintable(g_qmlProblems.join("\n")));
    }
};

QTEST_MAIN(TstUiFlow)
#include "tst_ui_flow.moc"
