#include "CreatePdf.h"

#include "pdfengine/PdfDocument.h"

#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryFile>

namespace dp {

QString createPdf(const QString& destination, const QPageLayout& layout, const QList<PdfImagePage>& images,
                  const std::atomic_bool& cancel, const std::function<void(int, int)>& progress)
{
    if (QFileInfo::exists(destination))
        return QStringLiteral("exists");
    QTemporaryFile temporary(destination + QStringLiteral(".XXXXXX"));
    if (!temporary.open())
        return QStringLiteral("write");

    const int pages = qMax(1, int(images.size()));
    {
        QPdfWriter writer(&temporary);
        writer.setResolution(72);
        if (!writer.setPageLayout(layout))
            return QStringLiteral("createPageLayout");
        QPainter painter;
        if (!painter.begin(&writer))
            return QStringLiteral("write");
        const QSizeF available(writer.width(), writer.height());
        for (int i = 0; i < pages; ++i) {
            if (cancel.load())
                return QStringLiteral("cancelled");
            if (i > 0 && !writer.newPage())
                return QStringLiteral("write");
            if (!images.isEmpty()) {
                const PdfImagePage& source = images.at(i);
                QImageReader reader(source.path);
                reader.setAutoTransform(true);
                QImage image = reader.read();
                if (image.isNull())
                    return QStringLiteral("createImageRead");
                if (source.quarterTurns != 0)
                    image = image.transformed(QTransform().rotate(source.quarterTurns * 90));
                QSizeF size(image.size());
                size.scale(available, Qt::KeepAspectRatio);
                const QPointF topLeft((available.width() - size.width()) / 2, (available.height() - size.height()) / 2);
                painter.drawImage(QRectF(topLeft, size), image);
            }
            if (progress)
                progress(i + 1, pages);
        }
        if (!painter.end())
            return QStringLiteral("write");
    }
    if (!temporary.flush())
        return QStringLiteral("write");
    temporary.close();
    {
        const auto result = pdf::PdfDocument::open(temporary.fileName(), {}, nullptr);
        if (!result || result->pageCount() != pages)
            return QStringLiteral("write");
    }
    if (cancel.load())
        return QStringLiteral("cancelled");
    if (!temporary.rename(destination))
        return QFileInfo::exists(destination) ? QStringLiteral("exists") : QStringLiteral("write");
    temporary.setAutoRemove(false);
    return {};
}

} // namespace dp
