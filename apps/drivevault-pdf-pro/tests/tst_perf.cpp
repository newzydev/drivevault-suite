// Phase 1 targets measured on generated files: opening a 500 page and a 3000 page file, the first page, many pages in a row, memory.
// The numbers are printed (PERF ...) so they can be written down; the limits here are loose on purpose (they catch a real regression,
// not a slow minute of the machine).
#include "TestPdf.h"
#include "pdfengine/PdfDocument.h"
#include "pdfengine/PdfiumRuntime.h"

#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <windows.h>
#include <psapi.h>

using namespace dp::pdf;

namespace {
double workingSetMb()
{
    PROCESS_MEMORY_COUNTERS counters{};
    GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters));
    return double(counters.WorkingSetSize) / (1024.0 * 1024.0);
}
void perf(const QString& line) { fprintf(stderr, "PERF %s\n", qPrintable(line)); }
} // namespace

class TstPerf : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

    QString write(const QString& name, const QByteArray& data)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() ? path : QString();
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }
    void cleanupTestCase() { shutdownPdfium(); }

    void aFiveHundredPageFileOpensAndTheFirstPageShowsQuickly()
    {
        const QString path = write("500.pdf", testpdf::make(500));
        ensurePdfiumInitialized();
        const double before = workingSetMb();
        QElapsedTimer t;
        t.start();
        auto doc = PdfDocument::open(path, {}, nullptr);
        const qint64 openMs = t.elapsed();
        QVERIFY(doc);
        QCOMPARE(doc->pageCount(), 500);
        t.restart();
        const QImage first = doc->render(0, 96.0 / 72.0);
        const qint64 firstMs = t.elapsed();
        QVERIFY(!first.isNull());
        t.restart();
        for (int i = 0; i < 40; ++i)
            QVERIFY(!doc->render(i * 12, 96.0 / 72.0).isNull());           // pages spread over the file, like a fast scroll
        const double perPageMs = double(t.elapsed()) / 40.0;
        const double grown = workingSetMb() - before;
        perf(QString("500 pages: open %1 ms, first page %2 ms, 40 pages %3 ms each, memory +%4 MB, file %5 KB")
                 .arg(openMs).arg(firstMs).arg(perPageMs, 0, 'f', 1).arg(grown, 0, 'f', 0).arg(QFileInfo(path).size() / 1024));
        QVERIFY2(openMs < 3000, "opening 500 pages took more than 3 s");
        QVERIFY2(firstMs < 1500, "the first page took more than 1.5 s");
        QVERIFY2(perPageMs < 200, "a page took more than 200 ms on average");
        QVERIFY2(grown < 400, "opening and showing pages used more than 400 MB");
    }

    void aThreeThousandPageFileOpens()
    {
        const QString path = write("3000.pdf", testpdf::make(3000));
        QElapsedTimer t;
        t.start();
        auto doc = PdfDocument::open(path, {}, nullptr);
        const qint64 openMs = t.elapsed();
        QVERIFY(doc);
        QCOMPARE(doc->pageCount(), 3000);
        t.restart();
        QVERIFY(!doc->render(2999, 96.0 / 72.0).isNull());
        const qint64 lastMs = t.elapsed();
        t.restart();
        const QList<SearchHit> hits = doc->searchPage(1500, "Page 1501", false, false);
        const qint64 searchMs = t.elapsed();
        QCOMPARE(hits.size(), 1);
        perf(QString("3000 pages: open %1 ms, last page %2 ms, search of one page %3 ms").arg(openMs).arg(lastMs).arg(searchMs));
        QVERIFY2(openMs < 8000, "opening 3000 pages took more than 8 s");
        QVERIFY2(lastMs < 1500, "the last page took more than 1.5 s");
    }

    void searchingTheWholeFileWorksAtASensibleSpeed()
    {
        auto doc = PdfDocument::open(write("search500.pdf", testpdf::make(500)), {}, nullptr);
        QVERIFY(doc);
        QElapsedTimer t;
        t.start();
        int found = 0;
        for (int p = 0; p < doc->pageCount(); ++p)
            found += int(doc->searchPage(p, "of 500", false, false).size());
        const qint64 ms = t.elapsed();
        perf(QString("search in 500 pages: %1 ms, %2 hits").arg(ms).arg(found));
        QCOMPARE(found, 500);
        QVERIFY2(ms < 10000, "searching 500 pages took more than 10 s");
    }

    void bigPagesAreRenderedWithinTheMemoryLimit()
    {
        // a poster sized page at a high zoom: the image is clamped, memory stays bounded
        auto doc = PdfDocument::open(write("poster.pdf", testpdf::make(1, QSizeF(5000, 7000))), {}, nullptr);
        QVERIFY(doc);
        const double before = workingSetMb();
        QElapsedTimer t;
        t.start();
        const QImage img = doc->render(0, 8.0 * 96.0 / 72.0);                // 800 % zoom of a 5000 x 7000 point page
        const qint64 ms = t.elapsed();
        QVERIFY(!img.isNull());
        const double grown = workingSetMb() - before;
        perf(QString("poster page at 800%%: %1x%2 px in %3 ms, memory +%4 MB").arg(img.width()).arg(img.height()).arg(ms).arg(grown, 0, 'f', 0));
        QVERIFY(qint64(img.width()) * img.height() <= 64000000);
        QVERIFY2(grown < 450, "a clamped page used more than 450 MB");
    }
};

QTEST_MAIN(TstPerf)
#include "tst_perf.moc"
