// Putting things on pages: the painter / writer (tools/Overlay) measured on rendered pixels and extracted text, including Thai text, a rotated
// page, many pages (watermark, page numbers), protected files and the error cases.
#include "DocumentController.h"
#include "EditorModel.h"
#include "TestPdf.h"
#include "pdfengine/PdfDocument.h"
#include "pdfengine/PdfiumRuntime.h"
#include "pdftools/Forms.h"
#include "pdftools/Overlay.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using dp::pdf::PdfDocument;
namespace tools = dp::tools;
using namespace dp::tools;
using dp::tools::EditItem;

namespace {
QString withoutMarks(const QString& s)
{
    QString out;
    for (QChar c : s)
        if (!c.isSpace() && c.category() != QChar::Mark_NonSpacing)
            out += c;
    return out;
}
EditItem rectItem(int page, QRectF r, QColor fill)
{
    EditItem it;
    it.id = 1;
    it.kind = Kind::Rect;
    it.page = page;
    it.rect = r;
    it.fill = fill;
    it.color = fill;
    it.width = 0;
    return it;
}
} // namespace

class TstEditor : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

    QString write(const QString& name, const QByteArray& data)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() ? path : QString();
    }
    bool qpdf(const QStringList& args)
    {
        const QFileInfo q(QStringLiteral(DP_TEST_QPDF));
        if (!q.exists())
            return true;
        QProcess p;
        p.start(q.absoluteFilePath(), args);
        return p.waitForFinished(30000) && p.exitCode() == 0;
    }
    bool check(const QString& file) { return qpdf({"--check", file}); }
    EditResult save(const QString& in, const QString& out, const QList<PageEdits>& pages, const QString& pw = {}, bool overwrite = false)
    {
        return saveWithEdits(in, pw, out, pages, overwrite);
    }
    static PageEdits pageEdits(int page, const QSizeF& size, const QList<EditItem>& items)
    {
        PageEdits pe;
        pe.page = page;
        pe.size = size;
        pe.items = items;
        return pe;
    }
    static QColor at(const QImage& img, double x, double y) { return QColor(img.pixel(int(x), int(y))); }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(QFontDatabase::addApplicationFont(QStringLiteral(DP_TEST_FONT_THAI)) >= 0);
        QVERIFY(QFontDatabase::addApplicationFont(QString(QStringLiteral(DP_TEST_FONT_THAI)).replace("NotoSansThai", "Inter")) >= 0);
    }
    void cleanupTestCase() { dp::pdf::shutdownPdfium(); }

    void shapesLandWhereTheyAreAndTheOldContentStays()
    {
        const QString in = write("a.pdf", testpdf::make(2));
        const QString out = m_dir.filePath("a-out.pdf");
        EditItem rect = rectItem(0, QRectF(100, 200, 120, 60), QColor(220, 20, 20));
        EditItem ell;
        ell.kind = Kind::Ellipse;
        ell.rect = QRectF(300, 200, 100, 100);
        ell.fill = QColor(20, 20, 220);
        ell.width = 0;
        EditItem line;
        line.kind = Kind::Line;
        line.a = QPointF(100, 400);
        line.b = QPointF(300, 400);
        line.width = 6;
        line.color = QColor(0, 160, 0);
        const EditResult r = save(in, out, {pageEdits(0, QSizeF(612, 792), {rect, ell, line})});
        QVERIFY2(r.ok, qPrintable(r.error + r.detail));
        QCOMPARE(r.pages, 1);
        QVERIFY(check(out));
        auto doc = PdfDocument::open(out, {}, nullptr);
        QVERIFY(doc);
        QCOMPARE(doc->pageCount(), 2);
        const QImage img = doc->render(0, 1.0, nullptr, 0);
        QCOMPARE(img.size(), QSize(612, 792));
        QVERIFY(at(img, 160, 230).red() > 200 && at(img, 160, 230).green() < 60);            // the rectangle
        QVERIFY(at(img, 350, 250).blue() > 200 && at(img, 350, 250).red() < 60);             // the ellipse
        QVERIFY(at(img, 305, 205).lightness() > 240);                                         // the corner of the ellipse box is empty
        QVERIFY(at(img, 200, 400).green() > 120 && at(img, 200, 400).red() < 60);           // the line
        QVERIFY(at(img, 20, 20).lightness() > 240);                                           // the rest of the page is white
        QVERIFY(doc->pageText(0).contains("Page 1 of 2"));                                    // the old text is still text
        // page 2 was not touched
        const QImage second = doc->render(1, 1.0, nullptr, 0);
        QVERIFY(at(second, 160, 230).lightness() > 240);
        QVERIFY(!QFileInfo::exists(out + ".part.tmp"));
        // the source is as it was
        QCOMPARE(PdfDocument::open(in, {}, nullptr)->pageCount(), 2);
    }

    void aRotatedPageGetsTheItemWhereItIsShown()
    {
        if (!QFileInfo(QStringLiteral(DP_TEST_QPDF)).exists())
            QSKIP("qpdf.exe is not in .tools\\qpdf");
        const QString plain = write("r0.pdf", testpdf::make(1));
        const QString turned = m_dir.filePath("r90.pdf");
        QVERIFY(qpdf({"--rotate=+90", plain, turned}));
        auto before = PdfDocument::open(turned, {}, nullptr);
        QVERIFY(before);
        const QSizeF shown = before->pageSizePt(0, 0);
        QCOMPARE(shown, QSizeF(792, 612));                                                    // turned: wider than high
        const QString out = m_dir.filePath("r90-out.pdf");
        const EditResult r = save(turned, out, {pageEdits(0, shown, {rectItem(0, QRectF(10, 10, 100, 50), QColor(220, 20, 20)),
                                                                   rectItem(0, QRectF(680, 540, 100, 60), QColor(20, 20, 220))})});
        QVERIFY2(r.ok, qPrintable(r.error + r.detail));
        QVERIFY(check(out));
        auto doc = PdfDocument::open(out, {}, nullptr);
        QVERIFY(doc);
        const QImage img = doc->render(0, 1.0, nullptr, 0);
        QCOMPARE(img.size(), QSize(792, 612));
        QVERIFY(at(img, 60, 35).red() > 200 && at(img, 60, 35).green() < 60);                // top left as shown
        QVERIFY(at(img, 730, 570).blue() > 200 && at(img, 730, 570).red() < 60);             // bottom right as shown
    }

    void thaiAndEnglishTextAreDrawnShapedAndStayText()
    {
        const QString in = write("t.pdf", testpdf::make(1));
        const QString out = m_dir.filePath("t-out.pdf");
        EditItem th;
        th.kind = Kind::Text;
        th.rect = QRectF(72, 300, 400, 60);
        th.text = QString::fromUtf8("สวัสดีครับ ภาษาไทย");
        th.font = "Noto Sans Thai";
        th.fontSize = 28;
        th.color = QColor(0, 0, 0);
        EditItem en = th;
        en.rect = QRectF(72, 400, 400, 60);
        en.text = "Hello Overlay";
        en.font = "Inter";
        const EditResult r = save(in, out, {pageEdits(0, QSizeF(612, 792), {th, en})});
        QVERIFY2(r.ok, qPrintable(r.error + r.detail));
        QVERIFY(check(out));
        auto doc = PdfDocument::open(out, {}, nullptr);
        QVERIFY(doc);
        const QString text = doc->pageText(0);
        QVERIFY2(text.contains("Hello Overlay"), qPrintable(text));
        // Thai: every letter is there, in order (the exact place of the vowel / tone marks is checked separately)
        QVERIFY2(withoutMarks(text).contains(withoutMarks(th.text)), qPrintable(text));
        qInfo("THAI extracted: %s", qPrintable(text.trimmed().replace('\n', '|')));
        QVERIFY2(text.contains(th.text), qPrintable(text));                // the exact Thai text, marks in place
        // ink where the text is
        const QImage img = doc->render(0, 2.0, nullptr, 0);
        int dark = 0;
        for (int y = 300 * 2; y < 360 * 2; ++y)
            for (int x = 72 * 2; x < 400 * 2; ++x)
                if (QColor(img.pixel(x, y)).lightness() < 100)
                    ++dark;
        QVERIFY2(dark > 800, qPrintable(QString::number(dark)));
        // the text can be found with the search of the reader
        QVERIFY(!doc->searchPage(0, "Overlay", false, false).isEmpty());
    }

    void boldAndItalicChangeTheLetters()
    {
        const QString in = write("bi.pdf", testpdf::make(1));
        auto inkOf = [&](bool bold, bool italic) {
            EditItem t;
            t.kind = Kind::Text;
            t.rect = QRectF(72, 300, 450, 60);
            t.text = "Hamburgefonstiv";
            t.font = "Inter";
            t.fontSize = 28;
            t.bold = bold;
            t.italic = italic;
            const QString out = m_dir.filePath(QString("bi-%1%2.pdf").arg(bold).arg(italic));
            if (!save(in, out, {pageEdits(0, QSizeF(612, 792), {t})}).ok)
                return -1;
            const QImage img = PdfDocument::open(out, {}, nullptr)->render(0, 1.0, nullptr, 0);
            int dark = 0;
            for (int y = 300; y < 360; ++y)
                for (int x = 72; x < 522; ++x)
                    if (QColor(img.pixel(x, y)).lightness() < 128)
                        ++dark;
            return dark;
        };
        const int plain = inkOf(false, false);
        const int bold = inkOf(true, false);
        QVERIFY(plain > 300);
        qInfo("INK plain %d bold %d", plain, bold);
        QVERIFY2(bold > plain * 1.12, qPrintable(QString("plain %1 bold %2").arg(plain).arg(bold)));
        QVERIFY(inkOf(false, true) > 300);
    }

    void thaiMarksComeBackInTheRightOrder()
    {
        const QString in = write("t2.pdf", testpdf::make(1));
        const QString out = m_dir.filePath("t2-out.pdf");
        EditItem th;
        th.kind = Kind::Text;
        th.rect = QRectF(72, 300, 450, 60);
        th.text = QString::fromUtf8("ผู้ใหญ่ เก็บน้ำ ปีนี้ เกี่ยวกับ");
        th.font = "Noto Sans Thai";
        th.fontSize = 24;
        QVERIFY(save(in, out, {pageEdits(0, QSizeF(612, 792), {th})}).ok);
        auto doc = PdfDocument::open(out, {}, nullptr);
        const QString text = doc->pageText(0);
        QVERIFY2(text.contains(th.text), qPrintable(QString("wanted: %1 got: %2").arg(th.text, text.trimmed())));
    }

    void imagesAndTransparencyAndTurning()
    {
        QImage png(80, 40, QImage::Format_RGB32);
        png.fill(QColor(10, 10, 230));
        const QString png1 = m_dir.filePath("blue.png");
        QVERIFY(png.save(png1));
        const QString in = write("i.pdf", testpdf::make(1));
        EditItem im;
        im.kind = Kind::Image;
        im.rect = QRectF(100, 100, 160, 80);
        im.imagePath = png1;
        EditItem hl;
        hl.kind = Kind::Highlight;
        hl.rect = QRectF(300, 100, 100, 40);
        hl.color = QColor(255, 235, 0);
        hl.opacity = 0.4;
        EditItem turned = rectItem(0, QRectF(100, 400, 200, 40), QColor(220, 20, 20));
        turned.angle = 90;                                                                      // about its centre (200, 420)
        const EditResult r = save(in, m_dir.filePath("i-out.pdf"), {pageEdits(0, QSizeF(612, 792), {im, hl, turned})});
        QVERIFY2(r.ok, qPrintable(r.error + r.detail));
        QVERIFY(check(m_dir.filePath("i-out.pdf")));
        auto doc = PdfDocument::open(m_dir.filePath("i-out.pdf"), {}, nullptr);
        const QImage img = doc->render(0, 1.0, nullptr, 0);
        QVERIFY(at(img, 180, 140).blue() > 200 && at(img, 180, 140).red() < 40);
        const QColor mix = at(img, 350, 120);                                                   // 40 % yellow on white: not white, not full yellow
        QVERIFY2(mix.lightness() < 250 && mix.blue() > 100 && mix.blue() < 230, qPrintable(mix.name()));
        QVERIFY(at(img, 200, 340).red() > 200 && at(img, 200, 340).green() < 60);                // standing up: tall now
        QVERIFY(at(img, 120, 420).lightness() > 240);                                           // where the flat rectangle would have been
    }

    void markOnManyPagesAndPageNumbers()
    {
        const QString in = write("m.pdf", testpdf::make(3));
        MarkDef num;
        num.type = MarkDef::PageNumber;
        num.text = "Seite {n} / {total}";
        num.font = "Inter";
        num.fontSize = 12;
        MarkDef wm;
        wm.type = MarkDef::Watermark;
        wm.text = "DRAFT";
        wm.font = "Inter";
        wm.fontSize = 80;
        wm.color = QColor(200, 0, 0);
        wm.opacity = 0.25;
        wm.fromPage = 1;
        QList<PageEdits> pages;
        for (int p = 0; p < 3; ++p)
            pages << pageEdits(p, QSizeF(612, 792), marksForPage({num, wm}, p, 3, QSizeF(612, 792)));
        QCOMPARE(pages[0].items.size(), 1);                                                     // the watermark starts on page 2
        QCOMPARE(pages[1].items.size(), 2);
        const EditResult r = save(in, m_dir.filePath("m-out.pdf"), pages);
        QVERIFY2(r.ok, qPrintable(r.error + r.detail));
        QCOMPARE(r.pages, 3);
        QVERIFY(check(m_dir.filePath("m-out.pdf")));
        auto doc = PdfDocument::open(m_dir.filePath("m-out.pdf"), {}, nullptr);
        QVERIFY(doc->pageText(0).contains("Seite 1 / 3"));
        QVERIFY(doc->pageText(2).contains("Seite 3 / 3"));
        QVERIFY(doc->pageText(1).contains("DRAFT"));
        QVERIFY(!doc->pageText(0).contains("DRAFT"));
        // the watermark has light red ink in the middle of page 2
        const QImage img = doc->render(1, 1.0, nullptr, 0);
        int red = 0;
        for (int y = 250; y < 550; y += 2)
            for (int x = 150; x < 460; x += 2) {
                const QColor c = at(img, x, y);
                if (c.red() > c.green() + 40)
                    ++red;
            }
        QVERIFY2(red > 150, qPrintable(QString::number(red)));
    }

    void protectedFilesStayProtectedAndErrorsAreClear()
    {
        const QString plain = write("e.pdf", testpdf::make(2));
        const EditItem rect = rectItem(0, QRectF(100, 200, 100, 50), QColor(220, 20, 20));
        const QList<PageEdits> one = {pageEdits(0, QSizeF(612, 792), {rect})};
        QCOMPARE(save(plain, m_dir.filePath("none.pdf"), {}).error, QStringLiteral("noEdits"));
        QCOMPARE(save(plain, m_dir.filePath("none.pdf"), {pageEdits(0, QSizeF(612, 792), {})}).error, QStringLiteral("noEdits"));
        QCOMPARE(save(plain, m_dir.filePath("none.pdf"), {pageEdits(7, QSizeF(612, 792), {rect})}).error, QStringLiteral("range"));
        QVERIFY(!QFileInfo::exists(m_dir.filePath("none.pdf")));
        EditItem missing = rect;
        missing.kind = Kind::Image;
        missing.imagePath = m_dir.filePath("nothere.png");
        const EditResult m = save(plain, m_dir.filePath("none.pdf"), {pageEdits(0, QSizeF(612, 792), {missing})});
        QCOMPARE(m.error, QStringLiteral("imageMissing"));
        QCOMPARE(m.detail, missing.imagePath);
        QCOMPARE(save(write("junk.pdf", "nope"), m_dir.filePath("none.pdf"), one).error, QStringLiteral("format"));

        const QString out = m_dir.filePath("e-out.pdf");
        QVERIFY(save(plain, out, one).ok);
        QCOMPARE(save(plain, out, one).error, QStringLiteral("exists"));
        QVERIFY(save(plain, out, one, {}, true).ok);

        if (QFileInfo(QStringLiteral(DP_TEST_QPDF)).exists()) {
            const QString locked = m_dir.filePath("locked.pdf");
            QVERIFY(qpdf({"--encrypt", "open-secret", "owner-secret", "256", "--", plain, locked}));
            QCOMPARE(save(locked, m_dir.filePath("l-out.pdf"), one).error, QStringLiteral("password"));
            const EditResult r = save(locked, m_dir.filePath("l-out.pdf"), one, "open-secret");
            QVERIFY2(r.ok, qPrintable(r.error + r.detail));
            QVERIFY(r.warnings.contains("encrypted"));
            dp::pdf::OpenError err = dp::pdf::OpenError::None;
            QVERIFY(!PdfDocument::open(m_dir.filePath("l-out.pdf"), {}, &err));
            QCOMPARE(err, dp::pdf::OpenError::Password);
            auto doc = PdfDocument::open(m_dir.filePath("l-out.pdf"), "open-secret", nullptr);
            QVERIFY(doc);
            QVERIFY(at(doc->render(0, 1.0, nullptr, 0), 150, 225).red() > 200);
        }
    }

    void cancelLeavesNothingBehind()
    {
        const QString in = write("c.pdf", testpdf::make(2));
        const QString out = m_dir.filePath("c-out.pdf");
        const EditItem rect = rectItem(0, QRectF(100, 200, 100, 50), QColor(220, 20, 20));
        int calls = 0;
        EditResult r = saveWithEdits(in, {}, out, {pageEdits(0, QSizeF(612, 792), {rect})}, false, [&](int, int) { ++calls; return true; });
        QVERIFY(r.ok);
        QVERIFY(calls >= 2);
        QFile::remove(out);
        r = saveWithEdits(in, {}, out, {pageEdits(0, QSizeF(612, 792), {rect})}, false, [](int, int) { return false; });
        QVERIFY(r.cancelled);
        QVERIFY(!QFileInfo::exists(out));
        QVERIFY(!QFileInfo::exists(out + ".part.tmp"));
    }

    void thePreviewPictureFollowsTheViewRotation()
    {
        const EditItem rect = rectItem(0, QRectF(10, 10, 100, 50), QColor(220, 20, 20));
        QImage a = renderOverlay({rect}, QSizeF(612, 792), 1.0, 0);
        QCOMPARE(a.size(), QSize(612, 792));
        QVERIFY(at(a, 50, 30).red() > 200);
        QCOMPARE(QColor::fromRgba(a.pixel(300, 400)).alpha(), 0);                               // transparent elsewhere
        QImage b = renderOverlay({rect}, QSizeF(612, 792), 1.0, 1);
        QCOMPARE(b.size(), QSize(792, 612));
        QVERIFY2(at(b, 792 - 30, 50).red() > 200, "top left of the page is the top right after a clockwise turn");
        QImage c = renderOverlay({rect}, QSizeF(612, 792), 0.5, 2);
        QCOMPARE(c.size(), QSize(306, 396));
        QVERIFY(at(c, 306 - 25, 396 - 15).red() > 200);
        QImage d = renderOverlay({rect}, QSizeF(612, 792), 1.0, 0, 1);                          // the item with this id is hidden (being edited)
        QCOMPARE(QColor::fromRgba(d.pixel(50, 30)).alpha(), 0);
    }

    // ---- the editor object behind the tool bar ------------------------------------------------------------------------------------------

    void drawingSelectingMovingAndUndo()
    {
        dp::DocumentController c(m_dir.filePath("dm1"));
        dp::EditorModel e(&c);
        QCOMPARE(e.beginDraft(0, "rect", 10, 10), 0);                        // no file open
        QCOMPARE(c.open(write("em1.pdf", testpdf::make(2))), QString());
        QVERIFY(!e.modified() && !e.canUndo());

        const int id = e.beginDraft(0, "rect", 100, 100);
        QVERIFY(id > 0);
        e.updateDraft(200, 160);
        QVERIFY(e.endDraft());
        QCOMPARE(e.selectedId(), id);
        QCOMPARE(e.count(), 1);
        QVERIFY(e.modified() && e.canUndo());
        QVariantMap info = e.itemInfo(id);
        QCOMPARE(info.value("kind").toString(), QStringLiteral("rect"));
        QCOMPARE(info.value("w").toDouble(), 100.0);
        QCOMPARE(info.value("h").toDouble(), 60.0);
        QCOMPARE(e.hitTest(0, 150, 130), id);
        QCOMPARE(e.hitTest(0, 50, 50), 0);
        QCOMPARE(e.hitTest(1, 150, 130), 0);                                  // another page

        // a click without a drag does not leave an empty shape behind
        e.beginDraft(0, "ellipse", 300, 300);
        QVERIFY(!e.endDraft());
        QCOMPARE(e.count(), 1);

        e.select(id);
        // moving is one undo step for the whole gesture
        e.beginGesture();
        e.moveSelected(10, 0);
        e.moveSelected(10, 5);
        e.endGesture();
        QCOMPARE(e.itemInfo(id).value("x").toDouble(), 120.0);
        QCOMPARE(e.itemInfo(id).value("y").toDouble(), 105.0);
        e.undo();
        QCOMPARE(e.itemInfo(id).value("x").toDouble(), 100.0);
        e.redo();
        QCOMPARE(e.itemInfo(id).value("x").toDouble(), 120.0);

        // eight handles; dragging the bottom right corner resizes
        QCOMPARE(e.itemInfo(id).value("handles").toList().size(), 8);
        QCOMPARE(e.handleAt(0, 220, 165, 6), 4);
        QCOMPARE(e.handleAt(0, 150, 130, 6), -1);
        e.dragHandle(4, 260, 205);
        QCOMPARE(e.itemInfo(id).value("w").toDouble(), 140.0);
        QCOMPARE(e.itemInfo(id).value("h").toDouble(), 100.0);
        e.dragHandle(0, 500, 500);                                            // past the opposite corner: the box is flipped, never negative
        QVERIFY(e.itemInfo(id).value("w").toDouble() >= 4);

        // style: changes the selected item, the next item gets it too
        e.setStyle("fill", QColor(10, 200, 10));
        QVERIFY(e.hasFill());
        e.setStyle("noFill", true);
        QVERIFY(!e.hasFill());
        e.setStyle("color", QColor(1, 2, 3));
        e.deleteSelected();
        QCOMPARE(e.count(), 0);
        QCOMPARE(e.selectedId(), 0);
        e.undo();
        QCOMPARE(e.count(), 1);

        // other files have their own edits
        QCOMPARE(c.open(write("em2.pdf", testpdf::make(1))), QString());
        QCOMPARE(e.count(), 0);
        QVERIFY(e.anyModified());                                             // the first file still has edits
        QVERIFY(!e.modified());
        c.closeTab(c.currentTab());
        QVERIFY(QTest::qWaitFor([&] { return c.isOpen() && c.fileName() == "em1.pdf"; }, 3000));
        QCOMPARE(e.count(), 1);
        e.discardEdits();
        QCOMPARE(e.count(), 0);
        QVERIFY(!e.anyModified());
    }

    void linesPenTextAndStampsHaveSensibleHitAreas()
    {
        dp::DocumentController c(m_dir.filePath("dm2"));
        dp::EditorModel e(&c);
        QCOMPARE(c.open(write("em3.pdf", testpdf::make(1))), QString());
        const int line = e.beginDraft(0, "arrow", 50, 50);
        e.updateDraft(250, 50);
        QVERIFY(e.endDraft());
        QCOMPARE(e.hitTest(0, 150, 52), line);
        QCOMPARE(e.hitTest(0, 150, 70), 0);
        QCOMPARE(e.itemInfo(line).value("handles").toList().size(), 2);
        QCOMPARE(e.handleAt(0, 252, 51, 6), 1);
        e.dragHandle(1, 250, 150);
        QCOMPARE(e.hitTest(0, 150, 100), line);

        const int pen = e.beginDraft(0, "pen", 300, 300);
        for (int i = 1; i <= 20; ++i)
            e.updateDraft(300 + i * 5, 300 + (i % 2) * 10);
        QVERIFY(e.endDraft());
        QCOMPARE(e.hitTest(0, 350, 305), pen);

        // text: a click makes a box of a usable size; typing sets the text; a turned box is hit where it is
        const int text = e.beginDraft(0, "text", 100, 400);
        QVERIFY(e.endDraft());
        QVariantMap info = e.itemInfo(text);
        QVERIFY(info.value("w").toDouble() >= 200 && info.value("h").toDouble() >= 20);
        e.beginEdit(text);
        QCOMPARE(e.editingId(), text);
        e.setText(text, QString::fromUtf8("สวัสดี"));
        e.endEdit();
        QCOMPARE(e.editingId(), 0);
        QCOMPARE(e.itemInfo(text).value("text").toString(), QString::fromUtf8("สวัสดี"));
        e.rotateSelected(90);
        QCOMPARE(e.itemInfo(text).value("handles").toList().size(), 0);       // no handles on a turned item (it is moved or turned back)
        const double cx = info.value("x").toDouble() + info.value("w").toDouble() / 2;
        const double cy = info.value("y").toDouble() + info.value("h").toDouble() / 2;
        QCOMPARE(e.hitTest(0, cx, cy + 80), text);                            // now tall and narrow
        QCOMPARE(e.hitTest(0, cx + 80, cy), 0);

        // later items are on top; to back / to front change that
        const int stamp = e.addStamp(0, cx, cy, "");
        QCOMPARE(e.hitTest(0, cx, cy), stamp);
        e.select(text);
        e.toFront();
        QCOMPARE(e.hitTest(0, cx, cy), text);
        e.toBack();
        QCOMPARE(e.hitTest(0, cx, cy), stamp);
    }

    void imagesAndMarksAndTheOverlayPicture()
    {
        dp::DocumentController c(m_dir.filePath("dm3"));
        dp::EditorModel e(&c);
        QCOMPARE(c.open(write("em4.pdf", testpdf::make(3))), QString());
        QImage png(60, 30, QImage::Format_RGB32);
        png.fill(QColor(0, 0, 255));
        QVERIFY(png.save(m_dir.filePath("p.png")));
        QCOMPARE(e.addImage(0, m_dir.filePath("p.png"), 200, 200), QString());
        QCOMPARE(e.addImage(0, m_dir.filePath("nope.png"), 200, 200), QStringLiteral("file"));
        QFile bad(m_dir.filePath("bad.png"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("not a picture");
        bad.close();
        QCOMPARE(e.addImage(0, m_dir.filePath("bad.png"), 200, 200), QStringLiteral("format"));
        const QVariantMap info = e.selectedInfo();
        QCOMPARE(info.value("kind").toString(), QStringLiteral("image"));
        QCOMPARE(info.value("w").toDouble() / info.value("h").toDouble(), 2.0);   // the proportions of the picture
        // dragging a corner keeps them
        e.dragHandle(2, info.value("x").toDouble() + 400, info.value("y").toDouble() - 100);
        QCOMPARE(qRound(e.selectedInfo().value("w").toDouble() / e.selectedInfo().value("h").toDouble() * 100), 200);

        QVERIFY(e.mark("watermark").isEmpty());
        QCOMPARE(e.markDefaults("watermark").value("text").toString(), QStringLiteral("DRAFT"));
        e.setMark("pageNumber", {{"text", "No. {n}"}, {"size", 10}, {"font", "Inter"}});
        e.setMark("watermark", {{"text", "COPY"}, {"font", "Inter"}, {"from", 2}});
        QCOMPARE(e.mark("watermark").value("from").toInt(), 2);
        QCOMPARE(e.mark("pageNumber").value("text").toString(), QStringLiteral("No. {n}"));
        QCOMPARE(e.count(), 3);                                               // one picture + two marks
        e.setMark("watermark", {{"text", "  "}});                             // an empty text removes the mark
        QVERIFY(e.mark("watermark").isEmpty());
        e.undo();
        QVERIFY(!e.mark("watermark").isEmpty());

        // what the provider draws: items of a page, marks included, the one being edited hidden
        int hidden = -1;
        const QList<EditItem> p0 = e.itemsForPage(c.docId(), 0, QSizeF(612, 792), 3, &hidden);
        QCOMPARE(p0.size(), 2);                                               // page number + picture (the watermark starts on page 2)
        QCOMPARE(hidden, 0);
        QCOMPARE(e.itemsForPage(c.docId(), 1, QSizeF(612, 792), 3, nullptr).size(), 2);       // number + watermark (the picture is on page 1)
        QCOMPARE(e.itemsForPage(c.docId() + 99, 0, QSizeF(612, 792), 3, nullptr).size(), 0);
    }

    void savingBurnsEverythingInAndOnlyOnRequest()
    {
        dp::DocumentController c(m_dir.filePath("dm4"));
        dp::EditorModel e(&c);
        const QString in = write("em5.pdf", testpdf::make(2));
        QCOMPARE(c.open(in), QString());
        QCOMPARE(e.save(m_dir.filePath("x.pdf")), QStringLiteral("noEdits"));
        const int r = e.beginDraft(0, "rect", 100, 100);
        e.updateDraft(220, 180);
        QVERIFY(e.endDraft());
        e.setStyle("fill", QColor(200, 20, 20));
        e.setStyle("width", 0);
        e.setMark("pageNumber", {{"text", "p. {n}"}, {"font", "Inter"}});
        QVERIFY(r > 0);
        QVERIFY(e.modified());

        QCOMPARE(e.save(in), QStringLiteral("sameFile"));
        const QString out = m_dir.filePath("em5-out.pdf");
        QSignalSpy done(&e, &dp::EditorModel::saveFinished);
        QCOMPARE(e.save(out), QString());
        QVERIFY(e.busy());
        QCOMPARE(e.save(m_dir.filePath("other.pdf")), QStringLiteral("busy"));
        QVERIFY(QTest::qWaitFor([&] { return done.count() > 0; }, 30000));
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        QCOMPARE(done.first().at(4).toInt(), 2);
        QVERIFY(!e.modified());                                               // saved: nothing to lose
        QCOMPARE(e.count(), 2);                                               // but still editable (the shape and the page number mark)
        QCOMPARE(e.save(out), QStringLiteral("exists"));
        auto doc = PdfDocument::open(out, {}, nullptr);
        QVERIFY(doc);
        QVERIFY(at(doc->render(0, 1.0, nullptr, 0), 160, 140).red() > 190);
        QVERIFY(doc->pageText(1).contains("p. 2"));
        QCOMPARE(PdfDocument::open(in, {}, nullptr)->pageText(1).contains("p. 2"), false);   // the open file was not touched
    }

    void polygonsAndNotesArePaintedAndHit()
    {
        const QString in = write("pn.pdf", testpdf::make(1));
        EditItem poly;
        poly.kind = Kind::Polygon;
        poly.points = {QPointF(100, 100), QPointF(250, 100), QPointF(175, 220)};         // a triangle
        poly.fill = QColor(20, 160, 20);
        poly.width = 0;
        EditItem note;
        note.kind = Kind::Note;
        note.rect = QRectF(300, 300, 180, 100);
        note.text = QString::fromUtf8("โน้ต: ตรวจแล้ว");
        note.font = "Noto Sans Thai";
        note.fontSize = 14;
        note.fill = QColor(255, 240, 140);
        const EditResult r = save(in, m_dir.filePath("pn-out.pdf"), {pageEdits(0, QSizeF(612, 792), {poly, note})});
        QVERIFY2(r.ok, qPrintable(r.error + r.detail));
        QVERIFY(check(m_dir.filePath("pn-out.pdf")));
        auto doc = PdfDocument::open(m_dir.filePath("pn-out.pdf"), {}, nullptr);
        const QImage img = doc->render(0, 1.0, nullptr, 0);
        QVERIFY(at(img, 175, 130).green() > 140 && at(img, 175, 130).red() < 60);       // inside the triangle
        QVERIFY(at(img, 110, 200).lightness() > 240);                                     // outside it (the corner of its box)
        QVERIFY(at(img, 470, 395).red() > 240 && at(img, 470, 395).blue() < 200);        // the yellow note, away from its text
        QVERIFY2(doc->pageText(0).contains(note.text), qPrintable(doc->pageText(0)));

        dp::DocumentController c(m_dir.filePath("dm5"));
        dp::EditorModel e(&c);
        QCOMPARE(c.open(write("pn2.pdf", testpdf::make(1))), QString());
        QCOMPARE(e.beginDraft(0, "polygon", 1, 1), 0);                                    // a polygon is made by clicks, not by a drag
        const int id = e.beginPolygon(0, 100, 100);
        QVERIFY(id > 0 && e.polygonOpen());
        e.movePolygon(200, 100);
        e.addPolygonPoint(200, 100);
        e.movePolygon(150, 200);
        e.addPolygonPoint(150, 200);
        e.movePolygon(160, 210);
        QVERIFY(e.finishPolygon());
        QVERIFY(!e.polygonOpen());
        QCOMPARE(e.hitTest(0, 150, 130), id);                                             // inside, not only on the edge
        QCOMPARE(e.hitTest(0, 400, 400), 0);
        QCOMPARE(e.itemInfo(id).value("handles").toList().size(), 8);
        // two corners are no polygon
        e.beginPolygon(0, 300, 300);
        e.addPolygonPoint(350, 300);
        QVERIFY(!e.finishPolygon());
        QCOMPARE(e.count(), 1);
        // a note starts with the yellow card and a usable size
        const int note1 = e.beginDraft(0, "note", 300, 300);
        QVERIFY(e.endDraft());
        QVERIFY(e.itemInfo(note1).value("w").toDouble() >= 180 && e.itemInfo(note1).value("h").toDouble() >= 100);
        QVERIFY(e.hasFill() == false);                                                    // the next item is not yellow because of it
        e.beginEdit(note1);
        QCOMPARE(e.editingId(), note1);
        e.endEdit();
    }

    void unsavedEditsSurviveAClosedProgramAndCanBeRestoredOrDeleted()
    {
        const QString file = write("rec.pdf", testpdf::make(1));
        const QString data = m_dir.filePath("recdata");
        {
            dp::DocumentController c(data);
            dp::EditorModel e(&c);
            QCOMPARE(c.open(file), QString());
            QVERIFY(!e.recoverable());
            const int id = e.beginDraft(0, "rect", 100, 100);
            e.updateDraft(220, 180);
            QVERIFY(e.endDraft());
            e.setStyle("fill", QColor(10, 20, 200));
            e.setMark("pageNumber", {{"text", "No. {n}"}, {"font", "Inter"}});
            const int t = e.beginDraft(0, "text", 100, 300);
            QVERIFY(e.endDraft());
            e.setText(t, QString::fromUtf8("กู้คืนได้"));
            QVERIFY(id > 0);
            QTest::qWait(2600);                                                       // the autosave runs a moment after the last change
            QVERIFY(QDir(data + "/recovery").entryList({"*.json"}).size() == 1);
        }                                                                              // "the program is closed" without saving
        {
            dp::DocumentController c(data);
            dp::EditorModel e(&c);
            QCOMPARE(c.open(file), QString());
            QVERIFY(e.recoverable());
            QCOMPARE(e.count(), 0);
            QVERIFY(e.recoverySourceMatches());
            QVERIFY(e.restoreRecovery());
            QVERIFY(!e.recoverable());
            QCOMPARE(e.count(), 3);                                                    // rectangle, text, page number mark
            QVERIFY(e.modified());
            QVERIFY(e.hitTest(0, 150, 130) > 0);
            const int text = e.hitTest(0, 150, 310);
            QVERIFY(text > 0);
            QCOMPARE(e.itemInfo(text).value("text").toString(), QString::fromUtf8("กู้คืนได้"));
            QCOMPARE(e.mark("pageNumber").value("text").toString(), QStringLiteral("No. {n}"));
            // new items do not clash with the restored ones
            const int next = e.beginDraft(0, "ellipse", 300, 100);
            e.updateDraft(380, 160);
            QVERIFY(e.endDraft());
            QVERIFY(next > text);
            e.undo();
            // saving makes the recovery file unnecessary
            QSignalSpy done(&e, &dp::EditorModel::saveFinished);
            QCOMPARE(e.save(m_dir.filePath("rec-out.pdf")), QString());
            QVERIFY(QTest::qWaitFor([&] { return done.count() > 0; }, 30000));
            QVERIFY(done.first().at(0).toBool());
            QCOMPARE(QDir(data + "/recovery").entryList({"*.json"}).size(), 0);
        }
        {   // declining deletes it
            dp::DocumentController c(data);
            dp::EditorModel e(&c);
            QCOMPARE(c.open(file), QString());
            QVERIFY(!e.recoverable());
            e.beginDraft(0, "rect", 10, 10);
            e.updateDraft(90, 90);
            QVERIFY(e.endDraft());
            QTest::qWait(2600);
        }
        QCOMPARE(write("rec.pdf", testpdf::make(2)), file); // replace the source after the draft was written
        {
            dp::DocumentController c(data);
            dp::EditorModel e(&c);
            QCOMPARE(c.open(file), QString());
            QVERIFY(e.recoverable());
            QVERIFY(!e.recoverySourceMatches());
            QVERIFY(!e.restoreRecovery());
            e.declineRecovery();
            QVERIFY(!e.recoverable());
            QCOMPARE(QDir(data + "/recovery").entryList({"*.json"}).size(), 0);
            dp::EditorModel again(&c);
            QVERIFY(!again.recoverable());
        }
    }

    void batesNumbersArePaddedAndStartWherePagesStart()
    {
        MarkDef m;
        m.type = MarkDef::Footer;
        m.text = "CASE-{n:0000} of {total} ({n})";
        const QList<EditItem> p = marksForPage({m}, 2, 12, QSizeF(612, 792));
        QCOMPARE(p.size(), 1);
        QCOMPARE(p.first().text, QStringLiteral("CASE-0003 of 12 (3)"));
        m.text = "{n:00}/{n:000000}";
        QCOMPARE(marksForPage({m}, 9, 12, QSizeF(612, 792)).first().text, QStringLiteral("10/000010"));
    }

    void formFieldsAreCreatedAsRealFormFieldsAndCanBeFilledAndSeenByTheEditor()
    {
        dp::DocumentController c(m_dir.filePath("dform"));
        dp::EditorModel e(&c);
        const QString in = write("form-src.pdf", testpdf::make(2));
        QCOMPARE(c.open(in), QString());
        const int t = e.beginDraft(0, "formText", 100, 100);
        e.updateDraft(300, 126);
        QVERIFY(e.endDraft());
        QCOMPARE(e.itemInfo(t).value("text").toString(), QStringLiteral("Text1"));
        const int k = e.beginDraft(0, "formCheck", 100, 160);                  // a click: a usable square box
        QVERIFY(e.endDraft());
        QCOMPARE(e.itemInfo(k).value("w").toDouble(), 18.0);
        QCOMPARE(e.itemInfo(k).value("text").toString(), QStringLiteral("Check1"));
        const int l = e.beginDraft(1, "formChoice", 100, 200);
        QVERIFY(e.endDraft());
        QCOMPARE(e.itemInfo(l).value("text").toString(), QStringLiteral("List1"));
        QCOMPARE(e.itemInfo(l).value("optionsText").toString(), QStringLiteral("Option 1, Option 2, Option 3"));
        e.setOptions(l, "Thailand, Japan\nFrance");
        QCOMPARE(e.itemInfo(l).value("optionsText").toString(), QStringLiteral("Thailand, Japan, France"));
        const int t2 = e.beginDraft(0, "formText", 100, 250);
        e.updateDraft(300, 276);
        QVERIFY(e.endDraft());
        e.beginEdit(t2);                                                        // the name can be typed
        e.setText(t2, "Text1");                                                // the same name: the written field gets another one
        e.endEdit();
        // the editor shows a marker for them; the saved file has real fields
        const QImage ov = tools::renderOverlay(e.itemsForPage(c.docId(), 0, QSizeF(612, 792), 2, nullptr), QSizeF(612, 792), 1.0, 0, 0);
        QVERIFY(QColor::fromRgba(ov.pixel(150, 100)).alpha() > 0);

        const QString out = m_dir.filePath("form-out.pdf");
        QSignalSpy done(&e, &dp::EditorModel::saveFinished);
        QCOMPARE(e.save(out), QString());
        QVERIFY(QTest::qWaitFor([&] { return done.count() > 0; }, 30000));
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        QVERIFY(check(out));
        const tools::FormRead r = tools::readFormFields(out, {});
        QVERIFY(r.ok && r.hasForm);
        QCOMPARE(r.fields.size(), 4);
        auto field = [&](const QString& n) -> const tools::FormField* { for (const tools::FormField& f : r.fields) if (f.name == n) return &f; return nullptr; };
        QVERIFY(field("Text1") && field("Text1")->type == "text" && field("Text1")->page == 0);
        QVERIFY(field("Text1_2") && field("Text1_2")->type == "text");
        QVERIFY(field("Check1") && field("Check1")->type == "checkbox");
        QVERIFY(field("List1") && field("List1")->type == "choice" && field("List1")->page == 1);
        QCOMPARE(field("List1")->options, (QStringList{"Thailand", "Japan", "France"}));
        // they can be filled in, and the filled copy shows the values
        tools::FormFill f = tools::fillForm(out, {}, m_dir.filePath("form-filled.pdf"), {{"Text1", "Hello"}, {"Check1", true}, {"List1", "Japan"}}, false);
        QVERIFY2(f.ok, qPrintable(f.error));
        QCOMPARE(f.filled, 3);
        auto doc = PdfDocument::open(m_dir.filePath("form-filled.pdf"), {}, nullptr);
        QVERIFY(doc);
        const QImage page = doc->render(0, 1.0, nullptr, 0);
        int dark = 0;                                                           // the tick in the box at (100..118, 160..178)
        for (int y = 160; y < 178; ++y)
            for (int x = 100; x < 118; ++x)
                if (QColor(page.pixel(x, y)).lightness() < 110)
                    ++dark;
        QVERIFY2(dark > 10, qPrintable(QString::number(dark)));
        int ink = 0;                                                            // "Hello" in the text field
        for (int y = 100; y < 126; ++y)
            for (int x = 102; x < 300; ++x)
                if (QColor(page.pixel(x, y)).lightness() < 110)
                    ++ink;
        QVERIFY2(ink > 40, qPrintable(QString::number(ink)));
        // the empty field boxes are drawn by the reader too (their border)
        const QImage empty = PdfDocument::open(out, {}, nullptr)->render(0, 1.0, nullptr, 0);
        QVERIFY(QColor(empty.pixel(150, 100)).lightness() < 250 || QColor(empty.pixel(150, 101)).lightness() < 250);
    }

    void bundledFontsMayBeEmbedded()
    {
        EditItem t;
        t.kind = Kind::Text;
        t.font = "Noto Sans Thai";
        QVERIFY(restrictedFonts({t}, {}).isEmpty());
        QVERIFY(restrictedFonts({}, {}).isEmpty());
    }
    void movingSnapsToThePageEdgeAndSticksOnlyWhileClose()
    {
        dp::DocumentController c(m_dir.filePath("dm2"));
        dp::EditorModel e(&c);
        QCOMPARE(c.open(write("em2.pdf", testpdf::make(1))), QString());
        QVERIFY(e.snapEnabled());
        const int id = e.beginDraft(0, "rect", 100, 100);
        e.updateDraft(200, 160);
        QVERIFY(e.endDraft());

        e.beginGesture();
        e.moveSelected(-98, 0);                                              // the left edge would be at 2: it sticks to the page edge 0
        QCOMPARE(e.itemInfo(id).value("x").toDouble(), 0.0);
        QCOMPARE(e.guides().size(), 1);
        QCOMPARE(e.guides().first().toMap().value("pos").toDouble(), 0.0);
        e.moveSelected(10, 0);                                               // the pointer is at 12 now: the item follows it (no runaway)
        QCOMPARE(e.itemInfo(id).value("x").toDouble(), 12.0);
        QVERIFY(e.guides().isEmpty());
        e.endGesture();

        e.setSnapEnabled(false);
        e.beginGesture();
        e.moveSelected(-10, 0);                                              // the edge would be at 2: no snap when switched off
        QCOMPARE(e.itemInfo(id).value("x").toDouble(), 2.0);
        e.endGesture();

        e.setSnapEnabled(true);
        e.moveSelected(30, 0);                                               // outside a gesture (keys) nothing snaps
        QCOMPARE(e.itemInfo(id).value("x").toDouble(), 32.0);
    }
    void aPictureCanBeReplacedAndKeepsItsCentreAndWidth()
    {
        dp::DocumentController c(m_dir.filePath("dm3"));
        dp::EditorModel e(&c);
        QCOMPARE(c.open(write("em3.pdf", testpdf::make(1))), QString());
        QImage wide(40, 20, QImage::Format_RGB32), tall(20, 40, QImage::Format_RGB32);
        wide.fill(Qt::red);
        tall.fill(Qt::blue);
        QVERIFY(wide.save(m_dir.filePath("wide.png")) && tall.save(m_dir.filePath("tall.png")));
        QCOMPARE(e.addImage(0, m_dir.filePath("wide.png"), 200, 300), QString());
        const int id = e.selectedId();
        QCOMPARE(e.itemInfo(id).value("w").toDouble(), 40.0);
        QCOMPARE(e.itemInfo(id).value("h").toDouble(), 20.0);

        QCOMPARE(e.replaceImage(id, m_dir.filePath("tall.png")), QString());
        const QVariantMap info = e.itemInfo(id);
        QCOMPARE(info.value("w").toDouble(), 40.0);
        QCOMPARE(info.value("h").toDouble(), 80.0);                          // the new proportions
        QCOMPARE(info.value("x").toDouble() + 20.0, 200.0);                  // the centre stays
        QCOMPARE(info.value("y").toDouble() + 40.0, 300.0);
        e.undo();
        QCOMPARE(e.itemInfo(id).value("h").toDouble(), 20.0);

        QCOMPARE(e.replaceImage(id, m_dir.filePath("missing.png")), QStringLiteral("file"));
        write("notimage.png", "not a picture");
        QCOMPARE(e.replaceImage(id, m_dir.filePath("notimage.png")), QStringLiteral("format"));
        const int rect = e.beginDraft(0, "rect", 10, 10);
        e.updateDraft(60, 60);
        QVERIFY(e.endDraft());
        QCOMPARE(e.replaceImage(rect, m_dir.filePath("tall.png")), QStringLiteral("notImage"));
    }
};

QTEST_MAIN(TstEditor)
#include "tst_editor.moc"
