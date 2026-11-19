#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QTimer>
#include <QWebEngineExtensionManager>
#include <QWebEngineExtensionInfo>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestInfo>
#include <QWebEngineView>
#include <atomic>
#include <cstdio>

#include <QBuffer>
#include <QWebEngineUrlScheme>
#include <QWebEngineUrlSchemeHandler>
#include <QWebEngineUrlRequestJob>
struct StubHandler : QWebEngineUrlSchemeHandler {
    void requestStarted(QWebEngineUrlRequestJob *job) override {
        auto *b = new QBuffer(job); b->setData("window.adsbygoogle=[];window.__stub=1;"); b->open(QIODevice::ReadOnly);
        job->reply("text/javascript", b);
    }
};
struct Interceptor : QWebEngineUrlRequestInterceptor {
    std::atomic<int> blocked{0}, redirected{0}, seen{0};
    void interceptRequest(QWebEngineUrlRequestInfo &i) override {
        ++seen;
        const QString p = i.requestUrl().path();
        if (p == QLatin1String("/ads.js") || p.startsWith(QLatin1String("/pagead/"))) { i.block(true); ++blocked; }
        else if (p == QLatin1String("/adsbygoogle.js")) {
            i.redirect(QUrl(QStringLiteral("dvres:abg.js")));
            ++redirected;
        }
    }
};

static QString js(QWebEnginePage *pg, const QString &code, int ms = 3000) {
    QEventLoop loop; QString out = "<timeout>";
    pg->runJavaScript(code, [&](const QVariant &v) { out = v.toString(); loop.quit(); });
    QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); return out;
}
static void wait(int ms) { QEventLoop l; QTimer::singleShot(ms, &l, &QEventLoop::quit); l.exec(); }

int main(int argc, char **argv) {
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-gpu");
    { QWebEngineUrlScheme s("dvres"); s.setSyntax(QWebEngineUrlScheme::Syntax::Path);
      s.setFlags(QWebEngineUrlScheme::SecureScheme | QWebEngineUrlScheme::CorsEnabled | QWebEngineUrlScheme::LocalAccessAllowed);
      QWebEngineUrlScheme::registerScheme(s); }
    QApplication app(argc, argv);
    const QString mode = argc > 1 ? argv[1] : "auto";
    const QString extDir = QDir::current().absoluteFilePath("ext");
    QWebEngineProfile profile("spike", &app);
    profile.setPersistentStoragePath(QDir::current().absoluteFilePath("store"));
    Interceptor icpt; profile.setUrlRequestInterceptor(&icpt);
    StubHandler stub; profile.installUrlSchemeHandler("dvres", &stub);
    // Chrome-like user agent test is separate; keep default here.
    QWebEngineView view; QWebEnginePage page(&profile); view.setPage(&page); view.resize(1100, 800);

    if (mode == "google") {   // manual: look at the sign-in
        page.load(QUrl("https://accounts.google.com/signin")); view.show(); return app.exec();
    }
    QWebEngineExtensionManager *mgr = profile.extensionManager();
    qInfo() << "extensionManager:" << (mgr ? "present" : "NULL");
    if (mgr) {
        QEventLoop l; bool done = false;
        QObject::connect(mgr, &QWebEngineExtensionManager::loadFinished, &l, [&](const QWebEngineExtensionInfo &e) {
            qInfo() << "loadFinished name=" << e.name() << "loaded=" << e.isLoaded() << "enabled=" << e.isEnabled()
                    << "error=" << e.error() << "id=" << e.id(); done = true; l.quit(); });
        if (mode != "install") { mgr->loadExtension(extDir); QTimer::singleShot(8000, &l, &QEventLoop::quit); l.exec(); }
        if (!done) qInfo() << "loadExtension: NO loadFinished within 8 s";
        qInfo() << "extensions listed:" << mgr->extensions().size();
        for (auto &e : mgr->extensions()) qInfo() << "  -" << e.name() << "loaded" << e.isLoaded() << "enabled" << e.isEnabled() << "err" << e.error();
        qInfo() << "installPath:" << mgr->installPath();
        if (mode == "toggle") {   // is setExtensionEnabled itself crashing, or only for ours?
            for (auto &e : mgr->extensions()) if (e.name() == "Google Hangouts") {
                qInfo() << "toggle hangouts off"; std::fflush(stderr); mgr->setExtensionEnabled(e, false); wait(1000); qInfo() << "toggle survived";
            }
        }
        if (mode == "install") {
            QWebEngineExtensionInfo got; bool inst = false; QEventLoop l2;
            QObject::connect(mgr, &QWebEngineExtensionManager::installFinished, &l2, [&](const QWebEngineExtensionInfo &e) {
                qInfo() << "installFinished" << e.name() << "loaded" << e.isLoaded() << "enabled" << e.isEnabled() << "err" << e.error(); got = e; inst = true; l2.quit(); });
            mgr->installExtension(extDir); QTimer::singleShot(8000, &l2, &QEventLoop::quit); l2.exec();
            qInfo() << "installed:" << inst; std::fflush(stderr);
            if (inst) { qInfo() << "calling setExtensionEnabled"; std::fflush(stderr); mgr->setExtensionEnabled(got, true); wait(1500); }
        }
        for (auto &e : mgr->extensions()) qInfo() << "  after enable:" << e.name() << "enabled" << e.isEnabled();
    }
    view.show();
    QEventLoop l; QObject::connect(&page, &QWebEnginePage::loadFinished, &l, &QEventLoop::quit);
    page.load(QUrl("http://127.0.0.1:8765/test.html")); l.exec(); wait(2500);
    qInfo() << "RESULTS:" << js(&page, "JSON.stringify({res:window.__res, stub:window.__stub||null, realAbg:window.__real_adsbygoogle||null, realAds:window.__adsjs||null, baitH:document.getElementById('bait').offsetHeight, extAttr:document.documentElement.getAttribute('data-dv-ext')})");
    qInfo() << "ENV:" << js(&page, "JSON.stringify({ua:navigator.userAgent, pipEnabled:document.pictureInPictureEnabled, h264:document.createElement('video').canPlayType('video/mp4; codecs=\"avc1.42E01E\"'), vp9:document.createElement('video').canPlayType('video/webm; codecs=vp9'), aac:document.createElement('audio').canPlayType('audio/mp4; codecs=\"mp4a.40.2\"'), notif:Notification.permission, widevine:!!navigator.requestMediaKeySystemAccess})");
    qInfo() << "COUNTS: seen" << icpt.seen.load() << "blocked" << icpt.blocked.load() << "redirected" << icpt.redirected.load();
    if (mode == "late" && mgr) {   // enable AFTER a page was loaded (profile fully started)
        for (auto &e : mgr->extensions()) if (e.name() == "DV Spike Ext") {
            qInfo() << "late: enabling"; std::fflush(stderr); mgr->setExtensionEnabled(e, true); wait(1500); qInfo() << "late: survived";
        }
        for (auto &e : mgr->extensions()) qInfo() << "  after:" << e.name() << "enabled" << e.isEnabled();
        QEventLoop l3; QObject::connect(&page, &QWebEnginePage::loadFinished, &l3, &QEventLoop::quit);
        page.load(QUrl("http://127.0.0.1:8765/test.html")); l3.exec(); wait(1500);
        qInfo() << "RESULTS2:" << js(&page, "document.documentElement.getAttribute('data-dv-ext')");
    }
    return 0;
}
