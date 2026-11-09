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
    RenderTask(std::shared_ptr<pdf::PdfDocument> doc, int page, double scale, std::shared_ptr<std::atomic_bool> cancelled)
        : m_doc(std::move(doc)), m_page(page), m_scale(scale), m_cancelled(std::move(cancelled)) {}

    void run() override
    {
        QImage image;
        if (m_doc && !m_cancelled->load())
            image = m_doc->render(m_page, m_scale, m_cancelled.get());
        m_doc.reset();
        emit done(image);
    }

signals:
    void done(const QImage& image);

private:
    std::shared_ptr<pdf::PdfDocument> m_doc;
    int m_page;
    double m_scale;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};

class PageResponse : public QQuickImageResponse {
    Q_OBJECT
public:
    PageResponse(std::shared_ptr<pdf::PdfDocument> doc, int page, double scale)
        : m_cancelled(std::make_shared<std::atomic_bool>(false))
    {
        auto* task = new RenderTask(std::move(doc), page, scale, m_cancelled);
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
    if (parts.size() >= 3) {
        doc = m_controller->documentFor(parts.at(0).toInt());
        page = parts.at(1).toInt();
        scale = parts.at(2).toDouble() / 1000.0;
    }
    return new PageResponse(std::move(doc), page, scale);
}

} // namespace dp

#include "PageImageProvider.moc"
