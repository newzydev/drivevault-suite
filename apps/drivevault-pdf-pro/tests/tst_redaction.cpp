#include "Redaction.h"
#include "TestPdf.h"
#include "pdfengine/PdfDocument.h"
#include "pdfengine/PdfiumRuntime.h"
#include "pdftools/RedactionAudit.h"
#include "pdftools/Security.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QByteArray sensitivePdf()
{
    const auto stream = [](const QByteArray& bytes, const QByteArray& dictionary = {}) {
        return "<< /Length " + QByteArray::number(bytes.size()) + " " + dictionary + " >>\nstream\n" + bytes + "\nendstream";
    };
    const QList<QByteArray> objects = {
        "<< /Type /Catalog /Pages 2 0 R /Names << /EmbeddedFiles << /Names [(secret.txt) 8 0 R] >> >> /Metadata 10 0 R >>",
        "<< /Type /Pages /Kids [3 0 R 11 0 R] /Count 2 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] /Rotate 90 /Resources << /Font << /F 4 0 R >> /XObject << /Im 13 0 R >> >> "
            "/Contents 5 0 R /Annots [6 0 R] >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        stream("BT /F 16 Tf 50 325 Td (SECRET-7342) Tj 0 -40 Td (Keep this visible) Tj 3 Tr 0 -40 Td (HIDDEN-7592) Tj ET "
               "q 115 0 0 55 45 65 cm /Im Do Q"),
        "<< /Type /Annot /Subtype /Text /Rect [200 250 215 265] /Contents (ANNOTATION-SECRET) >>",
        "<< /Title (TITLE-SECRET) /Author (AUTHOR-SECRET) >>",
        "<< /Type /Filespec /F (secret.txt) /EF << /F 9 0 R >> >>",
        stream("ATTACHMENT-SECRET", "/Type /EmbeddedFile"),
        stream("<xmp>METADATA-SECRET</xmp>", "/Type /Metadata /Subtype /XML"),
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] /Resources << /Font << /F 4 0 R >> >> /Contents 12 0 R >>",
        stream("BT /F 14 Tf 35 245 Td (Unmarked page) Tj ET"),
        stream(QByteArray::fromHex("0000ff0000ff0000ff0000ff"),
               "/Type /XObject /Subtype /Image /Width 2 /Height 2 /ColorSpace /DeviceRGB /BitsPerComponent 8")
    };
    QByteArray bytes = "%PDF-1.4\n";
    QByteArray xref = "xref\n0 " + QByteArray::number(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (qsizetype i = 0; i < objects.size(); ++i) {
        xref += QByteArray::number(bytes.size()).rightJustified(10, '0') + " 00000 n \n";
        bytes += QByteArray::number(i + 1) + " 0 obj\n" + objects.at(i) + "\nendobj\n";
    }
    const auto offset = bytes.size();
    return bytes + xref + "trailer\n<< /Size " + QByteArray::number(objects.size() + 1)
        + " /Root 1 0 R /Info 7 0 R >>\nstartxref\n" + QByteArray::number(offset) + "\n%%EOF\n";
}

QString writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() ? path : QString();
}

} // namespace

