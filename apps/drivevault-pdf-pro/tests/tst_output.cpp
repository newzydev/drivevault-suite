#include "ImageExport.h"
#include "ThaiOcr.h"
#include "ReplaceText.h"
#include "TestPdf.h"
#include "pdfengine/PdfDocument.h"
#include "pdfengine/PdfiumRuntime.h"
#include "pdftools/Security.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QFontDatabase>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QtTest>

class TstOutput : public QObject {
    Q_OBJECT

    static QString write(const QTemporaryDir& dir, const QString& name, const QByteArray& data)
    {
        const QString path = dir.filePath(name);
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() ? path : QString();
    }

private slots:
    void cleanupTestCase() { dp::pdf::shutdownPdfium(); }

    void thaiOcrMakesAnImagePageSearchableWithoutChangingItsPixels()
    {
        if (!dp::thaiOcrAvailable())
            QSKIP("Local ONNX Runtime and PaddleOCR models are absent");
        QTemporaryDir dir;
        const int fontId = QFontDatabase::addApplicationFont(DP_TEST_FONT_THAI);
        QVERIFY(fontId >= 0);
        const QString phrase = QStringLiteral("ภาษาไทย");
        const QString source = dir.filePath(QStringLiteral("สแกน.pdf"));
        {
            QImage image(1200, 700, QImage::Format_RGB32);
            image.fill(Qt::white);
            QPainter ink(&image);
            QFont font(QFontDatabase::applicationFontFamilies(fontId).first());
            font.setPixelSize(64);
            ink.setFont(font);
            ink.drawText(110, 180, phrase);
            ink.drawText(110, 340, QStringLiteral("เอกสารทดสอบ"));
            ink.end();
            QPdfWriter writer(source);
            writer.setPageSize(QPageSize(QSizeF(600, 350), QPageSize::Point));
            writer.setPageMargins({}, QPageLayout::Point);
            writer.setResolution(144);
            QPainter page(&writer);
            page.drawImage(0, 0, image);
            QVERIFY(page.end());
        }
        const auto input = dp::pdf::PdfDocument::open(source, {}, nullptr);
        QVERIFY(input);
        QVERIFY(input->pageText(0).isEmpty());
        const QString output = dir.filePath(QStringLiteral("ค้นหาได้.pdf"));
        std::atomic_bool cancel = false;
        QCOMPARE(dp::saveSearchableCopy(*input, {0}, output, cancel, {}), QString());
        const auto copy = dp::pdf::PdfDocument::open(output, {}, nullptr);
        QVERIFY(copy);
        const QString recognized = copy->pageText(0);
        QVERIFY2(recognized.contains(phrase), qPrintable(recognized));
        QVERIFY(!copy->searchPage(0, phrase, false, false).isEmpty());
        QCOMPARE(copy->render(0, 2), input->render(0, 2));
        QCOMPARE(dp::saveSearchableCopy(*input, {0}, output, cancel, {}), QStringLiteral("exists"));
        cancel = true;
        const QString abandoned = dir.filePath(QStringLiteral("cancelled.pdf"));
        QCOMPARE(dp::saveSearchableCopy(*input, {0}, abandoned, cancel, {}), QStringLiteral("cancelled"));
        QVERIFY(!QFileInfo::exists(abandoned));
    }

