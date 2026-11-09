#pragma once

#include <QQuickImageProvider>

namespace dp {

class DocumentController;
class EditorModel;

// image://ovl/<tabId>/<page>/<pixels per point * 1000>/<view rotation 0..3>/<revision>  - a transparent picture of what the user has put on a page
// (the revision only makes the URL change when the edits change).
class OverlayImageProvider : public QQuickImageProvider {
public:
    OverlayImageProvider(DocumentController* controller, EditorModel* editor)
        : QQuickImageProvider(QQuickImageProvider::Image, QQmlImageProviderBase::ForceAsynchronousImageLoading), m_controller(controller), m_editor(editor) {}
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    DocumentController* m_controller;
    EditorModel* m_editor;
};

} // namespace dp
