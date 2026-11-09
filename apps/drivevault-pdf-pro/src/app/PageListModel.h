#pragma once

#include "pdfengine/PdfDocument.h"

#include <QAbstractListModel>
#include <QMarginsF>
#include <QSizeF>
#include <QVariantList>

#include <atomic>
#include <memory>
#include <vector>

namespace dp {

class DocumentController;

// The working list of the page organizer: the pages of the new file, in order. Each entry is a page of a source file (the open file, or a file that
// was added), with an extra rotation and a crop, or a blank page. Nothing is written until save(); every change can be undone.
// QML singleton "Organizer" in DriveVault.PdfPro.Backend.
class PageListModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged FINAL)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged FINAL)
    Q_PROPERTY(bool modified READ modified NOTIFY historyChanged FINAL)           // differs from the list that was loaded
    Q_PROPERTY(bool busy READ busy NOTIFY savingChanged FINAL)
    Q_PROPERTY(double progress READ progress NOTIFY savingChanged FINAL)
    Q_PROPERTY(QString baseName READ baseName NOTIFY countChanged FINAL)           // file name (without .pdf) to propose for the result

public:
    enum Roles {
        SourceIdRole = Qt::UserRole + 1,   // id for the image provider (0 for a blank page)
        PageRole,                          // 0-based page of the source
        RotationRole,                      // extra quarter turns 0..3
        WidthRole,                         // size in points as shown (rotation applied)
        HeightRole,
        LabelRole,                         // "file name · page"
        BlankRole,
        CropRole,                          // {left, top, right, bottom} in points as shown
        FileRole
    };

    explicit PageListModel(DocumentController* controller, QObject* parent = nullptr);
    ~PageListModel() override;

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(m_items.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_items.size()); }
    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    bool modified() const;
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString baseName() const;

    // ---- filling the list ---------------------------------------------------------------------------------------------------
    Q_INVOKABLE void clear();                           // an empty list (used to start a merge)
    Q_INVOKABLE bool loadCurrent();                     // all pages of the current tab; false when no file is open
    // Adds all pages of a file at `insertAt` (-1 = at the end). "" when added, otherwise "password" | "format" | "file" | "tooLarge".
    Q_INVOKABLE QString addFile(const QString& pathOrUrl, const QString& password = QString(), int insertAt = -1);
    Q_INVOKABLE void addBlank(int insertAt);            // a blank page the size of its neighbour (A4 when there is none)

    // ---- editing (all undoable) ---------------------------------------------------------------------------------------------
    Q_INVOKABLE void removeRows(const QVariantList& rows);
    Q_INVOKABLE void rotate(const QVariantList& rows, int turns);           // +1 clockwise
    Q_INVOKABLE void duplicate(const QVariantList& rows);                   // a copy after each page
    Q_INVOKABLE void moveRows(const QVariantList& rows, int to);            // the pages (kept in their order) go together to position `to` of the rest
    Q_INVOKABLE void crop(const QVariantList& rows, double left, double top, double right, double bottom);   // points as shown; zeros remove the crop
    // dragging: one page moves to a new place; one undo step for the whole gesture
    Q_INVOKABLE void beginGesture();
    Q_INVOKABLE void endGesture();
    Q_INVOKABLE void moveOne(int from, int to);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void reset();                           // back to the list that was loaded

    // ---- result ---------------------------------------------------------------------------------------------------------------
    // Writes the pages of `rows` (empty = all) as a new file in the background. "" when started, otherwise "empty" | "busy" | "exists".
    Q_INVOKABLE QString save(const QString& outPath, const QVariantList& rows = {}, bool overwrite = false);
    Q_INVOKABLE void cancelSave();
    Q_INVOKABLE double pageWidthAt(int row) const;
    Q_INVOKABLE double pageHeightAt(int row) const;

signals:
    void countChanged();
    void historyChanged();
    void savingChanged();
    // error: "" | "password" | "format" | "file" | "range" | "exists" | "write" | "unknown" | "cancelled"; warnings: see tools::WriteResult
    void saveFinished(bool ok, const QString& error, const QString& errorFile, const QStringList& warnings, int pages, double bytes, const QString& path);

private:
    struct Source {
        int id = 0;
        std::shared_ptr<pdf::PdfDocument> doc;
        QString path;
        QString password;
        bool owned = false;           // registered by this model (an open tab is not)
        QString name;
    };
    struct Item {
        int source = -1;
        int page = 0;
        int rotation = 0;
        QMarginsF crop;
        bool blank = false;
        QSizeF blankSize = QSizeF(595.276, 841.89);
        bool operator==(const Item& o) const
        {
            return source == o.source && page == o.page && rotation == o.rotation && crop == o.crop && blank == o.blank && blankSize == o.blankSize;
        }
    };

    int addSource(const std::shared_ptr<pdf::PdfDocument>& doc, int id, const QString& path, const QString& password, bool owned);
    void pushUndo();                                      // before a change (not again inside a gesture)
    void applyItems(std::vector<Item> items);             // replaces the list as a reset
    QList<int> sortedRows(const QVariantList& rows) const;   // valid, unique, ascending
    QSizeF shownSize(const Item& item) const;
    void historyTouched();

    DocumentController* m_controller;
    std::vector<Source> m_sources;
    std::vector<Item> m_items;
    std::vector<Item> m_initial;
    std::vector<std::vector<Item>> m_undo;
    std::vector<std::vector<Item>> m_redo;
    bool m_inGesture = false;
    bool m_gestureSnapshotTaken = false;
    bool m_busy = false;
    double m_progress = 0.0;
    std::shared_ptr<std::atomic_bool> m_cancel;
};

} // namespace dp
