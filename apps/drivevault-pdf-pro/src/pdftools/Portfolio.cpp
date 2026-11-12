#include "Portfolio.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFFileSpecObjectHelper.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFSystemError.hh>
#include <qpdf/QPDFWriter.hh>

#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSet>
#include <QTemporaryDir>

namespace dp::tools {

namespace {

constexpr qint64 kLimit = qint64(1) << 30;

// the cover page: the title and the list of files (Qt shapes Thai and English text and embeds the font)
bool writeCover(const QString& path, const QString& title, const QStringList& names)
{
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(72);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);
    QPainter p(&writer);
    if (!p.isActive())
        return false;
    QFont head(QStringLiteral("Noto Sans Thai"));
    head.setPixelSize(26);
    head.setBold(true);
    QFont body(QStringLiteral("Noto Sans Thai"));
    body.setPixelSize(13);
    p.setFont(head);
    const QRectF titleBox(56, 70, 483, 70);
    p.drawText(titleBox, Qt::AlignLeft | Qt::TextWordWrap, title);
    p.setFont(body);
    p.setPen(QColor(90, 90, 100));
    p.drawText(QRectF(56, 150, 483, 20), Qt::AlignLeft, QStringLiteral("PDF portfolio - %1 file(s)").arg(names.size()));
    p.setPen(Qt::black);
    double y = 185;
    for (const QString& name : names) {
        if (y > 790) {
            p.drawText(QRectF(56, y, 483, 20), Qt::AlignLeft, QStringLiteral("..."));
            break;
        }
        p.drawText(QRectF(56, y, 483, 20), Qt::AlignLeft | Qt::TextSingleLine, name);
        y += 20;
    }
    p.end();
    return true;
}

} // namespace

QString createPortfolio(const QString& outFile, const QString& title, const QStringList& files, bool overwrite, const std::atomic_bool& cancel,
                        const std::function<void(int, int)>& progress)
{
    if (files.isEmpty())
        return QStringLiteral("noFiles");
    if (QFileInfo(outFile).suffix().compare(QLatin1String("pdf"), Qt::CaseInsensitive) != 0)
        return QStringLiteral("pdfSuffix");
    if (!overwrite && QFileInfo::exists(outFile))
        return QStringLiteral("exists");
    // unique names inside the portfolio, sizes checked before anything is read into memory
    QStringList names;
    QSet<QString> used;
    qint64 total = 0;
    for (const QString& file : files) {
        const QFileInfo info(file);
        if (!info.isFile() || info.size() <= 0 || !info.isReadable())
            return QStringLiteral("fileRead:") + info.fileName();
        total += info.size();
        if (total > kLimit)
            return QStringLiteral("tooLarge");
        QString name = info.fileName();
        const QString base = info.completeBaseName();
        const QString suffix = info.suffix().isEmpty() ? QString() : QStringLiteral(".") + info.suffix();
        for (int n = 2; used.contains(name.toLower()); ++n)
            name = base + QStringLiteral(" (") + QString::number(n) + QStringLiteral(")") + suffix;
        used.insert(name.toLower());
        names << name;
    }
    QTemporaryDir dir;
    if (!dir.isValid())
        return QStringLiteral("write");
    const QString cover = dir.filePath(QStringLiteral("cover.pdf"));
    const QString heading = title.trimmed().isEmpty() ? QStringLiteral("PDF portfolio") : title.trimmed();
    if (!writeCover(cover, heading, names))
        return QStringLiteral("write");
    const QString tmp = outFile + QStringLiteral(".part.tmp");
    try {
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        pdf.processFile(cover.toUtf8().constData());
        QPDFEmbeddedFileDocumentHelper efdh(pdf);
        for (int i = 0; i < files.size(); ++i) {
            if (cancel.load()) {
                QFile::remove(tmp);
                return QStringLiteral("cancelled");
            }
            const std::string name = names.at(i).toUtf8().toStdString();
            QPDFFileSpecObjectHelper spec = QPDFFileSpecObjectHelper::createFileSpec(pdf, name, QFileInfo(files.at(i)).absoluteFilePath().toUtf8().toStdString());
            efdh.replaceEmbeddedFile(name, spec);
            if (progress)
                progress(i + 1, int(files.size()));
        }
        QPDFObjectHandle root = pdf.getRoot();
        QPDFObjectHandle collection = QPDFObjectHandle::newDictionary();
        collection.replaceKey("/Type", QPDFObjectHandle::newName("/Collection"));
        collection.replaceKey("/View", QPDFObjectHandle::newName("/D"));            // details (list) view
        root.replaceKey("/Collection", collection);
        QPDFObjectHandle info = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
        info.replaceKey("/Title", QPDFObjectHandle::newUnicodeString(heading.toUtf8().toStdString()));
        pdf.getTrailer().replaceKey("/Info", info);
        QFile::remove(tmp);
        const QByteArray outputName = tmp.toUtf8();
        QPDFWriter writer(pdf, outputName.constData());
        writer.setObjectStreamMode(qpdf_o_generate);
        writer.setStreamDataMode(qpdf_s_compress);
        writer.write();
    } catch (const QPDFSystemError&) {
        QFile::remove(tmp);
        return QStringLiteral("write");
    } catch (const std::exception&) {
        QFile::remove(tmp);
        return QStringLiteral("unknown");
    }
    if (overwrite)
        QFile::remove(outFile);
    if (!QFile::rename(tmp, outFile)) {
        QFile::remove(tmp);
        return QStringLiteral("write");
    }
    return {};
}

} // namespace dp::tools
