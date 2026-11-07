#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;

namespace dv {

class SettingsManager;

// Speech models (ggml files for whisper.cpp): list, import, delete, select, and download on request.
// Models live in the "Model storage location" setting (default: <data>/models). Nothing is downloaded
// unless the user asks for it.
class ModelManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged FINAL)
    Q_PROPERTY(QString selected READ selected NOTIFY modelsChanged FINAL)          // id of the selected model
    Q_PROPERTY(QString directory READ directory NOTIFY modelsChanged FINAL)
    Q_PROPERTY(bool downloading READ downloading NOTIFY downloadChanged FINAL)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadChanged FINAL)
    Q_PROPERTY(QString downloadName READ downloadName NOTIFY downloadChanged FINAL)

public:
    explicit ModelManager(SettingsManager* settings, QObject* parent = nullptr);

    QVariantList models() const;     // [{id, name, description, sizeMb, installed, selected, removable}]
    QString selected() const;
    QString directory() const;
    bool downloading() const { return m_reply != nullptr; }
    double downloadProgress() const { return m_progress; }
    QString downloadName() const { return m_downloadName; }

    // Path of the selected model, or of any installed one when none is selected; empty if there is none.
    QString selectedPath() const;
    QString pathFor(const QString& id) const;

    Q_INVOKABLE QString select(const QString& id);
    Q_INVOKABLE QString importModel(const QString& pathOrUrl);      // copies a ggml-*.bin into the models folder; "" or error
    Q_INVOKABLE QString remove(const QString& id);
    Q_INVOKABLE QString download(const QString& id);                // from the catalog (Hugging Face); "" = started
    QString downloadFrom(const QString& url, const QString& fileName);   // any URL (also file://), used by tests
    Q_INVOKABLE void cancelDownload();

signals:
    void modelsChanged();
    void downloadChanged();
    void downloadFinished(bool ok, const QString& message);

private:
    QStringList searchDirs() const;
    void finishDownload(bool ok, const QString& message);

    SettingsManager* m_settings;
    QNetworkAccessManager* m_net = nullptr;
    QNetworkReply* m_reply = nullptr;
    QFile* m_file = nullptr;
    QString m_target;
    QString m_downloadName;
    double m_progress = 0;
};

} // namespace dv
