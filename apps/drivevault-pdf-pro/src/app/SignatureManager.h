#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace dp {

class DocumentController;

// The backend of the signatures window (QML singleton "Signatures"): the signatures of the open file with their checks, and signing a COPY with a
// certificate file. The open file is never changed. Work runs in the background.
class SignatureManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed FINAL)
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed FINAL)
    Q_PROPERTY(QVariantList signatures READ signatures NOTIFY changed FINAL)   // [{field, signer, reason, location, time, intact, wholeFile, trust, error}]
    Q_PROPERTY(QString error READ error NOTIFY changed FINAL)                  // of the last check: "" | "file" | "format" | "password"

public:
    explicit SignatureManager(DocumentController* controller, QObject* parent = nullptr);
    ~SignatureManager() override;

    bool busy() const { return m_busy; }
    bool loaded() const { return m_loaded; }
    QVariantList signatures() const { return m_signatures; }
    QString error() const { return m_error; }

    Q_INVOKABLE QString check();                          // "" when started, otherwise "noDocument" | "busy"
    Q_INVOKABLE void clear();
    // options: {pfx, pfxPassword, name, reason, location}. "" when started, otherwise "noDocument" | "busy" | "noCertificate" | "exists" | "sameFile".
    Q_INVOKABLE QString sign(const QVariantMap& options, const QString& outPath, bool overwrite = false);

signals:
    void changed();
    // error: see tools::SignResult
    void signFinished(bool ok, const QString& error, const QString& detail, const QString& signer, const QString& path);

private:
    DocumentController* m_controller;
    bool m_busy = false;
    bool m_loaded = false;
    QVariantList m_signatures;
    QString m_error;
};

} // namespace dp
