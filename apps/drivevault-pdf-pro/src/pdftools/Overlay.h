#pragma once

#include "PdfTools.h"

#include <QColor>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace dp::tools {

// What the user can put on a page. All positions are in points, origin at the top left of the page as it is shown without view rotation.
enum class Kind { Text, Image, Rect, Ellipse, Line, Arrow, Pen, Highlight, Underline, Strike, Stamp, Polygon, Note };

struct EditItem {
    int id = 0;
    Kind kind = Kind::Rect;
    int page = 0;                 // 0-based
    QRectF rect;                  // Text, Note, Image, Rect, Ellipse, Highlight, Underline, Strike, Stamp
    QPointF a, b;                 // Line, Arrow: the two ends
    QVector<QPointF> points;      // Pen, Polygon
    QString text;                 // Text, Stamp
    QString font = QStringLiteral("Noto Sans Thai");
    double fontSize = 14;         // points
    bool bold = false;
    bool italic = false;
    int align = 0;                // 0 left, 1 centre, 2 right
    QColor color = Qt::black;     // text, outline, line, highlight colour
    QColor fill;                  // invalid = no fill (Rect, Ellipse, Text background)
    double width = 2;             // outline / line width in points
    double opacity = 1.0;         // 0..1 for the whole item
    double angle = 0;             // degrees clockwise about the centre of `rect`
    QString imagePath;            // Image: PNG / JPEG / ... file

    QRectF bounds() const;        // the box that contains the item (before `angle`)
    QPointF center() const { return bounds().center(); }
};

// Something that is put on many pages.
struct MarkDef {
    enum Type { Watermark, PageNumber, Header, Footer } type = Watermark;
    QString text;                 // PageNumber: {n} and {total} are replaced; others: any text (also {n} / {total})
    QString font = QStringLiteral("Noto Sans Thai");
    double fontSize = 14;
    bool bold = false;
    bool italic = false;
    QColor color = Qt::black;
    double opacity = 1.0;
    int align = 1;                // Header / Footer / PageNumber: 0 left, 1 centre, 2 right
    int fromPage = 0;             // 0-based, inclusive
    int toPage = -1;              // -1 = the last page
};

// The items a mark puts on `page` (ids are negative). `size` is the page as shown.
QList<EditItem> marksForPage(const QList<MarkDef>& marks, int page, int pageCount, const QSizeF& size);

// Paints one item in page coordinates (1 unit = 1 point) on a painter that already maps points to its device.
// A line of text that was drawn (for the invisible text layer of the PDF): baseline start in item coordinates, the centre the item turns about.
struct TextRun {
    QString text;
    QPointF origin;
    QPointF center;
    double angle = 0;
    double size = 12;
    double width = 0;
};
using TextRuns = QList<TextRun>;

// `runs` (optional) receives the lines of text that were drawn.
void paintItem(class QPainter& painter, const EditItem& item, TextRuns* runs = nullptr);

// A transparent picture of the items of one page (what the editor shows over the page). `turns` = view rotation in quarter turns clockwise.
// `hiddenId` is not drawn (it is being edited).
QImage renderOverlay(const QList<EditItem>& items, const QSizeF& baseSize, double pixelsPerPoint, int turns, int hiddenId = 0);

// Font embedding permission: names of the fonts used by `items` / `marks` whose licence flags forbid embedding, and fonts that cannot be found.
QStringList restrictedFonts(const QList<EditItem>& items, const QList<MarkDef>& marks);

struct PageEdits {
    int page = 0;
    QSizeF size;                  // the page as shown (view rotation 0)
    QList<EditItem> items;        // marks already expanded
};

struct EditResult {
    bool ok = false;
    bool cancelled = false;
    // "" | "password" | "file" | "format" | "range" | "exists" | "write" | "unknown" | "imageMissing" | "fontRestricted" | "noEdits"
    QString error;
    QString detail;               // imageMissing: the file; fontRestricted: the font; others: the library's message
    QStringList warnings;         // "encrypted": the new file keeps the protection of the source
    int pages = 0;                // pages that got something
    qint64 bytes = 0;
};

// Writes `in` with the items burned into the pages as a new file (the items become part of the page contents: they can be selected, copied and
// searched as text where they are text, but they are no longer editable objects). The source is never changed. Existing `outFile` is refused
// unless `overwrite`. Progress is (pages done, total).
EditResult saveWithEdits(const QString& inFile, const QString& password, const QString& outFile, const QList<PageEdits>& pages, bool overwrite,
                         const Progress& progress = {});

} // namespace dp::tools
