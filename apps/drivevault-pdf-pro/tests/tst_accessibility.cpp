// Accessibility check and setting the title / language: tagged or not, figures without a description, tool tips, protection, a copy that keeps the rest.
#include "AccessibilityChecker.h"
#include "DocumentController.h"
#include "TestPdf.h"
#include "pdftools/Accessibility.h"
#include "pdfengine/PdfiumRuntime.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace dp::tools;

class TstAccessibility : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

    QString write(const QString& name, const QByteArray& data)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() ? path : QString();
    }
    static QString status(const AccessReport& r, const QString& id)
    {
        for (const AccessCheck& c : r.checks)
            if (c.id == id)
                return c.status;
        return QStringLiteral("missing");
    }
    static int count(const AccessReport& r, const QString& id)
    {
        for (const AccessCheck& c : r.checks)
            if (c.id == id)
                return c.count;
        return -1;
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }
    void cleanupTestCase() { dp::pdf::shutdownPdfium(); }

    void anUntaggedPlainFileFailsTheBasics()
    {
        const AccessReport r = checkAccessibility(write("plain.pdf", testpdf::make(2)), {});
        QVERIFY(r.ok);
        QCOMPARE(r.pages, 2);
        QCOMPARE(status(r, "tagged"), QStringLiteral("fail"));
        QCOMPARE(status(r, "figureAlt"), QStringLiteral("na"));
        QCOMPARE(status(r, "language"), QStringLiteral("fail"));
        QCOMPARE(status(r, "title"), QStringLiteral("fail"));
        QCOMPARE(status(r, "displayTitle"), QStringLiteral("warn"));
        QCOMPARE(status(r, "bookmarks"), QStringLiteral("na"));              // short document
        QCOMPARE(status(r, "fieldTips"), QStringLiteral("na"));
        QCOMPARE(status(r, "security"), QStringLiteral("na"));
        QCOMPARE(status(r, "pdfa"), QStringLiteral("na"));
    }

    void taggedFilesAreCheckedForFigureDescriptions()
    {
        const QByteArray extra = "/MarkInfo << /Marked true >> /Lang (th-TH) /StructTreeRoot << /Type /StructTreeRoot /K [ "
                                 "<< /Type /StructElem /S /Document /K [ << /Type /StructElem /S /Figure /Alt (A logo) >> << /Type /StructElem /S /Figure >> "
                                 "<< /Type /StructElem /S /Figure /ActualText (x) >> << /Type /StructElem /S /P >> ] >> ] >>";
        const AccessReport r = checkAccessibility(write("tagged.pdf", testpdf::makeFeatureDoc(extra)), {});
        QVERIFY(r.ok);
        QCOMPARE(status(r, "tagged"), QStringLiteral("pass"));
        QCOMPARE(status(r, "figureAlt"), QStringLiteral("fail"));
        QCOMPARE(count(r, "figureAlt"), 1);                                  // only the second figure has neither /Alt nor /ActualText
        QCOMPARE(status(r, "language"), QStringLiteral("pass"));

        // tagged but the structure has no figure: nothing to judge
        const AccessReport none = checkAccessibility(write("tagged2.pdf", testpdf::makeFeatureDoc("/MarkInfo << /Marked true >> /StructTreeRoot << /Type /StructTreeRoot /K [] >>")), {});
        QCOMPARE(status(none, "tagged"), QStringLiteral("pass"));
        QCOMPARE(status(none, "figureAlt"), QStringLiteral("na"));
        // /Marked false is not tagged even with a structure tree
        const AccessReport off = checkAccessibility(write("tagged3.pdf", testpdf::makeFeatureDoc("/MarkInfo << /Marked false >> /StructTreeRoot << /Type /StructTreeRoot >>")), {});
        QCOMPARE(status(off, "tagged"), QStringLiteral("fail"));
    }

    void longFilesWantBookmarksAndFormFieldsWantToolTips()
    {
        const AccessReport longFile = checkAccessibility(write("long.pdf", testpdf::make(12)), {});
        QCOMPARE(status(longFile, "bookmarks"), QStringLiteral("warn"));
        const AccessReport form = checkAccessibility(write("form.pdf", testpdf::makeFormDoc()), {});
        QVERIFY(form.ok);
        QCOMPARE(status(form, "fieldTips"), QStringLiteral("fail"));
        QVERIFY(count(form, "fieldTips") >= 4);
    }

    void errorsAreCoded()
    {
        QCOMPARE(checkAccessibility(m_dir.filePath("missing.pdf"), {}).error, QStringLiteral("file"));
        QCOMPARE(checkAccessibility(write("junk.pdf", "nope"), {}).error, QStringLiteral("format"));
    }

    void titleAndLanguageGoIntoACopyAndNothingElseChanges()
    {
        const QString in = write("src.pdf", testpdf::make(3));
        const QString out = m_dir.filePath("fixed.pdf");
        QCOMPARE(setTitleAndLanguage(in, {}, out, {}, {}, false).error, QStringLiteral("nothing"));
        QCOMPARE(setTitleAndLanguage(m_dir.filePath("none.pdf"), {}, out, "T", {}, false).error, QStringLiteral("file"));
        const AccessFix fix = setTitleAndLanguage(in, {}, out, QStringLiteral("Annual report รายงาน"), QStringLiteral("th-TH"), false);
        QVERIFY2(fix.ok, qPrintable(fix.error + fix.detail));
        const AccessReport r = checkAccessibility(out, {});
        QVERIFY(r.ok);
        QCOMPARE(r.pages, 3);
        QCOMPARE(status(r, "title"), QStringLiteral("pass"));
        QCOMPARE(status(r, "displayTitle"), QStringLiteral("pass"));
        QCOMPARE(status(r, "language"), QStringLiteral("pass"));
        for (const AccessCheck& c : r.checks) {
            if (c.id == "title")
                QCOMPARE(c.detail, QStringLiteral("Annual report รายงาน"));
            if (c.id == "language")
                QCOMPARE(c.detail, QStringLiteral("th-TH"));
        }
        // the source is untouched, an existing target is refused unless asked
        QCOMPARE(status(checkAccessibility(in, {}), "title"), QStringLiteral("fail"));
        QCOMPARE(setTitleAndLanguage(in, {}, out, "Other", {}, false).error, QStringLiteral("exists"));
        QVERIFY(setTitleAndLanguage(in, {}, out, "Other", {}, true).ok);
        // language only leaves the title alone
        const QString langOnly = m_dir.filePath("lang.pdf");
        QVERIFY(setTitleAndLanguage(in, {}, langOnly, {}, "en-US", false).ok);
        const AccessReport l = checkAccessibility(langOnly, {});
        QCOMPARE(status(l, "language"), QStringLiteral("pass"));
        QCOMPARE(status(l, "title"), QStringLiteral("fail"));
    }

    void aProtectionThatBlocksScreenReadersIsReported()
    {
        const QFileInfo q(QStringLiteral(DP_TEST_QPDF));
        if (!q.exists())
            QSKIP("qpdf.exe is not in .tools\\qpdf");
        const QString plain = write("enc-src.pdf", testpdf::make(1));
        // only the old RC4 formats can forbid screen readers (qpdf ignores the option for AES), so the test file uses RC4
        const QString blocked = m_dir.filePath("enc-blocked.pdf");
        QProcess p;
        p.start(q.absoluteFilePath(), {"--allow-weak-crypto", "--encrypt", "user", "owner", "128", "--accessibility=n", "--", plain, blocked});
        QVERIFY(p.waitForFinished(30000) && p.exitCode() == 0);
        const AccessReport r = checkAccessibility(blocked, "user");
        QVERIFY(r.ok);
        QCOMPARE(status(r, "security"), QStringLiteral("fail"));
        QCOMPARE(checkAccessibility(blocked, {}).error, QStringLiteral("password"));
        const QString allowed = m_dir.filePath("enc-ok.pdf");
        p.start(q.absoluteFilePath(), {"--encrypt", "user", "owner", "256", "--", plain, allowed});
        QVERIFY(p.waitForFinished(30000) && p.exitCode() == 0);
        QCOMPARE(status(checkAccessibility(allowed, "user"), "security"), QStringLiteral("pass"));
        // a copy of a protected file keeps its protection
        const AccessFix fix = setTitleAndLanguage(allowed, "user", m_dir.filePath("enc-fixed.pdf"), "T", {}, false);
        QVERIFY(fix.ok);
        QVERIFY(fix.warnings.contains(QStringLiteral("encrypted")));
        QCOMPARE(checkAccessibility(m_dir.filePath("enc-fixed.pdf"), {}).error, QStringLiteral("password"));
    }

    void theBackendChecksTheOpenFileAndSavesACopy()
    {
        dp::DocumentController c(m_dir.filePath("data"));
        dp::AccessibilityChecker a(&c);
        QCOMPARE(a.check(), QStringLiteral("noDocument"));
        QCOMPARE(c.open(write("open.pdf", testpdf::make(2))), QString());
        QCOMPARE(a.check(), QString());
        QVERIFY(QTest::qWaitFor([&] { return a.loaded(); }, 10000));
        QVERIFY(!a.checks().isEmpty());
        QCOMPARE(a.saveInfo({}, {}, m_dir.filePath("b-out.pdf")), QStringLiteral("nothing"));
        QCOMPARE(a.saveInfo("T", {}, c.filePath()), QStringLiteral("sameFile"));
        QSignalSpy done(&a, &dp::AccessibilityChecker::saveFinished);
        QCOMPARE(a.saveInfo("My title", "en-US", m_dir.filePath("b-out.pdf")), QString());
        QVERIFY(QTest::qWaitFor([&] { return done.count() > 0; }, 10000));
        QCOMPARE(done.first().at(0).toBool(), true);
        QCOMPARE(status(checkAccessibility(m_dir.filePath("b-out.pdf"), {}), "title"), QStringLiteral("pass"));
        QCOMPARE(a.saveInfo("T", {}, m_dir.filePath("b-out.pdf")), QStringLiteral("exists"));
    }
};

QTEST_MAIN(TstAccessibility)
#include "tst_accessibility.moc"
