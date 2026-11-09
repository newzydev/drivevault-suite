#include "BatchPdf.h"
#include "pdfengine/PdfDocument.h"
#include "pdftools/Compress.h"
#include "pdftools/Overlay.h"
#include "pdftools/Security.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

namespace dp {
namespace {

QString processFile(const QString& source, const QString& staged, const QString& mode, const QVariantMap& options)
{
    if (mode == QLatin1String("protect")) {
        tools::Protection protection;
        protection.userPassword = options.value("user").toString();
        protection.ownerPassword = options.value("owner").toString();
        return tools::protectPdf(source, staged, protection);
    }
    pdf::OpenError openError;
    const auto doc = pdf::PdfDocument::open(source, {}, &openError);
    if (!doc)
        return openError == pdf::OpenError::Password ? QStringLiteral("alreadyProtected") : QStringLiteral("format");
    if (doc->isEncrypted())
        return QStringLiteral("alreadyProtected");
    if (mode == QLatin1String("compress")) {
        tools::CompressOptions settings;
        settings.imageDpi = 150;
        settings.jpegQuality = 70;
        const auto result = tools::compressFile(source, {}, staged, settings);
        return !result.ok ? result.error : !result.written ? QStringLiteral("noGain") : QString();
    }
    tools::MarkDef mark;
    mark.text = options.value("watermark").toString();
    mark.fontSize = 32;
    mark.opacity = 0.25;
    if (!tools::restrictedFonts({}, {mark}).isEmpty())
        return QStringLiteral("fontRestricted");
    QList<tools::PageEdits> pages;
    for (int page = 0; page < doc->pageCount(); ++page) {
        const auto size = doc->pageSizePt(page);
        pages.append({page, size, tools::marksForPage({mark}, page, doc->pageCount(), size)});
    }
    const auto result = tools::saveWithEdits(source, {}, staged, pages, false);
    return result.ok ? QString() : result.error;
}

} // namespace

QVariantList batchPdf(const QString& source, const QString& destination, const QVariantMap& options,
                      const tools::Progress& progress, QString& error)
{
    error.clear();
    const QDir input(source);
    const QDir output(destination);
    if (source.isEmpty() || destination.isEmpty() || !input.exists() || !output.exists()
        || input.canonicalPath().compare(output.canonicalPath(), Qt::CaseInsensitive) == 0) {
        error = QStringLiteral("batchFolders");
        return {};
    }
    const QString mode = options.value("mode").toString();
    if (mode != QLatin1String("compress") && mode != QLatin1String("protect") && mode != QLatin1String("watermark")) {
        error = QStringLiteral("maintenanceOptions");
        return {};
    }
    const auto watermark = options.value("watermark").toString();
    if (mode == QLatin1String("watermark") && (watermark.trimmed().isEmpty() || watermark.size() > 120)) {
        error = QStringLiteral("maintenanceOptions");
        return {};
    }
    const auto files = input.entryInfoList({QStringLiteral("*.pdf")}, QDir::Files | QDir::NoSymLinks, QDir::Name);
    if (files.isEmpty()) {
        error = QStringLiteral("batchEmpty");
        return {};
    }
    const int total = int(files.size());
    QVariantList rows;
    for (int i = 0; i < total; ++i) {
        if (progress && !progress(i, total)) {
            error = QStringLiteral("batchCancelled");
            return rows;
        }
        const auto& file = files.at(i);
        const auto path = output.filePath(file.fileName());
        QString status;
        if (QFileInfo::exists(path)) {
            status = QStringLiteral("exists");
        } else {
            const QTemporaryDir stage(output.filePath(QStringLiteral(".dv-batch-XXXXXX")));
            if (!stage.isValid()) {
                status = QStringLiteral("write");
            } else {
                const auto staged = stage.filePath(QStringLiteral("copy.pdf"));
                status = processFile(file.absoluteFilePath(), staged, mode, options);
                if (status.isEmpty() && !QFile::rename(staged, path))
                    status = QFileInfo::exists(path) ? QStringLiteral("exists") : QStringLiteral("write");
            }
        }
        rows.append(QVariantMap{{"name", file.fileName()}, {"status", status}, {"path", status.isEmpty() ? path : QString()}});
    }
    if (progress)
        progress(total, total);
    return rows;
}

} // namespace dp
