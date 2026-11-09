#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace dp {

class DocumentController;

// The fill-in form dialog's backend (QML singleton "Forms"): lists the AcroForm fields of the open file and writes a filled COPY (the open file is
// never changed). Reading and writing run in the background.
class FormFiller : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed FINAL)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed FINAL)
    Q_PROPERTY(bool hasForm READ hasForm NOTIFY changed FINAL)
    Q_PROPERTY(bool xfa READ xfa NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList fields READ fields NOTIFY changed FINAL)         // [{name, type, value, checked, options, readOnly, multiline, page, maxLength}]
    Q_PROPERTY(QString error READ error NOTIFY changed FINAL)                // of the last load: "" | "password" | "file" | "format" | "unknown"

public:
    explicit FormFiller(DocumentController* controller, QObject* parent = nullptr);
    ~FormFiller() override;

    bool busy() const { return m_busy; }
    bool loaded() const { return m_loaded; }
    bool hasForm() const { return m_hasForm; }
    bool xfa() const { return m_xfa; }
    QVariantList fields() const { return m_fields; }
    QString error() const { return m_error; }

    Q_INVOKABLE QString load();                          // "" when started, otherwise "noDocument" | "busy"
    Q_INVOKABLE void clear();
    // values: name -> text / option name / bool. "" when started, otherwise "noDocument" | "busy" | "exists" | "sameFile" | "noValues".
    Q_INVOKABLE QString save(const QVariantMap& values, const QString& outPath, bool overwrite = false);

signals:
    void changed();
    // error: "" | "password" | "file" | "format" | "exists" | "write" | "unknown" | "noForm"; skipped: "name: reason"
    void saveFinished(bool ok, const QString& error, const QString& detail, int filled, const QStringList& skipped, const QStringList& warnings, const QString& path);

private:
    DocumentController* m_controller;
    bool m_busy = false;
    bool m_loaded = false;
    bool m_hasForm = false;
    bool m_xfa = false;
    QVariantList m_fields;
    QString m_error;
};

} // namespace dp
