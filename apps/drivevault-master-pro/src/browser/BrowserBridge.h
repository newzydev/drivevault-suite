#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

#include <functional>

class QTimer;

namespace dm::browser {

// The link between the AI agent and the browser of the program (qml/BrowserView.qml, Qt WebEngine). This class has NO WebEngine code, so it is built and tested
// with the rest of the backend: the agent calls request(op, args, done); the page that owns the real view answers with reply(). While no view exists the
// commands wait (the page is asked to create it with viewWanted()); every command ends after kTimeoutMs with an error, so a job never hangs on the browser.
//
// What the agent may do is only this fixed list (never a script of the model): open | read | look (see the page code).
class BrowserBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool viewReady READ viewReady WRITE setViewReady NOTIFY viewReadyChanged)
    Q_PROPERTY(bool compiledIn READ compiledIn CONSTANT)               // set once (main.cpp), before the page is loaded
public:
    using Done = std::function<void(const QVariantMap&)>;
    static constexpr int kTimeoutMs = 45000;

    explicit BrowserBridge(QObject* parent = nullptr);

    bool viewReady() const { return m_viewReady; }
    void setViewReady(bool ready);
    bool compiledIn() const { return m_compiledIn; }                    // false in the tests: only the program itself (main.cpp) has Qt WebEngine
    void setCompiledIn(bool on) { m_compiledIn = on; }

    // C++ side (the agent). `done` is called exactly once (an answer, a timeout or cancelAll()).
    void request(const QString& op, const QVariantMap& args, Done done);
    void cancelAll();

    // Which addresses the AGENT may open (the user's own address bar is not limited by this):
    // a file inside the working folder, http(s) on this computer (a dev server the agent started), and - only with the web switch on - a public http(s) address.
    // "" = allowed, otherwise why not (English).
    static QString checkAgentUrl(const QUrl& url, const QString& folder, bool webOn);

    Q_PROPERTY(QString storagePath READ storagePath WRITE setStoragePath NOTIFY storagePathChanged)   // where the browser keeps its cookies, cache and remembered permissions ("" = nothing is kept)
    QString storagePath() const { return m_storagePath; }
    void setStoragePath(const QString& path);

    Q_INVOKABLE void reply(int id, const QVariantMap& result);          // QML side: the answer to `command`
    // A picture of the page (an image the QML grab made) as base64 JPEG, at most `maxWidth` wide: small enough for the model's window. "" when it is not an image.
    Q_INVOKABLE QString encodeImage(const QVariant& image, int maxWidth) const;

signals:
    void viewReadyChanged();
    void storagePathChanged();
    void viewWanted();                                                  // the page must show / create the browser view now
    void command(int id, const QString& op, const QVariantMap& args);

private:
    struct Pending {
        QString op;
        QVariantMap args;
        Done done;
        QTimer* timer = nullptr;
        bool sent = false;
    };
    void flush();
    void finish(int id, const QVariantMap& result);

    QHash<int, Pending> m_pending;
    QList<int> m_order;
    int m_nextId = 1;
    bool m_viewReady = false;
    bool m_compiledIn = false;
    QString m_storagePath;
};

} // namespace dm::browser
