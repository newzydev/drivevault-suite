#pragma once

#include "pdfengine/PdfDocument.h"

#include <QMutex>
#include <QObject>
#include <QStringList>
#include <QUrl>

#include <memory>

namespace dp {

// The open document and the small amount of state the QML UI needs (singleton "Doc" in DriveVault.PdfPro.Backend).
class DocumentController : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY documentChanged FINAL)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY documentChanged FINAL)
    Q_PROPERTY(QString fileName READ fileName NOTIFY documentChanged FINAL)
    Q_PROPERTY(QString filePath READ filePath NOTIFY documentChanged FINAL)
    Q_PROPERTY(int docId READ docId NOTIFY documentChanged FINAL)                       // changes with every open: part of the image URLs
    Q_PROPERTY(QStringList recentFiles READ recentFiles NOTIFY recentFilesChanged FINAL)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged FINAL)   // "th" | "en"
    Q_PROPERTY(QString version READ version CONSTANT FINAL)
    Q_PROPERTY(QString buildNumber READ buildNumber CONSTANT FINAL)

public:
    explicit DocumentController(const QString& dataDir, QObject* parent = nullptr);

    bool isOpen() const;
    int pageCount() const;
    QString fileName() const;
    QString filePath() const;
    int docId() const { return m_docId; }
    QStringList recentFiles() const { return m_recent; }
    QString language() const { return m_language; }
    void setLanguage(const QString& lang);
    QString version() const;
    QString buildNumber() const;

    // "" when opened, otherwise an error code: "password" (needs / wrong password), "format", "file", "tooLarge", "unknown"
    Q_INVOKABLE QString open(const QString& pathOrUrl, const QString& password = QString());
    Q_INVOKABLE void close();
    Q_INVOKABLE double pageWidthPt(int page) const;
    Q_INVOKABLE double pageHeightPt(int page) const;
    Q_INVOKABLE void removeRecent(const QString& path);
    Q_INVOKABLE QString localPath(const QString& pathOrUrl) const;

    // For the image provider (any thread): the open document if it is document `id`, else null.
    std::shared_ptr<pdf::PdfDocument> documentFor(int id) const;

signals:
    void documentChanged();
    void recentFilesChanged();
    void languageChanged();

private:
    void addRecent(const QString& path);
    void saveRecent();

    mutable QMutex m_mutex;
    std::shared_ptr<pdf::PdfDocument> m_doc;
    int m_docId = 0;
    QString m_dataDir;
    QStringList m_recent;
    QString m_language;
};

} // namespace dp
