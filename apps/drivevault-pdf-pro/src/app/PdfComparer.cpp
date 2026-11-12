#include "PdfComparer.h"

#include "DocumentController.h"

#include <QFileInfo>
#include <QHash>
#include <QRunnable>
#include <QThreadPool>
#include <QUrl>

#include <algorithm>
#include <cmath>

namespace dp {

namespace {

QStringList linesOf(const QString& text)
{
    QStringList out;
    for (const QString& l : text.split(QLatin1Char('\n'))) {
        const QString t = l.simplified();
        if (!t.isEmpty())
            out << t;
    }
    return out;
}

double pixelDifference(const QImage& a, const QImage& b0)
{
    if (a.isNull() || b0.isNull())
        return 1.0;
    const QImage b = b0.size() == a.size() ? b0 : b0.scaled(a.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    qint64 differing = 0;
    for (int y = 0; y < a.height(); ++y) {
        const QRgb* pa = reinterpret_cast<const QRgb*>(a.constScanLine(y));
        const QRgb* pb = reinterpret_cast<const QRgb*>(b.constScanLine(y));
        for (int x = 0; x < a.width(); ++x)
            if (std::abs(qGray(pa[x]) - qGray(pb[x])) > 40)
                ++differing;
    }
    return double(differing) / double(std::max<qint64>(1, qint64(a.width()) * a.height()));
}

} // namespace

CompareResult comparePdfs(const std::shared_ptr<pdf::PdfDocument>& a, const std::shared_ptr<pdf::PdfDocument>& b, const std::atomic_bool* cancel,
                          const std::function<void(int, int)>& progress)
{
    CompareResult r;
    r.pagesA = a->pageCount();
    r.pagesB = b->pageCount();
    const int total = std::max(r.pagesA, r.pagesB);
    for (int i = 0; i < total; ++i) {
        if (cancel && cancel->load()) {
            r.cancelled = true;
            r.error = QStringLiteral("cancelled");
            return r;
        }
        PageDiff d;
        d.page = i;
        d.inA = i < r.pagesA;
        d.inB = i < r.pagesB;
        if (d.inA && d.inB) {
            const QStringList la = linesOf(a->pageText(i));
            const QStringList lb = linesOf(b->pageText(i));
            QHash<QString, int> counts;
            for (const QString& l : la)
                ++counts[l];
            int common = 0;
            for (const QString& l : lb) {
                if (counts.value(l) > 0) {
                    --counts[l];
                    ++common;
                } else {
                    ++d.addedLines;
                    if (d.addedSample.size() < 3)
                        d.addedSample << l;
                }
            }
            for (const QString& l : la)
                if (counts.value(l) > 0) {
                    --counts[l];
                    ++d.removedLines;
                    if (d.removedSample.size() < 3)
                        d.removedSample << l;
                }
            d.textSimilarity = la.isEmpty() && lb.isEmpty() ? 1.0 : 2.0 * common / double(la.size() + lb.size());
            d.pixelDifference = pixelDifference(a->render(i, 0.35, nullptr, 0), b->render(i, 0.35, nullptr, 0));
        } else {
            d.textSimilarity = 0.0;
            d.pixelDifference = 1.0;
        }
        r.pages.append(d);
        if (progress)
            progress(i + 1, total);
    }
    r.ok = true;
    return r;
}

PdfComparer::PdfComparer(DocumentController* controller, QObject* parent)
    : QObject(parent), m_controller(controller)
{
    connect(controller, &DocumentController::documentChanged, this, [this] { clear(); });
}

PdfComparer::~PdfComparer()
{
    if (m_cancel)
        m_cancel->store(true);
    QThreadPool::globalInstance()->waitForDone(15000);
}

void PdfComparer::clear()
{
    if (m_busy)
        return;
    m_has = false;
    m_pages.clear();
    m_summary.clear();
    emit changed();
}

void PdfComparer::cancel()
{
    if (m_cancel)
        m_cancel->store(true);
}

QString PdfComparer::start(const QString& otherPathOrUrl, const QString& password)
{
    const DocumentController::SourceInfo src = m_controller->currentSource();
    if (!src.doc)
        return QStringLiteral("noDocument");
    if (m_busy)
        return QStringLiteral("busy");
    const QUrl url(otherPathOrUrl);
    const QString other = QFileInfo(url.isLocalFile() ? url.toLocalFile() : otherPathOrUrl).absoluteFilePath();
    if (QFileInfo(other) == QFileInfo(src.path))
        return QStringLiteral("same");
    m_cancel = std::make_shared<std::atomic_bool>(false);
    auto cancelFlag = m_cancel;
    m_busy = true;
    m_progress = 0.0;
    m_has = false;
    m_pages.clear();
    emit changed();
    QThreadPool::globalInstance()->start(QRunnable::create([this, src, other, password, cancelFlag] {
        CompareResult r;
        pdf::OpenError err = pdf::OpenError::None;
        const auto b = pdf::PdfDocument::open(other, password, &err);
        if (!b) {
            r.error = err == pdf::OpenError::Password ? QStringLiteral("password") : err == pdf::OpenError::File ? QStringLiteral("file")
                    : err == pdf::OpenError::TooLarge ? QStringLiteral("tooLarge") : err == pdf::OpenError::Format ? QStringLiteral("format") : QStringLiteral("unknown");
        } else {
            r = comparePdfs(src.doc, b, cancelFlag.get(), [this](int done, int total) {
                QMetaObject::invokeMethod(this, [this, done, total] { m_progress = total > 0 ? double(done) / total : 0.0; emit changed(); }, Qt::QueuedConnection);
            });
        }
        QMetaObject::invokeMethod(this, [this, r] {
            m_busy = false;
            m_has = r.ok;
            QVariantList list;
            int different = 0;
            for (const PageDiff& d : r.pages) {
                const bool diff = d.differs();
                different += diff ? 1 : 0;
                list.append(QVariantMap{{"page", d.page},
                                        {"status", !d.inA ? QStringLiteral("onlyB") : !d.inB ? QStringLiteral("onlyA") : diff ? QStringLiteral("different") : QStringLiteral("same")},
                                        {"similarity", d.textSimilarity},
                                        {"pixels", d.pixelDifference},
                                        {"removed", d.removedLines},
                                        {"added", d.addedLines},
                                        {"removedSample", d.removedSample},
                                        {"addedSample", d.addedSample}});
            }
            m_pages = list;
            m_summary = QVariantMap{{"pagesA", r.pagesA}, {"pagesB", r.pagesB}, {"different", different}, {"same", int(r.pages.size()) - different}, {"error", r.error}};
            emit changed();
            emit finished(r.ok, r.error);
        }, Qt::QueuedConnection);
    }));
    return {};
}

} // namespace dp
