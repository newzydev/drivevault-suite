#include "PrintTask.h"

#include <QPageLayout>
#include <QPainter>
#include <QPrinter>
#include <QPrinterInfo>

#include <algorithm>

namespace dp {

PrintTask::PrintTask(std::shared_ptr<pdf::PdfDocument> doc, PrintOptions options, std::shared_ptr<std::atomic_bool> cancelled)
    : m_doc(std::move(doc))
    , m_options(std::move(options))
    , m_cancelled(std::move(cancelled))
{
    setAutoDelete(true);
}

void PrintTask::run()
{
    auto finishWith = [this](bool ok, const QString& message) { emit finished(ok, message); };
    if (!m_doc || m_options.pages.isEmpty()) {
        finishWith(false, QStringLiteral("nothing to print"));
        return;
    }

    QPrinter printer(QPrinter::HighResolution);
    if (!m_options.outputFile.isEmpty()) {
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(m_options.outputFile);
    } else {
        if (!m_options.printerName.isEmpty())
            printer.setPrinterName(m_options.printerName);
        if (!printer.isValid()) {
            finishWith(false, QStringLiteral("printer not found"));
            return;
        }
    }
    printer.setCopyCount(qMax(1, m_options.copies));
    printer.setColorMode(m_options.color ? QPrinter::Color : QPrinter::GrayScale);
    printer.setFullPage(false);

    QPainter painter;
    const int total = int(m_options.pages.size());
    bool started = false;
    for (int i = 0; i < total; ++i) {
        if (m_cancelled->load()) {
            if (started)
                painter.end();
            finishWith(false, QStringLiteral("cancelled"));
            return;
        }
        const int page = m_options.pages.at(i);
        const QSizeF pt = m_doc->pageSizePt(page, m_options.rotation);
        QPageLayout layout = printer.pageLayout();
        layout.setOrientation(pt.width() > pt.height() ? QPageLayout::Landscape : QPageLayout::Portrait);
        printer.setPageLayout(layout);
        if (!started) {
            if (!painter.begin(&printer)) {
                finishWith(false, QStringLiteral("cannot start printing"));
                return;
            }
            started = true;
        } else if (!printer.newPage()) {
            painter.end();
            finishWith(false, QStringLiteral("cannot add a page"));
            return;
        }

        const double dpi = std::min(300, std::max(72, printer.resolution()));
        const QImage image = m_doc->render(page, dpi / 72.0, m_cancelled.get(), m_options.rotation);
        if (image.isNull()) {
            if (m_cancelled->load()) {
                painter.end();
                finishWith(false, QStringLiteral("cancelled"));
                return;
            }
            continue;                              // an unreadable page prints as a blank page
        }
        const QRectF area = printer.pageRect(QPrinter::DevicePixel);
        QSizeF target;
        if (m_options.fit) {
            const double s = std::min(area.width() / image.width(), area.height() / image.height());
            target = QSizeF(image.width() * s, image.height() * s);
        } else {                                   // actual size: the page's size in points at the printer's resolution
            target = QSizeF(pt.width() * printer.resolution() / 72.0, pt.height() * printer.resolution() / 72.0);
        }
        const QRectF where(area.left() + (area.width() - target.width()) / 2, area.top() + (area.height() - target.height()) / 2,
                           target.width(), target.height());
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(where, image);
        emit progress(i + 1, total);
    }
    painter.end();
    finishWith(true, QString());
}

} // namespace dp
