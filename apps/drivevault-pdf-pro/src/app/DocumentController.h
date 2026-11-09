#pragma once

#include "pdfengine/PdfDocument.h"
#include "pdftools/PdfTools.h"

#include <QHash>
#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>
#include <vector>

namespace dp {

// The open documents (tabs) and the state the QML UI needs. QML singleton "Doc" in DriveVault.PdfPro.Backend.
// Everything refers to the current tab unless a document id is given. Pages are 0-based; positions are in points of the page as shown.
class DocumentController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY documentChanged FINAL)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY documentChanged FINAL)
    Q_PROPERTY(QString fileName READ fileName NOTIFY documentChanged FINAL)
    Q_PROPERTY(QString filePath READ filePath NOTIFY documentChanged FINAL)
    Q_PROPERTY(int docId READ docId NOTIFY documentChanged FINAL)                       // id of the current tab: part of the image URLs
    Q_PROPERTY(QVariantList tabs READ tabs NOTIFY tabsChanged FINAL)                    // [{id, title, path}]
    Q_PROPERTY(int currentTab READ currentTab NOTIFY documentChanged FINAL)
    Q_PROPERTY(int rotation READ rotation NOTIFY rotationChanged FINAL)                 // view rotation of the current tab, 0..3
    Q_PROPERTY(bool darkPages READ darkPages WRITE setDarkPages NOTIFY darkPagesChanged FINAL)   // view only: invert the page colours
    Q_PROPERTY(QStringList recentFiles READ recentFiles NOTIFY recentFilesChanged FINAL)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged FINAL)   // "th" | "en"
    Q_PROPERTY(QString version READ version CONSTANT FINAL)
    Q_PROPERTY(QString buildNumber READ buildNumber CONSTANT FINAL)
    // search
    Q_PROPERTY(int searchCount READ searchCount NOTIFY searchChanged FINAL)
    Q_PROPERTY(int searchCurrent READ searchCurrent NOTIFY searchChanged FINAL)         // index of the current hit, -1 none
    Q_PROPERTY(bool searching READ searching NOTIFY searchChanged FINAL)
    Q_PROPERTY(double searchProgress READ searchProgress NOTIFY searchChanged FINAL)    // 0..1
    // selection
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged FINAL)
    // annotations list (loaded in the background by requestAnnotations)
    Q_PROPERTY(QVariantList annotationItems READ annotationItems NOTIFY annotationsChanged FINAL)
    Q_PROPERTY(bool annotationsLoading READ annotationsLoading NOTIFY annotationsChanged FINAL)
    // printing
    Q_PROPERTY(bool printing READ printing NOTIFY printingChanged FINAL)
    Q_PROPERTY(double printProgress READ printProgress NOTIFY printingChanged FINAL)
    // splitting the current file
    Q_PROPERTY(bool splitting READ splitting NOTIFY splitChanged FINAL)
    Q_PROPERTY(double splitProgress READ splitProgress NOTIFY splitChanged FINAL)

