// The page organizer model (merge / reorder / delete / duplicate / rotate / crop / blank pages with undo, saving the result) and the split API of
// the controller. Results are checked by opening the written files.
#include "DocumentController.h"
#include "PageListModel.h"
#include "TestPdf.h"
#include "pdfengine/PdfDocument.h"
#include "pdfengine/PdfiumRuntime.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using dp::DocumentController;
using dp::PageListModel;
using dp::pdf::PdfDocument;

class TstOrganizer : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

    QString write(const QString& name, const QByteArray& data)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() ? path : QString();
    }
    static QList<int> pagesOf(const PageListModel& m)
    {
        QList<int> out;
        for (int i = 0; i < m.count(); ++i)
            out << m.data(m.index(i), PageListModel::PageRole).toInt();
        return out;
    }
    static QStringList filesOf(const PageListModel& m)
    {
        QStringList out;
        for (int i = 0; i < m.count(); ++i)
            out << m.data(m.index(i), PageListModel::FileRole).toString();
        return out;
    }
    // saves and waits; returns the signal arguments
    QList<QVariant> saveAndWait(PageListModel& m, const QString& path, const QVariantList& rows = {}, bool overwrite = false)
    {
        QSignalSpy done(&m, &PageListModel::saveFinished);
        const QString started = m.save(path, rows, overwrite);
        if (!started.isEmpty())
            return {started};
        if (!QTest::qWaitFor([&] { return done.count() > 0; }, 30000))
            return {"timeout"};
        return done.first();
    }
    static int numberOf(const std::shared_ptr<PdfDocument>& doc, int page)
    {
        const QString t = doc->pageText(page).trimmed();
        return t.startsWith("Page ") ? t.mid(5).split(' ').first().toInt() : -1;
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }
    void cleanupTestCase() { dp::pdf::shutdownPdfium(); }

    // ---- the list --------------------------------------------------------------------------------------------------------------

    void theOpenFileIsLoadedPageByPage()
    {
        DocumentController c(m_dir.filePath("d1"));
        PageListModel m(&c);
        QVERIFY(!m.loadCurrent());                                   // nothing open
        QCOMPARE(c.open(write("one.pdf", testpdf::make({QSizeF(612, 792), QSizeF(300, 400), QSizeF(500, 500)}, {"Page 1 of 3", "Page 2 of 3", "Page 3 of 3"}))), QString());
        QVERIFY(m.loadCurrent());
        QCOMPARE(m.count(), 3);
        QVERIFY(!m.modified());
        QVERIFY(!m.canUndo() && !m.canRedo());
        QCOMPARE(pagesOf(m), (QList<int>{0, 1, 2}));
        QCOMPARE(m.data(m.index(1), PageListModel::WidthRole).toDouble(), 300.0);
        QCOMPARE(m.data(m.index(1), PageListModel::HeightRole).toDouble(), 400.0);
        QCOMPARE(m.data(m.index(0), PageListModel::FileRole).toString(), QStringLiteral("one.pdf"));
        QCOMPARE(m.data(m.index(2), PageListModel::LabelRole).toString(), QString::fromUtf8("one.pdf · 3"));
        QCOMPARE(m.data(m.index(0), PageListModel::SourceIdRole).toInt(), c.docId());     // thumbnails come from the open tab
        QCOMPARE(m.baseName(), QStringLiteral("one-edited"));
        QVERIFY(m.data(m.index(9), PageListModel::PageRole).isNull());
    }

    void deletingMovingDuplicatingAndUndoWork()
    {
        DocumentController c(m_dir.filePath("d2"));
        PageListModel m(&c);
        QCOMPARE(c.open(write("five.pdf", testpdf::make(5))), QString());
        QVERIFY(m.loadCurrent());

        m.removeRows({1, 3, 3, 99, -1});                              // duplicates and wrong rows are ignored
        QCOMPARE(pagesOf(m), (QList<int>{0, 2, 4}));
        QVERIFY(m.modified() && m.canUndo());
        m.undo();
        QCOMPARE(pagesOf(m), (QList<int>{0, 1, 2, 3, 4}));
        QVERIFY(!m.modified());
        QVERIFY(m.canRedo());
        m.redo();
        QCOMPARE(pagesOf(m), (QList<int>{0, 2, 4}));
        m.undo();

        m.moveRows({0, 1}, 3);                                        // pages 1 and 2 go behind page 4 (position 3 of the rest [3,4,5])
        QCOMPARE(pagesOf(m), (QList<int>{2, 3, 4, 0, 1}));
        m.undo();
        m.moveRows({4}, 0);
        QCOMPARE(pagesOf(m), (QList<int>{4, 0, 1, 2, 3}));
        m.moveRows({0}, 99);                                          // beyond the end = the end
        QCOMPARE(pagesOf(m), (QList<int>{0, 1, 2, 3, 4}));
        m.reset();
        QCOMPARE(pagesOf(m), (QList<int>{0, 1, 2, 3, 4}));
        QVERIFY(!m.modified());

        m.reset();                                                    // nothing changed: no step
        m.duplicate({0, 2});
        QCOMPARE(pagesOf(m), (QList<int>{0, 0, 1, 2, 2, 3, 4}));
        m.undo();
        QCOMPARE(m.count(), 5);

        // a gesture (dragging) is one undo step however many moves it has
        m.beginGesture();
        m.moveOne(0, 1);
        m.moveOne(1, 2);
        m.moveOne(2, 4);
        m.endGesture();
        QCOMPARE(pagesOf(m), (QList<int>{1, 2, 3, 4, 0}));
        m.undo();
        QCOMPARE(pagesOf(m), (QList<int>{0, 1, 2, 3, 4}));
        m.moveOne(0, 0);                                              // no move
        m.moveOne(0, 9);                                              // invalid
        QCOMPARE(pagesOf(m), (QList<int>{0, 1, 2, 3, 4}));

        m.removeRows({0, 1, 2, 3, 4});
        QCOMPARE(m.count(), 0);
        m.reset();
        QCOMPARE(m.count(), 5);
        QVERIFY(!m.modified());
    }

    void rotatingCropAndBlankPagesChangeTheShownSize()
    {
        DocumentController c(m_dir.filePath("d3"));
        PageListModel m(&c);
        QCOMPARE(c.open(write("rc.pdf", testpdf::make(3))), QString());
        QVERIFY(m.loadCurrent());
        QSignalSpy changed(&m, &PageListModel::dataChanged);
        m.rotate({0, 2}, 1);
        QVERIFY(changed.count() >= 2);
        QCOMPARE(m.data(m.index(0), PageListModel::RotationRole).toInt(), 1);
        QCOMPARE(m.data(m.index(0), PageListModel::WidthRole).toDouble(), 792.0);
        QCOMPARE(m.data(m.index(1), PageListModel::WidthRole).toDouble(), 612.0);
        m.rotate({0}, -2);
        QCOMPARE(m.data(m.index(0), PageListModel::RotationRole).toInt(), 3);
        m.undo();
        QCOMPARE(m.data(m.index(0), PageListModel::RotationRole).toInt(), 1);

        m.crop({1}, 10, 20, 30, 40);
        const QVariantMap crop = m.data(m.index(1), PageListModel::CropRole).toMap();
        QCOMPARE(crop.value("left").toDouble(), 10.0);
        QCOMPARE(crop.value("bottom").toDouble(), 40.0);
        m.rotate({1}, 1);                                             // a rotation clears the crop of that page
        QCOMPARE(m.data(m.index(1), PageListModel::CropRole).toMap().value("left").toDouble(), 0.0);
        m.undo();
        m.crop({1}, -5, 0, 0, 0);                                     // negative margins are not accepted
        QCOMPARE(m.data(m.index(1), PageListModel::CropRole).toMap().value("left").toDouble(), 0.0);

        m.addBlank(1);                                                // between pages 1 and 2, the size of its neighbour (the turned first page)
        QCOMPARE(m.count(), 4);
        QVERIFY(m.data(m.index(1), PageListModel::BlankRole).toBool());
        QCOMPARE(m.data(m.index(1), PageListModel::WidthRole).toDouble(), 792.0);
        QCOMPARE(m.data(m.index(1), PageListModel::SourceIdRole).toInt(), 0);
        m.addBlank(-1);                                               // at the end
        QCOMPARE(m.count(), 5);
        QVERIFY(m.data(m.index(4), PageListModel::BlankRole).toBool());
    }

    void filesCanBeAddedAndATabPasswordIsKept()
    {
        DocumentController c(m_dir.filePath("d4"));
        PageListModel m(&c);
        QCOMPARE(c.open(write("base.pdf", testpdf::make(2))), QString());
        QVERIFY(m.loadCurrent());
        const QString other = write("other.pdf", testpdf::make(3, QSizeF(300, 400)));
        QCOMPARE(m.addFile(other), QString());
        QCOMPARE(m.count(), 5);
        QCOMPARE(filesOf(m), (QStringList{"base.pdf", "base.pdf", "other.pdf", "other.pdf", "other.pdf"}));
        const int addedId = m.data(m.index(2), PageListModel::SourceIdRole).toInt();
        QVERIFY(addedId != 0 && addedId != c.docId());
        QVERIFY(c.documentFor(addedId));                              // the thumbnails of an added file can be drawn
        QCOMPARE(m.baseName(), QStringLiteral("base-merged"));

        QCOMPARE(m.addFile(other, {}, 1), QString());                 // inserted after the first page; the file is not opened twice
        QCOMPARE(m.count(), 8);
        QCOMPARE(filesOf(m).mid(0, 5), (QStringList{"base.pdf", "other.pdf", "other.pdf", "other.pdf", "base.pdf"}));
        QCOMPARE(m.data(m.index(1), PageListModel::SourceIdRole).toInt(), addedId);
        m.undo();
        QCOMPARE(m.count(), 5);

        QCOMPARE(m.addFile(write("junk.pdf", "nope")), QStringLiteral("format"));
        QCOMPARE(m.addFile(m_dir.filePath("none.pdf")), QStringLiteral("file"));
        QCOMPARE(m.count(), 5);

        m.clear();
        QCOMPARE(m.count(), 0);
        QVERIFY(!c.documentFor(addedId));                             // unregistered when the list is cleared
        QCOMPARE(m.baseName(), QStringLiteral("merged"));
        QVERIFY(c.isOpen());                                          // the open tab is not touched
    }

    void aProtectedFileNeedsItsPasswordAndTheResultIsUnprotected()
    {
        const QFileInfo qpdf(QStringLiteral(DP_TEST_QPDF));
        if (!qpdf.exists())
            QSKIP("qpdf.exe is not in .tools\\qpdf");
        const QString plain = write("pl.pdf", testpdf::make(2));
        const QString locked = m_dir.filePath("lk.pdf");
        QProcess p;
        p.start(qpdf.absoluteFilePath(), {"--encrypt", "open-secret", "owner-secret", "256", "--", plain, locked});
        QVERIFY(p.waitForFinished(30000));
        QCOMPARE(p.exitCode(), 0);

        DocumentController c(m_dir.filePath("d5"));
        PageListModel m(&c);
        QCOMPARE(c.open(write("base5.pdf", testpdf::make(1))), QString());
        QVERIFY(m.loadCurrent());
        QCOMPARE(m.addFile(locked), QStringLiteral("password"));
        QCOMPARE(m.addFile(locked, "wrong"), QStringLiteral("password"));
        QCOMPARE(m.count(), 1);
        QCOMPARE(m.addFile(locked, "open-secret"), QString());
        QCOMPARE(m.count(), 3);
        const QList<QVariant> r = saveAndWait(m, m_dir.filePath("mixed.pdf"));
        QVERIFY2(r.at(0).toBool(), qPrintable(r.value(1).toString()));
        QVERIFY(r.at(3).toStringList().contains("encrypted"));
        auto doc = PdfDocument::open(m_dir.filePath("mixed.pdf"), {}, nullptr);
        QVERIFY(doc);
        QCOMPARE(doc->pageCount(), 3);

        // a file that is open in a tab with its password: the organizer keeps the password for the writing
        QCOMPARE(c.open(locked, "open-secret"), QString());
        PageListModel m2(&c);
        QVERIFY(m2.loadCurrent());
        QCOMPARE(m2.count(), 2);
        const QList<QVariant> r2 = saveAndWait(m2, m_dir.filePath("fromtab.pdf"), {1});
        QVERIFY2(r2.at(0).toBool(), qPrintable(r2.value(1).toString()));
        auto doc2 = PdfDocument::open(m_dir.filePath("fromtab.pdf"), {}, nullptr);
        QVERIFY(doc2);
        QCOMPARE(doc2->pageCount(), 1);
        QCOMPARE(numberOf(doc2, 0), 2);
    }

    // ---- saving ----------------------------------------------------------------------------------------------------------------

    void theResultIsWrittenInTheOrderOfTheListWithRotationCropAndBlankPages()
    {
        DocumentController c(m_dir.filePath("d6"));
        PageListModel m(&c);
        QCOMPARE(c.open(write("six.pdf", testpdf::make(6))), QString());
        QVERIFY(m.loadCurrent());
        QCOMPARE(m.addFile(write("two.pdf", testpdf::make(2, QSizeF(300, 400)))), QString());
        m.removeRows({1, 2});                                         // six pages 2 and 3 are gone
        m.moveRows({0}, 3);                                           // page 1 of six moves behind page 5 (after 4, 5? see the check below)
        m.rotate({0}, 1);
        m.crop({1}, 0, 50, 0, 0);
        m.addBlank(2);
        // the list now: write it and look at what the file has
        QList<int> srcPages = pagesOf(m);
        QStringList srcFiles = filesOf(m);
        const QString out = m_dir.filePath("organized.pdf");
        const QList<QVariant> r = saveAndWait(m, out);
        QVERIFY2(r.at(0).toBool(), qPrintable(r.value(1).toString()));
        QCOMPARE(r.at(4).toInt(), m.count());
        QVERIFY(r.at(5).toDouble() > 100);
        QCOMPARE(r.at(6).toString(), QDir::toNativeSeparators(out));
        auto doc = PdfDocument::open(out, {}, nullptr);
        QVERIFY(doc);
        QCOMPARE(doc->pageCount(), m.count());
        for (int i = 0; i < m.count(); ++i) {
            if (m.data(m.index(i), PageListModel::BlankRole).toBool()) {
                QVERIFY(doc->pageText(i).trimmed().isEmpty());
                continue;
            }
            const QString file = srcFiles.at(i);
            const int want = srcPages.at(i) + 1;
            const QString text = doc->pageText(i).trimmed();
            QVERIFY2(text == QString("Page %1 of %2").arg(want).arg(file == "six.pdf" ? 6 : 2), qPrintable(QString("row %1: '%2' (%3 %4)").arg(i).arg(text, file).arg(want)));
        }
        // the sizes follow the list: the first row is turned, the second is cropped by 50 points at the top
        QCOMPARE(doc->pageSizePt(0), QSizeF(m.pageWidthAt(0), m.pageHeightAt(0)));
        QCOMPARE(doc->pageSizePt(1).height(), m.pageHeightAt(1) - 50);
        QVERIFY(!QFileInfo::exists(out + ".part.tmp"));
        doc.reset();
        // an independent reader agrees that the file is sound
        const QFileInfo qpdf(QStringLiteral(DP_TEST_QPDF));
        if (qpdf.exists()) {
            QProcess check;
            check.start(qpdf.absoluteFilePath(), {"--check", out});
            QVERIFY(check.waitForFinished(30000));
            QVERIFY2(check.exitCode() == 0, check.readAllStandardOutput().constData());
        }
    }

    void onlyTheSelectedRowsAreWrittenAndNothingIsOverwrittenByMistake()
    {
        DocumentController c(m_dir.filePath("d7"));
        PageListModel m(&c);
        QCOMPARE(c.open(write("sel.pdf", testpdf::make(8))), QString());
        QVERIFY(m.loadCurrent());
        QCOMPARE(m.save(m_dir.filePath("x.pdf"), {99}), QStringLiteral("empty"));           // rows that do not exist: nothing to write
        const QList<QVariant> r = saveAndWait(m, m_dir.filePath("some.pdf"), {5, 1, 6});    // written in the order of the list, not of the selection
        QVERIFY(r.at(0).toBool());
        auto doc = PdfDocument::open(m_dir.filePath("some.pdf"), {}, nullptr);
        QCOMPARE(doc->pageCount(), 3);
        QCOMPARE(numberOf(doc, 0), 2);
        QCOMPARE(numberOf(doc, 1), 6);
        QCOMPARE(numberOf(doc, 2), 7);
        doc.reset();                                                  // an open file cannot be replaced on Windows

        QCOMPARE(m.save(m_dir.filePath("some.pdf")), QStringLiteral("exists"));
        QCOMPARE(PdfDocument::open(m_dir.filePath("some.pdf"), {}, nullptr)->pageCount(), 3);   // untouched
        const QList<QVariant> again = saveAndWait(m, m_dir.filePath("some.pdf"), {}, true);
        QVERIFY2(again.at(0).toBool(), qPrintable(again.value(1).toString()));
        QCOMPARE(PdfDocument::open(m_dir.filePath("some.pdf"), {}, nullptr)->pageCount(), 8);

        PageListModel empty(&c);
        QCOMPARE(empty.save(m_dir.filePath("none.pdf")), QStringLiteral("empty"));
        QVERIFY(!QFileInfo::exists(m_dir.filePath("none.pdf")));
    }

    void aSaveReportsProgressCanBeCancelledAndRefusesASecondOne()
    {
        DocumentController c(m_dir.filePath("d8"));
        PageListModel m(&c);
        QCOMPARE(c.open(write("long.pdf", testpdf::make(400))), QString());
        QVERIFY(m.loadCurrent());
        QSignalSpy state(&m, &PageListModel::savingChanged);
        QSignalSpy done(&m, &PageListModel::saveFinished);
        QCOMPARE(m.save(m_dir.filePath("long-copy.pdf")), QString());
        QVERIFY(m.busy());
        QCOMPARE(m.save(m_dir.filePath("long-copy2.pdf")), QStringLiteral("busy"));
        m.cancelSave();
        QVERIFY(QTest::qWaitFor([&] { return done.count() > 0; }, 30000));
        // cancelled or (if it was quicker than the cancel) finished: both leave a clean state
        QVERIFY(!m.busy());
        const bool ok = done.first().at(0).toBool();
        if (!ok) {
            QCOMPARE(done.first().at(1).toString(), QStringLiteral("cancelled"));
            QVERIFY(!QFileInfo::exists(m_dir.filePath("long-copy.pdf")));
        }
        QVERIFY(state.count() >= 2);
        QVERIFY(!QFileInfo::exists(m_dir.filePath("long-copy.pdf.part.tmp")));
        QVERIFY(!QFileInfo::exists(m_dir.filePath("long-copy2.pdf")));
    }

    // ---- splitting -------------------------------------------------------------------------------------------------------------

    void theSplitPreviewShowsTheFilesThatWouldBeWritten()
    {
        DocumentController c(m_dir.filePath("d9"));
        QVERIFY(c.splitPreview("every", 2).isEmpty());                // nothing open
        QCOMPARE(c.open(write("doc.pdf", testpdf::make(7))), QString());
        QVariantList p = c.splitPreview("ranges", "1-3, 5, 7-");
        QCOMPARE(p.size(), 3);
        QCOMPARE(p.at(0).toMap().value("name").toString(), QStringLiteral("doc-pages-1-3.pdf"));
        QCOMPARE(p.at(0).toMap().value("count").toInt(), 3);
        QCOMPARE(p.at(2).toMap().value("pages").toString(), QStringLiteral("7"));
        p = c.splitPreview("every", 3);
        QCOMPARE(p.size(), 3);
        QCOMPARE(p.at(2).toMap().value("count").toInt(), 1);
        QVERIFY(c.splitPreview("ranges", "9").isEmpty());             // not valid for 7 pages
        QVERIFY(c.splitPreview("every", 0).isEmpty());
        QVERIFY(c.splitPreview("bookmarks", QVariant()).isEmpty());   // no bookmarks
        QVERIFY(c.splitPreview("what", 1).isEmpty());

        QCOMPARE(c.open(write("book.pdf", testpdf::makeFeatureDoc())), QString());
        p = c.splitPreview("bookmarks", QVariant());
        QCOMPARE(p.size(), 2);                                        // Chapter 1 (page 1) and Chapter 2 (pages 2-3; "Section 2.1" is nested)
        QCOMPARE(p.at(1).toMap().value("pages").toString(), QStringLiteral("2-3"));
        QVERIFY(p.at(1).toMap().value("name").toString().contains("Chapter 2"));
    }

    void splittingWritesNewFilesNextToNothingItOverwrites()
    {
        DocumentController c(m_dir.filePath("d10"));
        QCOMPARE(c.startSplit({{"mode", "every"}, {"n", 2}, {"outDir", m_dir.filePath("out")}}), QStringLiteral("noDocument"));
        const QString src = write("split.pdf", testpdf::make(7));
        QCOMPARE(c.open(src), QString());
        const QString dir = m_dir.filePath("parts");
        QCOMPARE(c.startSplit({{"mode", "ranges"}, {"text", "9"}, {"outDir", dir}}), QStringLiteral("badRange"));
        QCOMPARE(c.startSplit({{"mode", "bookmarks"}, {"outDir", dir}}), QStringLiteral("noParts"));
        QCOMPARE(c.startSplit({{"mode", "every"}, {"n", 3}, {"outDir", QString()}}), QStringLiteral("noFolder"));
        QCOMPARE(c.startSplit({{"mode", "size"}, {"sizeMb", 0.0001}, {"outDir", dir}}), QStringLiteral("badRange"));

        QSignalSpy done(&c, &DocumentController::splitFinished);
        QCOMPARE(c.startSplit({{"mode", "every"}, {"n", 3}, {"outDir", dir}}), QString());
        QVERIFY(c.splitting());
        QCOMPARE(c.startSplit({{"mode", "every"}, {"n", 3}, {"outDir", dir}}), QStringLiteral("busy"));
        QVERIFY(QTest::qWaitFor([&] { return done.count() > 0; }, 30000));
        QVERIFY(done.first().at(0).toBool());
        QStringList files = done.first().at(2).toStringList();
        QCOMPARE(files.size(), 3);
        QVERIFY(!c.splitting());
        QCOMPARE(c.splitProgress(), 1.0);
        QCOMPARE(QFileInfo(files.at(0)).fileName(), QStringLiteral("split-pages-1-3.pdf"));
        QCOMPARE(PdfDocument::open(files.at(0), {}, nullptr)->pageCount(), 3);
        QCOMPARE(PdfDocument::open(files.at(2), {}, nullptr)->pageCount(), 1);
        auto last = PdfDocument::open(files.at(2), {}, nullptr);
        QCOMPARE(numberOf(last, 0), 7);
        QCOMPARE(PdfDocument::open(src, {}, nullptr)->pageCount(), 7);          // the file itself is as it was

        // again into the same folder: nothing is overwritten, the new files get " (2)"
        done.clear();
        QCOMPARE(c.startSplit({{"mode", "every"}, {"n", 3}, {"outDir", dir}}), QString());
        QVERIFY(QTest::qWaitFor([&] { return done.count() > 0; }, 30000));
        const QStringList second = done.first().at(2).toStringList();
        QCOMPARE(QFileInfo(second.at(0)).fileName(), QStringLiteral("split-pages-1-3 (2).pdf"));
        QVERIFY(QFileInfo::exists(files.at(0)));
    }

    void splitByBookmarksRangesAndSizeGiveTheRightFiles()
    {
        DocumentController c(m_dir.filePath("d11"));
        QCOMPARE(c.open(write("bm.pdf", testpdf::makeFeatureDoc())), QString());
        QSignalSpy done(&c, &DocumentController::splitFinished);
        QCOMPARE(c.startSplit({{"mode", "bookmarks"}, {"outDir", m_dir.filePath("bm-out")}}), QString());
        QVERIFY(QTest::qWaitFor([&] { return done.count() > 0; }, 30000));
        QVERIFY(done.first().at(0).toBool());
        QStringList files = done.first().at(2).toStringList();
        QCOMPARE(files.size(), 2);
        QCOMPARE(PdfDocument::open(files.at(0), {}, nullptr)->pageCount(), 1);
        QCOMPARE(PdfDocument::open(files.at(1), {}, nullptr)->pageCount(), 2);
        QVERIFY(done.first().at(3).toStringList().contains("bookmarks"));       // the parts do not carry the bookmarks

        done.clear();
        QCOMPARE(c.open(write("rg.pdf", testpdf::make(10))), QString());
        QCOMPARE(c.startSplit({{"mode", "ranges"}, {"text", "2-4, 9-"}, {"outDir", m_dir.filePath("rg-out")}}), QString());
        QVERIFY(QTest::qWaitFor([&] { return done.count() > 0; }, 30000));
        files = done.first().at(2).toStringList();
        QCOMPARE(files.size(), 2);
        QCOMPARE(PdfDocument::open(files.at(0), {}, nullptr)->pageCount(), 3);
        QCOMPARE(PdfDocument::open(files.at(1), {}, nullptr)->pageCount(), 2);
        QCOMPARE(numberOf(PdfDocument::open(files.at(1), {}, nullptr), 0), 9);

        done.clear();
        QCOMPARE(c.open(write("sz.pdf", testpdf::make(120))), QString());
        QCOMPARE(c.startSplit({{"mode", "size"}, {"sizeMb", 0.004}, {"outDir", m_dir.filePath("sz-out")}}), QString());   // about 4 KB per file
        QVERIFY(QTest::qWaitFor([&] { return done.count() > 0; }, 60000));
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        files = done.first().at(2).toStringList();
        QVERIFY(files.size() >= 3);
        int pages = 0;
        for (const QString& f : files) {
            QVERIFY2(QFileInfo(f).size() <= 0.004 * 1024 * 1024, qPrintable(f));
            pages += PdfDocument::open(f, {}, nullptr)->pageCount();
        }
        QCOMPARE(pages, 120);
    }

    void aCancelledSplitLeavesNoFilesBehind()
    {
        DocumentController c(m_dir.filePath("d12"));
        QCOMPARE(c.open(write("cancel.pdf", testpdf::make(300))), QString());
        const QString dir = m_dir.filePath("cancel-out");
        QSignalSpy done(&c, &DocumentController::splitFinished);
        QCOMPARE(c.startSplit({{"mode", "every"}, {"n", 1}, {"outDir", dir}}), QString());     // 300 files
        c.cancelSplit();
        QVERIFY(QTest::qWaitFor([&] { return done.count() > 0; }, 60000));
        if (!done.first().at(0).toBool()) {
            QCOMPARE(done.first().at(1).toString(), QStringLiteral("cancelled"));
            QCOMPARE(QDir(dir).entryList({"*.pdf"}).size(), 0);                                  // what was written is removed again
        } else {
            QCOMPARE(done.first().at(2).toStringList().size(), 300);                             // it was faster than the cancel
        }
        QVERIFY(!c.splitting());
    }
};

QTEST_MAIN(TstOrganizer)
#include "tst_organizer.moc"
