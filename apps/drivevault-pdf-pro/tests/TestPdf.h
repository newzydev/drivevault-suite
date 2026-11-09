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

} // namespace testpdf
