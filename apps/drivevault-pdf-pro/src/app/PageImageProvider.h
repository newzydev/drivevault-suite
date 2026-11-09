#pragma once

#include <QQuickAsyncImageProvider>

namespace dp {

class DocumentController;

// image://pdf/<docId>/<page>/<scale * 1000>  - the page rendered at <scale> pixels per point. Rendering runs on the global thread pool
// (PDFium calls are serialised inside PdfDocument); a request that is no longer needed is cancelled before it starts.
class PageImageProvider : public QQuickAsyncImageProvider {
public:
    explicit PageImageProvider(DocumentController* controller) : m_controller(controller) {}
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

private:
    DocumentController* m_controller;
};

} // namespace dp
