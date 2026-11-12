// PDF portfolio (files of any type embedded in one PDF) and the extra batch modes (page numbers, title and language).
#include "BatchPdf.h"
#include "TestPdf.h"
#include "pdfengine/PdfDocument.h"
#include "pdfengine/PdfiumRuntime.h"
#include "pdftools/Accessibility.h"
#include "pdftools/Portfolio.h"

#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>

using namespace dp::tools;

class TstPortfolio : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    std::atomic_bool m_never{false};

    QString write(const QString& relative, const QByteArray& data)
    {
        const QString path = m_dir.filePath(relative);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() ? path : QString();
    }
    QByteArray qpdfOut(const QStringList& args, int* code = nullptr)
    {
        const QFileInfo q(QStringLiteral(DP_TEST_QPDF));
        QProcess p;
        p.start(q.absoluteFilePath(), args);
        p.waitForFinished(30000);
        if (code)
            *code = p.exitCode();
        return p.readAllStandardOutput();
    }
    static QString status(const AccessReport& r, const QString& id)
    {
        for (const AccessCheck& c : r.checks)
            if (c.id == id)
                return c.status;
        return QStringLiteral("missing");
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(QFontDatabase::addApplicationFont(QStringLiteral(DP_TEST_FONT_THAI)) >= 0);
    }
    void cleanupTestCase() { dp::pdf::shutdownPdfium(); }

    void filesOfAnyTypeAreEmbeddedWithTheirContent()
    {
        if (!QFileInfo(QStringLiteral(DP_TEST_QPDF)).exists())
            QSKIP("qpdf.exe is not in .tools\\qpdf");
        QByteArray binary;
        for (int i = 0; i < 5000; ++i)
            binary.append(char(i * 7));
        const QString text = write("in/notes.txt", "Hello \xE0\xB8\xAA\xE0\xB8\xA7\xE0\xB8\xB1\xE0\xB8\xAA\xE0\xB8\x94\xE0\xB8\xB5\n");
        const QString bin = write("in/data.bin", binary);
        const QString same = write("other/notes.txt", "second file with the same name");
        const QString pdf = write("in/doc.pdf", testpdf::make(2));
        const QString out = m_dir.filePath("portfolio.pdf");
        QList<int> steps;
        const QString error = createPortfolio(out, QStringLiteral("My files ไฟล์"), {text, bin, same, pdf}, false, m_never,
                                              [&](int done, int total) { QCOMPARE(total, 4); steps << done; });
        QCOMPARE(error, QString());
        QCOMPARE(steps, (QList<int>{1, 2, 3, 4}));
        int code = -1;
        qpdfOut({"--check", out}, &code);
        QVERIFY2(code == 0, "qpdf --check failed");

        const QString listing = QString::fromUtf8(qpdfOut({"--list-attachments", out}));
        QVERIFY(listing.contains("notes.txt"));
        QVERIFY(listing.contains("notes (2).txt"));                            // a second file with the same name is renamed
        QVERIFY(listing.contains("data.bin"));
        QVERIFY(listing.contains("doc.pdf"));
        QCOMPARE(qpdfOut({"--show-attachment=data.bin", out}), binary);          // the bytes are unchanged
        QCOMPARE(qpdfOut({"--show-attachment=notes (2).txt", out}), QByteArray("second file with the same name"));

        // it asks readers to show a list of files, and the cover page is a real page that names them
        const QString plain = m_dir.filePath("portfolio-qdf.pdf");
        qpdfOut({"--qdf", "--object-streams=disable", out, plain});
        QFile f(plain);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();
        QVERIFY(bytes.contains("/Collection"));
        auto doc = dp::pdf::PdfDocument::open(out, {}, nullptr);
        QVERIFY(doc);
        QCOMPARE(doc->pageCount(), 1);
        const QString cover = doc->pageText(0);
        QVERIFY(cover.contains("data.bin") && cover.contains("My files"));
        QVERIFY2(cover.contains(QStringLiteral("\u0e44\u0e1f\u0e25")), qPrintable(cover));                  // the Thai part of the title (before its tone mark)
        QCOMPARE(checkAccessibility(out, {}).checks.isEmpty(), false);
        QCOMPARE(status(checkAccessibility(out, {}), "title"), QStringLiteral("pass"));
    }

    void refusedCasesLeaveNothingBehind()
    {
        const QString file = write("r/a.txt", "x");
        const QString out = m_dir.filePath("r-out.pdf");
        QCOMPARE(createPortfolio(out, "T", {}, false, m_never), QStringLiteral("noFiles"));
        QCOMPARE(createPortfolio(m_dir.filePath("r-out.txt"), "T", {file}, false, m_never), QStringLiteral("pdfSuffix"));
        QCOMPARE(createPortfolio(out, "T", {m_dir.filePath("r/missing.bin")}, false, m_never), QStringLiteral("fileRead:missing.bin"));
        write("r/empty.bin", "");
        QCOMPARE(createPortfolio(out, "T", {m_dir.filePath("r/empty.bin")}, false, m_never), QStringLiteral("fileRead:empty.bin"));
        QVERIFY(!QFileInfo::exists(out));
        QCOMPARE(createPortfolio(out, "T", {file}, false, m_never), QString());
        QCOMPARE(createPortfolio(out, "T", {file}, false, m_never), QStringLiteral("exists"));       // never over an existing file unless asked
        QCOMPARE(createPortfolio(out, "T", {file}, true, m_never), QString());
        std::atomic_bool stop{true};
        const QString cancelled = m_dir.filePath("r-cancel.pdf");
        QCOMPARE(createPortfolio(cancelled, "T", {file}, false, stop), QStringLiteral("cancelled"));
        QVERIFY(!QFileInfo::exists(cancelled));
        QVERIFY(!QFileInfo::exists(cancelled + ".part.tmp"));
        QVERIFY(QFileInfo::exists(file));                                                          // the source is only read
    }

    void batchCanNumberThePagesAndSetTitleAndLanguage()
    {
        write("batch/one.pdf", testpdf::make(3));
        write("batch/two.pdf", testpdf::make(2));
        QDir().mkpath(m_dir.filePath("batch-numbered"));
        QString error;
        QVariantList rows = dp::batchPdf(m_dir.filePath("batch"), m_dir.filePath("batch-numbered"), {{"mode", "pageNumbers"}}, {}, error);
        QCOMPARE(error, QString());
        QCOMPARE(rows.size(), 2);
        for (const QVariant& r : rows)
            QCOMPARE(r.toMap().value("status").toString(), QString());
        auto doc = dp::pdf::PdfDocument::open(m_dir.filePath("batch-numbered/one.pdf"), {}, nullptr);
        QVERIFY(doc);
        QCOMPARE(doc->pageCount(), 3);
        QVERIFY(doc->pageText(0).contains("1 / 3"));
        QVERIFY(doc->pageText(2).contains("3 / 3"));
        QVERIFY(doc->pageText(0).contains("Page 1 of 3"));                                         // the old text stays

        QDir().mkpath(m_dir.filePath("batch-titled"));
        rows = dp::batchPdf(m_dir.filePath("batch"), m_dir.filePath("batch-titled"), {{"mode", "titleLanguage"}, {"language", "th-TH"}}, {}, error);
        QCOMPARE(error, QString());
        QCOMPARE(rows.size(), 2);
        for (const QVariant& r : rows)
            QCOMPARE(r.toMap().value("status").toString(), QString());
        const AccessReport report = checkAccessibility(m_dir.filePath("batch-titled/two.pdf"), {});
        QCOMPARE(status(report, "title"), QStringLiteral("pass"));
        QCOMPARE(status(report, "language"), QStringLiteral("pass"));
        for (const AccessCheck& c : report.checks)
            if (c.id == "title")
                QCOMPARE(c.detail, QStringLiteral("two"));                                         // the title is the file name

        // a bad language tag is refused before any file is touched
        QDir().mkpath(m_dir.filePath("batch-bad"));
        dp::batchPdf(m_dir.filePath("batch"), m_dir.filePath("batch-bad"), {{"mode", "titleLanguage"}, {"language", "not a tag!"}}, {}, error);
        QCOMPARE(error, QStringLiteral("maintenanceOptions"));
        QVERIFY(QDir(m_dir.filePath("batch-bad")).entryList({"*.pdf"}).isEmpty());
    }
};

QTEST_MAIN(TstPortfolio)
#include "tst_portfolio.moc"