public:
    explicit DocumentController(const QString& dataDir, QObject* parent = nullptr);
    ~DocumentController() override;

    bool isOpen() const;
    int pageCount() const;
    QString fileName() const;
    QString filePath() const;
    QString dataDir() const { return m_dataDir; }
    int docId() const;
    QVariantList tabs() const;
    int currentTab() const;
    int rotation() const;
    bool darkPages() const { return m_dark; }
    void setDarkPages(bool dark);
    QStringList recentFiles() const { return m_recent; }
    QString language() const { return m_language; }
    void setLanguage(const QString& lang);
    QString version() const;
    QString buildNumber() const;
    int searchCount() const;
    int searchCurrent() const { return m_searchCurrent; }
    bool searching() const { return m_searching; }
    double searchProgress() const { return m_searchProgress; }
    bool hasSelection() const;
    QVariantList annotationItems() const { return m_annotations; }
    bool annotationsLoading() const { return m_annotationsLoading; }
    bool printing() const { return m_printing; }
    double printProgress() const { return m_printProgress; }
    bool splitting() const { return m_splitting; }
    double splitProgress() const { return m_splitProgress; }

    // ---- documents and tabs -----------------------------------------------------------------------------------------------
    // "" when opened (an already open file just becomes the current tab), otherwise an error code:
    // "password" (needs / wrong password), "format", "file", "tooLarge", "unknown"
    Q_INVOKABLE QString open(const QString& pathOrUrl, const QString& password = QString());
    Q_INVOKABLE void close();                       // closes the current tab
    Q_INVOKABLE void closeTab(int index);
    Q_INVOKABLE void switchTab(int index);
    Q_INVOKABLE void nextTab(int step = 1);
    Q_INVOKABLE double pageWidthPt(int page) const;      // as shown: the view rotation is applied
    Q_INVOKABLE double pageHeightPt(int page) const;
    Q_INVOKABLE void removeRecent(const QString& path);
    Q_INVOKABLE QString localPath(const QString& pathOrUrl) const;
    Q_INVOKABLE void rotateView(int turns);              // +1 = 90 degrees clockwise, -1 = counter clockwise
    // The remembered view of the current file: {page, zoom, zoomMode, viewMode, rotation} (empty map for a file never seen).
    Q_INVOKABLE QVariantMap viewState() const;
    Q_INVOKABLE void saveViewState(const QVariantMap& state);                        // the current tab
    Q_INVOKABLE void saveViewStateFor(int docId, const QVariantMap& state);           // a tab by id (the view may have changed tab since)

    // ---- search -------------------------------------------------------------------------------------------------------------
    Q_INVOKABLE void search(const QString& text, bool matchCase, bool wholeWord, int startPage);
    Q_INVOKABLE void clearSearch();
    Q_INVOKABLE void searchStep(int delta);              // next / previous hit (wraps); emits searchTarget
    Q_INVOKABLE QVariantList searchRects(int page) const;   // [{x, y, w, h, current}]

    // ---- selection and copy -------------------------------------------------------------------------------------------------
    Q_INVOKABLE void selectStart(int page, double x, double y);
    Q_INVOKABLE void selectMove(int page, double x, double y);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE QVariantList selectionRects(int page) const;   // [{x, y, w, h}]
    Q_INVOKABLE QString selectedText() const;
    Q_INVOKABLE bool copySelection() const;               // to the clipboard; false when nothing is selected

    // ---- structure ----------------------------------------------------------------------------------------------------------
    Q_INVOKABLE QVariantList outline() const;             // [{title, page, level}]
    Q_INVOKABLE void requestAnnotations();                // fills annotationItems in the background: [{page, type, text, x, y, w, h}]
    Q_INVOKABLE QVariantMap linkAt(int page, double x, double y) const;   // {kind: none|page|uri, page, uri}
    Q_INVOKABLE bool openUri(const QString& uri) const;   // only http / https / mailto are opened

    // ---- printing -----------------------------------------------------------------------------------------------------------
    Q_INVOKABLE QStringList printerNames() const;
    Q_INVOKABLE QString defaultPrinter() const;
    // options: {printer, outputFile, pages: "all" | "current" | "1-3,5", currentPage (0-based), copies, color, fit}.
    // Returns "" when the job started, otherwise a short error code: "noDocument", "badRange", "busy".
    Q_INVOKABLE QString startPrint(const QVariantMap& options);
    Q_INVOKABLE void cancelPrint();

    // ---- splitting the current file (the file itself is never changed; the parts are new files) -----------------------------
    // mode: "ranges" (param: text such as "1-3,5,8-", one file per part), "every" (param: pages per file), "bookmarks" (one file per top level bookmark).
    // Returns [{name, pages, count}] = the files that would be written; empty when the mode / text is not valid for this file.
    Q_INVOKABLE QVariantList splitPreview(const QString& mode, const QVariant& param) const;
    // options: {mode, text, n, sizeMb, outDir}; mode may also be "size" (each file stays under sizeMb megabytes; measured by writing).
    // Returns "" when started, otherwise: "noDocument", "badRange", "noParts", "noFolder", "busy".
    Q_INVOKABLE QString startSplit(const QVariantMap& options);
    Q_INVOKABLE void cancelSplit();

    // ---- for the page organizer ------------------------------------------------------------------------------------------
    struct SourceInfo {
        int id = 0;
        std::shared_ptr<pdf::PdfDocument> doc;
        QString path;
        QString password;
    };
    SourceInfo currentSource() const;
    // Makes a document known to the image provider under a new id (for the thumbnails of the organizer); unregister when it is not needed any more.
    int registerExtra(std::shared_ptr<pdf::PdfDocument> doc);
    void unregisterExtra(int id);

    // For the image provider and tests (any thread): the open document with this id, else null.
    std::shared_ptr<pdf::PdfDocument> documentFor(int id) const;
    int rotationFor(int id) const;

