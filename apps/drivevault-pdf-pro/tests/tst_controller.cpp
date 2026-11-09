// DocumentController: opening files, error codes, the recent files list and the language setting.
#include "DocumentController.h"
#include "TestPdf.h"
#include "pdfengine/PdfiumRuntime.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

using dp::DocumentController;

class TstController : public QObject {
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

    void openingAFileGivesPagesNameAndANewDocumentId()
    {
        DocumentController c(m_dir.filePath("data1"));
        QVERIFY(!c.isOpen());
        QCOMPARE(c.pageCount(), 0);
        QSignalSpy changed(&c, &DocumentController::documentChanged);
        const QString path = write("a.pdf", testpdf::make(4));
        QCOMPARE(c.open(path), QString());
        QVERIFY(c.isOpen());
        QCOMPARE(c.pageCount(), 4);
        QCOMPARE(c.fileName(), QStringLiteral("a.pdf"));
        QCOMPARE(c.pageWidthPt(0), 612.0);
        QCOMPARE(c.pageHeightPt(0), 792.0);
        QCOMPARE(changed.count(), 1);
        const int firstId = c.docId();
        QVERIFY(c.documentFor(firstId));
        QVERIFY(!c.documentFor(firstId + 7));

        QCOMPARE(c.open(write("b.pdf", testpdf::make(2))), QString());
        QVERIFY(c.docId() != firstId);
        QVERIFY(!c.documentFor(firstId));          // an image request of the old document is refused
        QCOMPARE(c.pageCount(), 2);

        c.close();
        QVERIFY(!c.isOpen());
        QCOMPARE(c.pageCount(), 0);
        QCOMPARE(c.pageWidthPt(0), 0.0);
    }

    void aFileUrlIsAccepted()
    {
        DocumentController c(m_dir.filePath("data2"));
        const QString path = write("url.pdf", testpdf::make(1));
        QCOMPARE(c.open(QUrl::fromLocalFile(path).toString()), QString());
        QCOMPARE(c.fileName(), QStringLiteral("url.pdf"));
    }

    void failuresGiveACodeAndKeepTheOpenDocument()
    {
        DocumentController c(m_dir.filePath("data3"));
        QCOMPARE(c.open(write("ok.pdf", testpdf::make(3))), QString());
        QCOMPARE(c.open(write("bad.pdf", "not a pdf")), QStringLiteral("format"));
        QCOMPARE(c.open(m_dir.filePath("none.pdf")), QStringLiteral("file"));
        QVERIFY(c.isOpen());                        // the file that was open stays open
        QCOMPARE(c.fileName(), QStringLiteral("ok.pdf"));
        QVERIFY(!c.recentFiles().contains(QDir::toNativeSeparators(m_dir.filePath("bad.pdf"))));
    }

    void recentFilesAreNewestFirstUniqueLimitedAndPersisted()
    {
        const QString data = m_dir.filePath("data4");
        QStringList paths;
        {
            DocumentController c(data);
            for (int i = 0; i < 12; ++i) {
                const QString p = write(QString("r%1.pdf").arg(i), testpdf::make(1));
                paths.append(QDir::toNativeSeparators(p));
                QCOMPARE(c.open(p), QString());
            }
            QCOMPARE(c.recentFiles().size(), 10);
            QCOMPARE(c.recentFiles().first(), paths.last());
            QCOMPARE(c.open(paths.at(5)), QString());          // opening again moves it to the top, no duplicate
            QCOMPARE(c.recentFiles().first(), paths.at(5));
            QCOMPARE(c.recentFiles().count(paths.at(5)), 1);
            c.removeRecent(paths.at(11));
            QVERIFY(!c.recentFiles().contains(paths.at(11)));
        }
        DocumentController again(data);
        QCOMPARE(again.recentFiles().size(), 9);
        QCOMPARE(again.recentFiles().first(), paths.at(5));
    }

    void theLanguageIsPersistedAndOnlyThaiOrEnglishIsAccepted()
    {
        const QString data = m_dir.filePath("data5");
        {
            DocumentController c(data);
            QVERIFY(c.language() == "th" || c.language() == "en");
            QSignalSpy spy(&c, &DocumentController::languageChanged);
            c.setLanguage("th");
            c.setLanguage("fr");                              // rejected
            QCOMPARE(c.language(), QStringLiteral("th"));
            c.setLanguage("en");
            QCOMPARE(c.language(), QStringLiteral("en"));
            QVERIFY(spy.count() >= 1);
        }
        QCOMPARE(DocumentController(data).language(), QStringLiteral("en"));
    }
};

QTEST_MAIN(TstController)
#include "tst_controller.moc"
