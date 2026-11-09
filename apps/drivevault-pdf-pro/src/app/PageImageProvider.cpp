#include "PageImageProvider.h"

#include "DocumentController.h"

#include <QImage>
#include <QObject>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QRunnable>
#include <QThreadPool>

#include <atomic>
#include <memory>

namespace dp {

namespace {

// The work item (deleted by the thread pool after run()); it reports to the response through a queued signal.
class RenderTask : public QObject, public QRunnable {
    Q_OBJECT
public:
    RenderTask(std::shared_ptr<pdf::PdfDocument> doc, int page, double scale, int rotation, bool dark, std::shared_ptr<std::atomic_bool> cancelled)
        : m_doc(std::move(doc)), m_page(page), m_scale(scale), m_rotation(rotation), m_dark(dark), m_cancelled(std::move(cancelled)) {}

    void run() override
    {
        QImage image;
        if (m_doc && !m_cancelled->load())
            image = m_doc->render(m_page, m_scale, m_cancelled.get(), m_rotation);
        if (m_dark && !image.isNull())
            image.invertPixels(QImage::InvertRgb);          // dark view: only what is shown, the file is not changed
        m_doc.reset();
        emit done(image);
    }

signals:
    void done(const QImage& image);

private:
    std::shared_ptr<pdf::PdfDocument> m_doc;
    int m_page;
    double m_scale;
    int m_rotation;
    bool m_dark;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};

class PageResponse : public QQuickImageResponse {
    Q_OBJECT
public:
    PageResponse(std::shared_ptr<pdf::PdfDocument> doc, int page, double scale, int rotation, bool dark)
        : m_cancelled(std::make_shared<std::atomic_bool>(false))
    {
        auto* task = new RenderTask(std::move(doc), page, scale, rotation, dark, m_cancelled);
        connect(task, &RenderTask::done, this, &PageResponse::handleDone, Qt::QueuedConnection);
        QThreadPool::globalInstance()->start(task);
    }

    QQuickTextureFactory* textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }
    void cancel() override { m_cancelled->store(true); }

private slots:
    void handleDone(const QImage& image)
    {
        m_image = image;
        emit finished();
    }

private:
    QImage m_image;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};

} // namespace

QQuickImageResponse* PageImageProvider::requestImageResponse(const QString& id, const QSize&)
{
    const QStringList parts = id.split(QLatin1Char('/'));
    std::shared_ptr<pdf::PdfDocument> doc;
    int page = -1;
    double scale = 0.0;
    int rotation = 0;
    bool dark = false;
    if (parts.size() >= 3) {
        const int docId = parts.at(0).toInt();
        doc = m_controller->documentFor(docId);
        page = parts.at(1).toInt();
        scale = parts.at(2).toDouble() / 1000.0;
        rotation = parts.size() >= 4 ? parts.at(3).toInt() : 0;
        dark = parts.size() >= 5 && parts.at(4) == QLatin1String("1");
    }
    return new PageResponse(std::move(doc), page, scale, rotation, dark);
}

} // namespace dp

#include "PageImageProvider.moc"
