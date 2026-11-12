#pragma once

#include "pdftools/Overlay.h"

#include <QColor>
#include <QMutex>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <map>
#include <memory>

namespace dp {

class DocumentController;

// The editor behind the "Edit" tool bar (QML singleton "Editor"): the things the user puts on the pages of each open file (text, pictures, shapes,
// pen, highlights, stamps, watermark, page numbers ...), with undo / redo. Nothing is written until save(); the result is a new file and the open
// file is never changed. Positions are points of the page as shown without view rotation, origin top left.
class EditorModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged FINAL)               // edit mode (tool bar visible)
    Q_PROPERTY(QString tool READ tool WRITE setTool NOTIFY toolChanged FINAL)
    Q_PROPERTY(int selectedId READ selectedId NOTIFY selectionChanged FINAL)
    Q_PROPERTY(int editingId READ editingId NOTIFY editingChanged FINAL)                         // the text item being typed into (not drawn by the overlay)
    Q_PROPERTY(bool polygonOpen READ polygonOpen NOTIFY changed FINAL)                           // corners are being added
    Q_PROPERTY(bool recoverable READ recoverable NOTIFY changed FINAL)                            // unsaved edits of an earlier run were found for the open file
    Q_PROPERTY(int rev READ rev NOTIFY changed FINAL)                                            // changes whenever the overlay of the open file changes
    Q_PROPERTY(int count READ count NOTIFY changed FINAL)                                        // items + marks of the open file
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed FINAL)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed FINAL)
    Q_PROPERTY(bool modified READ modified NOTIFY changed FINAL)                                 // the open file has edits that are not saved
    Q_PROPERTY(bool anyModified READ anyModified NOTIFY changed FINAL)                           // any open file has
    Q_PROPERTY(bool busy READ busy NOTIFY savingChanged FINAL)
    Q_PROPERTY(double progress READ progress NOTIFY savingChanged FINAL)
    Q_PROPERTY(QString pendingImage READ pendingImage WRITE setPendingImage NOTIFY toolChanged FINAL)
    Q_PROPERTY(bool snapEnabled READ snapEnabled WRITE setSnapEnabled NOTIFY snapChanged FINAL)    // moving / resizing sticks to page edges, the page centre and other items
    Q_PROPERTY(QVariantList guides READ guides NOTIFY changed FINAL)                              // [{page, vertical, pos}] shown while an edge is snapped
    Q_PROPERTY(QStringList fonts READ fonts CONSTANT FINAL)
    // the style of the next item (and of the selected item when it is changed)
    Q_PROPERTY(QColor color READ color NOTIFY styleChanged FINAL)
    Q_PROPERTY(QColor fillColor READ fillColor NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool hasFill READ hasFill NOTIFY styleChanged FINAL)
    Q_PROPERTY(double strokeWidth READ strokeWidth NOTIFY styleChanged FINAL)
    Q_PROPERTY(double opacity READ opacity NOTIFY styleChanged FINAL)
    Q_PROPERTY(QString fontFamily READ fontFamily NOTIFY styleChanged FINAL)
    Q_PROPERTY(double fontSize READ fontSize NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool bold READ bold NOTIFY styleChanged FINAL)
    Q_PROPERTY(bool italic READ italic NOTIFY styleChanged FINAL)
    Q_PROPERTY(int align READ align NOTIFY styleChanged FINAL)

