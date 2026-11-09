// The real QML UI offscreen with real mouse clicks: open a file, page buttons, zoom, thumbnails, language, close, password dialog.
// Fails on any QML warning.
#include "DocumentController.h"
#include "PageImageProvider.h"
#include "TestPdf.h"
#include "pdfengine/PdfiumRuntime.h"

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
    void clickItem(QQuickItem* item)
    {
        const QPointF p = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        QTest::mouseClick(m_window, Qt::LeftButton, {}, p.toPoint());
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
    bool typeInto(const QString& fieldName, const char* chars)
    {
        QQuickItem* field = find(fieldName);
        if (!field)
            return false;
        clickItem(field);
        QTest::keyClick(m_window, Qt::Key_A, Qt::ControlModifier);
        for (const char* c = chars; *c; ++c)
            QTest::keyClick(m_window, *c);
        QTest::keyClick(m_window, Qt::Key_Return);
        QTest::qWait(300);
        return true;
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
        QVERIFY(find("homeOpen"));
        QVERIFY(find("homeOpen")->isVisible());
        QVERIFY(!find("pages"));
    }

    void openingAFileShowsTheViewerAndRendersThePages()
    {
        QCOMPARE(m_doc->open(write("six.pdf", testpdf::make(6))), QString());
        QVERIFY(waitFor([&] { return find("pages") != nullptr; }));
        QCOMPARE(text("pageField"), QStringLiteral("1"));
        QVERIFY2(waitFor([&] { return imageReady("pageImage_0"); }), "page 1 was not rendered");
        QQuickItem* image = find("pageImage_0");
        QVERIFY(image->width() > 300 && image->height() > image->width());     // a portrait page
        QVERIFY(find("thumbnails")->isVisible());
    }

    void thePageButtonsAndTheFieldMoveThroughThePages()
    {
        QVERIFY(click("nextPage"));
        QVERIFY(waitFor([&] { return text("pageField") == QLatin1String("2"); }));
        QVERIFY(click("nextPage"));
        QVERIFY(waitFor([&] { return text("pageField") == QLatin1String("3"); }));
        QVERIFY(click("prevPage"));
        QVERIFY(waitFor([&] { return text("pageField") == QLatin1String("2"); }));
        QVERIFY(typeInto("pageField", "5"));
        QVERIFY(waitFor([&] { return imageReady("pageImage_4"); }));            // page 5 is rendered when it comes into view
        QVERIFY(waitFor([&] { return text("pageField") == QLatin1String("5"); }));
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
        QVERIFY(click("actualSize"));
        QCOMPARE(text("zoomLabel"), QStringLiteral("100%"));
        QVERIFY(click("fitPage"));
        QVERIFY(text("zoomLabel").chopped(1).toInt() <= fit);                    // a whole page fits in no more than the width does
        QVERIFY(waitFor([&] { return imageReady("pageImage_4"); }));
    }

    void theThumbnailPanelCanBeHidden()
    {
        QVERIFY(find("thumbnails")->isVisible());
        QVERIFY(click("toggleThumbs"));
        QVERIFY(!find("thumbnails")->isVisible());
        QVERIFY(click("toggleThumbs"));
        QVERIFY(find("thumbnails")->isVisible());
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

    void closingTheFileGoesBackToHomeAndTheFileIsInTheRecentList()
    {
        QVERIFY(click("closeFile"));
        QVERIFY(waitFor([&] { return find("homeOpen") && find("homeOpen")->isVisible(); }));
        QVERIFY(!m_doc->isOpen());
        QVERIFY(find("recent_0"));                          // the file just closed is the first recent file
        QVERIFY(click("recent_0"));
        QVERIFY(waitFor([&] { return find("pages") != nullptr && m_doc->isOpen(); }));
        QCOMPARE(m_doc->fileName(), QStringLiteral("six.pdf"));
        m_doc->close();
        QVERIFY(waitFor([&] { return find("homeOpen") && find("homeOpen")->isVisible(); }));
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
        QVERIFY(waitFor([&] { return find("homeOpen") && find("homeOpen")->isVisible(); }));
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
