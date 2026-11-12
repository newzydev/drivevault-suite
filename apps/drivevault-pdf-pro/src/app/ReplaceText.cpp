#include "ReplaceText.h"

#include "pdfengine/PdfDocument.h"
#include "pdftools/Overlay.h"

#include <QFile>
#include <QFileInfo>
#include <QFontMetricsF>
#include <QTemporaryDir>

#include <cstring>

namespace dp {
namespace {

bool sameOutside(const QImage& before, const QImage& after, const QRectF& area, const QSizeF& size)
{
    if (before.isNull() || after.size() != before.size() || after.format() != before.format())
        return false;
    const double sx = before.width() / size.width(), sy = before.height() / size.height();
    const QRect ignore = QRectF(area.x() * sx, area.y() * sy, area.width() * sx, area.height() * sy)
                             .toAlignedRect().intersected(before.rect());
    const size_t stride = size_t(before.width()) * 4;  // opaque PDFium RGB32 images
    for (int y = 0; y < before.height(); ++y) {
        const uchar* a = before.constScanLine(y);
        const uchar* b = after.constScanLine(y);
        if (y < ignore.top() || y > ignore.bottom()) {
            if (std::memcmp(a, b, stride) != 0)
                return false;
        } else {
            const size_t left = size_t(ignore.left()) * 4, right = size_t(ignore.right() + 1) * 4;
            if (std::memcmp(a, b, left) != 0 || std::memcmp(a + right, b + right, stride - right) != 0)
                return false;
        }
    }
    return true;
}

} // namespace

QString replaceTextRun(const pdf::PdfDocument& doc, int page, QPointF point, const QString& original,
                       const QString& replacement, const QString& destination, bool remove)
{
    if (QFileInfo::exists(destination))
        return QStringLiteral("exists");
    const auto run = doc.editableTextAt(page, point);
    const QString text = replacement.trimmed();
    if (run.text.isEmpty() || run.text != original)
        return QStringLiteral("editRunUnsupported");
    if (!remove && (text.isEmpty() || text == original || text.size() > 512 || text.contains(QLatin1Char('\n')) || text.contains(QLatin1Char('\r'))))
        return QStringLiteral("editRunValue");

    tools::EditItem item;
    if (!remove) {
        item.kind = tools::Kind::Text;
        item.page = page;
        item.text = text;
        item.color = run.color;
        item.fontSize = run.fontSize;
        constexpr double precision = 16;  // same shaping resolution as the existing editor
        QFont font(item.font);
        font.setPixelSize(qRound(item.fontSize * precision));
        font.setHintingPreference(QFont::PreferNoHinting);
        const QFontMetricsF metrics(font);
        const double inkWidth = metrics.horizontalAdvance(text) / precision;
        const double available = run.bounds.right() - run.baseline.x() + 1.5;
        if (inkWidth > available)
            return QStringLiteral("editRunTooWide");
        item.rect = QRectF(run.baseline.x() - 2, run.baseline.y() - metrics.ascent() / precision - 1,
                           available + 4, metrics.height() / precision + 2);
    }

    QTemporaryDir staging(QFileInfo(destination).dir().filePath(QStringLiteral(".drivevault-edit-XXXXXX")));
    if (!staging.isValid())
        return QStringLiteral("write");
    const QString strippedPath = staging.filePath(QStringLiteral("without-run.pdf"));
    auto mutableDoc = pdf::PdfDocument::open(doc.path(), {}, nullptr);
    if (!mutableDoc || !mutableDoc->writeWithoutTextRun(page, point, run, strippedPath))
        return QStringLiteral("editRunUnsupported");
    mutableDoc.reset();

    auto stripped = pdf::PdfDocument::open(strippedPath, {}, nullptr);
    const auto size = doc.pageSizePt(page);
    if (!stripped || stripped->pageCount() != doc.pageCount() || stripped->pageSizePt(page) != size)
        return QStringLiteral("editRunVerify");
    const QString beforeText = doc.pageText(page);
    const QString strippedText = stripped->pageText(page);
    if (beforeText.count(run.text) != strippedText.count(run.text) + 1)
        return QStringLiteral("editRunVerify");
    if (remove) {
        if (!sameOutside(doc.render(page, 1.5), stripped->render(page, 1.5), run.bounds.adjusted(-2, -2, 2, 2), size))
            return QStringLiteral("editRunVerify");
        stripped.reset();
        return QFile::rename(strippedPath, destination) ? QString() : QStringLiteral("write");
    }
    stripped.reset();

    const QString copyPath = staging.filePath(QStringLiteral("edited.pdf"));
    const auto saved = tools::saveWithEdits(strippedPath, {}, copyPath, {{page, size, {item}, {}}}, false);
    if (!saved.ok)
        return saved.error;
    auto copy = pdf::PdfDocument::open(copyPath, {}, nullptr);
    if (!copy || copy->pageCount() != doc.pageCount() || copy->pageSizePt(page) != size
        || copy->pageText(page).count(text) <= strippedText.count(text))
        return QStringLiteral("editRunVerify");
    const QRectF area = run.bounds.united(item.rect.adjusted(2, 1, -2, -1)).adjusted(-2, -2, 2, 2);
    if (!sameOutside(doc.render(page, 1.5), copy->render(page, 1.5), area, size))
        return QStringLiteral("editRunVerify");
    copy.reset();
    return QFile::rename(copyPath, destination) ? QString() : QStringLiteral("write");
}

} // namespace dp