public:
    explicit EditorModel(DocumentController* controller, QObject* parent = nullptr);
    ~EditorModel() override;

    bool active() const { return m_active; }
    void setActive(bool on);
    QString tool() const { return m_tool; }
    void setTool(const QString& tool);
    int selectedId() const { return m_selected; }
    int editingId() const { return m_editing; }
    bool polygonOpen() const { return m_poly != 0; }
    bool recoverable() const { return m_recoverable; }
    int rev() const { return m_rev; }
    int count() const;
    bool canUndo() const;
    bool canRedo() const;
    bool modified() const;
    bool anyModified() const;
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString pendingImage() const { return m_pendingImage; }
    void setPendingImage(const QString& pathOrUrl);
    QStringList fonts() const { return m_fonts; }
    QColor color() const { return m_style.color; }
    QColor fillColor() const { return m_style.fill; }
    bool hasFill() const { return m_style.fill.isValid(); }
    double strokeWidth() const { return m_style.width; }
    double opacity() const { return m_style.opacity; }
    QString fontFamily() const { return m_style.font; }
    double fontSize() const { return m_style.fontSize; }
    bool bold() const { return m_style.bold; }
    bool italic() const { return m_style.italic; }
    int align() const { return m_style.align; }
    bool snapEnabled() const { return m_snap; }
    void setSnapEnabled(bool on);
    QVariantList guides() const { return m_guides; }

    // ---- style: sets the next item's style and, when something is selected, changes the selected item (one undo step)
    // key: color | fill | noFill | width | opacity | font | size | bold | italic | align
    Q_INVOKABLE void setStyle(const QString& key, const QVariant& value);

    // ---- selection and hit testing (points)
    Q_INVOKABLE void select(int id);
    Q_INVOKABLE int hitTest(int page, double x, double y) const;
    Q_INVOKABLE int handleAt(int page, double x, double y, double tolerance) const;     // -1 none; see itemInfo().handles
    Q_INVOKABLE QVariantMap itemInfo(int id) const;       // {id, kind, page, x, y, w, h, angle, text, handles:[{x,y}], resizable}
    Q_INVOKABLE QVariantMap selectedInfo() const { return itemInfo(m_selected); }

    // ---- creating
    Q_INVOKABLE int beginDraft(int page, const QString& tool, double x, double y);       // press: starts an item, one undo step until endDraft()
    Q_INVOKABLE void updateDraft(double x, double y);                                     // drag
    Q_INVOKABLE bool endDraft();                                                          // release: false when the item was too small and was dropped
    Q_INVOKABLE QString addImage(int page, const QString& pathOrUrl, double x, double y);  // "" or "format" | "file"
    // polygon: click for each corner (beginPolygon, addPolygonPoint ...), movePolygon follows the pointer, finishPolygon closes it (false: fewer than 3 corners, dropped)
    Q_INVOKABLE int beginPolygon(int page, double x, double y);
    Q_INVOKABLE void addPolygonPoint(double x, double y);
    Q_INVOKABLE void movePolygon(double x, double y);
    Q_INVOKABLE bool finishPolygon();
    Q_INVOKABLE QString replaceImage(int id, const QString& pathOrUrl);                   // swaps the picture of an Image item (centre and width stay); "" or "file" | "format" | "notImage"
    Q_INVOKABLE int addStamp(int page, double x, double y, const QString& text);

    // ---- editing (all undoable)
    Q_INVOKABLE void beginGesture();
    Q_INVOKABLE void endGesture();
    Q_INVOKABLE void moveSelected(double dx, double dy);
    Q_INVOKABLE void dragHandle(int handle, double x, double y);
    Q_INVOKABLE void rotateSelected(double degrees);
    Q_INVOKABLE void beginEdit(int id);                   // the text of this item is typed into a field above it
    Q_INVOKABLE void endEdit();
    Q_INVOKABLE void setText(int id, const QString& text);
    Q_INVOKABLE void setOptions(int id, const QString& text);   // the entries of a drop-down field, separated by commas or lines
    Q_INVOKABLE void deleteSelected();
    Q_INVOKABLE void toFront();
    Q_INVOKABLE void toBack();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void discardEdits();                      // forget all edits of the open file
    Q_INVOKABLE bool modifiedFor(int tabId) const;        // that file has edits that are not saved
    // autosave: the edits of every changed file are written to <data dir>/recovery a moment after each change and found again when the file is opened
    Q_INVOKABLE bool recoverySourceMatches() const;
    Q_INVOKABLE bool restoreRecovery(bool allowUnverifiedSource = false);
    Q_INVOKABLE void declineRecovery();                    // delete them
    Q_INVOKABLE void forgetRecovery();                     // delete the recovery files of all open files (the user chose to quit without saving)

    // ---- on every page: watermark, page numbers, header, footer
    // type: watermark | pageNumber | header | footer. m: {text, font, size, bold, italic, color, opacity, align, from (1 based), to (1 based, 0 = last)}
    Q_INVOKABLE void setMark(const QString& type, const QVariantMap& m);
    Q_INVOKABLE void removeMark(const QString& type);
    Q_INVOKABLE QVariantMap mark(const QString& type) const;      // {} when not set
    Q_INVOKABLE QVariantMap markDefaults(const QString& type) const;

    // ---- result
    // Writes a new file with the edits burned in. "" when started, otherwise "noDocument" | "noEdits" | "busy" | "exists" | "fontRestricted".
    Q_INVOKABLE QString save(const QString& outPath, bool overwrite = false);
    Q_INVOKABLE void cancelSave();
    Q_INVOKABLE QStringList restrictedFontNames() const;

    // For the overlay image provider (any thread): the items of one page, marks included.
    QList<tools::EditItem> itemsForPage(int tabId, int page, const QSizeF& size, int pageCount, int* hiddenId) const;

