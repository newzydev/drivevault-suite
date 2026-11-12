#pragma once

#include "pdfengine/PdfDocument.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <functional>
#include <memory>

namespace dp {

class DocumentController;

// One page of the comparison (pages are paired by position).
struct PageDiff {
    int page = 0;                     // 0-based
    bool inA = false;
    bool inB = false;
    double textSimilarity = 1.0;      // 0..1 by lines of text; 1 = same text
    double pixelDifference = 0.0;     // share of pixels that differ clearly (0..1)
    int removedLines = 0;             // lines of A that B does not have
    int addedLines = 0;               // lines of B that A does not have
    QStringList removedSample;        // a few of them
    QStringList addedSample;
    bool differs() const { return !inA || !inB || removedLines > 0 || addedLines > 0 || pixelDifference > 0.002; }
};

struct CompareResult {
    bool ok = false;
    bool cancelled = false;
    QString error;                    // "" | "password" | "file" | "format" | "tooLarge" | "unknown" | "cancelled"
    int pagesA = 0;
    int pagesB = 0;
    QList<PageDiff> pages;
};

// Compares two documents page by page: the text by lines and the picture of each page (rendered small).
CompareResult comparePdfs(const std::shared_ptr<pdf::PdfDocument>& a, const std::shared_ptr<pdf::PdfDocument>& b, const std::atomic_bool* cancel = nullptr,
                          const std::function<void(int, int)>& progress = {});

// The backend of the compare window (QML singleton "Comparer"): the open file against another file chosen by the user. Nothing is written.
class PdfComparer : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed FINAL)
    Q_PROPERTY(double progress READ progress NOTIFY changed FINAL)
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList pages READ pages NOTIFY changed FINAL)         // [{page, status, similarity, pixels, removed, added, removedSample, addedSample}]
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY changed FINAL)      // {pagesA, pagesB, different, same, error}
public:
    explicit PdfComparer(DocumentController* controller, QObject* parent = nullptr);
    ~PdfComparer() override;
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    bool hasResult() const { return m_has; }
    QVariantList pages() const { return m_pages; }
    QVariantMap summary() const { return m_summary; }
    // "" when started, otherwise "noDocument" | "busy" | "same" (the same file)
    Q_INVOKABLE QString start(const QString& otherPathOrUrl, const QString& password = QString());
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clear();
signals:
    void changed();
    void finished(bool ok, const QString& error);   // error "password": the other file needs its password
private:
    DocumentController* m_controller;
    bool m_busy = false;
    double m_progress = 0.0;
    bool m_has = false;
    QVariantList m_pages;
    QVariantMap m_summary;
    std::shared_ptr<std::atomic_bool> m_cancel;
};

} // namespace dp
