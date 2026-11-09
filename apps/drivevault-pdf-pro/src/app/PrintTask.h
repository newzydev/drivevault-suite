#pragma once

#include "pdfengine/PdfDocument.h"

#include <QImage>
#include <QObject>
#include <QRunnable>
#include <QString>

#include <atomic>
#include <memory>

namespace dp {

struct PrintOptions {
    QString printerName;          // empty: the default printer
    QString outputFile;           // set: write a PDF file instead of printing (used by the tests)
    QList<int> pages;             // 0-based, in the order to print
    int copies = 1;
    bool color = true;
    bool fit = true;              // true: shrink / enlarge to the paper; false: actual size
    int rotation = 0;             // the view rotation is printed too
};

// Prints pages of a document on a worker thread: each page is rendered by PDFium at up to 300 dpi and drawn on the printer.
class PrintTask : public QObject, public QRunnable {
    Q_OBJECT
public:
    PrintTask(std::shared_ptr<pdf::PdfDocument> doc, PrintOptions options, std::shared_ptr<std::atomic_bool> cancelled);
    void run() override;

signals:
    void progress(int done, int total);
    void finished(bool ok, const QString& message);     // message: "" when ok, "cancelled", or an error text

private:
    std::shared_ptr<pdf::PdfDocument> m_doc;
    PrintOptions m_options;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};

} // namespace dp
