#pragma once

#include <QImage>
#include <QList>
#include <QRectF>
#include <QSizeF>
#include <QString>

#include <atomic>
#include <memory>

namespace dp::pdf {

enum class OpenError { None, Password, Format, File, TooLarge, Unknown };

// One opened PDF file. All methods are thread safe (they serialise on the PDFium mutex); pages are 0-based.
class PdfDocument {
public:
    // Opens `path` (Unicode path). An encrypted file needs `password`; a wrong or missing one gives OpenError::Password.
    static std::shared_ptr<PdfDocument> open(const QString& path, const QString& password, OpenError* error);
    ~PdfDocument();
    PdfDocument(const PdfDocument&) = delete;
    PdfDocument& operator=(const PdfDocument&) = delete;

    QString path() const { return m_path; }
    int pageCount() const { return int(m_pageSizes.size()); }
    // Page size in points (1/72 inch), with the page rotation applied.
    QSizeF pageSizePt(int page) const;
    // Renders the page at `pxPerPt` pixels per point (96/72 = 100 % on screen) onto white. The result is clamped to a safe size.
    // `cancel` (optional) is checked before the work starts. A null image means failure or cancel.
    QImage render(int page, double pxPerPt, const std::atomic_bool* cancel = nullptr) const;
    // All text of a page in reading order (UTF-16 converted), lines separated by \n.
    QString pageText(int page) const;

private:
    PdfDocument() = default;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    QString m_path;
    QList<QSizeF> m_pageSizes;
};

} // namespace dp::pdf
