// The PDFium wrapper against real (generated) PDF files: page count and sizes, rendering measured on pixels, text, encryption.
#include "TestPdf.h"
#include "pdfengine/PdfDocument.h"
#include "pdfengine/PdfiumRuntime.h"

#include <QFile>
#include <QFileInfo>
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
        QVERIFY(qint64(huge.width()) * huge.height() <= 150000000);
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
