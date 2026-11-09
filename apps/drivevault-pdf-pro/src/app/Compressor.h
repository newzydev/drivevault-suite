#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantMap>

#include <atomic>
#include <memory>

namespace dp {

class DocumentController;

// The compress dialog's backend (QML singleton "Compressor"): makes a smaller copy of the open file in a temporary file, tells what changed with real
// numbers, lets the user compare a page before / after, and only then saves it under a name the user picks. Nothing is ever changed in place.
class Compressor : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed FINAL)
    Q_PROPERTY(double progress READ progress NOTIFY changed FINAL)
    Q_PROPERTY(bool hasResult READ hasResult NOTIFY changed FINAL)           // a smaller copy is ready to be compared and saved
    Q_PROPERTY(QVariantMap result READ result NOTIFY changed FINAL)         // see below
    Q_PROPERTY(int beforeId READ beforeId NOTIFY changed FINAL)             // image provider ids of the original and the smaller copy
    Q_PROPERTY(int afterId READ afterId NOTIFY changed FINAL)
    Q_PROPERTY(QString baseName READ baseName NOTIFY changed FINAL)         // file name (without .pdf) to propose

public:
    explicit Compressor(DocumentController* controller, QObject* parent = nullptr);
    ~Compressor() override;

    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    bool hasResult() const { return m_afterId != 0; }
    QVariantMap result() const { return m_result; }
    int beforeId() const { return m_beforeId; }
    int afterId() const { return m_afterId; }
    QString baseName() const;

    // The settings of a level: {dpi, quality, strip, linearize}. "low" only rewrites the structure, "medium" 150 dpi / JPEG 80, "high" 110 dpi / 65.
    Q_INVOKABLE QVariantMap preset(const QString& level) const;
    // options: {dpi, quality, strip, linearize}. "" when started, otherwise "noDocument" | "busy".
    Q_INVOKABLE QString start(const QVariantMap& options);
    Q_INVOKABLE void cancel();
    // Copies the smaller copy to `outPath`. "" when saved, otherwise "noResult" | "exists" | "sameFile" | "write".
    Q_INVOKABLE QString saveAs(const QString& outPath, bool overwrite = false);
    Q_INVOKABLE void discard();                       // forget the smaller copy and delete its temporary file

signals:
    void changed();
    // error: "" | "password" | "file" | "format" | "write" | "unknown" | "noJpeg" | "cancelled"; `result` has the numbers
    void finished(bool ok, const QString& error);

private:
    DocumentController* m_controller;
    bool m_busy = false;
    double m_progress = 0.0;
    int m_beforeId = 0;
    int m_afterId = 0;
    QString m_tempFile;
    QString m_sourcePath;
    QString m_password;
    QString m_name;
    QVariantMap m_result;
    std::shared_ptr<std::atomic_bool> m_cancel;
};

} // namespace dp
