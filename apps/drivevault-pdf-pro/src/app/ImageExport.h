#pragma once

#include <QList>
#include <QString>

#include <atomic>
#include <functional>

namespace dp::pdf { class PdfDocument; }

namespace dp {

struct ImageExport {
    enum class Target { Image, Zip, Folder };
    QList<int> pages;
    Target target = Target::Image;
    bool jpeg = false;
    bool transparent = false;
    int dpi = 144;
    int quality = 90;
    int rotation = 0;
};

// The destination is a new image, ZIP, or directory. Returns an empty code on success.
QString exportImages(const pdf::PdfDocument& doc, const QString& destination, const ImageExport& options,
                     const std::atomic_bool& cancel, const std::function<void(int, int)>& progress);

} // namespace dp