signals:
    void documentChanged();
    void tabsChanged();
    void rotationChanged();
    void darkPagesChanged();
    void recentFilesChanged();
    void languageChanged();
    void searchChanged();
    void searchTarget(int page, double y);                // scroll here: a hit became the current one
    void selectionChanged();
    void annotationsChanged();
    void printingChanged();
    void printFinished(bool ok, const QString& message);
    void splitChanged();
    void splitFinished(bool ok, const QString& error, const QStringList& files, const QStringList& warnings, const QString& errorFile);

private:
    struct Tab {
        int id = 0;
        std::shared_ptr<pdf::PdfDocument> doc;
        int rotation = 0;
        QVariantMap view;
        QString password;                                     // in memory only: the pages of a protected file are written with it
    };
    struct SelPoint {
        int page = -1;
        int ch = -1;
        bool valid() const { return page >= 0 && ch >= 0; }
    };

    Tab* current();                                       // m_mutex must be held
    const Tab* current() const;
    std::shared_ptr<pdf::PdfDocument> currentDoc() const;
    int currentRotation() const;
    void addRecent(const QString& path);
    void saveRecent();
    void resetPerDocumentState();                         // search, selection, annotations: they belong to one tab
    void stopSearch();
    QString viewKey(const QString& path) const;
    QVariantMap loadSavedView(const QString& path) const;
    bool selectionRange(SelPoint& from, SelPoint& to) const;

    mutable QMutex m_mutex;                               // guards m_tabs and m_currentIndex
    std::vector<Tab> m_tabs;
    int m_currentIndex = -1;
    int m_nextId = 1;
    QString m_dataDir;
    QStringList m_recent;
    QString m_language;
    bool m_dark = false;

    // search state: only touched on the UI thread (the worker hands its results over with queued calls)
    struct Query {
        QString text;
        bool matchCase = false;
        bool wholeWord = false;
        int startPage = 0;
    };
    std::vector<pdf::SearchHit> m_hits;
    QHash<int, QList<int>> m_hitsByPage;                  // page -> indexes into m_hits
    std::shared_ptr<std::atomic_bool> m_searchCancel;
    int m_searchRun = 0;                                  // identifies the current search; results of an older run are dropped
    int m_searchCurrent = -1;
    bool m_searching = false;
    double m_searchProgress = 0.0;
    Query m_lastQuery;

    SelPoint m_selAnchor;
    SelPoint m_selFocus;

    QVariantList m_annotations;
    bool m_annotationsLoading = false;
    int m_annotationsRun = 0;

    bool m_printing = false;
    double m_printProgress = 0.0;
    std::shared_ptr<std::atomic_bool> m_printCancel;

    bool m_splitting = false;
    double m_splitProgress = 0.0;
    std::shared_ptr<std::atomic_bool> m_splitCancel;
    std::map<int, std::shared_ptr<pdf::PdfDocument>> m_extras;      // documents of the organizer (guarded by m_mutex)
};

} // namespace dp
