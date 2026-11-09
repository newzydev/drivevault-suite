#include "BatchPdf.h"
#include "TestPdf.h"
#include "pdfengine/PdfDocument.h"
#include "pdfengine/PdfiumRuntime.h"
#include "pdftools/DocumentMaintenance.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QString writeFile(const QString& path, const QByteArray& data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() ? path : QString();
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool qpdf(const QStringList& args)
{
    QProcess process;
    process.start(DP_TEST_QPDF, args);
    return process.waitForFinished() && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}
} // namespace

class TstMaintenance : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { QVERIFY(QFontDatabase::addApplicationFont(DP_TEST_FONT_THAI) >= 0); }
    void cleanupTestCase() { dp::pdf::shutdownPdfium(); }

    void metadataAndSelectiveAttachmentRemoval()
    {
        QTemporaryDir dir;
        const auto base = writeFile(dir.filePath("base.pdf"), testpdf::makeFeatureDoc());
        const auto one = writeFile(dir.filePath("private.txt"), "ATTACHMENT_SECRET_727");
        const auto two = writeFile(dir.filePath("keep.txt"), "Public file");
        const auto attached = dir.filePath("attached.pdf");
        QVERIFY(qpdf({base, "--add-attachment", one, "--", "--add-attachment", two, "--", attached}));
        const auto original = readFile(attached);
        const auto info = dp::tools::inspectDocument(attached, {});
        const auto attachments = info.value("attachments").toList();
        QCOMPARE(attachments.size(), 2);
        QString id;
        for (const auto& file : attachments)
            if (file.toMap().value("name") == QStringLiteral("private.txt"))
                id = file.toMap().value("id").toString();
        QVERIFY(!id.isEmpty());
        const auto edited = dir.filePath("edited.pdf");
        const QVariantMap fields{{"Title", QStringLiteral("ชื่อภาษาไทย")}, {"Author", "AUTHOR-SECRET"}, {"Keywords", "two words"}};
        QCOMPARE(dp::tools::maintainDocument(attached, {}, edited, "metadata", fields, {id}), QString());
        QCOMPARE(dp::tools::maintainDocument(attached, {}, edited, "metadata"), QStringLiteral("exists"));
        QCOMPARE(readFile(attached), original);
        const auto changed = dp::tools::inspectDocument(edited, {});
        QCOMPARE(changed.value("Title"), fields.value("Title"));
        QCOMPARE(changed.value("Author"), fields.value("Author"));
        QCOMPARE(changed.value("Keywords"), fields.value("Keywords"));
        const auto remaining = changed.value("attachments").toList();
        QCOMPARE(remaining.size(), 1);
        QCOMPARE(remaining.first().toMap().value("name").toString(), QStringLiteral("keep.txt"));
        const auto raw = dir.filePath("raw.pdf");
        QVERIFY(qpdf({edited, "--qdf", "--stream-data=uncompress", raw}));
        QVERIFY(!readFile(raw).contains("ATTACHMENT_SECRET_727"));

        const auto clean = dir.filePath("clean.pdf");
        QCOMPARE(dp::tools::maintainDocument(edited, {}, clean, "clean"), QString());
        const auto scrubbed = dp::tools::inspectDocument(clean, {});
        QVERIFY(scrubbed.value("Author").toString().isEmpty());
        QVERIFY(scrubbed.value("Title").toString().isEmpty());
        QVERIFY(scrubbed.value("attachments").toList().isEmpty());
        const auto result = dp::pdf::PdfDocument::open(clean, {}, nullptr);
        QVERIFY(result);
        QVERIFY(result->annotations().isEmpty());
        QVERIFY(result->pageText(0).contains("Hello search world"));
        const auto cleanRaw = dir.filePath("clean-raw.pdf");
        QVERIFY(qpdf({clean, "--qdf", "--stream-data=uncompress", cleanRaw}));
        for (const auto token : {"AUTHOR-SECRET", "/EmbeddedFiles", "/EmbeddedFile", "/Annots", "Remember this"})
            QVERIFY2(!readFile(cleanRaw).contains(token), token);
    }

    void repairsBrokenXrefAndRejectsUnrecoverableInput()
    {
        QTemporaryDir dir;
        auto bytes = testpdf::make(3);
        bytes.truncate(bytes.lastIndexOf("\nstartxref"));
        bytes += "\nstartxref\n1\n%%EOF\n";
        const auto broken = writeFile(dir.filePath("broken.pdf"), bytes);
        const auto repaired = dir.filePath("repaired.pdf");
        QCOMPARE(dp::tools::maintainDocument(broken, {}, repaired, "metadata"), QStringLiteral("format"));
        QCOMPARE(dp::tools::maintainDocument(broken, {}, repaired, "repair"), QString());
        QVERIFY(qpdf({"--check", repaired}));
        const auto doc = dp::pdf::PdfDocument::open(repaired, {}, nullptr);
        QVERIFY(doc);
        QCOMPARE(doc->pageCount(), 3);
        QVERIFY(doc->pageText(2).contains("Page 3 of 3"));
        QCOMPARE(readFile(broken), bytes);
        const auto invalid = writeFile(dir.filePath("invalid.pdf"), "not a PDF");
        const auto failed = dir.filePath("failed.pdf");
        QCOMPARE(dp::tools::maintainDocument(invalid, {}, failed, "repair"), QStringLiteral("format"));
        QVERIFY(!QFileInfo::exists(failed));
    }

    void cleanupRemovesXmpWithoutChangingPagePixels()
    {
        QTemporaryDir dir;
        const auto source = dir.filePath("xmp.pdf");
        {
            QPdfWriter writer(source);
            writer.setTitle("PRIVATE_TITLE");
            writer.setDocumentXmpMetadata("<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
                                          "<rdf:Description>PRIVATE_XMP</rdf:Description></rdf:RDF>");
            QPainter painter(&writer);
            QVERIFY(painter.isActive());
            painter.fillRect(120, 160, 1000, 600, Qt::blue);
        }
        const auto rawSource = dir.filePath("raw-source.pdf");
        QVERIFY(qpdf({source, "--qdf", "--stream-data=uncompress", rawSource}));
        QVERIFY(readFile(rawSource).contains("PRIVATE_XMP"));
        const auto cleaned = dir.filePath("clean.pdf");
        QCOMPARE(dp::tools::maintainDocument(source, {}, cleaned, "clean"), QString());
        const auto raw = dir.filePath("raw.pdf");
        QVERIFY(qpdf({cleaned, "--qdf", "--stream-data=uncompress", raw}));
        QVERIFY(!readFile(raw).contains("PRIVATE_XMP"));
        QVERIFY(!readFile(raw).contains("/Metadata"));
        const auto before = dp::pdf::PdfDocument::open(source, {}, nullptr);
        const auto after = dp::pdf::PdfDocument::open(cleaned, {}, nullptr);
        QVERIFY(before && after);
        QCOMPARE(before->render(0, 1), after->render(0, 1));
    }

    void batchReportsEachFileAndKeepsCompletedCopies()
    {
        QTemporaryDir dir;
        const auto in = dir.filePath("in");
        const auto out = dir.filePath("out");
        QVERIFY(QDir().mkdir(in));
        QVERIFY(QDir().mkdir(out));
        const auto first = writeFile(QDir(in).filePath("a.pdf"), testpdf::make(2));
        QVERIFY(!writeFile(QDir(in).filePath("b.pdf"), "broken").isEmpty());
        QVERIFY(!writeFile(QDir(in).filePath("skip.pdf"), testpdf::make(1)).isEmpty());
        QVERIFY(!writeFile(QDir(out).filePath("skip.pdf"), "existing").isEmpty());
        QString error;
        const QVariantMap protect{{"mode", "protect"}, {"user", "reader"}, {"owner", "owner"}};
        const auto rows = dp::batchPdf(in, out, protect, {}, error);
        QCOMPARE(error, QString());
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows.at(0).toMap().value("status").toString(), QString());
        QCOMPARE(rows.at(1).toMap().value("status").toString(), QStringLiteral("format"));
        QCOMPARE(rows.at(2).toMap().value("status").toString(), QStringLiteral("exists"));
        QCOMPARE(readFile(QDir(out).filePath("skip.pdf")), QByteArray("existing"));
        const auto protectedFile = QDir(out).filePath("a.pdf");
        QVERIFY(!dp::pdf::PdfDocument::open(protectedFile, {}, nullptr));
        QVERIFY(dp::pdf::PdfDocument::open(protectedFile, "reader", nullptr));
        QCOMPARE(dp::tools::maintainDocument(protectedFile, "reader", dir.filePath("copy.pdf"), "clean"), QStringLiteral("ownerRequired"));
        const auto clean = dir.filePath("clean.pdf");
        QCOMPARE(dp::tools::maintainDocument(protectedFile, "owner", clean, "clean"), QString());
        QVERIFY(dp::pdf::PdfDocument::open(clean, "reader", nullptr));
        QVERIFY(!dp::pdf::PdfDocument::open(clean, {}, nullptr));
        QCOMPARE(readFile(first), testpdf::make(2));

        const auto water = dir.filePath("water");
        QVERIFY(QDir().mkdir(water));
        const auto waterRows = dp::batchPdf(in, water, {{"mode", "watermark"}, {"watermark", "CONFIDENTIAL"}},
            [](int done, int) { return done < 1; }, error);
        QCOMPARE(error, QStringLiteral("batchCancelled"));
        QCOMPARE(waterRows.size(), 1);
        QCOMPARE(waterRows.first().toMap().value("status").toString(), QString());
        const auto marked = dp::pdf::PdfDocument::open(QDir(water).filePath("a.pdf"), {}, nullptr);
        QVERIFY(marked);
        QVERIFY(marked->pageText(1).contains("CONFIDENTIAL"));
        QVERIFY(!QFileInfo::exists(QDir(water).filePath("b.pdf")));
        QVERIFY(dp::batchPdf(in, in, protect, {}, error).isEmpty());
        QCOMPARE(error, QStringLiteral("batchFolders"));
    }

    void batchCompressesPhotoAndPreservesText()
    {
        QTemporaryDir dir;
        const auto in = dir.filePath("in"), out = dir.filePath("out");
        QVERIFY(QDir().mkdir(in));
        QVERIFY(QDir().mkdir(out));
        const auto photo = writeFile(QDir(in).filePath("photo.pdf"), testpdf::makePhotoDoc());
        QString error;
        const auto rows = dp::batchPdf(in, out, {{"mode", "compress"}}, {}, error);
        QCOMPARE(error, QString());
        QCOMPARE(rows.size(), 1);
        QCOMPARE(rows.first().toMap().value("status").toString(), QString());
        const auto copy = QDir(out).filePath("photo.pdf");
        QVERIFY(QFileInfo(copy).size() < QFileInfo(photo).size());
        const auto result = dp::pdf::PdfDocument::open(copy, {}, nullptr);
        QVERIFY(result);
        QVERIFY(result->pageText(0).contains("Compress me"));
    }
};

QTEST_MAIN(TstMaintenance)
#include "tst_maintenance.moc"
