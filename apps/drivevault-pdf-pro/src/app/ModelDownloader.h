#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <thread>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace dp::assistant {

// Downloads the language model of the AI assistant (Qwen3.5-4B Q4_K_M, about 2.7 GB, Apache-2.0) from Hugging Face, only when the user presses the button.
// One pinned revision with a fixed size and SHA-256: a damaged or swapped file is deleted and reported. A stopped or failed download keeps its .part file and
// continues where it left off. The checksum is computed on a worker thread.
class ModelDownloader : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed FINAL)
    Q_PROPERTY(double progress READ progress NOTIFY changed FINAL)       // 0..1
    Q_PROPERTY(QString message READ message NOTIFY changed FINAL)        // detail of an error (English), otherwise empty
    Q_PROPERTY(QString code READ code NOTIFY changed FINAL)               // idle | starting | downloading | checking | installed | cancelled | failed (translated by the UI)
    Q_PROPERTY(QString sizeText READ sizeText CONSTANT FINAL)

public:
    explicit ModelDownloader(QObject* parent = nullptr);
    ~ModelDownloader() override;

    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString message() const { return m_message; }
    QString code() const { return m_code; }
    QString sizeText() const { return tr("about 2.7 GB"); }

    static QString defaultUrl();
    static qint64 defaultSize();
    static QString defaultSha256();

    Q_INVOKABLE QString download();                       // "" = started, otherwise why not
    Q_INVOKABLE void cancel();
    // Any file into any folder (tests). `url` may be http on this computer, https or file.
    QString downloadFile(const QString& url, const QString& folder, const QString& name, qint64 size, const QString& sha256);

    static QString sha256Of(const QString& path, const std::atomic_bool* cancel = nullptr);

signals:
    void changed();
    void finished(bool ok, const QString& message);

private:
    void startRequest();
    void onReadyRead();
    void onReplyFinished();
    void verify();
    void fail(const QString& message);
    void setCode(const QString& code, const QString& message = {});

    QNetworkAccessManager* m_net = nullptr;
    QNetworkReply* m_reply = nullptr;
    QFile* m_file = nullptr;
    QString m_url, m_folder, m_name, m_sha;
    qint64 m_size = 0;
    qint64 m_have = 0;
    bool m_busy = false;
    bool m_cancelled = false;
    bool m_rangeChecked = false;
    double m_progress = 0;
    QString m_message, m_code = QStringLiteral("idle");
    std::atomic_bool m_cancelFlag{false};
    std::thread m_worker;
};

} // namespace dp::assistant