    void replacesExistingThaiTextWithShapedSearchableText()
    {
        QTemporaryDir dir;
        const int fontId = QFontDatabase::addApplicationFont(DP_TEST_FONT_THAI);
        QVERIFY(fontId >= 0);
        const QString original = QStringLiteral("ภาษาไทยทดสอบ");
        const QString replacement = QStringLiteral("น้ำใจ");
        const QString source = dir.filePath("original.pdf");
        {
            QPdfWriter writer(source);
            writer.setPageSize(QPageSize(QSizeF(600, 350), QPageSize::Point));
            writer.setPageMargins({}, QPageLayout::Point);
            writer.setResolution(72);
            QPainter page(&writer);
            QFont font(QFontDatabase::applicationFontFamilies(fontId).first());
            font.setPixelSize(28);
            page.setFont(font);
            page.drawText(100, 100, original);
            page.drawText(100, 230, QStringLiteral("Unchanged"));
            page.fillRect(QRect(320, 85, 80, 80), Qt::blue);
            QVERIFY(page.end());
        }
        const auto input = dp::pdf::PdfDocument::open(source, {}, nullptr);
        QVERIFY(input);
        const auto hits = input->searchPage(0, original, true, false);
        QVERIFY(!hits.isEmpty());
        const QPointF point = hits.front().rects.front().center();
        QCOMPARE(input->editableTextAt(0, point).text, original);
        const QString output = dir.filePath("edited.pdf");
        QCOMPARE(dp::replaceTextRun(*input, 0, point, original, replacement, output), QString());
        const auto copy = dp::pdf::PdfDocument::open(output, {}, nullptr);
        QVERIFY(copy);
        QVERIFY(!copy->pageText(0).contains(original));
        QVERIFY(copy->pageText(0).contains(replacement));
        QVERIFY(copy->pageText(0).contains(QStringLiteral("Unchanged")));
        QCOMPARE(copy->render(0, 1).pixelColor(350, 100), QColor(Qt::blue));
        QVERIFY(input->pageText(0).contains(original));
        QCOMPARE(dp::replaceTextRun(*input, 0, point, original, replacement, source), QStringLiteral("exists"));
    }

    void imageDimensionsTransparencyAndPixels()
    {
        QTemporaryDir dir;
        const auto source = write(dir, "source.pdf", testpdf::make(2, QSizeF(200, 300)));
        const auto doc = dp::pdf::PdfDocument::open(source, {}, nullptr);
        QVERIFY(doc);
        std::atomic_bool cancel = false;
        dp::ImageExport job;
        job.pages = {1};
        job.rotation = 1;
        const auto progress = [](int, int) {};
        const auto png = dir.filePath("rotated.png");
        QCOMPARE(dp::exportImages(*doc, png, job, cancel, progress), QString());
        QImage image(png);
        QCOMPARE(image.size(), QSize(600, 400));
        QCOMPARE(image.dotsPerMeterX(), qRound(144 / 0.0254));
        QCOMPARE(image.convertToFormat(QImage::Format_RGB32), doc->render(1, 2, nullptr, 1));
        QCOMPARE(dp::exportImages(*doc, png, job, cancel, progress), QStringLiteral("exists"));

        job.transparent = true;
        const auto alphaPath = dir.filePath("transparent.png");
        QCOMPARE(dp::exportImages(*doc, alphaPath, job, cancel, progress), QString());
        const QImage alpha(alphaPath);
        QCOMPARE(alpha.size(), image.size());
        QCOMPARE(alpha.pixelColor(0, 0).alpha(), 0);
        int ink = 0;
        for (int y = 0; y < alpha.height(); ++y)
            for (int x = 0; x < alpha.width(); ++x)
                ink += alpha.pixelColor(x, y).alpha() == 255 && image.pixelColor(x, y).red() < 20;
        QVERIFY(ink > 100);

        job.jpeg = true;
        job.dpi = 72;
        job.quality = 70;
        const auto jpeg = dir.filePath("page.jpg");
        QCOMPARE(dp::exportImages(*doc, jpeg, job, cancel, progress), QString());
        const QImage jpg(jpeg);
        QCOMPARE(jpg.size(), QSize(300, 200));
        QVERIFY(!jpg.hasAlphaChannel());
        QCOMPARE(jpg.pixelColor(0, 0), QColor(Qt::white));
    }

