#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

namespace dp {

class DocumentController;

// The backend of the accessibility window (QML singleton "Access"): a quick check of the open file and a copy with the title / language set.
// The open file is never changed. Work runs in the background.
class AccessibilityChecker : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed FINAL)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList checks READ checks NOTIFY changed FINAL)    // [{id, status, count, detail}]
    Q_PROPERTY(QString error READ error NOTIFY changed FINAL)           // of the last check: "" | "password" | "file" | "format" | "unknown"
    Q_PROPERTY(QString title READ title NOTIFY changed FINAL)           // the title the file has now
    Q_PROPERTY(QString language READ language NOTIFY changed FINAL)

public:
    explicit AccessibilityChecker(DocumentController* controller, QObject* parent = nullptr);
    ~AccessibilityChecker() override;

    bool busy() const { return m_busy; }
    bool loaded() const { return m_loaded; }
    QVariantList checks() const { return m_checks; }
    QString error() const { return m_error; }
    QString title() const { return m_title; }
    QString language() const { return m_language; }

    Q_INVOKABLE QString check();                          // "" when started, otherwise "noDocument" | "busy"
    Q_INVOKABLE void clear();
    // "" when started, otherwise "noDocument" | "busy" | "exists" | "sameFile" | "nothing"
    Q_INVOKABLE QString saveInfo(const QString& title, const QString& language, const QString& outPath, bool overwrite = false);

signals:
    void changed();
    // error: see tools::AccessFix
    void saveFinished(bool ok, const QString& error, const QString& detail, const QString& path);

private:
    DocumentController* m_controller;
    bool m_busy = false;
    bool m_loaded = false;
    QVariantList m_checks;
    QString m_error;
    QString m_title;
    QString m_language;
};

} // namespace dp
