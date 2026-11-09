#include "OverlayImageProvider.h"

#include "DocumentController.h"
#include "EditorModel.h"
#include "pdftools/Overlay.h"

#include <algorithm>
#include <cmath>

namespace dp {

QImage OverlayImageProvider::requestImage(const QString& id, QSize* size, const QSize&)
{
    const QStringList parts = id.split(QLatin1Char('/'));
    QImage empty(1, 1, QImage::Format_ARGB32_Premultiplied);
    empty.fill(Qt::transparent);
    if (parts.size() < 4)
        return empty;
    const int tab = parts.at(0).toInt();
    const int page = parts.at(1).toInt();
    double scale = parts.at(2).toDouble() / 1000.0;
    const int turns = parts.at(3).toInt();
    const auto doc = m_controller->documentFor(tab);
    if (!doc || page < 0 || page >= doc->pageCount() || scale <= 0)
        return empty;
    const QSizeF base = doc->pageSizePt(page, 0);
    int hidden = 0;
    const QList<tools::EditItem> items = m_editor->itemsForPage(tab, page, base, doc->pageCount(), &hidden);
    if (items.isEmpty())
        return empty;
    scale = std::min(scale, std::sqrt(16.0e6 / std::max(1.0, base.width() * base.height())));      // at most 16 million pixels
    const QImage img = tools::renderOverlay(items, base, scale, turns, hidden);
    if (size)
        *size = img.size();
    return img;
}

} // namespace dp