    void zipExtractsWithWindowsTarAndFolderCancelsAtomically()
    {
        QTemporaryDir dir;
        const auto source = write(dir, "source.pdf", testpdf::make(3, QSizeF(200, 300)));
        const auto doc = dp::pdf::PdfDocument::open(source, {}, nullptr);
        QVERIFY(doc);
        std::atomic_bool cancel = false;
        dp::ImageExport job;
        job.target = dp::ImageExport::Target::Zip;
        job.pages = {2, 0};
        job.dpi = 72;
        const auto zip = dir.filePath("selected.zip");
        QList<int> reported;
        const auto progress = [&](int done, int total) {
            QCOMPARE(total, 2);
            reported.append(done);
        };
        QCOMPARE(dp::exportImages(*doc, zip, job, cancel, progress), QString());
        QCOMPARE(reported, (QList<int>{1, 2}));
        const auto extracted = dir.filePath("extracted");
        QVERIFY(QDir().mkdir(extracted));
        QProcess tar;
        tar.start("tar.exe", {"-xf", zip, "-C", extracted});
        QVERIFY(tar.waitForFinished());
        QVERIFY2(tar.exitStatus() == QProcess::NormalExit && tar.exitCode() == 0, tar.readAllStandardError().constData());
        QCOMPARE(QDir(extracted).entryList(QDir::Files), (QStringList{"source-0001.png", "source-0003.png"}));
        for (const int page : job.pages) {
            const QString file = QDir(extracted).filePath(QString("source-%1.png").arg(page + 1, 4, 10, QLatin1Char('0')));
            QCOMPARE(QImage(file).convertToFormat(QImage::Format_RGB32), doc->render(page, 1));
        }
        const auto cancelAfterFirst = [&](int, int) { cancel.store(true); };
        const auto abandonedZip = dir.filePath("cancelled.zip");
        QCOMPARE(dp::exportImages(*doc, abandonedZip, job, cancel, cancelAfterFirst), QStringLiteral("cancelled"));
        QVERIFY(!QFileInfo::exists(abandonedZip));

        job.target = dp::ImageExport::Target::Folder;
        const auto folder = dir.filePath("images");
        cancel.store(false);
        QCOMPARE(dp::exportImages(*doc, folder, job, cancel, cancelAfterFirst), QStringLiteral("cancelled"));
        QVERIFY(!QFileInfo::exists(folder));
        QVERIFY(QDir(dir.path()).entryList({".dv-images-*"}, QDir::AllEntries | QDir::Hidden).isEmpty());
        cancel.store(false);
        QCOMPARE(dp::exportImages(*doc, folder, job, cancel, progress), QString());
        QCOMPARE(QDir(folder).entryList(QDir::Files), QDir(extracted).entryList(QDir::Files));
    }

