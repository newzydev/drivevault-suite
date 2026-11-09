// Compress: smaller copies of a file (structure always; images optionally reduced / stored as JPEG). Measured: file sizes, the pictures rendered
// before and after, the text that is still text, a sound result (qpdf --check), protected files, "never bigger", cancel.
#include "Compressor.h"
#include "DocumentController.h"
#include "TestPdf.h"
#include "pdfengine/PdfDocument.h"
#include "pdfengine/PdfiumRuntime.h"
#include "pdftools/Compress.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QProcess>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using dp::pdf::PdfDocument;
using namespace dp::tools;

namespace {

QByteArray buildPdf(const QList<QByteArray>& bodies)
{
    QByteArray out = "%PDF-1.5\n%\xE2\xE3\xCF\xD3\n";
    QList<qint64> offsets;
    for (int i = 0; i < bodies.size(); ++i) {
        offsets.append(out.size());
        out += QByteArray::number(i + 1) + " 0 obj\n" + bodies.at(i) + "\nendobj\n";
    }
    const qint64 xref = out.size();
    out += "xref\n0 " + QByteArray::number(bodies.size() + 1) + "\n0000000000 65535 f \n";
    for (qint64 o : offsets)
        out += QByteArray::number(o).rightJustified(10, '0') + " 00000 n \n";
    out += "trailer\n<< /Size " + QByteArray::number(bodies.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
    return out;
}
QByteArray streamObject(const QByteArray& dictEntries, const QByteArray& data)
{
    return "<< " + dictEntries + " /Length " + QByteArray::number(data.size()) + " >>\nstream\n" + data + "\nendstream";
}

// a photo-like picture: smooth colour fields plus fine noise
QImage photo(int w, int h)
{
    QImage img(w, h, QImage::Format_RGB888);
    QRandomGenerator rng(7);
    for (int y = 0; y < h; ++y) {
        uchar* line = img.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const double fx = double(x) / w, fy = double(y) / h;
            line[x * 3 + 0] = uchar(qBound(0.0, 120 + 100 * std::sin(fx * 9) + int(rng.bounded(24)) - 12, 255.0));
            line[x * 3 + 1] = uchar(qBound(0.0, 130 + 90 * std::cos(fy * 7 + fx * 3) + int(rng.bounded(24)) - 12, 255.0));
            line[x * 3 + 2] = uchar(qBound(0.0, 128 + 80 * std::sin((fx + fy) * 5) + int(rng.bounded(24)) - 12, 255.0));
        }
    }
    return img;
}

enum class Storage { Raw, Flate, Jpeg };

// One page 612 x 792 with the text "Compress me" and the picture shown 400 x 300 points big.
QByteArray pdfWithImage(const QImage& img, Storage storage, const QByteArray& extraCs = "/DeviceRGB")
{
    QByteArray data;
    QByteArray filter;
    if (storage == Storage::Jpeg) {
        QBuffer buf(&data);
        buf.open(QIODevice::WriteOnly);
        QImageWriter w(&buf, "jpeg");
        w.setQuality(95);
        w.write(img);
        filter = " /Filter /DCTDecode";
    } else {
        for (int y = 0; y < img.height(); ++y)
            data.append(reinterpret_cast<const char*>(img.constScanLine(y)), img.width() * 3);
        if (storage == Storage::Flate) {
            data = qCompress(data, 6).mid(4);
            filter = " /Filter /FlateDecode";
        }
    }
    const QByteArray content = "BT /F1 24 Tf 72 700 Td (Compress me) Tj ET q 400 0 0 300 50 300 cm /Im0 Do Q";
    return buildPdf({
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [4 0 R] /Count 1 >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R >> /XObject << /Im0 6 0 R >> >> /Contents 5 0 R >>",
        streamObject("", content),
        streamObject("/Type /XObject /Subtype /Image /Width " + QByteArray::number(img.width()) + " /Height " + QByteArray::number(img.height())
                         + " /ColorSpace " + extraCs + " /BitsPerComponent 8" + filter,
                     data),
    });
}

double meanOf(const QImage& img, QRect r)
{
    double sum = 0;
    int n = 0;
    for (int y = r.top(); y < r.bottom(); y += 3)
        for (int x = r.left(); x < r.right(); x += 3) {
            sum += qGray(img.pixel(x, y));
            ++n;
        }
    return n ? sum / n : 0;
}

} // namespace

