// Digital signatures: a copy signed with a certificate file, the checks of the signature (intact, changed afterwards, trust), refused cases.
#include "DocumentController.h"
#include "SignatureManager.h"
#include "TestPdf.h"
#include "pdftools/Signature.h"
#include "pdfengine/PdfDocument.h"
#include "pdfengine/PdfiumRuntime.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace dp::tools;

class TstSignature : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    QString m_pfx;

    QString write(const QString& name, const QByteArray& data)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() ? path : QString();
    }
    // a self-signed certificate made in memory by the .NET classes of Windows PowerShell (nothing is put in the certificate store)
    bool makePfx(const QString& path, const QString& password)
    {
        const QString script = QStringLiteral(
            "$rsa=[System.Security.Cryptography.RSA]::Create(2048);"
            "$req=New-Object System.Security.Cryptography.X509Certificates.CertificateRequest('CN=DriveVault Test Signer',$rsa,[System.Security.Cryptography.HashAlgorithmName]::SHA256,[System.Security.Cryptography.RSASignaturePadding]::Pkcs1);"
            "$cert=$req.CreateSelfSigned([DateTimeOffset]::UtcNow.AddDays(-1),[DateTimeOffset]::UtcNow.AddDays(2));"
            "[IO.File]::WriteAllBytes('%1',$cert.Export([System.Security.Cryptography.X509Certificates.X509ContentType]::Pfx,'%2'))").arg(QDir::toNativeSeparators(path), password);
        QProcess p;
        p.start(QStringLiteral("powershell.exe"), {"-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", script});
        return p.waitForFinished(60000) && p.exitCode() == 0 && QFileInfo::exists(path);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_pfx = m_dir.filePath("signer.pfx");
        if (!makePfx(m_pfx, "pw"))
            m_pfx.clear();
    }
    void cleanupTestCase() { dp::pdf::shutdownPdfium(); }

    void aSignedCopyIsIntactAndShowsTheSigner()
    {
        if (m_pfx.isEmpty())
            QSKIP("could not make a test certificate (PowerShell / .NET)");
        const QString in = write("in.pdf", testpdf::make(2));
        const QString out = m_dir.filePath("signed.pdf");
        SignOptions o;
        o.pfxPath = m_pfx;
        o.pfxPassword = "pw";
        o.reason = "I approve";
        o.location = "Hat Yai";
        const SignResult r = signPdf(in, out, o, false);
        QVERIFY2(r.ok, qPrintable(r.error + " " + r.detail));
        QCOMPARE(r.signer, QStringLiteral("DriveVault Test Signer"));
        QVERIFY(!QFileInfo::exists(out + ".part.tmp"));
        // it is still a normal PDF with the same pages and text
        auto doc = dp::pdf::PdfDocument::open(out, {}, nullptr);
        QVERIFY(doc);
        QCOMPARE(doc->pageCount(), 2);
        QVERIFY(doc->pageText(1).contains("Page 2 of 2"));

        QString err;
        const QList<SignatureInfo> sigs = verifySignatures(out, {}, &err);
        QCOMPARE(err, QString());
        QCOMPARE(sigs.size(), 1);
        QCOMPARE(sigs[0].signer, QStringLiteral("DriveVault Test Signer"));
        QCOMPARE(sigs[0].fieldName, QStringLiteral("Signature1"));
        QCOMPARE(sigs[0].reason, QStringLiteral("I approve"));
        QCOMPARE(sigs[0].location, QStringLiteral("Hat Yai"));
        QVERIFY2(sigs[0].intact, qPrintable(sigs[0].error));
        QVERIFY(sigs[0].coversWholeFile);
        QCOMPARE(sigs[0].trust, QStringLiteral("untrusted"));              // self-signed: Windows does not trust it, and the program says so

        // the same checks on the source: no signatures
        QVERIFY(verifySignatures(in, {}).isEmpty());
    }

    void changingTheFileAfterSigningIsDetected()
    {
        if (m_pfx.isEmpty())
            QSKIP("could not make a test certificate");
        const QString in = write("in2.pdf", testpdf::make(1));
        const QString out = m_dir.filePath("signed2.pdf");
        SignOptions o;
        o.pfxPath = m_pfx;
        o.pfxPassword = "pw";
        QVERIFY(signPdf(in, out, o, false).ok);
        QFile f(out);
        QVERIFY(f.open(QIODevice::ReadWrite));
        QByteArray bytes = f.readAll();
        // 1. something appended after the signature: still intact, but not the whole file any more
        f.seek(bytes.size());
        f.write("\n% appended after signing\n");
        f.close();
        QList<SignatureInfo> s = verifySignatures(out, {});
        QCOMPARE(s.size(), 1);
        QVERIFY(s[0].intact);
        QVERIFY(!s[0].coversWholeFile);
        // 2. a signed byte changed: the signature no longer matches
        const int at = 0;                                                  // the header "%PDF-1.x": the digit after "-1." is signed too
        QCOMPARE(QByteArray(bytes.mid(0, 5)), QByteArray("%PDF-"));
        QFile g(out);
        QVERIFY(g.open(QIODevice::ReadWrite));
        g.seek(at + 7);
        g.write(bytes.mid(7, 1) == "3" ? "4" : "3");
        g.close();
        s = verifySignatures(out, {});
        QCOMPARE(s.size(), 1);
        QVERIFY(!s[0].intact);
        QCOMPARE(s[0].error, QStringLiteral("badSignature"));
    }

    void theWindowObjectSignsACopyAndListsTheSignature()
    {
        if (m_pfx.isEmpty())
            QSKIP("could not make a test certificate");
        dp::DocumentController c(m_dir.filePath("mgrdata"));
        dp::SignatureManager m(&c);
        QCOMPARE(m.check(), QStringLiteral("noDocument"));
        const QString in = write("mgr.pdf", testpdf::make(1));
        QCOMPARE(c.open(in), QString());
        QSignalSpy changed(&m, &dp::SignatureManager::changed);
        QCOMPARE(m.check(), QString());
        QVERIFY(QTest::qWaitFor([&] { return m.loaded(); }, 20000));
        QCOMPARE(m.signatures().size(), 0);
        QCOMPARE(m.sign({{"pfx", m_pfx}}, in, true), QStringLiteral("sameFile"));
        QCOMPARE(m.sign({{"pfx", m_dir.filePath("none.pfx")}}, m_dir.filePath("o.pdf")), QStringLiteral("noCertificate"));
        QSignalSpy done(&m, &dp::SignatureManager::signFinished);
        const QString out = m_dir.filePath("mgr-signed.pdf");
        QCOMPARE(m.sign({{"pfx", m_pfx}, {"pfxPassword", "pw"}, {"reason", "Test"}}, out), QString());
        QVERIFY(m.busy());
        QVERIFY(QTest::qWaitFor([&] { return done.count() > 0; }, 30000));
        QVERIFY2(done.first().at(0).toBool(), qPrintable(done.first().at(1).toString()));
        QCOMPARE(m.sign({{"pfx", m_pfx}, {"pfxPassword", "pw"}}, out), QStringLiteral("exists"));
        // open the signed copy: its signature is listed
        QCOMPARE(c.open(out), QString());
        QCOMPARE(m.check(), QString());
        QVERIFY(QTest::qWaitFor([&] { return m.loaded(); }, 20000));
        QCOMPARE(m.signatures().size(), 1);
        QVERIFY(m.signatures().first().toMap().value("intact").toBool());
        QCOMPARE(m.signatures().first().toMap().value("reason").toString(), QStringLiteral("Test"));
    }

    void refusedCasesAreClear()
    {
        if (m_pfx.isEmpty())
            QSKIP("could not make a test certificate");
        const QString in = write("in3.pdf", testpdf::make(1));
        SignOptions o;
        o.pfxPath = m_pfx;
        o.pfxPassword = "wrong";
        QCOMPARE(signPdf(in, m_dir.filePath("x.pdf"), o, false).error, QStringLiteral("wrongPfxPassword"));
        o.pfxPassword = "pw";
        o.pfxPath = write("notapfx.pfx", "this is not a certificate");
        QCOMPARE(signPdf(in, m_dir.filePath("x.pdf"), o, false).error, QStringLiteral("badPfx"));
        o.pfxPath = m_pfx;
        QCOMPARE(signPdf(write("junk.pdf", "nope"), m_dir.filePath("x.pdf"), o, false).error, QStringLiteral("format"));
        QVERIFY(!QFileInfo::exists(m_dir.filePath("x.pdf")));
        const QString out = m_dir.filePath("s3.pdf");
        QVERIFY(signPdf(in, out, o, false).ok);
        QCOMPARE(signPdf(in, out, o, false).error, QStringLiteral("exists"));
        // signing a signed file would break the first signature: refused
        QCOMPARE(signPdf(out, m_dir.filePath("twice.pdf"), o, false).error, QStringLiteral("alreadySigned"));
        QVERIFY(!QFileInfo::exists(m_dir.filePath("twice.pdf")));
        // a protected file is refused (the signature would have to be written inside the encryption)
        const QFileInfo qpdf(QStringLiteral(DP_TEST_QPDF));
        if (qpdf.exists()) {
            const QString locked = m_dir.filePath("locked.pdf");
            QProcess p;
            p.start(qpdf.absoluteFilePath(), {"--encrypt", "open-secret", "owner-secret", "256", "--", in, locked});
            QVERIFY(p.waitForFinished(30000));
            QCOMPARE(signPdf(locked, m_dir.filePath("y.pdf"), o, false).error, QStringLiteral("password"));
        }
    }
};

QTEST_MAIN(TstSignature)
#include "tst_signature.moc"
