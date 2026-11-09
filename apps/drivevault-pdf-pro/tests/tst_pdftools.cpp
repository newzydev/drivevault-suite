// qpdf tools: merge, reorder, delete, duplicate, rotate, crop, blank pages, links, warnings, protected sources, split plans.
// The results are checked by opening the written files with the PDFium reader (page count, text, size, pixels), not only by return values.
#include "TestPdf.h"
#include "pdfengine/PdfDocument.h"
#include "pdfengine/PdfiumRuntime.h"
#include "pdftools/PdfTools.h"
#include "pdftools/SplitPlan.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

using namespace dp::tools;
using dp::pdf::PdfDocument;

class TstPdfTools : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

    QString write(const QString& name, const QByteArray& data)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() ? path : QString();
    }
    static PageSpec page(const QString& file, int index, int rotation = 0)
    {
        PageSpec s;
        s.file = file;
        s.page = index;
        s.rotation = rotation;
        return s;
    }
    std::shared_ptr<PdfDocument> openOut(const QString& path) { return PdfDocument::open(path, {}, nullptr); }
    // "Page 3 of 4" -> 3
    static int pageNumberOf(const std::shared_ptr<PdfDocument>& doc, int page)
    {
        const QString t = doc->pageText(page).trimmed();
        return t.startsWith("Page ") ? t.mid(5).split(' ').first().toInt() : -1;
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }
    void cleanupTestCase() { dp::pdf::shutdownPdfium(); }

    // ---- merge, reorder, delete, duplicate ------------------------------------------------------------------------------------

    void mergingTwoFilesKeepsTheOrderOfTheList()
    {
        const QString a = write("a.pdf", testpdf::make(3));
        const QString b = write("b.pdf", testpdf::make(2, QSizeF(300, 400)));
        QList<PageSpec> specs;
        for (int i = 0; i < 3; ++i)
            specs << page(a, i);
        for (int i = 0; i < 2; ++i)
            specs << page(b, i);
        const QString out = m_dir.filePath("merged.pdf");
        const WriteResult r = writePages(specs, out);
        QVERIFY2(r.ok, qPrintable(r.error + " " + r.detail));
        QCOMPARE(r.pages, 5);
        QVERIFY(r.bytes > 100 && r.bytes == QFileInfo(out).size());
        auto doc = openOut(out);
        QVERIFY(doc);
        QCOMPARE(doc->pageCount(), 5);
        QCOMPARE(doc->pageText(0).trimmed(), QStringLiteral("Page 1 of 3"));
        QCOMPARE(doc->pageText(2).trimmed(), QStringLiteral("Page 3 of 3"));
        QCOMPARE(doc->pageText(3).trimmed(), QStringLiteral("Page 1 of 2"));
        QCOMPARE(doc->pageSizePt(0), QSizeF(612, 792));
        QCOMPARE(doc->pageSizePt(3), QSizeF(300, 400));            // each page keeps its own size
        QVERIFY(r.warnings.isEmpty());
        QVERIFY(!QFileInfo::exists(out + ".part.tmp"));           // no temporary file is left
    }

    void reorderingDeletingAndDuplicatingPagesWorks()
    {
        const QString a = write("c.pdf", testpdf::make(5));
        // 5, 1, 1 (a copy), 3 - pages 2 and 4 are deleted
        const QList<PageSpec> specs{page(a, 4), page(a, 0), page(a, 0), page(a, 2)};
        const QString out = m_dir.filePath("reordered.pdf");
        const WriteResult r = writePages(specs, out);
        QVERIFY2(r.ok, qPrintable(r.error + " " + r.detail));
        auto doc = openOut(out);
        QVERIFY(doc);
        QCOMPARE(doc->pageCount(), 4);
        QCOMPARE(pageNumberOf(doc, 0), 5);
        QCOMPARE(pageNumberOf(doc, 1), 1);
        QCOMPARE(pageNumberOf(doc, 2), 1);                         // the same page twice
        QCOMPARE(pageNumberOf(doc, 3), 3);
    }

    void aPageCanBeUsedManyTimesAndTheSourceIsNotChanged()
    {
        const QString a = write("d.pdf", testpdf::make(2));
        const QByteArray before = QFile(a).readAll().size() ? QByteArray() : QByteArray();
        Q_UNUSED(before);
        QFile src(a);
        QVERIFY(src.open(QIODevice::ReadOnly));
        const QByteArray original = src.readAll();
        src.close();
        QList<PageSpec> specs;
        for (int i = 0; i < 10; ++i)
            specs << page(a, 1);
        const WriteResult r = writePages(specs, m_dir.filePath("ten.pdf"));
        QVERIFY2(r.ok, qPrintable(r.error + " " + r.detail));
        QCOMPARE(openOut(m_dir.filePath("ten.pdf"))->pageCount(), 10);
        QVERIFY(src.open(QIODevice::ReadOnly));
        QCOMPARE(src.readAll(), original);                         // the source file is byte for byte the same
    }

    // ---- rotate, crop, blank ---------------------------------------------------------------------------------------------------

    void rotationTurnsThePagesAndAddsToTheirOwnRotation()
    {
        const QString a = write("e.pdf", testpdf::make(2));
        QList<PageSpec> specs{page(a, 0, 1), page(a, 1, -1), page(a, 0, 2), page(a, 1, 0), page(a, 1, 5)};
        const QString out = m_dir.filePath("rotated.pdf");
        QVERIFY(writePages(specs, out).ok);
        auto doc = openOut(out);
        QVERIFY(doc);
        QCOMPARE(doc->pageSizePt(0), QSizeF(792, 612));            // 90
        QCOMPARE(doc->pageSizePt(1), QSizeF(792, 612));            // 270
        QCOMPARE(doc->pageSizePt(2), QSizeF(612, 792));            // 180
        QCOMPARE(doc->pageSizePt(3), QSizeF(612, 792));
        QCOMPARE(doc->pageSizePt(4), QSizeF(792, 612));            // 5 turns = 1
        // the text of page 1 sits at the top left; turned 90 degrees clockwise it is at the top right
        const QImage img = doc->render(0, 0.5);
        int left = 0, right = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qGray(img.pixel(x, y)) < 100)
                    (x < img.width() / 2 ? left : right)++;
        QVERIFY2(right > 50 && left == 0, qPrintable(QString("left %1 right %2").arg(left).arg(right)));
        // a file that was already turned: the turn is added
        QList<PageSpec> again{page(out, 0, 1)};
        QVERIFY(writePages(again, m_dir.filePath("rotated2.pdf")).ok);
        QCOMPARE(openOut(m_dir.filePath("rotated2.pdf"))->pageSizePt(0), QSizeF(612, 792));      // 90 + 90 = 180
    }

    void cropCutsTheEdgesAsThePageIsShown()
    {
        const QString a = write("f.pdf", testpdf::make(1));
        PageSpec upright = page(a, 0);
        upright.crop = QMarginsF(10, 20, 30, 40);                   // left, top, right, bottom
        QVERIFY(writePages({upright}, m_dir.filePath("crop0.pdf")).ok);
        QCOMPARE(openOut(m_dir.filePath("crop0.pdf"))->pageSizePt(0), QSizeF(612 - 40, 792 - 60));

        // turned a quarter: left / right and top / bottom swap places in the file, the shown size is what was asked for
        PageSpec turned = page(a, 0, 1);
        turned.crop = QMarginsF(10, 20, 30, 40);
        QVERIFY(writePages({turned}, m_dir.filePath("crop1.pdf")).ok);
        QCOMPARE(openOut(m_dir.filePath("crop1.pdf"))->pageSizePt(0), QSizeF(792 - 40, 612 - 60));

        // the text must still be where it was: cut 100 points from the top = the text (at 92 points from the top) is gone
        PageSpec cut = page(a, 0);
        cut.crop = QMarginsF(0, 150, 0, 0);
        QVERIFY(writePages({cut}, m_dir.filePath("crop2.pdf")).ok);
        auto doc = openOut(m_dir.filePath("crop2.pdf"));
        QCOMPARE(doc->pageSizePt(0), QSizeF(612, 792 - 150));
        const QImage img = doc->render(0, 1.0);
        int dark = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qGray(img.pixel(x, y)) < 100)
                    ++dark;
        QCOMPARE(dark, 0);

        // too much: the crop is skipped and reported
        PageSpec all = page(a, 0);
        all.crop = QMarginsF(400, 0, 300, 0);
        const WriteResult r = writePages({all}, m_dir.filePath("crop3.pdf"));
        QVERIFY(r.ok);
        QVERIFY(r.warnings.contains("cropTooBig:1"));
        QCOMPARE(openOut(m_dir.filePath("crop3.pdf"))->pageSizePt(0), QSizeF(612, 792));
    }

    void blankPagesCanBeInserted()
    {
        const QString a = write("g.pdf", testpdf::make(2));
        PageSpec blank;
        blank.blank = true;
        blank.blankSize = QSizeF(300, 200);
        const WriteResult r = writePages({page(a, 0), blank, page(a, 1)}, m_dir.filePath("blank.pdf"));
        QVERIFY2(r.ok, qPrintable(r.error + r.detail));
        auto doc = openOut(m_dir.filePath("blank.pdf"));
        QVERIFY(doc);
        QCOMPARE(doc->pageCount(), 3);
        QCOMPARE(doc->pageSizePt(1), QSizeF(300, 200));
        QVERIFY(doc->pageText(1).trimmed().isEmpty());
        const QImage img = doc->render(1, 1.0);
        QCOMPARE(img.size(), QSize(300, 200));
        QCOMPARE(QColor(img.pixel(50, 50)), QColor(Qt::white));
        QCOMPARE(pageNumberOf(doc, 2), 2);
        // only blank pages
        QVERIFY(writePages({blank, blank}, m_dir.filePath("blank2.pdf")).ok);
        QCOMPARE(openOut(m_dir.filePath("blank2.pdf"))->pageCount(), 2);
    }

    // ---- what is not carried over, errors --------------------------------------------------------------------------------------

    void linksToPagesThatAreLeftOutAreRemovedAndTheRestKept()
    {
        const QString f = write("feature.pdf", testpdf::makeFeatureDoc());
        // page 1 has a link to page 2 and a web link. Page 1 alone: the link to page 2 would point nowhere
        const WriteResult alone = writePages({page(f, 0)}, m_dir.filePath("l1.pdf"));
        QVERIFY2(alone.ok, qPrintable(alone.error + alone.detail));
        QVERIFY(alone.warnings.contains("linksRemoved:1"));
        QVERIFY(alone.warnings.contains("bookmarks"));              // the file has bookmarks, they are not copied
        auto d1 = openOut(m_dir.filePath("l1.pdf"));
        QVERIFY(d1);
        QCOMPARE(d1->pageCount(), 1);
        QCOMPARE(d1->linkAt(0, QPointF(150, 270)).kind, dp::pdf::LinkTarget::None);       // the page link is gone
        QCOMPARE(d1->linkAt(0, QPointF(150, 330)).kind, dp::pdf::LinkTarget::Uri);        // the web link stays
        QCOMPARE(d1->annotations().size(), 2);                       // comment and highlight stay
        QVERIFY(d1->outline().isEmpty());
        // pages 1 and 2 together: the link keeps working and leads to the new page 2
        const WriteResult both = writePages({page(f, 0), page(f, 1)}, m_dir.filePath("l2.pdf"));
        QVERIFY(both.ok);
        QVERIFY(!both.warnings.contains(QStringLiteral("linksRemoved:1")));
        auto d2 = openOut(m_dir.filePath("l2.pdf"));
        const auto link = d2->linkAt(0, QPointF(150, 270));
        QCOMPARE(link.kind, dp::pdf::LinkTarget::Page);
        QCOMPARE(link.page, 1);
        // the page that is not wanted is not smuggled in through the link: the file stays small
        QVERIFY(QFileInfo(m_dir.filePath("l1.pdf")).size() < QFileInfo(f).size() + 1500);
    }

    void protectedSourcesNeedTheirPasswordAndGiveAnUnprotectedFile()
    {
        const QFileInfo qpdf(QStringLiteral(DP_TEST_QPDF));
        if (!qpdf.exists())
            QSKIP("qpdf.exe is not in .tools\\qpdf");
        const QString plain = write("p.pdf", testpdf::make(2));
        const QString locked = m_dir.filePath("locked.pdf");
        QProcess p;
        p.start(qpdf.absoluteFilePath(), {"--encrypt", "open-secret", "owner-secret", "256", "--", plain, locked});
        QVERIFY(p.waitForFinished(30000));
        QCOMPARE(p.exitCode(), 0);

        QString error;
        QCOMPARE(pageCountOf(locked, {}, &error), 0);
        QCOMPARE(error, QStringLiteral("password"));
        QCOMPARE(pageCountOf(locked, "wrong", &error), 0);
        QCOMPARE(error, QStringLiteral("password"));
        QCOMPARE(pageCountOf(locked, "open-secret", &error), 2);
        QVERIFY(error.isEmpty());

        PageSpec noPw = page(locked, 0);
        const WriteResult refused = writePages({noPw}, m_dir.filePath("o1.pdf"));
        QVERIFY(!refused.ok);
        QCOMPARE(refused.error, QStringLiteral("password"));
        QCOMPARE(refused.errorFile, locked);
        QVERIFY(!QFileInfo::exists(m_dir.filePath("o1.pdf")));

        PageSpec withPw = page(locked, 1);
        withPw.password = "open-secret";
        const WriteResult ok = writePages({withPw, page(plain, 0)}, m_dir.filePath("o2.pdf"));
        QVERIFY2(ok.ok, qPrintable(ok.error + ok.detail));
        QVERIFY(ok.warnings.contains("encrypted"));
        auto doc = PdfDocument::open(m_dir.filePath("o2.pdf"), {}, nullptr);      // opens without any password
        QVERIFY(doc);
        QCOMPARE(doc->pageCount(), 2);
        QCOMPARE(pageNumberOf(doc, 0), 2);
    }

    void errorsAreReportedAndNothingIsWritten()
    {
        const QString a = write("h.pdf", testpdf::make(2));
        QCOMPARE(writePages({}, m_dir.filePath("none.pdf")).error, QStringLiteral("range"));
        QCOMPARE(writePages({page(a, 9)}, m_dir.filePath("none.pdf")).error, QStringLiteral("range"));
        QCOMPARE(writePages({page(a, -1)}, m_dir.filePath("none.pdf")).error, QStringLiteral("range"));
        const WriteResult missing = writePages({page(m_dir.filePath("missing.pdf"), 0)}, m_dir.filePath("none.pdf"));
        QCOMPARE(missing.error, QStringLiteral("file"));
        const WriteResult junk = writePages({page(write("junk.pdf", "not a pdf at all"), 0)}, m_dir.filePath("none.pdf"));
        QCOMPARE(junk.error, QStringLiteral("format"));
        QVERIFY(!QFileInfo::exists(m_dir.filePath("none.pdf")));
        QVERIFY(!QFileInfo::exists(m_dir.filePath("none.pdf.part.tmp")));
        QString error;
        QCOMPARE(pageCountOf(m_dir.filePath("missing.pdf"), {}, &error), 0);
        QCOMPARE(error, QStringLiteral("file"));
        QCOMPARE(pageCountOf(write("junk2.pdf", "xx"), {}, &error), 0);
        QCOMPARE(error, QStringLiteral("format"));
    }

    void anExistingOutputIsNotOverwrittenUnlessAsked()
    {
        const QString a = write("i.pdf", testpdf::make(3));
        const QString out = write("existing.pdf", "precious");
        const WriteResult refused = writePages({page(a, 0)}, out);
        QVERIFY(!refused.ok);
        QCOMPARE(refused.error, QStringLiteral("exists"));
        QFile f(out);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("precious"));
        f.close();
        QVERIFY(writePages({page(a, 0)}, out, {}, true).ok);
        QCOMPARE(openOut(out)->pageCount(), 1);
        // the output may not be the source either (it is refused as "exists" like any other existing file)
        QCOMPARE(writePages({page(a, 0)}, a).error, QStringLiteral("exists"));
    }

    void progressIsReportedAndAWriteCanBeCancelled()
    {
        const QString a = write("j.pdf", testpdf::make(20));
        QList<PageSpec> specs;
        for (int i = 0; i < 20; ++i)
            specs << page(a, i);
        QList<int> seen;
        const WriteResult ok = writePages(specs, m_dir.filePath("prog.pdf"), [&](int done, int total) {
            seen << done;
            return total == 20;
        });
        QVERIFY(ok.ok);
        QVERIFY(seen.size() >= 20 && seen.first() == 0 && seen.last() == 20);
        const WriteResult stopped = writePages(specs, m_dir.filePath("stop.pdf"), [](int done, int) { return done < 5; });
        QVERIFY(!stopped.ok);
        QVERIFY(stopped.cancelled);
        QVERIFY(!QFileInfo::exists(m_dir.filePath("stop.pdf")));
        QVERIFY(!QFileInfo::exists(m_dir.filePath("stop.pdf.part.tmp")));
    }

    void aBigMergeIsFastAndCorrect()
    {
        const QString a = write("big1.pdf", testpdf::make(300));
        const QString b = write("big2.pdf", testpdf::make(300));
        QList<PageSpec> specs;
        for (int i = 0; i < 300; ++i) {
            specs << page(a, i);
            specs << page(b, 299 - i);                              // interleaved, the second file reversed
        }
        QElapsedTimer t;
        t.start();
        const WriteResult r = writePages(specs, m_dir.filePath("bigmerge.pdf"));
        const qint64 ms = t.elapsed();
        fprintf(stderr, "PERF merge of 600 pages from 2 files: %lld ms, %lld KB\n", ms, r.bytes / 1024);
        QVERIFY(r.ok);
        QVERIFY2(ms < 10000, "merging 600 pages took more than 10 s");
        auto doc = openOut(m_dir.filePath("bigmerge.pdf"));
        QCOMPARE(doc->pageCount(), 600);
        QCOMPARE(doc->pageText(0).trimmed(), QStringLiteral("Page 1 of 300"));
        QCOMPARE(doc->pageText(1).trimmed(), QStringLiteral("Page 300 of 300"));
        QCOMPARE(doc->pageText(598).trimmed(), QStringLiteral("Page 300 of 300"));
        QCOMPARE(doc->pageText(599).trimmed(), QStringLiteral("Page 1 of 300"));
    }

    // ---- split plans -----------------------------------------------------------------------------------------------------------

    void splitByRangesMakesOneFilePerPart()
    {
        bool ok = false;
        auto parts = splitByRanges("1-3, 5, 8-", 10, &ok);
        QVERIFY(ok);
        QCOMPARE(parts.size(), 3);
        QCOMPARE(parts.at(0).pages, (QList<int>{0, 1, 2}));
        QCOMPARE(parts.at(1).pages, (QList<int>{4}));
        QCOMPARE(parts.at(2).pages, (QList<int>{7, 8, 9}));
        QCOMPARE(parts.at(0).suffix, QStringLiteral("pages-1-3"));
        QCOMPARE(parts.at(1).suffix, QStringLiteral("pages-5"));
        parts = splitByRanges("2-99", 5, &ok);
        QVERIFY(ok);
        QCOMPARE(parts.at(0).pages, (QList<int>{1, 2, 3, 4}));
        for (const char* bad : {"", "0", "9", "5-3", "abc", "1,,x", "-"}) {
            splitByRanges(bad, 5, &ok);
            QVERIFY2(!ok, bad);
        }
    }

    void splitEveryNPagesAndByBookmarks()
    {
        auto parts = splitEvery(4, 10);
        QCOMPARE(parts.size(), 3);
        QCOMPARE(parts.at(2).pages, (QList<int>{8, 9}));
        QCOMPARE(parts.at(0).suffix, QStringLiteral("pages-1-4"));
        QVERIFY(splitEvery(0, 10).isEmpty());
        QCOMPARE(splitEvery(100, 3).size(), 1);

        // bookmarks at pages 2, 5 and 5 again; the pages before the first one are a part of their own
        const QList<QPair<QString, int>> bm{{"Intro: part/1", 1}, {"Next", 4}, {"Same page", 4}, {"No page", -1}};
        parts = splitByBookmarks(bm, 8);
        QCOMPARE(parts.size(), 3);
        QCOMPARE(parts.at(0).pages, (QList<int>{0}));
        QCOMPARE(parts.at(1).pages, (QList<int>{1, 2, 3}));
        QCOMPARE(parts.at(2).pages, (QList<int>{4, 5, 6, 7}));
        QVERIFY(parts.at(1).suffix.contains("Intro"));
        QVERIFY(!parts.at(1).suffix.contains('/') && !parts.at(1).suffix.contains(':'));
        QVERIFY(splitByBookmarks({}, 8).isEmpty());
        QCOMPARE(splitByBookmarks({{"A", 0}}, 3).size(), 1);
    }

    void fileNamesAreSafeAndNeverOverwriteAnything()
    {
        QCOMPARE(safeFileName("a/b\\c:d*e?f\"g<h>i|j"), QStringLiteral("a_b_c_d_e_f_g_h_i_j"));
        QCOMPARE(safeFileName("  x.  "), QStringLiteral("x"));
        QCOMPARE(safeFileName(QString(200, 'a')).size(), 60);
        QCOMPARE(rangeLabel({0, 1, 2, 4, 6, 7}), QStringLiteral("1-3,5,7-8"));
        QDir d(m_dir.path());
        QVERIFY(d.mkpath("names"));
        const QString dir = m_dir.filePath("names");
        const QString p1 = uniqueOutputPath(dir, "doc", "pages-1-3");
        QCOMPARE(QFileInfo(p1).fileName(), QStringLiteral("doc-pages-1-3.pdf"));
        QFile(p1).open(QIODevice::WriteOnly);
        const QString p2 = uniqueOutputPath(dir, "doc", "pages-1-3");
        QCOMPARE(QFileInfo(p2).fileName(), QStringLiteral("doc-pages-1-3 (2).pdf"));
        QFile(p2).open(QIODevice::WriteOnly);
        QCOMPARE(QFileInfo(uniqueOutputPath(dir, "doc", "pages-1-3")).fileName(), QStringLiteral("doc-pages-1-3 (3).pdf"));
    }

    void splitBySizeMeasuresRealFilesAndRespectsTheLimit()
    {
        // 60 pages; every page is about the same size, so a limit of about a third of the whole file gives about three parts
        const QString a = write("size.pdf", testpdf::make(60));
        QString error;
        QVERIFY(writePages({page(a, 0)}, m_dir.filePath("probe-one.pdf")).ok);        // sizes below are of files as the tools write them
        QList<PageSpec> everything;
        for (int i = 0; i < 60; ++i)
            everything << page(a, i);
        QVERIFY(writePages(everything, m_dir.filePath("whole.pdf")).ok);
        const qint64 whole = QFileInfo(m_dir.filePath("whole.pdf")).size();
        const QList<SplitPart> parts = planSplitBySize(a, {}, whole / 3, {}, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY2(parts.size() >= 3 && parts.size() <= 6, qPrintable(QString::number(parts.size())));
        // every page once, in order
        QList<int> all;
        for (const SplitPart& p : parts)
            all += p.pages;
        QList<int> expected;
        for (int i = 0; i < 60; ++i)
            expected << i;
        QCOMPARE(all, expected);
        // write the parts and look at the real sizes
        for (int i = 0; i < parts.size(); ++i) {
            QList<PageSpec> specs;
            for (int p : parts.at(i).pages)
                specs << page(a, p);
            const QString out = m_dir.filePath(QString("size-%1.pdf").arg(i));
            QVERIFY(writePages(specs, out).ok);
            QVERIFY2(QFileInfo(out).size() <= whole / 3, qPrintable(QString("part %1 is %2 bytes").arg(i).arg(QFileInfo(out).size())));
        }
        // a very small limit: many small parts, every part with more than one page really is under the limit, no endless loop
        const QList<SplitPart> tiny = planSplitBySize(a, {}, 1024, {}, &error);
        QVERIFY(tiny.size() > 10);
        int tinyPages = 0;
        for (int i = 0; i < tiny.size(); ++i) {
            tinyPages += tiny.at(i).pages.size();
            if (tiny.at(i).pages.size() > 1) {
                QList<PageSpec> specs;
                for (int p : tiny.at(i).pages)
                    specs << page(a, p);
                const QString out = m_dir.filePath(QString("tiny-%1.pdf").arg(i));
                QVERIFY(writePages(specs, out).ok);
                QVERIFY2(QFileInfo(out).size() <= 1024, qPrintable(QString("part %1: %2 bytes").arg(i).arg(QFileInfo(out).size())));
            }
        }
        QCOMPARE(tinyPages, 60);
        QCOMPARE(planSplitBySize(m_dir.filePath("missing.pdf"), {}, 5000, {}, &error).size(), 0);
        QCOMPARE(error, QStringLiteral("file"));
        // cancel
        QCOMPARE(planSplitBySize(a, {}, whole / 3, [](int, int) { return false; }, &error).size(), 0);
        QCOMPARE(error, QStringLiteral("cancelled"));
    }
};

QTEST_MAIN(TstPdfTools)
#include "tst_pdftools.moc"
