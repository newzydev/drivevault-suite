// Thai OCR measured on generated scans (image-only PDF pages with Thai document text in several sizes, weights, noise levels and small tilts): the
// character error rate of the recognised text layer against the known text. Also the pure Thai text rules. The pages are synthetic - they say nothing
// about handwriting, stamps, photographs or very old print.

#include "ThaiOcr.h"
#include "pdfengine/PdfDocument.h"
#include "pdfengine/PdfiumRuntime.h"

#include <QFontDatabase>
#include <QPainter>
#include <QPdfWriter>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <vector>

namespace {

const QStringList kLines = {
    QStringLiteral("ด้วยมหาวิทยาลัยมีความประสงค์จะจัดซื้อครุภัณฑ์คอมพิวเตอร์สำหรับห้องปฏิบัติการ"),
    QStringLiteral("โดยวิธีเฉพาะเจาะจง ตามพระราชบัญญัติการจัดซื้อจัดจ้างและการบริหารพัสดุภาครัฐ"),
    QStringLiteral("งบประมาณทั้งสิ้น 1,250,000 บาท (หนึ่งล้านสองแสนห้าหมื่นบาทถ้วน)"),
    QStringLiteral("ให้ผู้ที่เกี่ยวข้องดำเนินการตามขั้นตอนและรายงานผลให้ทราบภายในวันที่ 30 กันยายน 2569"),
    QStringLiteral("คณะกรรมการตรวจรับพัสดุได้ตรวจสอบรายการแล้วเห็นว่าถูกต้องครบถ้วนตามสัญญา"),
    QStringLiteral("จึงเรียนมาเพื่อโปรดพิจารณาอนุมัติและลงนามในเอกสารฉบับนี้ต่อไป"),
    QStringLiteral("นักศึกษาที่ได้รับทุนการศึกษาต้องส่งรายงานผลการเรียนทุกภาคการศึกษา"),
    QStringLiteral("การประชุมครั้งนี้มีผู้เข้าร่วมทั้งหมด 24 คน โดยมีนายกสภามหาวิทยาลัยเป็นประธาน"),
};

QString compact(const QString& text)
{
    QString out = dp::canonicalThai(text);
    out.remove(QRegularExpression(QStringLiteral("\\s")));
    return out;
}

// Levenshtein distance over UTF-16 code units (Thai is in the BMP).
int editDistance(const QString& a, const QString& b)
{
    std::vector<int> prev(size_t(b.size()) + 1), cur(size_t(b.size()) + 1);
    for (int j = 0; j <= b.size(); ++j)
        prev[size_t(j)] = j;
    for (int i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (int j = 1; j <= b.size(); ++j)
            cur[size_t(j)] = std::min({prev[size_t(j)] + 1, cur[size_t(j - 1)] + 1, prev[size_t(j - 1)] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[size_t(b.size())];
}

struct Variant {
    const char* name;
    int pixels;          // font size at 200 dpi
    int weight;
    int paper;           // background grey
    int noise;           // +- per pixel
    double tilt;         // degrees
};

const Variant kVariants[] = {
    {"12pt clean", 34, QFont::Normal, 255, 0, 0.0},
    {"14pt clean", 40, QFont::Normal, 255, 0, 0.0},
    {"16pt bold", 46, QFont::Bold, 255, 0, 0.0},
    {"10pt clean", 28, QFont::Normal, 255, 0, 0.0},
    {"12pt noisy grey paper", 34, QFont::Normal, 232, 18, 0.0},
    {"14pt noisy", 40, QFont::Normal, 245, 14, 0.0},
    {"12pt tilt 0.6", 34, QFont::Normal, 255, 6, 0.6},
    {"12pt tilt -0.8 noisy", 34, QFont::Normal, 240, 12, -0.8},
    {"10pt noisy bold", 28, QFont::DemiBold, 238, 16, 0.0},
    {"16pt grey paper", 46, QFont::Normal, 225, 10, 0.0},
    {"8pt clean", 22, QFont::Normal, 255, 0, 0.0},
    {"9pt noisy grey paper", 25, QFont::Normal, 228, 20, 0.0},
};

} // namespace

class TstOcr : public QObject {
    Q_OBJECT

    // One variant as an image-only A4 page; returns the text that is on it.
    static QString drawPage(QPainter& page, const QFont& base, const Variant& v, int index)
    {
        QImage image(1654, 2339, QImage::Format_RGB32);
        image.fill(QColor(v.paper, v.paper, v.paper));
        QString reference;
        {
            QPainter ink(&image);
            ink.setRenderHint(QPainter::TextAntialiasing);
            QFont font = base;
            font.setPixelSize(v.pixels);
            font.setWeight(QFont::Weight(v.weight));
            ink.setFont(font);
            ink.setPen(QColor(25, 25, 25));
            ink.translate(image.width() / 2.0, image.height() / 2.0);
            ink.rotate(v.tilt);
            ink.translate(-image.width() / 2.0, -image.height() / 2.0);
            const int gap = int(v.pixels * 2.1);
            for (int n = 0; n < 8; ++n) {
                const QString line = kLines.at((index + n) % kLines.size());
                ink.drawText(130, 220 + n * gap, line);
                reference += line;
            }
        }
        if (v.noise > 0) {
            QRandomGenerator rng(quint32(1000 + index));
            for (int y = 0; y < image.height(); ++y) {
                QRgb* row = reinterpret_cast<QRgb*>(image.scanLine(y));
                for (int x = 0; x < image.width(); ++x) {
                    const int d = int(rng.bounded(2 * v.noise + 1)) - v.noise;
                    const int g = std::clamp(qGray(row[x]) + d, 0, 255);
                    row[x] = qRgb(g, g, g);
                }
            }
        }
        page.drawImage(0, 0, image);
        return reference;
    }

private slots:
    void cleanupTestCase() { dp::pdf::shutdownPdfium(); }

    void thaiTextRulesMatchTheModelsRuntime()
    {
        // sara am written as nikhahit + sara aa (either order with a tone mark) becomes the single character
        QCOMPARE(dp::canonicalThai(QString::fromUtf8("นํา")), QString::fromUtf8("นำ"));
        QCOMPARE(dp::canonicalThai(QString::fromUtf8("น้ํา")), QString::fromUtf8("น้ำ"));
        QCOMPARE(dp::canonicalThai(QString::fromUtf8("นํ้า")), QString::fromUtf8("น้ำ"));
        // marks in one order (vowel, tone), a doubled mark is one, two sara e are one sara ae
        QCOMPARE(dp::canonicalThai(QString::fromUtf8("ก่ิ")), QString::fromUtf8("กิ่"));
        QCOMPARE(dp::canonicalThai(QString::fromUtf8("ก่่")), QString::fromUtf8("ก่"));
        QCOMPARE(dp::canonicalThai(QString::fromUtf8("เเก")), QString::fromUtf8("แก"));
        // legacy glyph variants, zero width characters, spaces
        QCOMPARE(dp::canonicalThai(QString(QChar(0xF705))), QString(QChar(0x0E48)));
        QCOMPARE(dp::canonicalThai(QString::fromUtf8("ก​ข  ค  ง ")), QString::fromUtf8("กข ค ง"));
        // lookalikes
        QCOMPARE(dp::fixThaiScripts(QString::fromUtf8("W.ศ.")), QString::fromUtf8("พ.ศ."));
        QCOMPARE(dp::fixThaiScripts(QString::fromUtf8("wฤษภาคม")), QString::fromUtf8("พฤษภาคม"));
        QCOMPARE(dp::fixThaiScripts(QString::fromUtf8("Windows")), QString::fromUtf8("Windows"));
        QCOMPARE(dp::fixThaiScripts(QString::fromUtf8("๗o.๘๒")), QString::fromUtf8("๗๐.๘๒"));
        // spelling check: a mark without a consonant under it, a leading vowel without a consonant after it
        QCOMPARE(dp::invalidThaiShare(QString::fromUtf8("สวัสดี")), 0.0);
        QVERIFY(dp::invalidThaiShare(QString::fromUtf8("ูี้ซ์ ริ ร้ร้ ไ้")) > 0.2);
        QVERIFY(dp::invalidThaiShare(QString::fromUtf8("เ ")) > 0.9);
        // the line filter: confident real text stays, unsure or ill-spelled or tiny lines go
        QVERIFY(dp::keepOcrLine(QString::fromUtf8("ภาษาไทย"), 0.97));
        QVERIFY(dp::keepOcrLine(QString::fromUtf8("ภาษาไทย"), 0.6));               // a searchable copy keeps unsure but well spelled lines
        QVERIFY(!dp::keepOcrLine(QString::fromUtf8("ภาษาไทย"), 0.4));
        QVERIFY(!dp::keepOcrLine(QStringLiteral("a"), 0.99));
        QVERIFY(!dp::keepOcrLine(QStringLiteral("  "), 0.99));
        QVERIFY(!dp::keepOcrLine(QString::fromUtf8("ูี้ซ์ ริ ร้ร้ ไ้"), 0.85));
        QVERIFY(dp::keepOcrLine(QString::fromUtf8("ูี้ซ์ ริ ร้ร้ ไ้"), 0.95));         // sure enough: the model says it is there
    }

    void measuresCharacterErrorRateOnGeneratedScans()
    {
        if (!dp::thaiOcrAvailable())
            QSKIP("Local ONNX Runtime and OCR models are absent");
        QTemporaryDir dir;
        const int fontId = QFontDatabase::addApplicationFont(DP_TEST_FONT_THAI);
        QVERIFY(fontId >= 0);
        const QFont base(QFontDatabase::applicationFontFamilies(fontId).first());
        const int count = int(sizeof(kVariants) / sizeof(kVariants[0]));
        const QString source = dir.filePath(QStringLiteral("scans.pdf"));
        QStringList references;
        {
            QPdfWriter writer(source);
            writer.setPageSize(QPageSize(QPageSize::A4));
            writer.setPageMargins({}, QPageLayout::Point);
            writer.setResolution(200);
            QPainter page(&writer);
            for (int i = 0; i < count; ++i) {
                if (i > 0)
                    QVERIFY(writer.newPage());
                references << drawPage(page, base, kVariants[i], i);
            }
            QVERIFY(page.end());
        }
        const auto input = dp::pdf::PdfDocument::open(source, {}, nullptr);
        QVERIFY(input);
        QCOMPARE(input->pageCount(), count);
        QList<int> pages;
        for (int i = 0; i < count; ++i)
            pages.append(i);
        const QString output = dir.filePath(QStringLiteral("searchable.pdf"));
        std::atomic_bool cancel = false;
        dp::ThaiOcrReport report;
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(dp::saveSearchableCopy(*input, pages, output, cancel, {}, &report), QString());
        const double seconds = double(timer.elapsed()) / 1000.0;
        const auto copy = dp::pdf::PdfDocument::open(output, {}, nullptr);
        QVERIFY(copy);
        int errors = 0, total = 0;
        for (int i = 0; i < count; ++i) {
            const QString want = compact(references.at(i)), got = compact(copy->pageText(i));
            const int distance = editDistance(want, got);
            errors += distance;
            total += want.size();
            fprintf(stderr, "OCR %-24s CER %.3f  (%d errors in %lld characters)\n", kVariants[i].name, double(distance) / double(want.size()), distance,
                    static_cast<long long>(want.size()));
        }
        const double cer = double(errors) / double(total);
        fprintf(stderr, "OCR TOTAL CER %.4f over %d pages, %.1f s (%.2f s a page), recognised %d, unrecognised %d\n", cer, count, seconds,
                seconds / count, report.recognizedPages, report.unrecognizedPages);
        QCOMPARE(report.recognizedPages, count);
        // regression floor: measured 0.0033 with OpenThai 2.0 OCR Text (0.0073 with the PaddleOCR models it replaced), 0.32 s a page (0.77)
        QVERIFY2(cer < 0.02, qPrintable(QString::number(cer)));
    }

    // The same text in the Thai fonts that come with Windows (printed only: a PC without them skips the font).
    void measuresCharacterErrorRateInWindowsThaiFonts()
    {
        if (!dp::thaiOcrAvailable())
            QSKIP("Local ONNX Runtime and OCR models are absent");
        // loaded from the files (the offscreen platform of the tests has no system font database)
        const QStringList files = {QStringLiteral("tahoma.ttf"), QStringLiteral("LeelawUI.ttf"), QStringLiteral("THSarabun.ttf"),
                                   QStringLiteral("angsana.ttc"), QStringLiteral("cordia.ttc"), QStringLiteral("browalia.ttc")};
        QList<QFont> fonts;
        for (const QString& file : files) {
            const QString path = QStringLiteral("C:/Windows/Fonts/") + file;
            if (!QFileInfo::exists(path))
                continue;
            const int id = QFontDatabase::addApplicationFont(path);
            const QStringList families = id >= 0 ? QFontDatabase::applicationFontFamilies(id) : QStringList();
            if (!families.isEmpty())
                fonts << QFont(families.first());
        }
        if (fonts.isEmpty())
            QSKIP("none of the Windows Thai fonts is installed");
        QTemporaryDir dir;
        const Variant sizes[] = {{"12pt", 34, QFont::Normal, 255, 6, 0.0}, {"9pt noisy", 25, QFont::Normal, 235, 16, 0.0}};
        const QString source = dir.filePath(QStringLiteral("fonts.pdf"));
        QStringList references, names;
        {
            QPdfWriter writer(source);
            writer.setPageSize(QPageSize(QPageSize::A4));
            writer.setPageMargins({}, QPageLayout::Point);
            writer.setResolution(200);
            QPainter page(&writer);
            int index = 0;
            for (const QFont& font : std::as_const(fonts)) {
                for (const Variant& v : sizes) {
                    if (index > 0)
                        QVERIFY(writer.newPage());
                    Variant adjusted = v;
                    if (font.family() == QLatin1String("TH Sarabun New") || font.family() == QLatin1String("Angsana New") ||
                        font.family() == QLatin1String("Cordia New") || font.family() == QLatin1String("Browallia New"))
                        adjusted.pixels = int(v.pixels * 1.3);        // these fonts are small for their nominal size
                    references << drawPage(page, font, adjusted, 20 + index);
                    names << QStringLiteral("%1 %2").arg(font.family(), v.name);
                    ++index;
                }
            }
            QVERIFY(page.end());
        }
        const auto input = dp::pdf::PdfDocument::open(source, {}, nullptr);
        QVERIFY(input);
        QList<int> pages;
        for (int i = 0; i < input->pageCount(); ++i)
            pages.append(i);
        const QString output = dir.filePath(QStringLiteral("searchable.pdf"));
        std::atomic_bool cancel = false;
        dp::ThaiOcrReport report;
        QCOMPARE(dp::saveSearchableCopy(*input, pages, output, cancel, {}, &report), QString());
        const auto copy = dp::pdf::PdfDocument::open(output, {}, nullptr);
        QVERIFY(copy);
        int errors = 0, total = 0;
        for (int i = 0; i < pages.size(); ++i) {
            const QString want = compact(references.at(i)), got = compact(copy->pageText(i));
            const int distance = editDistance(want, got);
            errors += distance;
            total += want.size();
            fprintf(stderr, "OCRF %-30s CER %.3f  (%d errors in %lld characters)\n", qPrintable(names.at(i)), double(distance) / double(want.size()), distance,
                    static_cast<long long>(want.size()));
        }
        fprintf(stderr, "OCRF TOTAL CER %.4f over %d pages\n", double(errors) / double(total), int(pages.size()));
        QVERIFY(report.recognizedPages > 0);
        // regression floor: measured 0.0097 (0.0062 with the PaddleOCR models; 9 pt noisy condensed fonts are the weak spot)
        QVERIFY2(double(errors) / double(total) < 0.03, qPrintable(QString::number(double(errors) / double(total))));
    }
};

QTEST_MAIN(TstOcr)
#include "tst_ocr.moc"