class TstRedaction : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        const int font = QFontDatabase::addApplicationFont(DP_TEST_FONT_THAI);
        QVERIFY(font >= 0);
        QGuiApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).first()));
    }
    void cleanupTestCase() { dp::pdf::shutdownPdfium(); }

    void removesVisibleAndHiddenDataAndPreservesUnmarkedPixels()
    {
        QTemporaryDir dir;
        const QByteArray original = sensitivePdf();
        const auto input = writeFile(dir.filePath("source.pdf"), original);
        const auto source = dp::pdf::PdfDocument::open(input, {}, nullptr);
        QVERIFY(source);
        QVERIFY(source->pageText(0).contains("HIDDEN-7592"));
        QCOMPARE(source->pageSizePt(0), QSizeF(400, 300));
        const auto matches = source->matchPage(0, QRegularExpression("SECRET-[0-9]{4}"), 10);
        QCOMPARE(matches.size(), 1);
        QCOMPARE(matches.first().rects.size(), 1);
        const auto secret = matches.first().rects.first().adjusted(-2, -2, 2, 2);
        const QList<dp::RedactionMark> marks{{0, secret}, {0, QRectF(60, 50, 30, 40)}};
        const auto output = dir.filePath("redacted.pdf");
        QCOMPARE(dp::redactPdf(*source, output, marks, Qt::black, {}, {}), QString());
        QCOMPARE(dp::redactPdf(*source, output, marks, Qt::black, {}, {}), QStringLiteral("exists"));
        QFile untouched(input);
        QVERIFY(untouched.open(QIODevice::ReadOnly));
        QCOMPARE(untouched.readAll(), original);

        const auto copy = dp::pdf::PdfDocument::open(output, {}, nullptr);
        QVERIFY(copy);
        QCOMPARE(copy->pageCount(), 2);
        QCOMPARE(copy->pageSizePt(0), source->pageSizePt(0));
        QVERIFY(copy->pageText(0).isEmpty());
        QVERIFY(copy->pageText(1).isEmpty());
        QVERIFY(copy->annotations().isEmpty());
        const auto rendered = copy->render(0, 200.0 / 72);
        const auto point = [&](QPointF p) {
            return QPoint(qRound(p.x() * rendered.width() / 400), qRound(p.y() * rendered.height() / 300));
        };
        QCOMPARE(rendered.pixelColor(point(secret.center())), QColor(Qt::black));
        const auto sourcePixels = source->render(0, 200.0 / 72);
        QCOMPARE(sourcePixels.pixelColor(point(QPointF(75, 70))), QColor(Qt::blue));
        QCOMPARE(rendered.pixelColor(point(QPointF(75, 70))), QColor(Qt::black));
        QCOMPARE(rendered.pixelColor(point(QPointF(105, 70))), QColor(Qt::blue));
        const auto unmarked = source->render(1, 200.0 / 72);
        QCOMPARE(copy->render(1, 200.0 / 72), unmarked);

        QFile bytes(output);
        QVERIFY(bytes.open(QIODevice::ReadOnly));
        const auto saved = bytes.readAll();
        QVERIFY(!dp::tools::auditRedaction(output, {QByteArray(32, 0), QByteArray(32, 0)}));
        for (const QByteArray token : {"SECRET-7342", "HIDDEN-7592", "ANNOTATION-SECRET", "ATTACHMENT-SECRET",
                                       "METADATA-SECRET", "AUTHOR-SECRET", "/Prev", "/Info", "/Annots", "/EmbeddedFile"})
            QVERIFY2(!saved.contains(token), token.constData());
        QProcess check;
        check.start(DP_TEST_QPDF, {"--check", output});
        QVERIFY(check.waitForFinished());
        QVERIFY2(check.exitCode() == 0, check.readAllStandardError().constData());
    }

    void refusesProtectedInvalidAndCancelledJobs()
    {
        QTemporaryDir dir;
        const auto input = writeFile(dir.filePath("source.pdf"), testpdf::make(2));
        const auto source = dp::pdf::PdfDocument::open(input, {}, nullptr);
        QVERIFY(source);
        const auto output = dir.filePath("redacted.pdf");
        const QList<dp::RedactionMark> marks{{0, QRectF(30, 30, 100, 40)}};
        QCOMPARE(dp::redactPdf(*source, output, {{0, QRectF(-5, 0, 30, 30)}}, Qt::black, {}, {}),
                 QStringLiteral("redactMarks"));
        QCOMPARE(dp::redactPdf(*source, output, marks, Qt::transparent, {}, {}), QStringLiteral("redactMarks"));
        QCOMPARE(dp::redactPdf(*source, output, marks, Qt::red, "REDACTED", [](int done, int) { return done < 2; }),
                 QStringLiteral("cancelled"));
        QVERIFY(!QFileInfo::exists(output));
        QVERIFY(QDir(dir.path()).entryList({".dv-redact-*"}, QDir::AllEntries | QDir::Hidden).isEmpty());

        const auto protectedFile = dir.filePath("protected.pdf");
        QCOMPARE(dp::tools::protectPdf(input, protectedFile, {"", "owner"}), QString());
        const auto protectedDoc = dp::pdf::PdfDocument::open(protectedFile, {}, nullptr);
        QVERIFY(protectedDoc);
        QCOMPARE(dp::redactPdf(*protectedDoc, output, marks, Qt::black, {}, {}), QStringLiteral("redactProtected"));
        QVERIFY(!QFileInfo::exists(output));
        QCOMPARE(dp::redactPdf(*source, output, marks, Qt::red, "REDACTED", {}), QString());
        const auto copy = dp::pdf::PdfDocument::open(output, {}, nullptr);
        QVERIFY(copy);
        const auto image = copy->render(0, 1);
        QCOMPARE(image.pixelColor(32, 32), QColor(Qt::red));
        int lightPixels = 0;
        for (int y = 40; y < 60; ++y)
            for (int x = 45; x < 115; ++x)
                lightPixels += image.pixelColor(x, y).green() > 200;
        QVERIFY(lightPixels > 20);
    }
};

QTEST_MAIN(TstRedaction)
#include "tst_redaction.moc"
