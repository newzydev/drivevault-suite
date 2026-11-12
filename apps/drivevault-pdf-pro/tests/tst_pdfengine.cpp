// The PDFium wrapper against real (generated) PDF files: page count and sizes, rendering measured on pixels, text, encryption.
#include "TestPdf.h"
#include "pdfengine/PdfDocument.h"
#include "pdfengine/PdfiumRuntime.h"

#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <atomic>

using namespace dp::pdf;

class TstPdfEngine : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

    QString write(const QString& name, const QByteArray& data)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly))
            return {};
        f.write(data);
        return path;
    }
    static int darkPixels(const QImage& img, int fromY = 0)
    {
        int n = 0;
        for (int y = fromY; y < img.height(); ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x)
                if (qGray(line[x]) < 100)
                    ++n;
        }
        return n;
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }
    void cleanupTestCase() { shutdownPdfium(); }

    void opensAFileAndReportsPagesAndSizes()
    {
        const QString path = write("three.pdf", testpdf::make({QSizeF(612, 792), QSizeF(595, 842), QSizeF(300, 400)},
                                                              {"one", "two", "three"}));
        OpenError err = OpenError::Unknown;
        auto doc = PdfDocument::open(path, {}, &err);
        QVERIFY(doc);
        QCOMPARE(err, OpenError::None);
        QCOMPARE(doc->pageCount(), 3);
        QCOMPARE(doc->pageSizePt(0), QSizeF(612, 792));
        QCOMPARE(doc->pageSizePt(1), QSizeF(595, 842));
        QCOMPARE(doc->pageSizePt(2), QSizeF(300, 400));
        QCOMPARE(doc->path(), path);
    }

    void refusesFilesThatAreNotPdfOrMissing()
    {
        OpenError err = OpenError::None;
        QVERIFY(!PdfDocument::open(write("text.pdf", "this is not a pdf file at all"), {}, &err));
        QCOMPARE(err, OpenError::Format);
        QVERIFY(!PdfDocument::open(write("empty.pdf", ""), {}, &err));
        QCOMPARE(err, OpenError::Format);
        QVERIFY(!PdfDocument::open(m_dir.filePath("missing.pdf"), {}, &err));
        QCOMPARE(err, OpenError::File);
    }

    void rendersWhiteBackgroundWithTheTextOnIt()
    {
        auto doc = PdfDocument::open(write("page.pdf", testpdf::make(1)), {}, nullptr);
        QVERIFY(doc);
        const QImage img = doc->render(0, 1.0);          // 1 px per point
        QCOMPARE(img.size(), QSize(612, 792));
        QCOMPARE(QColor(img.pixel(2, 2)), QColor(Qt::white));
        const int dark = darkPixels(img);
        QVERIFY2(dark > 300, qPrintable(QString("only %1 dark pixels: the text was not drawn").arg(dark)));
        // the text is near the top (PDF y = 692), so the bottom half stays white
        QCOMPARE(darkPixels(img, img.height() / 2), 0);
    }

    void renderSizeFollowsTheScaleAndIsClamped()
    {
        auto doc = PdfDocument::open(write("scale.pdf", testpdf::make(1)), {}, nullptr);
        QVERIFY(doc);
        QCOMPARE(doc->render(0, 2.0).size(), QSize(1224, 1584));
        QCOMPARE(doc->render(0, 96.0 / 72.0).size(), QSize(816, 1056));
        const QImage huge = doc->render(0, 1000.0);        // would be 612000 px wide
        QVERIFY(!huge.isNull());
        QVERIFY(huge.width() <= 16384 && huge.height() <= 16384);
        QVERIFY(qint64(huge.width()) * huge.height() <= 64000000);
        QVERIFY(doc->render(5, 1.0).isNull());              // no such page
        QVERIFY(doc->render(-1, 1.0).isNull());
        QVERIFY(doc->render(0, 0.0).isNull());
    }

    void aCancelledRenderGivesNothing()
    {
        auto doc = PdfDocument::open(write("cancel.pdf", testpdf::make(1)), {}, nullptr);
        QVERIFY(doc);
        std::atomic_bool cancel{true};
        QVERIFY(doc->render(0, 1.0, &cancel).isNull());
        cancel = false;
        QVERIFY(!doc->render(0, 1.0, &cancel).isNull());
    }

    void extractsThePageText()
    {
        auto doc = PdfDocument::open(write("text2.pdf", testpdf::make(3)), {}, nullptr);
        QVERIFY(doc);
        QCOMPARE(doc->pageText(0).trimmed(), QStringLiteral("Page 1 of 3"));
        QCOMPARE(doc->pageText(2).trimmed(), QStringLiteral("Page 3 of 3"));
        QVERIFY(doc->pageText(9).isEmpty());
    }

    void manyPagesOpenAndEveryPageRenders()
    {
        auto doc = PdfDocument::open(write("many.pdf", testpdf::make(600)), {}, nullptr);
        QVERIFY(doc);
        QCOMPARE(doc->pageCount(), 600);
        QVERIFY(!doc->render(599, 0.5).isNull());
        QCOMPARE(doc->pageText(599).trimmed(), QStringLiteral("Page 600 of 600"));
    }

    void rendersFromSeveralThreadsAtOnce()
    {
        auto doc = PdfDocument::open(write("threads.pdf", testpdf::make(8)), {}, nullptr);
        QVERIFY(doc);
        std::atomic_int ok{0};
        QList<QThread*> threads;
        for (int t = 0; t < 4; ++t) {
            QThread* th = QThread::create([&] {
                for (int i = 0; i < 8; ++i)
                    if (darkPixels(doc->render(i, 0.7)) > 100 && doc->pageText(i).contains(QStringLiteral("Page")))
                        ++ok;
            });
            threads.append(th);
            th->start();
        }
        for (QThread* th : threads) {
            th->wait();
            delete th;
        }
        QCOMPARE(int(ok), 32);
    }

    void rotationSwapsTheSizeAndTurnsThePicture()
    {
        auto doc = PdfDocument::open(write("rot.pdf", testpdf::make(1)), {}, nullptr);
        QVERIFY(doc);
        QCOMPARE(doc->pageSizePt(0, 0), QSizeF(612, 792));
        QCOMPARE(doc->pageSizePt(0, 1), QSizeF(792, 612));
        QCOMPARE(doc->pageSizePt(0, 2), QSizeF(612, 792));
        QCOMPARE(doc->pageSizePt(0, 5), QSizeF(792, 612));      // 5 = 1 (any number of turns is accepted)
        const QImage turned = doc->render(0, 1.0, nullptr, 1);
        QCOMPARE(turned.size(), QSize(792, 612));
        // the text sits at the top left of the upright page: turned 90 degrees clockwise it is at the top right
        int left = 0, right = 0;
        for (int y = 0; y < turned.height(); ++y)
            for (int x = 0; x < turned.width(); ++x)
                if (qGray(turned.pixel(x, y)) < 100)
                    (x < turned.width() / 2 ? left : right)++;
        QVERIFY2(right > 300 && left == 0, qPrintable(QString("left %1 right %2").arg(left).arg(right)));
    }

    void searchFindsEveryMatchWithRectanglesOnThePage()
    {
        auto doc = PdfDocument::open(write("feature.pdf", testpdf::makeFeatureDoc()), {}, nullptr);
        QVERIFY(doc);
        QCOMPARE(doc->searchPage(0, "search", false, false).size(), 3);       // "search", "search", "SEARCH"
        QCOMPARE(doc->searchPage(0, "search", true, false).size(), 2);        // match case
        QCOMPARE(doc->searchPage(0, "SEARCH", true, false).size(), 1);
        QCOMPARE(doc->searchPage(0, "wor", false, true).size(), 0);           // whole word only
        QCOMPARE(doc->searchPage(0, "world", false, true).size(), 1);
        QCOMPARE(doc->searchPage(1, "search", false, false).size(), 0);
        QCOMPARE(doc->searchPage(1, "Second", false, false).size(), 1);
        QVERIFY(doc->searchPage(0, "", false, false).isEmpty());
        QVERIFY(doc->searchPage(0, "nothing like this", false, false).isEmpty());

        const QList<SearchHit> hits = doc->searchPage(0, "search", false, false);
        QCOMPARE(hits.first().rects.size(), 1);
        const QRectF r = hits.first().rects.first();
        // "Hello search world" is drawn at PDF y = 700 (baseline): about 92 points from the top of the page, "search" starts after "Hello "
        QVERIFY2(r.left() > 100 && r.left() < 140 && r.top() > 70 && r.bottom() < 115 && r.width() > 30 && r.width() < 80,
                 qPrintable(QString("%1,%2 %3x%4").arg(r.left()).arg(r.top()).arg(r.width()).arg(r.height())));
        // rotated view: the same word, turned with the page
        const QRectF r1 = doc->searchPage(0, "search", false, false, 1).first().rects.first();
        QVERIFY2(r1.left() > 600 && r1.top() > 100 && r1.top() < 160, qPrintable(QString("%1,%2").arg(r1.left()).arg(r1.top())));
    }

    void textRectanglesAndCharacterIndexesAgree()
    {
        auto doc = PdfDocument::open(write("feature2.pdf", testpdf::makeFeatureDoc()), {}, nullptr);
        QVERIFY(doc);
        const QString text = doc->pageText(0);
        const int idx = text.indexOf("world");
        QVERIFY(idx > 0);
        QCOMPARE(doc->textRange(0, idx, 5), QStringLiteral("world"));
        QVERIFY(doc->charCount(0) >= text.size() - 2);
        for (int rotation : {0, 1, 2, 3}) {
            const QList<QRectF> rects = doc->textRects(0, idx, 5, rotation);
            QCOMPARE(rects.size(), 1);
            const int found = doc->charIndexAt(0, rects.first().center(), 2.0, rotation);
            QVERIFY2(found >= idx && found < idx + 5, qPrintable(QString("rotation %1: %2 not in %3..%4").arg(rotation).arg(found).arg(idx).arg(idx + 4)));
            const QRectF glyph = doc->textRects(0, idx, 1, rotation).first();
            const bool vertical = rotation % 2 != 0;
            const QPointF offset = vertical ? QPointF(0, glyph.height() / 4) : QPointF(glyph.width() / 4, 0);
            const QPointF forward = rotation < 2 ? offset : -offset;
            const auto leading = doc->textPositionAt(0, glyph.center() - forward, 0, rotation);
            const auto trailing = doc->textPositionAt(0, glyph.center() + forward, 0, rotation);
            QCOMPARE(leading.boundary, idx);
            QCOMPARE(trailing.boundary, idx + 1);
            QVERIFY(vertical ? leading.caret.width() > 0 : leading.caret.height() > 0);
        }
        QCOMPARE(doc->charIndexAt(0, QPointF(500, 700), 1.0), -1);          // empty place
        QVERIFY(doc->textRange(0, 0, 0).isEmpty());
        QVERIFY(doc->textRects(0, 0, 0).isEmpty());
    }

    void theOutlineKeepsTheNestingAndThePages()
    {
        auto doc = PdfDocument::open(write("feature3.pdf", testpdf::makeFeatureDoc()), {}, nullptr);
        QVERIFY(doc);
        const QList<OutlineItem> o = doc->outline();
        QCOMPARE(o.size(), 3);
        QCOMPARE(o.at(0).title, QStringLiteral("Chapter 1"));
        QCOMPARE(o.at(0).page, 0);
        QCOMPARE(o.at(0).level, 0);
        QCOMPARE(o.at(1).title, QStringLiteral("Chapter 2"));
        QCOMPARE(o.at(1).page, 1);
        QCOMPARE(o.at(2).title, QStringLiteral("Section 2.1"));
        QCOMPARE(o.at(2).page, 2);
        QCOMPARE(o.at(2).level, 1);
        auto plain = PdfDocument::open(write("plain3.pdf", testpdf::make(2)), {}, nullptr);
        QVERIFY(plain->outline().isEmpty());
    }

    void linksPointToAPageOrToAWebAddress()
    {
        auto doc = PdfDocument::open(write("feature4.pdf", testpdf::makeFeatureDoc()), {}, nullptr);
        QVERIFY(doc);
        // the page link is at PDF y 500..540 = 252..292 from the top
        const LinkTarget toPage = doc->linkAt(0, QPointF(150, 270));
        QCOMPARE(toPage.kind, LinkTarget::Page);
        QCOMPARE(toPage.page, 1);
        const LinkTarget toWeb = doc->linkAt(0, QPointF(150, 330));
        QCOMPARE(toWeb.kind, LinkTarget::Uri);
        QCOMPARE(toWeb.uri, QStringLiteral("https://example.com/"));
        QCOMPARE(doc->linkAt(0, QPointF(450, 450)).kind, LinkTarget::None);
        QCOMPARE(doc->linkAt(1, QPointF(150, 270)).kind, LinkTarget::None);         // page 2 has no links
        // with the page turned the same link is somewhere else
        QCOMPARE(doc->linkAt(0, QPointF(150, 270), 1).kind, LinkTarget::None);
    }

    void annotationsAreListedWithTheirTextAndPlace()
    {
        auto doc = PdfDocument::open(write("feature5.pdf", testpdf::makeFeatureDoc()), {}, nullptr);
        QVERIFY(doc);
        const QList<AnnotationInfo> a = doc->annotations();
        QCOMPARE(a.size(), 2);                      // the two links are not annotations to show
        QCOMPARE(a.at(0).type, QStringLiteral("Text"));
        QCOMPARE(a.at(0).contents, QStringLiteral("Remember this"));
        QCOMPARE(a.at(0).page, 0);
        QVERIFY(qAbs(a.at(0).rect.left() - 300) < 2 && qAbs(a.at(0).rect.top() - (792 - 620)) < 2);
        QCOMPARE(a.at(1).type, QStringLiteral("Highlight"));
        QCOMPARE(a.at(1).contents, QStringLiteral("Important"));
        QVERIFY(doc->annotations().size() == 2);
        auto plain = PdfDocument::open(write("plain5.pdf", testpdf::make(2)), {}, nullptr);
        QVERIFY(plain->annotations().isEmpty());
    }

    void thaiTextWrittenByQtIsExtractedSearchedAndSelectable()
    {
        const int fontId = QFontDatabase::addApplicationFont(QStringLiteral(DP_TEST_FONT_THAI));
        QVERIFY(fontId >= 0);
        const QString family = QFontDatabase::applicationFontFamilies(fontId).value(0);
        const QString thai = QString::fromUtf8("สวัสดีชาวโลก โปรแกรมอ่านไฟล์ PDF ภาษาไทย");
        const QString path = m_dir.filePath("thai.pdf");
        {
            QPdfWriter writer(path);
            writer.setPageSize(QPageSize(QPageSize::A4));
            QPainter painter(&writer);
            QFont font(family);
            font.setPixelSize(80);
            painter.setFont(font);
            painter.drawText(QPointF(300, 1200), thai);
            painter.end();
        }
        auto doc = PdfDocument::open(path, {}, nullptr);
        QVERIFY(doc);
        // A file written by Qt keeps every letter but not the exact place of the Thai vowel / tone marks (the marks are separate glyphs that
        // are drawn with an offset and come out after the next letter) and the spaces around Latin words. So the check is on the letters
        // without marks and spaces: all of them are there, in order. (Our own writer in Phase 4 has to do better than this.)
        auto base = [](QString s) {
            QString out;
            for (const QChar c : s) {
                const ushort u = c.unicode();
                const bool mark = u == 0x0E31 || (u >= 0x0E34 && u <= 0x0E3A) || (u >= 0x0E47 && u <= 0x0E4E);
                if (!mark && !c.isSpace())
                    out += c;
            }
            return out;
        };
        const QString extracted = doc->pageText(0);
        QCOMPARE(base(extracted), base(thai));
        QVERIFY(extracted.contains(QString::fromUtf8("ชาวโลก")));          // a word without marks above the line is exact
        QVERIFY(extracted.contains(QStringLiteral("PDF")));
        // search in Thai, and the character under the middle of the hit is part of the hit
        const QList<SearchHit> hits = doc->searchPage(0, QString::fromUtf8("ชาวโลก"), false, false);
        QCOMPARE(hits.size(), 1);
        QVERIFY(!hits.first().rects.isEmpty());
        const QPointF mid = hits.first().rects.first().center();
        const int idx = doc->charIndexAt(0, mid, 3.0);
        QVERIFY(idx >= 0);
        const QString around = doc->textRange(0, qMax(0, idx - 3), 8);
        QVERIFY2(around.contains(QString::fromUtf8("าว")) || around.contains(QString::fromUtf8("โลก")) || around.contains(QString::fromUtf8("ช")),
                 qPrintable(around));
    }

    void anEncryptedFileNeedsThePassword()
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

        OpenError err = OpenError::None;
        QVERIFY(!PdfDocument::open(locked, {}, &err));
        QCOMPARE(err, OpenError::Password);
        QVERIFY(!PdfDocument::open(locked, "wrong", &err));
        QCOMPARE(err, OpenError::Password);
        auto doc = PdfDocument::open(locked, "open-secret", &err);
        QVERIFY(doc);
        QCOMPARE(err, OpenError::None);
        QCOMPARE(doc->pageCount(), 2);
        QCOMPARE(doc->pageText(1).trimmed(), QStringLiteral("Page 2 of 2"));
        QVERIFY(darkPixels(doc->render(0, 1.0)) > 300);
    }
};

QTEST_MAIN(TstPdfEngine)
#include "tst_pdfengine.moc"
