// Fill-in forms (AcroForm): reading the fields and writing a filled copy that every viewer shows (measured on rendered pixels and by reading the file back).
#include "TestPdf.h"
#include "pdfengine/PdfDocument.h"
#include "pdfengine/PdfiumRuntime.h"
#include "pdftools/Forms.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
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
QByteArray stream(const QByteArray& dict, const QByteArray& data)
{
    return "<< " + dict + " /Length " + QByteArray::number(data.size()) + " >>\nstream\n" + data + "\nendstream";
}

// A page with: text field "name", read-only text "locked", checkbox "agree", combo "country", radio group "size" (S / L).
QByteArray formPdf()
{
    const QByteArray da = "/DA (/Helv 12 Tf 0 g) ";
    return buildPdf({
        "<< /Type /Catalog /Pages 2 0 R /AcroForm 6 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 4 0 R >> >> /Contents 5 0 R /Annots [7 0 R 8 0 R 9 0 R 10 0 R 14 0 R 15 0 R] >>",
        "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>",
        stream("", "BT /F1 18 Tf 72 740 Td (Application form) Tj ET"),
        "<< /Fields [7 0 R 8 0 R 9 0 R 10 0 R 11 0 R] /DR << /Font << /Helv 4 0 R >> >> /DA (/Helv 12 Tf 0 g) >>",
        "<< /Type /Annot /Subtype /Widget /FT /Tx /T (name) /Rect [72 650 300 680] " + da + "/F 4 /P 3 0 R >>",
        "<< /Type /Annot /Subtype /Widget /FT /Tx /Ff 1 /T (locked) /V (fixed) /Rect [72 600 300 630] " + da + "/F 4 /P 3 0 R >>",
        "<< /Type /Annot /Subtype /Widget /FT /Btn /T (agree) /V /Off /AS /Off /Rect [72 550 92 570] /F 4 /P 3 0 R /AP << /N << /Yes 12 0 R /Off 13 0 R >> >> >>",
        "<< /Type /Annot /Subtype /Widget /FT /Ch /Ff 131072 /T (country) /Opt [(Thailand) (Japan) (France)] /Rect [72 500 300 530] " + da + "/F 4 /P 3 0 R >>",
        "<< /FT /Btn /Ff 32768 /T (size) /V /Off /Kids [14 0 R 15 0 R] >>",
        stream("/Type /XObject /Subtype /Form /BBox [0 0 20 20]", "0 g 3 3 14 14 re f"),
        stream("/Type /XObject /Subtype /Form /BBox [0 0 20 20]", ""),
        "<< /Type /Annot /Subtype /Widget /Parent 11 0 R /AS /Off /Rect [72 450 92 470] /F 4 /P 3 0 R /AP << /N << /S 12 0 R /Off 13 0 R >> >> >>",
        "<< /Type /Annot /Subtype /Widget /Parent 11 0 R /AS /Off /Rect [120 450 140 470] /F 4 /P 3 0 R /AP << /N << /L 12 0 R /Off 13 0 R >> >> >>",
    });
}

int darkIn(const QImage& img, QRect r)
{
    int n = 0;
    for (int y = r.top(); y < r.bottom(); ++y)
        for (int x = r.left(); x < r.right(); ++x)
            if (QColor(img.pixel(x, y)).lightness() < 110)
                ++n;
    return n;
}

} // namespace

class TstForms : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

    QString write(const QString& name, const QByteArray& data)
    {
        const QString path = m_dir.filePath(name);
        QFile f(path);
        return f.open(QIODevice::WriteOnly) && f.write(data) == data.size() ? path : QString();
    }
    bool qpdf(const QStringList& args)
    {
        const QFileInfo q(QStringLiteral(DP_TEST_QPDF));
        if (!q.exists())
            return true;
        QProcess p;
        p.start(q.absoluteFilePath(), args);
        return p.waitForFinished(30000) && p.exitCode() == 0;
    }
    static const FormField* field(const FormRead& r, const QString& name)
    {
        for (const FormField& f : r.fields)
            if (f.name == name)
                return &f;
        return nullptr;
    }

