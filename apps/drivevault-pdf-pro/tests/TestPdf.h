#pragma once

#include <QByteArray>
#include <QList>
#include <QSizeF>
#include <QString>
#include <QStringList>

// Builds small, valid PDF files for the tests (Helvetica text, no compression), so the tests need no sample files.
namespace testpdf {

inline QByteArray make(const QList<QSizeF>& sizes, const QStringList& texts)
{
    QByteArray out = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    QList<qint64> offsets;
    auto addObject = [&](const QByteArray& body) {
        offsets.append(out.size());
        out += QByteArray::number(offsets.size()) + " 0 obj\n" + body + "\nendobj\n";
    };
    const int n = int(sizes.size());
    // object 1: catalog, 2: pages, 3: font, then per page: page object + content stream
    addObject("<< /Type /Catalog /Pages 2 0 R >>");
    QByteArray kids;
    for (int i = 0; i < n; ++i)
        kids += QByteArray::number(4 + i * 2) + " 0 R ";
    addObject("<< /Type /Pages /Kids [ " + kids + "] /Count " + QByteArray::number(n) + " >>");
    addObject("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
    for (int i = 0; i < n; ++i) {
        const QString text = i < texts.size() ? texts.at(i) : QString();
        QByteArray escaped = text.toLatin1();
        escaped.replace("\\", "\\\\").replace("(", "\\(").replace(")", "\\)");
        const QByteArray content = "BT /F1 24 Tf 72 " + QByteArray::number(int(sizes.at(i).height()) - 100) + " Td (" + escaped + ") Tj ET";
        addObject("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + QByteArray::number(sizes.at(i).width()) + " "
                  + QByteArray::number(sizes.at(i).height()) + "] /Resources << /Font << /F1 3 0 R >> >> /Contents "
                  + QByteArray::number(5 + i * 2) + " 0 R >>");
        addObject("<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content + "\nendstream");
    }
    const qint64 xref = out.size();
    out += "xref\n0 " + QByteArray::number(offsets.size() + 1) + "\n0000000000 65535 f \n";
    for (qint64 o : offsets)
        out += QByteArray::number(o).rightJustified(10, '0') + " 00000 n \n";
    out += "trailer\n<< /Size " + QByteArray::number(offsets.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
    return out;
}

inline QByteArray make(int pages, const QSizeF& size = QSizeF(612, 792))
{
    QList<QSizeF> sizes;
    QStringList texts;
    for (int i = 1; i <= pages; ++i) {
        sizes.append(size);
        texts.append(QStringLiteral("Page %1 of %2").arg(i).arg(pages));
    }
    return make(sizes, texts);
}


// A small document with the structures the reader shows: outline (nested), two link annotations (to page 2 and to a web address),
// a comment and a highlight, and searchable text. Pages: 1 = "Hello search world" / "search again, SEARCH", 2 = "Second page", 3 = "Third page".
inline QByteArray makeFeatureDoc()
{
    auto stream = [](const QByteArray& content) {
        return "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content + "\nendstream";
    };
    const QList<QByteArray> bodies = {
        /* 1 */ "<< /Type /Catalog /Pages 2 0 R /Outlines 9 0 R >>",
        /* 2 */ "<< /Type /Pages /Kids [4 0 R 6 0 R 8 0 R] /Count 3 >>",
        /* 3 */ "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>",
        /* 4 */ "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R >> >> /Contents 5 0 R /Annots [14 0 R 15 0 R 16 0 R 17 0 R] >>",
        /* 5 */ stream("BT /F1 18 Tf 72 700 Td (Hello search world) Tj 0 -24 Td (search again, SEARCH) Tj ET"),
        /* 6 */ "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R >> >> /Contents 7 0 R >>",
        /* 7 */ stream("BT /F1 18 Tf 72 700 Td (Second page) Tj ET"),
        /* 8 */ "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R >> >> /Contents 10 0 R >>",
        /* 9 */ "<< /Type /Outlines /First 11 0 R /Last 12 0 R /Count 3 >>",
        /* 10 */ stream("BT /F1 18 Tf 72 700 Td (Third page) Tj ET"),
        /* 11 */ "<< /Title (Chapter 1) /Parent 9 0 R /Next 12 0 R /Dest [4 0 R /Fit] >>",
        /* 12 */ "<< /Title (Chapter 2) /Parent 9 0 R /Prev 11 0 R /First 13 0 R /Last 13 0 R /Count 1 /Dest [6 0 R /Fit] >>",
        /* 13 */ "<< /Title (Section 2.1) /Parent 12 0 R /Dest [8 0 R /Fit] >>",
        /* 14 */ "<< /Type /Annot /Subtype /Link /Rect [72 500 272 540] /Border [0 0 0] /Dest [6 0 R /Fit] >>",
        /* 15 */ "<< /Type /Annot /Subtype /Link /Rect [72 440 272 480] /Border [0 0 0] /A << /S /URI /URI (https://example.com/) >> >>",
        /* 16 */ "<< /Type /Annot /Subtype /Text /Rect [300 600 320 620] /Contents (Remember this) /Name /Note >>",
        /* 17 */ "<< /Type /Annot /Subtype /Highlight /Rect [72 650 200 670] /QuadPoints [72 670 200 670 72 650 200 650] /C [1 1 0] /Contents (Important) >>",
    };
    QByteArray out = "%PDF-1.4\n";
    QList<qint64> offsets;
    for (int i = 0; i < bodies.size(); ++i) {
        offsets.append(out.size());
        out += QByteArray::number(i + 1) + " 0 obj\n" + bodies.at(i) + "\nendobj\n";
    }
    const qint64 xref = out.size();
    out += "xref\n0 " + QByteArray::number(offsets.size() + 1) + "\n0000000000 65535 f \n";
    for (qint64 o : offsets)
        out += QByteArray::number(o).rightJustified(10, '0') + " 00000 n \n";
    out += "trailer\n<< /Size " + QByteArray::number(offsets.size() + 1) + " /Root 1 0 R >>\nstartxref\n" + QByteArray::number(xref) + "\n%%EOF\n";
    return out;
}

} // namespace testpdf
