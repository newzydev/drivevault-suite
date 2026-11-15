// Thai OCR on a real document: every page of the given PDF that has a text layer is drawn to an image, the images are put in an image-only PDF, the OCR
// makes it searchable, and the recognised text is compared with the document's own text layer (the ground truth). Skipped unless DV_TEST_REAL_PDF
// names a file (it is only read). Only numbers are printed, never the text of the document.
//   DV_TEST_REAL_PDF=<file.pdf>   DV_TEST_REAL_PAGES=<most pages, default 8>
// Two measures: CER (edit distance, sensitive to reading order) and a bag of characters (precision / recall, blind to the order: tables and columns).

#include "ThaiOcr.h"
#include "pdfengine/PdfDocument.h"
#include "pdfengine/PdfiumRuntime.h"

#include <QHash>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <vector>

namespace {

// The same normaliser for every engine: NFC, sara am composed, no white space.
QString canon(QString text)
{
    text = text.normalized(QString::NormalizationForm_C);
    text.replace(QStringLiteral("ํา"), QStringLiteral("ำ"));
    text.remove(QRegularExpression(QStringLiteral("\\s")));
    return text;
}

int editDistance(const QString& a, const QString& b)
{
    std::vector<int> prev(static_cast<size_t>(b.size()) + 1), cur(static_cast<size_t>(b.size()) + 1);
    for (int j = 0; j <= b.size(); ++j)
        prev[static_cast<size_t>(j)] = j;
    for (int i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (int j = 1; j <= b.size(); ++j)
            cur[static_cast<size_t>(j)] = std::min({prev[static_cast<size_t>(j)] + 1, cur[static_cast<size_t>(j - 1)] + 1,
                                                    prev[static_cast<size_t>(j - 1)] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[static_cast<size_t>(b.size())];
}

int commonCharacters(const QString& a, const QString& b)
{
    QHash<QChar, int> counts;
    for (const QChar c : a)
        ++counts[c];
    int common = 0;
    for (const QChar c : b) {
        auto it = counts.find(c);
        if (it != counts.end() && it.value() > 0) {
            --it.value();
            ++common;
        }
    }
    return common;
}

} // namespace

class TstOcrReal : public QObject {
    Q_OBJECT
private slots:
    void cleanupTestCase() { dp::pdf::shutdownPdfium(); }

    void measuresARealDocument()
    {
        const QString path = qEnvironmentVariable("DV_TEST_REAL_PDF");
        if (path.isEmpty() || !dp::thaiOcrAvailable())
            QSKIP("set DV_TEST_REAL_PDF to a PDF file (and have the OCR models) to measure a real document");
        const int most = qEnvironmentVariableIntValue("DV_TEST_REAL_PAGES") > 0 ? qEnvironmentVariableIntValue("DV_TEST_REAL_PAGES") : 8;
        const auto original = dp::pdf::PdfDocument::open(path, {}, nullptr);
        QVERIFY(original);
        QList<int> pages;
        for (int p = 0; p < original->pageCount() && pages.size() < most; ++p)
            if (canon(original->pageText(p)).size() >= 150)
                pages << p;
        QVERIFY2(!pages.isEmpty(), "the document has no page with a text layer to compare with");

        QTemporaryDir dir;
        const QString scan = dir.filePath(QStringLiteral("scan.pdf"));
        {
            QPdfWriter writer(scan);
            writer.setResolution(216);                                   // the OCR renders at 3 pixels a point
            QPainter painter;
            for (int n = 0; n < pages.size(); ++n) {
                const QSizeF size = original->pageSizePt(pages.at(n));
                writer.setPageSize(QPageSize(size, QPageSize::Point));
                writer.setPageMargins({}, QPageLayout::Point);
                if (n == 0)
                    QVERIFY(painter.begin(&writer));
                else
                    QVERIFY(writer.newPage());
                const QImage image = original->render(pages.at(n), 3);
                QVERIFY(!image.isNull());
                painter.drawImage(QRect(0, 0, writer.width(), writer.height()), image);
            }
            QVERIFY(painter.end());
        }
        const auto input = dp::pdf::PdfDocument::open(scan, {}, nullptr);
        QVERIFY(input);
        QCOMPARE(input->pageCount(), int(pages.size()));
        QList<int> all;
        for (int n = 0; n < pages.size(); ++n)
            all << n;
        const QString output = dir.filePath(QStringLiteral("searchable.pdf"));
        std::atomic_bool cancel = false;
        dp::ThaiOcrReport report;
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(dp::saveSearchableCopy(*input, all, output, cancel, {}, &report), QString());
        const double seconds = double(timer.elapsed()) / 1000.0;
        const auto copy = dp::pdf::PdfDocument::open(output, {}, nullptr);
        QVERIFY(copy);
        long long wantTotal = 0, gotTotal = 0, errors = 0, common = 0;
        // DV_TEST_REAL_DUMP=<folder>: also write each page as a picture and both texts there, to look at a page that reads badly
        const QString dump = qEnvironmentVariable("DV_TEST_REAL_DUMP");
        for (int n = 0; n < pages.size() && !dump.isEmpty(); ++n) {
            QDir().mkpath(dump);
            const QString base = QDir(dump).filePath(QStringLiteral("page-%1").arg(pages.at(n) + 1));
            original->render(pages.at(n), 1.2).save(base + QStringLiteral(".png"));
            QFile truth(base + QStringLiteral("-truth.txt")), ocr(base + QStringLiteral("-ocr.txt"));
            if (truth.open(QIODevice::WriteOnly))
                truth.write(original->pageText(pages.at(n)).toUtf8());
            if (ocr.open(QIODevice::WriteOnly))
                ocr.write(copy->pageText(n).toUtf8());
        }
        for (int n = 0; n < pages.size(); ++n) {
            const QString want = canon(original->pageText(pages.at(n))), got = canon(copy->pageText(n));
            const int distance = editDistance(want, got), same = commonCharacters(want, got);
            wantTotal += want.size();
            gotTotal += got.size();
            errors += distance;
            common += same;
            fprintf(stderr, "REAL page %-3d  %5d characters in the text layer  CER %.3f  recall %.3f  precision %.3f\n", pages.at(n) + 1, int(want.size()),
                    double(distance) / want.size(), double(same) / want.size(), got.isEmpty() ? 0.0 : double(same) / got.size());
        }
        fprintf(stderr, "REAL TOTAL %d pages, %lld characters: CER %.4f  recall %.4f  precision %.4f  %.1f s (%.2f s a page), recognised %d\n", int(pages.size()),
                wantTotal, double(errors) / double(wantTotal), double(common) / double(wantTotal), gotTotal ? double(common) / double(gotTotal) : 0.0, seconds,
                seconds / pages.size(), report.recognizedPages);
        QVERIFY(report.recognizedPages > 0);
    }
};

QTEST_MAIN(TstOcrReal)
#include "tst_ocrreal.moc"
