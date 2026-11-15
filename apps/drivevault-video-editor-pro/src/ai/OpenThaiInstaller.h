#pragma once

#include "ai/AiTypes.h"

#include <QList>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>

#include <atomic>
#include <memory>
#include <thread>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace dv {

// Downloads the OpenThai 2.0 ASR model (fp32 ONNX export, about 2.6 GB) from Hugging Face into <data>\models\openthai-asr - only when the user presses
// the button. The files come from one pinned revision and every file is checked against its SHA-256 before it is used; a damaged or swapped file is
// deleted and reported. A stopped or failed download keeps its .part file and continues where it left off. Hashing and unpacking run on a worker thread.
class OpenThaiInstaller : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed FINAL)
    Q_PROPERTY(double progress READ progress NOTIFY changed FINAL)         // 0..1 over all files
    Q_PROPERTY(QString message READ message NOTIFY changed FINAL)          // what is happening / the last result
    Q_PROPERTY(QString sizeText READ sizeText CONSTANT FINAL)              // "about 2.6 GB"

public:
    struct Item {
        QString name;                      // file name in the model folder (or the zip's name)
        QString url;
        qint64 size = 0;
        QString sha256;                    // lower case hex
        QMap<QString, qint64> unpack;      // a stored (uncompressed) zip: the files to take out of it, name -> size
    };

    explicit OpenThaiInstaller(QObject* parent = nullptr);
    ~OpenThaiInstaller() override;

    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString message() const { return m_message; }
    QString sizeText() const;

    // The pinned model files (revision, sizes and checksums are fixed in the program).
    static QList<Item> defaultItems();

    Q_INVOKABLE QString download();                       // "" = started, otherwise why not
    Q_INVOKABLE void cancel();
    // For tests and tools: any items into any folder.
    QString downloadItems(const QList<Item>& items, const QString& folder);

    // Extracts the wanted files of a stored zip into `outDir` (pure; used by the installer and by tests).
    static bool extractStoredZip(const QString& zipPath, const QString& outDir, const QMap<QString, qint64>& wanted, QString* error);
    static QString sha256Of(const QString& path, const std::atomic_bool* cancel = nullptr);

signals:
    void changed();
    void finished(bool ok, const QString& message);

private:
    bool itemPresent(const Item& item) const;
    void startNext();
    void startItem();
    void onReadyRead();
    void onReplyFinished();
    void verifyAndPlace();
    void fail(const QString& message);
    void succeed();
    void setMessage(const QString& m);
    void updateProgress();
    QString partPath(const Item& item) const;

    QNetworkAccessManager* m_net = nullptr;
    QNetworkReply* m_reply = nullptr;
    QFile* m_file = nullptr;
    QList<Item> m_items;
    QString m_folder;
    int m_index = -1;
    qint64 m_have = 0;                    // bytes of the current item already on disk
    qint64 m_doneBytes = 0;               // bytes of the finished items
    qint64 m_totalBytes = 0;
    bool m_restarted = false;
    bool m_busy = false;
    bool m_cancelled = false;
    double m_progress = 0;
    QString m_message;
    std::atomic_bool m_cancelFlag{false};
    std::thread m_worker;
};

} // namespace dv
