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
    Q_PROPERTY(QString userAgent READ userAgent CONSTANT)              // how the browser introduces itself (main.cpp: the engine's own name without the "QtWebEngine/x" part)
    Q_PROPERTY(bool compiledIn READ compiledIn CONSTANT)               // set once (main.cpp), before the page is loaded
public:
    using Done = std::function<void(const QVariantMap&)>;
    static constexpr int kTimeoutMs = 45000;

    explicit BrowserBridge(QObject* parent = nullptr);

    bool viewReady() const { return m_viewReady; }
    void setViewReady(bool ready);
    bool compiledIn() const { return m_compiledIn; }                    // false in the tests: only the program itself (main.cpp) has Qt WebEngine
    void setCompiledIn(bool on) { m_compiledIn = on; }
    QString userAgent() const { return m_userAgent; }
    void setUserAgent(const QString& ua) { m_userAgent = ua; }

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

    // ---- live preview: the page of the AI's work follows the files while the AI writes them ----
    // The agent tells the bridge which web page it just wrote / changed (previewFile: the browser opens it once per job and file); the page asks the bridge to watch
    // the folder of the pages it shows (the html / css / js / svg / json files of that folder are checked twice a second) and reloads when something changed.
    void previewFile(const QString& path);                              // C++ side (the agent): emits previewRequested
    Q_INVOKABLE void watchFolder(const QString& folder);                // QML: start watching (the same folder twice is one)
    Q_INVOKABLE void unwatchAll();
    Q_INVOKABLE QString toLocalFile(const QString& url) const;          // file:///C:/x/y.html -> C:/x/y.html, "" for another address
    // May the AI click, type and drag in a page with this address? Only in pages on this computer (files, localhost): never on a website.
    Q_INVOKABLE bool aiMayInteract(const QString& url) const;
    // Real mouse input into a Qt Quick item (the web view): kind move | press | release | wheel at x, y (item coordinates). The page sees an ordinary trusted event.
    Q_INVOKABLE bool mouse(QObject* item, const QString& kind, qreal x, qreal y, qreal wheelDelta = 0);

    Q_INVOKABLE void reply(int id, const QVariantMap& result);          // QML side: the answer to `command`
    // ---- downloads: a file the user's click started is saved in the Downloads folder (1.28.0: the browser no longer refuses them) ----
    void setDownloadsFolder(const QString& folder) { m_downloadsFolder = folder; }   // diagnostics / tests only (main.cpp, with --data-dir): where downloads go instead of the real Downloads folder
    Q_INVOKABLE QString downloadsFolder() const;                        // the Downloads folder of this user (created when it is missing)
    // A file name that is safe on Windows (no path, no forbidden characters, no device names) and does not overwrite a file of `folder`: "a.pdf" -> "a (2).pdf".
    Q_INVOKABLE QString uniqueDownloadName(const QString& folder, const QString& suggested) const;
    Q_INVOKABLE bool riskyDownload(const QString& name) const;          // a kind of file that can run (exe, msi, bat, ps1, js, lnk ...): the bar warns, nothing is ever opened by the program
    // A picture of the page (an image the QML grab made) as base64 JPEG, at most `maxWidth` wide: small enough for the model's window. "" when it is not an image.
    Q_INVOKABLE QString encodeImage(const QVariant& image, int maxWidth) const;

signals:
    void viewReadyChanged();
    void storagePathChanged();
    void viewWanted();                                                  // the page must show / create the browser view now
    void previewRequested(const QString& url);                          // the AI wrote a web page: show it (file:/// address)
    void liveFilesChanged(const QString& folder);                       // something in a watched folder changed: reload the tabs that show a file of it
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
    QString signatureOf(const QString& folder) const;                   // names, sizes and times of the watched kinds of files of a folder

    QHash<int, Pending> m_pending;
    QList<int> m_order;
    int m_nextId = 1;
    bool m_viewReady = false;
    bool m_compiledIn = false;
    QString m_storagePath;
    QString m_downloadsFolder;
    QString m_userAgent;
    QHash<QString, QString> m_watched;                                  // folder -> signature
    QTimer* m_poll = nullptr;
    bool m_buttonDown = false;
};

} // namespace dm::browser
