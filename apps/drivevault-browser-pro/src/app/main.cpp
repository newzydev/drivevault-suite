#include "BrowserApp.h"
#include "UrlUtil.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QtWebEngineQuick/QQuickWebEngineProfile>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <qqml.h>

int main(int argc, char* argv[])
{
    QtWebEngineQuick::initialize();                           // before the application object
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("DriveVault"));
    app.setApplicationName(QStringLiteral("DriveVault Browser Pro"));
    app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/DriveVault/Browser/resources/icons/app.png")));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addPositionalArgument(QStringLiteral("url"), QStringLiteral("Addresses to open in tabs."), QStringLiteral("[url...]"));
    QCommandLineOption dataDirOpt(QStringLiteral("data-dir"), QStringLiteral("Keep profile, settings, bookmarks and history in <dir> (tests, portable runs)."), QStringLiteral("dir"));
    QCommandLineOption langOpt(QStringLiteral("lang"), QStringLiteral("Language for this run: th or en."), QStringLiteral("code"));
    QCommandLineOption shotOpt(QStringLiteral("screenshot"), QStringLiteral("Render the window to <file> (PNG) and exit (diagnostics)."), QStringLiteral("file"));
    QCommandLineOption sizeOpt(QStringLiteral("size"), QStringLiteral("Window size WxH for --screenshot."), QStringLiteral("WxH"));
    QCommandLineOption panelOpt(QStringLiteral("panel"), QStringLiteral("Open a side panel: history, bookmarks, downloads or settings (diagnostics)."), QStringLiteral("name"));
    QCommandLineOption privateOpt(QStringLiteral("private"), QStringLiteral("Start in a private window."));
    QCommandLineOption selfTestOpt(QStringLiteral("selftest"), QStringLiteral("Run the window's own checks (tabs, pin, bookmarks, history, suggestions, find, zoom) on the page given as address, write the results (JSON) to <file>, exit 0 when all passed (diagnostics)."), QStringLiteral("file"));
    QCommandLineOption probeOpt(QStringLiteral("probe"), QStringLiteral("Open the page given as address, wait, read what the page recorded in window.__res and how many requests the ad blocker stopped, write that (JSON) to <file> and exit (diagnostics, tests)."), QStringLiteral("file"));
    parser.addOptions({dataDirOpt, langOpt, shotOpt, sizeOpt, panelOpt, privateOpt, selfTestOpt, probeOpt});
    parser.process(app);

    for (const char* font : {":/qt/qml/DriveVault/Browser/resources/fonts/Inter-Variable.ttf", ":/qt/qml/DriveVault/Browser/resources/fonts/NotoSansThai-Variable.ttf"})
        QFontDatabase::addApplicationFont(QString::fromLatin1(font));
    QFont f(QStringLiteral("Inter"));
    f.setFamilies({QStringLiteral("Inter"), QStringLiteral("Noto Sans Thai"), QStringLiteral("Leelawadee UI"), QStringLiteral("Segoe UI")});
    f.setPixelSize(13);
    app.setFont(f);

    const QString dataDir = parser.isSet(dataDirOpt)
        ? QDir(parser.value(dataDirOpt)).absolutePath()
        : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/DriveVault/BrowserPro");
    QDir().mkpath(dataDir);

    // The ad blocker's request filter is made before the profile and the application object of the pages: it must outlive every profile that uses it.
    dv::br::AdBlockInterceptor interceptor;

    // The shared profile of the normal windows (a private window has its own, off the record). It lives longer than the QML engine.
    QQuickWebEngineProfile profile;
    profile.setUrlRequestInterceptor(&interceptor);
    profile.setStorageName(QStringLiteral("DriveVaultBrowserPro"));
    profile.setPersistentStoragePath(dataDir + QStringLiteral("/profile"));
    profile.setCachePath(dataDir + QStringLiteral("/cache"));
    profile.setPersistentCookiesPolicy(QQuickWebEngineProfile::ForcePersistentCookies);
    profile.setPersistentPermissionsPolicy(QQuickWebEngineProfile::PersistentPermissionsPolicy::StoreOnDisk);
    // The engine's own name ("QtWebEngine/x") is left out and the system is named as Chrome does: some sites turn an unknown browser away.
    profile.setHttpUserAgent(profile.httpUserAgent().replace(QRegularExpression(QStringLiteral(" QtWebEngine/[0-9.]+")), QString()).replace(QStringLiteral("Windows NT 6.2"), QStringLiteral("Windows NT 10.0")));

    dv::br::BrowserApp browser(dataDir, &profile, &interceptor);
    browser.adblock()->start();                                // reads the lists that are already on this PC; nothing is downloaded
    browser.downloads()->setFolder(parser.isSet(dataDirOpt) ? dataDir + QStringLiteral("/downloads") : QString());     // a test run never writes to the real Downloads folder
    QObject::connect(&profile, &QQuickWebEngineProfile::downloadRequested, browser.downloads(), &dv::br::DownloadModel::handle);
    if (parser.isSet(langOpt))
        browser.settings()->setLanguage(parser.value(langOpt));
    browser.setInitialUrls(parser.positionalArguments());
    browser.setStartPrivate(parser.isSet(privateOpt));

    QQmlApplicationEngine engine;
    qmlRegisterSingletonInstance("DriveVault.Browser.Backend", 1, 0, "Browser", &browser);
    qmlRegisterUncreatableType<dv::br::BrowserSettings>("DriveVault.Browser.Backend", 1, 0, "BrowserSettings", QStringLiteral("owned by Browser"));
    qmlRegisterUncreatableType<dv::br::BookmarkStore>("DriveVault.Browser.Backend", 1, 0, "BookmarkStore", QStringLiteral("owned by Browser"));
    qmlRegisterUncreatableType<dv::br::HistoryStore>("DriveVault.Browser.Backend", 1, 0, "HistoryStore", QStringLiteral("owned by Browser"));
    qmlRegisterUncreatableType<dv::br::DownloadModel>("DriveVault.Browser.Backend", 1, 0, "DownloadModel", QStringLiteral("owned by Browser"));
    qmlRegisterUncreatableType<dv::br::AdBlockController>("DriveVault.Browser.Backend", 1, 0, "AdBlockController", QStringLiteral("owned by Browser"));
    qmlRegisterUncreatableType<dv::br::ab::ListManager>("DriveVault.Browser.Backend", 1, 0, "ListManager", QStringLiteral("owned by AdBlockController"));
    engine.loadFromModule("DriveVault.Browser", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    QObject* root = engine.rootObjects().constFirst();

    if (parser.isSet(panelOpt))
        QMetaObject::invokeMethod(root, "showPanel", Q_ARG(QVariant, parser.value(panelOpt)));

    if (parser.isSet(selfTestOpt) || parser.isSet(probeOpt)) {
        QObject* win = root->property("firstWindow").value<QObject*>();
        if (!win)
            return 3;
        const QString file = parser.isSet(selfTestOpt) ? parser.value(selfTestOpt) : parser.value(probeOpt);
        const char* method = parser.isSet(selfTestOpt) ? "selfTest" : "probe";
        QTimer::singleShot(2500, win, [win, method] { QMetaObject::invokeMethod(win, method); });         // the page has started loading
        auto* poll = new QTimer(&app);
        int ticks = 0;
        QObject::connect(poll, &QTimer::timeout, &app, [&app, win, file, poll, &ticks] {
            const QString result = win->property("selfTestResult").toString();
            if (result.isEmpty() && ++ticks < 120)       // 120 x 250 ms = 30 s
                return;
            poll->stop();
            QFile out(file);
            bool allOk = !result.isEmpty();
            if (allOk)
                for (const QJsonValue& v : QJsonDocument::fromJson(result.toUtf8()).array())
                    allOk = allOk && v.toObject().value(QStringLiteral("ok")).toBool();
            if (out.open(QIODevice::WriteOnly))
                out.write(result.isEmpty() ? QByteArray("[]  (timed out)") : QJsonDocument::fromJson(result.toUtf8()).toJson());
            app.exit(allOk ? 0 : 1);
        });
        poll->start(250);
    }

    if (parser.isSet(shotOpt)) {
        // diagnostics: wait for the page to draw, save a picture of the window, exit
        auto* window = root->property("firstWindow").value<QQuickWindow*>();
        if (!window)
            return 3;
        if (parser.isSet(sizeOpt)) {
            const QStringList wh = parser.value(sizeOpt).split(QLatin1Char('x'));
            if (wh.size() == 2)
                window->resize(wh[0].toInt(), wh[1].toInt());
        }
        const QString file = parser.value(shotOpt);
        QTimer::singleShot(3500, &app, [&app, window, file] {
            const QImage img = window->grabWindow();
            app.exit(!img.isNull() && img.save(file) ? 0 : 2);
        });
    }
    return app.exec();
}
