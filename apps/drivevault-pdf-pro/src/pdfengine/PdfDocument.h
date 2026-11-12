#pragma once

#include <QImage>
#include <QColor>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QRegularExpression>
#include <QSizeF>
#include <QString>

#include <atomic>
#include <memory>

namespace dp::pdf {

enum class OpenError { None, Password, Format, File, TooLarge, Unknown };

// One entry of the outline (bookmarks): `page` is 0-based or -1 when the entry does not point to a page.
struct OutlineItem {
    QString title;
    int page = -1;
    int level = 0;
};

// An annotation (comment, highlight, stamp ...) found in the file. `rect` is in points, top-left origin of the page as shown.
struct AnnotationInfo {
    int page = 0;
    QString type;          // "Text", "Highlight", "Underline", "Stamp", "FreeText", "Square", "Ink" ...
    QString contents;
    QRectF rect;
};

// What lies under a point of a page.
struct LinkTarget {
    enum Kind { None, Page, Uri } kind = None;
    int page = -1;         // Kind::Page
    QString uri;           // Kind::Uri
};

// One search hit: the page and the rectangles (one per line) in points, top-left origin of the page as shown.
struct SearchHit {
    int page = 0;
    QList<QRectF> rects;
};

struct TextPosition {
    int boundary = -1;
    QRectF caret;
};

// Raw PDF coordinates for rebuilding simple text objects in a sanitized document.
struct RedactionGlyph {
    char32_t character;
    QPointF origin;
    QRectF bounds;
    QRectF shownBounds;
};

struct RedactionGeometry {
    QList<QRectF> masks;
    QList<QList<RedactionGlyph>> text;
    bool valid = false;
};

struct EditableTextRun {
    QString text;
    QRectF bounds;
    QPointF baseline;
    QColor color;
    double fontSize = 0;
};

// One opened PDF file. All methods are thread safe (they serialise on the PDFium mutex); pages are 0-based.
// `rotation` (0..3 = 0, 90, 180, 270 degrees clockwise) is a view rotation: the file is not changed.
class PdfDocument {
public:
    // Opens `path` (Unicode path). An encrypted file needs `password`; a wrong or missing one gives OpenError::Password.
    static std::shared_ptr<PdfDocument> open(const QString& path, const QString& password, OpenError* error);
    ~PdfDocument();
    PdfDocument(const PdfDocument&) = delete;
    PdfDocument& operator=(const PdfDocument&) = delete;

    QString path() const { return m_path; }
    int pageCount() const { return int(m_pageSizes.size()); }
    // Page size in points (1/72 inch) with the page's own rotation and the view rotation applied.
    QSizeF pageSizePt(int page, int rotation = 0) const;
    QString pageLabel(int page) const;                    // the label the file gives the page (i, ii, A-1 ...); "" when the file has no page labels
    // Renders the page at `pxPerPt` pixels per point (96/72 = 100 % on screen) onto white. The result is clamped to a safe size.
    // `cancel` (optional) is checked before the work starts. A null image means failure or cancel.
    QImage render(int page, double pxPerPt, const std::atomic_bool* cancel = nullptr, int rotation = 0,
                  bool transparent = false, bool includeAnnotations = true) const;

    // ---- text ----------------------------------------------------------------------------------------------------------------
    // All text of a page in reading order, lines separated by \n.
    QString pageText(int page) const;
    int charCount(int page) const;
    // The text of the characters [start, start + count) of a page.
    QString textRange(int page, int start, int count) const;
    // Index of the character at / near the point (points, top-left origin of the page as shown), or -1. `tolerance` is in points.
    int charIndexAt(int page, QPointF pt, double tolerance, int rotation = 0) const;
    TextPosition textPositionAt(int page, QPointF pt, double tolerance, int rotation = 0) const;
    // The rectangles (one per line) covered by the characters [start, start + count), in points as shown.
    QList<QRectF> textRects(int page, int start, int count, int rotation = 0) const;
    // All matches of `text` on one page (case insensitive unless `matchCase`).
    QList<SearchHit> searchPage(int page, const QString& text, bool matchCase, bool wholeWord, int rotation = 0) const;
    QList<SearchHit> matchPage(int page, const QRegularExpression& pattern, int limit) const;
    bool isEncrypted() const;
    RedactionGeometry redactionGeometry(int page, const QList<QRectF>& shownMasks) const;
    EditableTextRun editableTextAt(int page, QPointF point) const;
    // On a private document instance, remove the selected object and write a full copy to a new path.
    bool writeWithoutTextRun(int page, QPointF point, const EditableTextRun& expected, const QString& destination);

    // ---- structure ------------------------------------------------------------------------------------------------------------
    QList<OutlineItem> outline() const;
    QList<AnnotationInfo> annotations(int rotation = 0) const;      // every annotation except links and widgets, all pages
    // The link under the point of a page (points, top-left origin as shown).
    LinkTarget linkAt(int page, QPointF pt, int rotation = 0) const;

private:
    PdfDocument() = default;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    QString m_path;
    QList<QSizeF> m_pageSizes;       // as shown (the page's own /Rotate applied)
};

} // namespace dp::pdf