class TstCompress : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

    QString write(const QString& name, const QByteArray& data)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() ? path : QString();
    }
    bool qpdfCheck(const QString& file, const QString& password = QString())
    {
        const QFileInfo qpdf(QStringLiteral(DP_TEST_QPDF));
        if (!qpdf.exists())
            return true;
        QProcess p;
        QStringList args;
        if (!password.isEmpty())
            args << "--password=" + password;
        args << "--check" << file;
        p.start(qpdf.absoluteFilePath(), args);
        return p.waitForFinished(30000) && p.exitCode() == 0;
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }
    void cleanupTestCase() { dp::pdf::shutdownPdfium(); }

    void aBigPhotoIsReducedAndTheTextStaysText()
    {
        const QString in = write("photo.pdf", pdfWithImage(photo(2000, 1500), Storage::Jpeg));
        const QString out = m_dir.filePath("photo-small.pdf");
        CompressOptions o;
        o.imageDpi = 150;
        o.jpegQuality = 70;
        const CompressResult r = compressFile(in, {}, out, o);
        QVERIFY2(r.ok, qPrintable(r.error + r.detail));
        QVERIFY(r.written);
        QCOMPARE(r.images, 1);
        QCOMPARE(r.imagesDownsampled, 1);
        QVERIFY2(r.outBytes < r.inBytes / 3, qPrintable(QString("%1 -> %2").arg(r.inBytes).arg(r.outBytes)));
        QCOMPARE(r.outBytes, QFileInfo(out).size());
        qInfo("PERF compress 2000x1500 JPEG photo shown at 360 dpi, 150 dpi q70: %lld -> %lld bytes", r.inBytes, r.outBytes);
        QVERIFY(qpdfCheck(out));

        auto before = PdfDocument::open(in, {}, nullptr);
        auto after = PdfDocument::open(out, {}, nullptr);
        QVERIFY(before && after);
        QCOMPARE(after->pageCount(), 1);
        QVERIFY(after->pageText(0).contains("Compress me"));                 // still text, not a picture
        QImage a = before->render(0, 1.0, nullptr, 0);
        QImage b = after->render(0, 1.0, nullptr, 0);
        QCOMPARE(a.size(), b.size());
        // the picture area (50..450 x 192..492 from the top) has about the same brightness, and the pictures are alike pixel by pixel
        const QRect area(60, 200, 380, 280);
        QVERIFY(std::abs(meanOf(a, area) - meanOf(b, area)) < 6.0);
        double diff = 0;
        int n = 0;
        for (int y = area.top(); y < area.bottom(); y += 4)
            for (int x = area.left(); x < area.right(); x += 4, ++n)
                diff += std::abs(qGray(a.pixel(x, y)) - qGray(b.pixel(x, y)));
        QVERIFY2(diff / n < 14.0, qPrintable(QString::number(diff / n)));
    }

    void aLosslessPhotoBecomesJpegAndAPlainDrawingKeepsItsEdges()
    {
        const QString in = write("flate-photo.pdf", pdfWithImage(photo(1200, 900), Storage::Flate));
        CompressOptions o;
        o.jpegQuality = 75;
        CompressResult r = compressFile(in, {}, m_dir.filePath("flate-photo-small.pdf"), o);
        QVERIFY2(r.ok && r.written, qPrintable(r.error));
        QCOMPARE(r.imagesChanged, 1);
        QCOMPARE(r.imagesDownsampled, 0);
        QVERIFY(r.outBytes < r.inBytes);

        // two colours only (a drawing): not turned into JPEG (that would blur the edges); a reduction of the pixels is lossless
        QImage drawing(1600, 1200, QImage::Format_RGB888);
        drawing.fill(Qt::white);
        for (int y = 0; y < drawing.height(); ++y)
            for (int x = 0; x < drawing.width(); ++x)
                if (((x / 40) + (y / 40)) % 2 == 0)
                    drawing.setPixel(x, y, qRgb(0, 0, 0));
        const QString in2 = write("drawing.pdf", pdfWithImage(drawing, Storage::Raw));
        CompressOptions o2;
        o2.jpegQuality = 75;
        r = compressFile(in2, {}, m_dir.filePath("drawing-small.pdf"), o2);
        QVERIFY(r.ok);
        QCOMPARE(r.imagesChanged, 0);                                         // only structure: the raw stream is deflated by the writer
        QVERIFY(r.written);
        QVERIFY(r.outBytes < r.inBytes / 10);
        o2.imageDpi = 100;
        r = compressFile(in2, {}, m_dir.filePath("drawing-small2.pdf"), o2);
        QVERIFY(r.ok && r.written);
        QCOMPARE(r.imagesDownsampled, 1);
        auto d = PdfDocument::open(m_dir.filePath("drawing-small2.pdf"), {}, nullptr);
        QVERIFY(d);
        const QImage img = d->render(0, 1.0, nullptr, 0);
        // checker squares are 40 / 1600 * 400 pt = 10 pt wide: their middles stay black / white
        QVERIFY(qGray(img.pixel(55, 200 + 5)) < 40 || qGray(img.pixel(55 + 10, 200 + 5)) < 40);
        QVERIFY(qGray(img.pixel(55, 200 + 5)) > 215 || qGray(img.pixel(55 + 10, 200 + 5)) > 215);
    }

    void whenNothingGetsSmallerNothingIsWritten()
    {
        const QString in = write("tiny.pdf", testpdf::make(1));
        const QString out = m_dir.filePath("tiny-out.pdf");
        CompressOptions o;
        o.imageDpi = 150;
        o.jpegQuality = 60;
        const CompressResult r = compressFile(in, {}, out, o);
        QVERIFY(r.ok);
        QVERIFY(!r.written);
        QCOMPARE(r.outBytes, r.inBytes);
        QVERIFY(!QFileInfo::exists(out));
        QVERIFY(!QFileInfo::exists(out + ".part.tmp"));

        // a small picture is left alone ("already small")
        const QString in2 = write("smallpic.pdf", pdfWithImage(photo(40, 40), Storage::Raw));
        const CompressResult r2 = compressFile(in2, {}, m_dir.filePath("smallpic-out.pdf"), o);
        QVERIFY(r2.ok);
        QCOMPARE(r2.imagesChanged, 0);
        QCOMPARE(r2.imagesSkipped, 1);
    }

    void aPictureShownSmallerThanTheTargetIsNotReducedAndBadFilesAreRefused()
    {
        // 600 x 450 pixels shown 400 x 300 pt = 108 dpi: below the 150 target
        const QString in = write("low.pdf", pdfWithImage(photo(600, 450), Storage::Raw));
        CompressOptions o;
        o.imageDpi = 150;
        const CompressResult r = compressFile(in, {}, m_dir.filePath("low-out.pdf"), o);
        QVERIFY(r.ok);
        QCOMPARE(r.imagesDownsampled, 0);
        QCOMPARE(r.imagesChanged, 0);

        const CompressResult bad = compressFile(write("junk.pdf", "not a pdf at all"), {}, m_dir.filePath("junk-out.pdf"), o);
        QVERIFY(!bad.ok);
        QCOMPARE(bad.error, QStringLiteral("format"));
        const CompressResult none = compressFile(m_dir.filePath("missing.pdf"), {}, m_dir.filePath("missing-out.pdf"), o);
        QVERIFY(!none.ok);
        QVERIFY(!QFileInfo::exists(m_dir.filePath("missing-out.pdf")));
    }

    void progressAndCancel()
    {
        const QString in = write("cancel.pdf", pdfWithImage(photo(2000, 1500), Storage::Jpeg));
        const QString out = m_dir.filePath("cancel-out.pdf");
        CompressOptions o;
        o.imageDpi = 150;
        o.jpegQuality = 70;
        int calls = 0;
        CompressResult r = compressFile(in, {}, out, o, [&](int, int) { ++calls; return true; });
        QVERIFY(r.ok);
        QVERIFY(calls >= 2);
        QFile::remove(out);
        r = compressFile(in, {}, out, o, [&](int, int) { return false; });
        QVERIFY(r.cancelled);
        QCOMPARE(r.error, QStringLiteral("cancelled"));
        QVERIFY(!QFileInfo::exists(out));
        QVERIFY(!QFileInfo::exists(out + ".part.tmp"));
    }

    void aProtectedFileStaysProtected()
    {
        const QFileInfo qpdf(QStringLiteral(DP_TEST_QPDF));
        if (!qpdf.exists())
            QSKIP("qpdf.exe is not in .tools\\qpdf");
        const QString plain = write("pp.pdf", pdfWithImage(photo(2000, 1500), Storage::Jpeg));
        const QString locked = m_dir.filePath("pp-locked.pdf");
        QProcess p;
        p.start(qpdf.absoluteFilePath(), {"--encrypt", "open-secret", "owner-secret", "256", "--", plain, locked});
        QVERIFY(p.waitForFinished(30000));
        QCOMPARE(p.exitCode(), 0);
        CompressOptions o;
        o.imageDpi = 150;
        o.jpegQuality = 70;
        CompressResult r = compressFile(locked, {}, m_dir.filePath("x.pdf"), o);
        QVERIFY(!r.ok);
        QCOMPARE(r.error, QStringLiteral("password"));
        r = compressFile(locked, "open-secret", m_dir.filePath("pp-out.pdf"), o);
        QVERIFY2(r.ok && r.written, qPrintable(r.error + r.detail));
        QVERIFY(r.warnings.contains("encrypted"));
        QVERIFY(r.outBytes < r.inBytes / 3);
        dp::pdf::OpenError err = dp::pdf::OpenError::None;
        QVERIFY(!PdfDocument::open(m_dir.filePath("pp-out.pdf"), {}, &err));    // still asks for the password
        QCOMPARE(err, dp::pdf::OpenError::Password);
        auto doc = PdfDocument::open(m_dir.filePath("pp-out.pdf"), "open-secret", &err);
        QVERIFY(doc);
        QVERIFY(doc->pageText(0).contains("Compress me"));
        QVERIFY(qpdfCheck(m_dir.filePath("pp-out.pdf"), "open-secret"));
    }

    void theCompressorMakesAPreviewableCopyAndSavesItOnlyOnRequest()
    {
        dp::DocumentController c(m_dir.filePath("dc"));
        dp::Compressor z(&c);
        QCOMPARE(z.start(z.preset("medium")), QStringLiteral("noDocument"));
        QCOMPARE(z.preset("low").value("dpi").toInt(), 0);
        QCOMPARE(z.preset("medium").value("dpi").toInt(), 150);
        QVERIFY(z.preset("high").value("quality").toInt() < z.preset("medium").value("quality").toInt());

        const QString in = write("cz.pdf", pdfWithImage(photo(2000, 1500), Storage::Jpeg));
        QCOMPARE(c.open(in), QString());
        QSignalSpy done(&z, &dp::Compressor::finished);
        QCOMPARE(z.start(z.preset("medium")), QString());
        QVERIFY(z.busy());
        QCOMPARE(z.start(z.preset("high")), QStringLiteral("busy"));
        QVERIFY(QTest::qWaitFor([&] { return done.count() > 0; }, 30000));
        QVERIFY(done.first().at(0).toBool());
        QVERIFY(z.hasResult());
        QVERIFY(c.documentFor(z.afterId()));                      // the smaller copy can be drawn for the comparison
        QCOMPARE(z.beforeId(), c.docId());
        const QVariantMap r = z.result();
        QVERIFY(r.value("written").toBool());
        QVERIFY(r.value("outBytes").toDouble() < r.value("inBytes").toDouble() / 3);
        QCOMPARE(r.value("downsampled").toInt(), 1);
        QCOMPARE(z.baseName(), QStringLiteral("cz-compressed"));

        QCOMPARE(z.saveAs(in), QStringLiteral("sameFile"));
        const QString out = m_dir.filePath("cz-saved.pdf");
        QCOMPARE(z.saveAs(out), QString());
        QCOMPARE(z.saveAs(out), QStringLiteral("exists"));
        QCOMPARE(QFileInfo(out).size(), qint64(r.value("outBytes").toDouble()));
        QVERIFY(!QFileInfo::exists(out + ".part.tmp"));
        QCOMPARE(PdfDocument::open(in, {}, nullptr)->pageCount(), 1);     // the original is as it was

        const int after = z.afterId();
        z.discard();
        QVERIFY(!z.hasResult());
        QVERIFY(!c.documentFor(after));
        QCOMPARE(z.saveAs(m_dir.filePath("none.pdf")), QStringLiteral("noResult"));

        // nothing to gain: no result, nothing to save
        QCOMPARE(c.open(write("plain.pdf", testpdf::make(1))), QString());
        done.clear();
        QCOMPARE(z.start(z.preset("high")), QString());
        QVERIFY(QTest::qWaitFor([&] { return done.count() > 0; }, 30000));
        QVERIFY(done.first().at(0).toBool());
        QVERIFY(!z.hasResult());
        QVERIFY(!z.result().value("written").toBool());
    }

    void fastWebViewAndStrippingStillGiveASoundFile()
    {
        const QString in = write("lin.pdf", pdfWithImage(photo(2000, 1500), Storage::Jpeg));
        CompressOptions o;
        o.imageDpi = 150;
        o.jpegQuality = 70;
        o.linearize = true;
        o.stripExtras = true;
        const CompressResult r = compressFile(in, {}, m_dir.filePath("lin-out.pdf"), o);
        QVERIFY2(r.ok && r.written, qPrintable(r.error));
        QVERIFY(qpdfCheck(m_dir.filePath("lin-out.pdf")));
        QFile f(m_dir.filePath("lin-out.pdf"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.read(1024).contains("/Linearized"));
    }
};

QTEST_MAIN(TstCompress)
#include "tst_compress.moc"
