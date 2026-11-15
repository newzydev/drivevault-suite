#pragma once

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace dm::ai {

// Two tools that let the local agent look things up on the Internet: web_search (DuckDuckGo's HTML page, no account, no key) and fetch_page (read one web
// page as text). Only when the user has the switch "Let the AI search the web" on. What leaves this PC: the search words, and the address of a page that is
// opened (never a file, never the conversation). A page must be http(s) on a PUBLIC address: this computer, the local network and link-local addresses are
// refused (also after a redirect), and the size of what is read is limited.
class WebTools : public QObject {
    Q_OBJECT
public:
    explicit WebTools(QNetworkAccessManager* net, QObject* parent = nullptr);
    ~WebTools() override;

    struct SearchHit {
        QString title, url, snippet;
    };

    static QJsonArray specs();
    static bool handles(const QString& tool);                           // web_search | fetch_page
    static QString describe(const QString& tool, const QJsonObject& args);

    // Asynchronous: `done` is called once with the text for the model and whether it is an error. cancel() ends the work quietly (done is not called).
    void run(const QString& tool, const QJsonObject& args, std::function<void(const QString&, bool)> done);
    void cancel();

    // Pure, tested.
    static QList<SearchHit> parseSearch(const QString& html, int maxHits = 6);
    static QString formatHits(const QList<SearchHit>& hits);
    static QString htmlToText(const QString& html, int maxChars);
    static bool addressIsPublic(const QHostAddress& address);
    static QString checkUrl(const QUrl& url);                           // "" when it may be opened, otherwise why not (English)

    static constexpr int kMaxPageBytes = 600000;
    static constexpr int kMaxPageChars = 12000;

private:
    void search(const QString& query);
    void fetch(const QUrl& url, int hop);
    void finish(const QString& text, bool error);

    QNetworkAccessManager* m_net;
    QNetworkReply* m_reply = nullptr;
    std::function<void(const QString&, bool)> m_done;
    int m_token = 0;                                                    // a cancel / new run makes late answers of the old one do nothing
};

} // namespace dm::ai
