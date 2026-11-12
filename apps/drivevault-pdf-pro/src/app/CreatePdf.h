#pragma once

#include <QList>
#include <QPageLayout>
#include <QString>

#include <atomic>
#include <functional>

namespace dp {

struct PdfImagePage {
    QString path;
    int quarterTurns = 0;
};

QString createPdf(const QString& destination, const QPageLayout& layout, const QList<PdfImagePage>& images,
                  const std::atomic_bool& cancel, const std::function<void(int, int)>& progress);

} // namespace dp