signals:
    void activeChanged();
    void toolChanged();
    void selectionChanged();
    void editingChanged();
    void styleChanged();
    void snapChanged();
    void changed();
    void savingChanged();
    // error: "" | "password" | "file" | "format" | "range" | "exists" | "write" | "unknown" | "imageMissing" | "fontRestricted" | "cancelled" | "noEdits"
    void saveFinished(bool ok, const QString& error, const QString& detail, const QStringList& warnings, int pages, double bytes, const QString& path);

private:
    struct Snapshot {
        QList<tools::EditItem> items;
        QList<tools::MarkDef> marks;
    };
    struct State {
        Snapshot now;
        std::vector<Snapshot> undo;
        std::vector<Snapshot> redo;
        int nextId = 1;
        bool dirty = false;
        QString path;                   // the file this belongs to (for the recovery file)
        QString sourceStamp;
    };
    State* state();                     // of the open file (created when needed); null when no file is open
    const State* state() const;
    tools::EditItem* find(int id);
    const tools::EditItem* find(int id) const;
    void pushUndo();                    // before a change (once per gesture)
    void touch();                       // after a change
    void applyStyleTo(tools::EditItem& it, const QString& key, const QVariant& value);
    void loadStyleFrom(const tools::EditItem& it);
    tools::EditItem newItem(int page, tools::Kind kind) const;
    void pruneClosedTabs();

    DocumentController* m_controller;
    mutable QRecursiveMutex m_mutex;
    std::map<int, State> m_states;      // key: tab id
    bool m_active = false;
    QString m_tool = QStringLiteral("select");
    int m_selected = 0;
    int m_editing = 0;
    int m_rev = 0;
    bool m_inGesture = false;
    bool m_snap = true;
    QVariantList m_guides;
    QRectF m_rawBounds;                 // where the dragged item would be without snapping
    bool m_rawValid = false;
    QList<double> snapTargets(const tools::EditItem& self, bool xAxis) const;
    // the value (or edge) closest to a target within the tolerance; `guide` gets the target, NaN when nothing is near
    double snapValue(const tools::EditItem& self, bool xAxis, double v, double* guide) const;
    bool m_gestureSnapshot = false;
    int m_draft = 0;
    int m_poly = 0;
    bool m_recoverable = false;
    class QTimer* m_autosave = nullptr;
    QString recoveryPath(const QString& file) const;
    void writeRecovery();
    void checkRecovery();
    QPointF m_draftStart;
    bool m_busy = false;
    double m_progress = 0.0;
    QString m_pendingImage;
    QStringList m_fonts;
    tools::EditItem m_style;            // colours, width, font ... of the next item
    std::shared_ptr<std::atomic_bool> m_cancel;
};

} // namespace dp