    void aes256PermissionsAndUnlock()
    {
        QTemporaryDir dir;
        const QByteArray original = testpdf::makeFeatureDoc();
        const auto source = write(dir, QStringLiteral("ต้นฉบับ.pdf"), original);
        const auto encrypted = dir.filePath(QStringLiteral("มีรหัส.pdf"));
        const auto unlocked = dir.filePath("unlocked.pdf");
        dp::tools::Protection settings{QStringLiteral("เปิดไฟล์"), QStringLiteral("เจ้าของ"), false, false, false, false};
        QCOMPARE(dp::tools::protectPdf(source, source, settings), QStringLiteral("exists"));
        QCOMPARE(dp::tools::protectPdf(source, encrypted, settings), QString());
        QVERIFY(!dp::pdf::PdfDocument::open(encrypted, {}, nullptr));
        const auto protectedDoc = dp::pdf::PdfDocument::open(encrypted, settings.userPassword, nullptr);
        QVERIFY(protectedDoc);
        QCOMPARE(protectedDoc->pageCount(), 3);
        QVERIFY(protectedDoc->pageText(1).contains("Second page"));

        QProcess qpdf;
        qpdf.start(DP_TEST_QPDF, {QStringLiteral("--password=") + settings.userPassword, "--show-encryption", encrypted});
        QVERIFY(qpdf.waitForFinished());
        QCOMPARE(qpdf.exitCode(), 0);
        const QByteArray details = qpdf.readAllStandardOutput();
        QVERIFY(details.contains("R = 6"));
        QVERIFY(details.contains("stream encryption method: AESv3"));
        QVERIFY(details.contains("print high resolution: not allowed"));
        QVERIFY(details.contains("extract for any purpose: not allowed"));
        QVERIFY(details.contains("modify annotations: not allowed"));
        QVERIFY(details.contains("modify other: not allowed"));

        QCOMPARE(dp::tools::unlockPdf(encrypted, unlocked, "wrong"), QStringLiteral("password"));
        QVERIFY(!QFileInfo::exists(unlocked));
        QCOMPARE(dp::tools::unlockPdf(encrypted, unlocked, settings.userPassword), QString());
        const auto doc = dp::pdf::PdfDocument::open(unlocked, {}, nullptr);
        QVERIFY(doc);
        QCOMPARE(doc->pageText(1), protectedDoc->pageText(1));
        QCOMPARE(doc->outline().size(), 3);
        QCOMPARE(dp::tools::unlockPdf(unlocked, dir.filePath("copy.pdf"), "wrong"), QStringLiteral("notProtected"));
        QCOMPARE(dp::tools::unlockPdf(encrypted, unlocked, settings.ownerPassword), QStringLiteral("exists"));
        QFile input(source);
        QVERIFY(input.open(QIODevice::ReadOnly));
        QCOMPARE(input.readAll(), original);
    }

    void permissionsOnlyRequiresOwnerPassword()
    {
        QTemporaryDir dir;
        const auto source = write(dir, "original.pdf", testpdf::make(1));
        const auto restricted = dir.filePath("restricted.pdf");
        const auto unlocked = dir.filePath("unlocked.pdf");
        dp::tools::Protection settings;
        settings.ownerPassword = "owner-secret";
        settings.userPassword = settings.ownerPassword;
        settings.copy = false;
        QCOMPARE(dp::tools::protectPdf(source, restricted, settings), QStringLiteral("invalidPasswords"));
        settings.userPassword.clear();
        QCOMPARE(dp::tools::protectPdf(source, restricted, settings), QString());
        QVERIFY(dp::pdf::PdfDocument::open(restricted, {}, nullptr));
        QCOMPARE(dp::tools::protectPdf(restricted, unlocked, settings), QStringLiteral("alreadyProtected"));
        QCOMPARE(dp::tools::unlockPdf(restricted, unlocked, {}), QStringLiteral("ownerRequired"));
        QCOMPARE(dp::tools::unlockPdf(restricted, unlocked, "wrong"), QStringLiteral("password"));
        QVERIFY(!QFileInfo::exists(unlocked));
        QCOMPARE(dp::tools::unlockPdf(restricted, unlocked, settings.ownerPassword), QString());
        QVERIFY(dp::pdf::PdfDocument::open(unlocked, {}, nullptr));
    }

    void legacyPdfUsesPdfDocEncoding()
    {
        QTemporaryDir dir;
        const auto source = write(dir, "original.pdf", testpdf::make(1));
        const auto restricted = dir.filePath("legacy.pdf");
        const auto unlocked = dir.filePath("unlocked.pdf");
        const QString password = QStringLiteral("café");
        QProcess qpdf;
        qpdf.start(DP_TEST_QPDF, {"--encrypt", password, "owner", "128", "--use-aes=y", "--", source, restricted});
        QVERIFY(qpdf.waitForFinished());
        QVERIFY2(qpdf.exitCode() == 0, qpdf.readAllStandardError().constData());
        QCOMPARE(dp::tools::unlockPdf(restricted, unlocked, password), QString());
        QVERIFY(dp::pdf::PdfDocument::open(unlocked, {}, nullptr));
    }
};

QTEST_MAIN(TstOutput)
#include "tst_output.moc"
