#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <atomic>

class QThread;

namespace dv {

class ProjectManager;
class SettingsManager;

// Proxy editing: small H.264 stand-ins for high-resolution video, used by the preview only (the exporter always reads the
// original). Proxies are built on a worker thread, live in <cache>\proxy and are matched to the source by path + size + date.
class ProxyManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged FINAL)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged FINAL)
    Q_PROPERTY(int proxyCount READ proxyCount NOTIFY proxiesChanged FINAL)

public:
    ProxyManager(ProjectManager* project, SettingsManager* settings, QObject* parent = nullptr);
    ~ProxyManager() override;

    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString message() const { return m_message; }
    int proxyCount() const;

    // Existing proxy file for a media file, or "". Cheap enough to call while snapshots are built.
    QString proxyPathFor(const QString& mediaPath) const;
    QString directory() const;

    // "none" (can have one) | "ready" | "building" | "notNeeded" (not video, or small) | "unsupported" (variable frame rate)
    Q_INVOKABLE QString state(const QString& mediaId) const;
    // Builds the proxy of one media item ("" = started) / of every item that is wider than 1280 px (returns how many).
    Q_INVOKABLE QString createProxy(const QString& mediaId);
    Q_INVOKABLE int createMissingProxies();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE qint64 clearProxies();
    Q_INVOKABLE qint64 proxyBytes() const;

    void setMaxWidth(int w) { m_maxWidth = w; }
    void waitForIdle(int timeoutMs = 300000);

signals:
    void busyChanged();
    void progressChanged();
    void messageChanged();
    void proxiesChanged();

private:
    void startNext();
    QString fileFor(const QString& mediaPath) const;

    ProjectManager* m_project;
    SettingsManager* m_settings;
    QThread* m_thread = nullptr;
    std::atomic_bool m_cancel{false};
    bool m_busy = false;
    double m_progress = 0;
    int m_maxWidth = 960;
    QString m_message;
    QStringList m_queue;        // media ids waiting
    QString m_current;          // media id being built
};

} // namespace dv