private slots:
    void initTestCase() { QVERIFY(m_dir.isValid()); }
    void cleanupTestCase() { dp::pdf::shutdownPdfium(); }

    void theFieldsAreListedWithTypesValuesAndOptions()
    {
        const FormRead r = readFormFields(write("f.pdf", formPdf()), {});
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(r.hasForm);
        QCOMPARE(r.fields.size(), 5);
        QCOMPARE(field(r, "name")->type, QStringLiteral("text"));
        QCOMPARE(field(r, "name")->page, 0);
        QVERIFY(field(r, "locked")->readOnly);
        QCOMPARE(field(r, "locked")->value, QStringLiteral("fixed"));
        QCOMPARE(field(r, "agree")->type, QStringLiteral("checkbox"));
        QVERIFY(!field(r, "agree")->checked);
        QCOMPARE(field(r, "country")->type, QStringLiteral("choice"));
        QCOMPARE(field(r, "country")->options, (QStringList{"Thailand", "Japan", "France"}));
        QCOMPARE(field(r, "size")->type, QStringLiteral("radio"));
        QCOMPARE(field(r, "size")->options, (QStringList{"S", "L"}));
        QCOMPARE(field(r, "size")->page, 0);

        const FormRead none = readFormFields(write("p.pdf", testpdf::make(1)), {});
        QVERIFY(none.ok && !none.hasForm && none.fields.isEmpty());
        QCOMPARE(readFormFields(write("junk.pdf", "nope"), {}).error, QStringLiteral("format"));
        QCOMPARE(readFormFields(m_dir.filePath("missing.pdf"), {}).error, QStringLiteral("file"));
    }

    void aFilledCopyShowsTheValuesInAnyViewer()
    {
        const QString in = write("g.pdf", formPdf());
        const QString out = m_dir.filePath("g-out.pdf");
        auto before = PdfDocument::open(in, {}, nullptr);
        const QImage b = before->render(0, 1.0, nullptr, 0);
        QCOMPARE(darkIn(b, QRect(76, 112, 220, 26)), 0);                      // the empty name field (y from the top: 792 - 680 = 112)

        QMap<QString, QVariant> values{{"name", "Somchai Jaidee"}, {"agree", true}, {"country", "Japan"}, {"size", "L"},
                                       {"locked", "changed"}, {"nobody", "x"}};
        const FormFill r = fillForm(in, {}, out, values, false);
        QVERIFY2(r.ok, qPrintable(r.error + r.detail));
        QCOMPARE(r.filled, 4);
        QVERIFY(r.skipped.contains("locked: readonly"));
        QVERIFY(r.skipped.contains("nobody: unknown"));
        QVERIFY(qpdf({"--check", out}));
        QVERIFY(!QFileInfo::exists(out + ".part.tmp"));

        // read back
        const FormRead back = readFormFields(out, {});
        QCOMPARE(field(back, "name")->value, QStringLiteral("Somchai Jaidee"));
        QVERIFY(field(back, "agree")->checked);
        QCOMPARE(field(back, "country")->value, QStringLiteral("Japan"));
        QCOMPARE(field(back, "size")->value, QStringLiteral("L"));
        QCOMPARE(field(back, "locked")->value, QStringLiteral("fixed"));      // the read-only field is as it was

        // drawn: ink in the text field, the combo, the checked box and the chosen radio button - and not in the others
        auto doc = PdfDocument::open(out, {}, nullptr);
        QVERIFY(doc);
        const QImage a = doc->render(0, 1.0, nullptr, 0);
        QVERIFY2(darkIn(a, QRect(76, 112, 220, 26)) > 80, qPrintable(QString::number(darkIn(a, QRect(76, 112, 220, 26)))));
        QVERIFY(darkIn(a, QRect(76, 262, 220, 26)) > 40);                      // country (792 - 530 = 262)
        QVERIFY(darkIn(a, QRect(74, 222, 16, 16)) > 100);                      // agree
        QVERIFY(darkIn(a, QRect(122, 322, 16, 16)) > 100);                     // size L
        QCOMPARE(darkIn(a, QRect(74, 322, 16, 16)), 0);                        // size S stays off
        QCOMPARE(PdfDocument::open(in, {}, nullptr)->pageCount(), 1);         // the source is as it was
    }

    void textThatTheFormFontCannotShowIsRefusedNotGarbled()
    {
        const QString in = write("h.pdf", formPdf());
        const FormFill r = fillForm(in, {}, m_dir.filePath("h-out.pdf"), {{"name", QString::fromUtf8("สมชาย")}, {"country", "Mars"}}, false);
        QVERIFY(r.ok);
        QCOMPARE(r.filled, 0);
        QVERIFY(r.skipped.contains(QString::fromUtf8("name: unicode")));
        QVERIFY(r.skipped.contains("country: option"));
        QCOMPARE(field(readFormFields(m_dir.filePath("h-out.pdf"), {}), "name")->value, QString());
    }

    void unfilledCheckboxesCanBeClearedAgain()
    {
        const QString in = write("i.pdf", formPdf());
        QVERIFY(fillForm(in, {}, m_dir.filePath("i1.pdf"), {{"agree", true}, {"size", "S"}}, false).ok);
        QVERIFY(fillForm(m_dir.filePath("i1.pdf"), {}, m_dir.filePath("i2.pdf"), {{"agree", false}, {"size", ""}}, false).ok);
        const FormRead r = readFormFields(m_dir.filePath("i2.pdf"), {});
        QVERIFY(!field(r, "agree")->checked);
        QCOMPARE(field(r, "size")->value, QString());
        const QImage img = PdfDocument::open(m_dir.filePath("i2.pdf"), {}, nullptr)->render(0, 1.0, nullptr, 0);
        QCOMPARE(darkIn(img, QRect(74, 222, 16, 16)), 0);
        QCOMPARE(darkIn(img, QRect(74, 322, 16, 16)), 0);
    }

    void errorsAndProtectedFiles()
    {
        const QString plain = write("j.pdf", testpdf::make(1));
        QCOMPARE(fillForm(plain, {}, m_dir.filePath("j-out.pdf"), {{"a", "b"}}, false).error, QStringLiteral("noForm"));
        QVERIFY(!QFileInfo::exists(m_dir.filePath("j-out.pdf")));
        const QString in = write("k.pdf", formPdf());
        QVERIFY(fillForm(in, {}, m_dir.filePath("k-out.pdf"), {{"name", "A"}}, false).ok);
        QCOMPARE(fillForm(in, {}, m_dir.filePath("k-out.pdf"), {{"name", "B"}}, false).error, QStringLiteral("exists"));
        QVERIFY(fillForm(in, {}, m_dir.filePath("k-out.pdf"), {{"name", "B"}}, true).ok);
        QCOMPARE(field(readFormFields(m_dir.filePath("k-out.pdf"), {}), "name")->value, QStringLiteral("B"));
        QCOMPARE(fillForm(write("junk2.pdf", "nope"), {}, m_dir.filePath("x.pdf"), {{"a", "b"}}, false).error, QStringLiteral("format"));

        if (QFileInfo(QStringLiteral(DP_TEST_QPDF)).exists()) {
            const QString locked = m_dir.filePath("k-locked.pdf");
            QVERIFY(qpdf({"--encrypt", "open-secret", "owner-secret", "256", "--", in, locked}));
            QCOMPARE(fillForm(locked, {}, m_dir.filePath("l-out.pdf"), {{"name", "A"}}, false).error, QStringLiteral("password"));
            const FormFill r = fillForm(locked, "open-secret", m_dir.filePath("l-out.pdf"), {{"name", "Secret"}}, false);
            QVERIFY2(r.ok, qPrintable(r.error + r.detail));
            QVERIFY(r.warnings.contains("encrypted"));
            QCOMPARE(field(readFormFields(m_dir.filePath("l-out.pdf"), "open-secret"), "name")->value, QStringLiteral("Secret"));
            QCOMPARE(readFormFields(m_dir.filePath("l-out.pdf"), {}).error, QStringLiteral("password"));
        }
    }
};

QTEST_MAIN(TstForms)
#include "tst_forms.moc"
