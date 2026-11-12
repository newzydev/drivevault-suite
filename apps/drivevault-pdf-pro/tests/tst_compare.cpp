// Compare two PDFs: pages with other text, extra / missing pages, identical files, and the controller object.
#include "DocumentController.h"
#include "PdfComparer.h"
#include "TestPdf.h"
#include "pdfengine/PdfDocument.h"
#include "pdfengine/PdfiumRuntime.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using dp::pdf::PdfDocument;

class TstCompare : public QObject {
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
    void cleanupTestCase() { dp::pdf::shutdownPdfium(); }

    void identicalFilesHaveNoDifferences()
    {
        const QString f = write("a.pdf", testpdf::make(3));
        auto a = PdfDocument::open(f, {}, nullptr);
        auto b = PdfDocument::open(f, {}, nullptr);
        const dp::CompareResult r = dp::comparePdfs(a, b);
        QVERIFY(r.ok);
        QCOMPARE(r.pages.size(), 3);
        for (const dp::PageDiff& d : r.pages) {
            QVERIFY(!d.differs());
            QCOMPARE(d.textSimilarity, 1.0);
        }
    }

    void changedTextAndMissingPagesAreFound()
    {
        auto a = PdfDocument::open(write("b1.pdf", testpdf::make({QSizeF(612, 792), QSizeF(612, 792), QSizeF(612, 792)}, {"Alpha one", "Beta two", "Gamma three"})), {}, nullptr);
        auto b = PdfDocument::open(write("b2.pdf", testpdf::make({QSizeF(612, 792), QSizeF(612, 792)}, {"Alpha one", "Beta CHANGED"})), {}, nullptr);
        const dp::CompareResult r = dp::comparePdfs(a, b);
        QVERIFY(r.ok);
        QCOMPARE(r.pagesA, 3);
        QCOMPARE(r.pagesB, 2);
        QCOMPARE(r.pages.size(), 3);
        QVERIFY(!r.pages[0].differs());
        QVERIFY(r.pages[1].differs());
        QCOMPARE(r.pages[1].removedLines, 1);
        QCOMPARE(r.pages[1].addedLines, 1);
        QCOMPARE(r.pages[1].removedSample, QStringList{"Beta two"});
        QCOMPARE(r.pages[1].addedSample, QStringList{"Beta CHANGED"});
        QVERIFY(r.pages[1].pixelDifference > 0.0005);
        QVERIFY(r.pages[2].inA && !r.pages[2].inB);
        QVERIFY(r.pages[2].differs());
    }

    void cancelStopsTheComparison()
    {
        const QString f = write("c.pdf", testpdf::make(5));
        auto a = PdfDocument::open(f, {}, nullptr);
        std::atomic_bool stop(true);
        const dp::CompareResult r = dp::comparePdfs(a, a, &stop);
        QVERIFY(!r.ok && r.cancelled);
    }

    void theControllerObjectComparesTheOpenFileWithAnother()
    {
        dp::DocumentController c(m_dir.filePath("data"));
        dp::PdfComparer cmp(&c);
        QCOMPARE(cmp.start("x.pdf"), QStringLiteral("noDocument"));
        const QString one = write("d1.pdf", testpdf::make({QSizeF(612, 792)}, {"Same line"}));
        const QString two = write("d2.pdf", testpdf::make({QSizeF(612, 792)}, {"Other line"}));
        QCOMPARE(c.open(one), QString());
        QCOMPARE(cmp.start(one), QStringLiteral("same"));
        QSignalSpy done(&cmp, &dp::PdfComparer::finished);
        QCOMPARE(cmp.start(two), QString());
        QVERIFY(cmp.busy());
        QCOMPARE(cmp.start(two), QStringLiteral("busy"));
        QVERIFY(QTest::qWaitFor([&] { return done.count() > 0; }, 20000));
        QVERIFY(done.first().at(0).toBool());
        QVERIFY(cmp.hasResult());
        QCOMPARE(cmp.summary().value("different").toInt(), 1);
        QCOMPARE(cmp.pages().first().toMap().value("status").toString(), QStringLiteral("different"));
        // a file that cannot be read
        done.clear();
        QCOMPARE(cmp.start(write("junk.pdf", "nope")), QString());
        QVERIFY(QTest::qWaitFor([&] { return done.count() > 0; }, 20000));
        QVERIFY(!done.first().at(0).toBool());
        QCOMPARE(done.first().at(1).toString(), QStringLiteral("format"));
        QVERIFY(!cmp.hasResult());
    }
};

QTEST_MAIN(TstCompare)
#include "tst_compare.moc"
