#include "BrowserApp.h"
#include "ProfileSession.h"
#include "StandInHandler.h"
#include "UrlUtil.h"

#include <QCommandLineParser>
#include <memory>
#include <QDir>
#include <QFontDatabase>
#include <QApplication>
#include <QLockFile>
#include <QIcon>
#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QProcess>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTimer>
#include <QTemporaryDir>
#include <QtWebEngineQuick/QQuickWebEngineProfile>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <qqml.h>

int main(int argc, char* argv[])
{
    dv::br::StandInHandler::registerScheme();                 // "dvres:" - before the application object and the engine
    if (qEnvironmentVariableIsSet("DV_NO_AUDIO"))              // tests and smoke starts are silent (suite rule): a page that plays a sound is muted
        qputenv("QTWEBENGINE_CHROMIUM_FLAGS", qgetenv("QTWEBENGINE_CHROMIUM_FLAGS") + " --mute-audio");
    QtWebEngineQuick::initialize();                           // before the application object
    QApplication app(argc, argv);
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
    QCommandLineOption guestOpt(QStringLiteral("guest-session"), QStringLiteral("Start with a temporary guest profile."));
    QCommandLineOption selfTestOpt(QStringLiteral("selftest"), QStringLiteral("Run the window's own checks (tabs, pin, bookmarks, history, suggestions, find, zoom) on the page given as address, write the results (JSON) to <file>, exit 0 when all passed (diagnostics)."), QStringLiteral("file"));
    QCommandLineOption probeOpt(QStringLiteral("probe"), QStringLiteral("Open the page given as address, wait, read what the page recorded in window.__res and how many requests the ad blocker stopped, write that (JSON) to <file> and exit (diagnostics, tests)."), QStringLiteral("file"));
    QCommandLineOption updateOpt(QStringLiteral("update-lists"), QStringLiteral("Do what the button \"Update filter lists\" does (download the switched-on lists from their official addresses), write the result (JSON) to <file>, exit (diagnostics; downloads only because this option was given)."), QStringLiteral("file"));
    QCommandLineOption surveyOpt(QStringLiteral("survey"), QStringLiteral("Open the page given as address, wait (--wait), look at it (title, text length, frames, walls that talk about ad blockers, scroll lock) and how many requests the ad blocker stopped, write that (JSON) to <file> and exit (diagnostics: tests on real websites, only when the owner has allowed network use)."), QStringLiteral("file"));
    QCommandLineOption waitOpt(QStringLiteral("wait"), QStringLiteral("Milliseconds to wait before --probe / --survey / --screenshot act (default 2500 / 2500 / 3500)."), QStringLiteral("ms"));
    QCommandLineOption pipTestOpt(QStringLiteral("pip-test"), QStringLiteral("Open the page given as address (it plays a video as window.__video), pop the video out into the small window and back (button path and close path), write the checks (JSON) to <file>, exit 0 when all passed (diagnostics)."), QStringLiteral("file"));
    QCommandLineOption mediaTestOpt(QStringLiteral("media-test"), QStringLiteral("Open the page given as address (it loads a video file and an HLS playlist), check that both are noticed, save them through the program's own path into <data>\downloads, write the checks (JSON) to <file>, exit 0 when all passed (diagnostics)."), QStringLiteral("file"));
    parser.addOptions({dataDirOpt, langOpt, shotOpt, sizeOpt, panelOpt, privateOpt, guestOpt, selfTestOpt, probeOpt, updateOpt, surveyOpt, waitOpt, pipTestOpt, mediaTestOpt});
    parser.process(app);

    for (const char* font : {":/qt/qml/DriveVault/Browser/resources/fonts/Inter-Variable.ttf", ":/qt/qml/DriveVault/Browser/resources/fonts/NotoSansThai-Variable.ttf"})
        QFontDatabase::addApplicationFont(QString::fromLatin1(font));
    QFont f(QStringLiteral("Inter"));
    f.setFamilies({QStringLiteral("Inter"), QStringLiteral("Noto Sans Thai"), QStringLiteral("Leelawadee UI"), QStringLiteral("Segoe UI")});
    f.setPixelSize(13);
    app.setFont(f);

    const auto guestDirectory = parser.isSet(guestOpt) ? std::make_unique<QTemporaryDir>() : nullptr;
    if (guestDirectory && !guestDirectory->isValid()) return 1;
    const QString profileRoot = guestDirectory ? guestDirectory->path() : parser.isSet(dataDirOpt)
        ? QDir(parser.value(dataDirOpt)).absolutePath()
        : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/DriveVault/BrowserPro");
    QDir().mkpath(profileRoot);
    const bool diagnostics = parser.isSet(dataDirOpt) && (parser.isSet(shotOpt) || parser.isSet(selfTestOpt)
        || parser.isSet(probeOpt) || parser.isSet(updateOpt) || parser.isSet(surveyOpt)
        || parser.isSet(pipTestOpt) || parser.isSet(mediaTestOpt));
    bool reopenWithProfile = false;
    // Release the guest WebEngine profile and its instance lock before opening the signed-in profile.
    const auto reopen = qScopeGuard([&] {
        if (reopenWithProfile)
            QProcess::startDetached(app.applicationFilePath(), app.arguments().mid(1));
    });
    QLockFile profileLock(profileRoot + QStringLiteral("/browser.lock"));
    profileLock.setStaleLockTime(0);
    if (!diagnostics && !profileLock.tryLock()) return 1;
    dv::br::ProfileSession session(profileRoot, bool(guestDirectory));
    if ((!diagnostics || QFileInfo::exists(profileRoot + QStringLiteral("/account.bin"))) && !session.start()) return 0;
    const QString dataDir = session.directory();
    QDir().mkpath(dataDir);

    // The ad blocker's request filter is made before the profile and the application object of the pages: it must outlive every profile that uses it.
    dv::br::AdBlockInterceptor interceptor;
    dv::br::StandInHandler standIns;

    // The shared profile of the normal windows (a private window has its own, off the record). It lives longer than the QML engine.
    QQuickWebEngineProfile profile;
    profile.setStorageName(QStringLiteral("DriveVaultBrowserPro"));
    profile.setPersistentStoragePath(dataDir + QStringLiteral("/profile"));
    profile.setCachePath(dataDir + QStringLiteral("/cache"));
    profile.setOffTheRecord(false);
    profile.setPersistentCookiesPolicy(QQuickWebEngineProfile::ForcePersistentCookies);
    profile.setPersistentPermissionsPolicy(QQuickWebEngineProfile::PersistentPermissionsPolicy::StoreOnDisk);
    // The engine's own name ("QtWebEngine/x") is left out and the system is named as Chrome does: some sites turn an unknown browser away.
    profile.setHttpUserAgent(profile.httpUserAgent().replace(QRegularExpression(QStringLiteral(" QtWebEngine/[0-9.]+")), QString()).replace(QStringLiteral("Windows NT 6.2"), QStringLiteral("Windows NT 10.0")));

    dv::br::BrowserApp browser(dataDir, &profile, &interceptor, &standIns);
    browser.adblock()->attach(&profile);                       // the request filter and the "dvres:" stand-ins on the shared profile
    browser.adblock()->start();                                // reads the lists that are already on this PC; nothing is downloaded
    browser.downloads()->setFolder(parser.isSet(dataDirOpt) ? dataDir + QStringLiteral("/downloads") : QString());     // a test run never writes to the real Downloads folder
    QObject::connect(&profile, &QQuickWebEngineProfile::downloadRequested, browser.downloads(), &dv::br::DownloadModel::handle);
    if (parser.isSet(langOpt))
        browser.settings()->setLanguage(parser.value(langOpt));
    browser.setInitialUrls(parser.positionalArguments());
    browser.setStartPrivate(parser.isSet(privateOpt));

    QQmlApplicationEngine engine;
    qmlRegisterSingletonInstance("DriveVault.Browser.Backend", 1, 0, "ProfileSession", &session);
    qmlRegisterSingletonInstance("DriveVault.Browser.Backend", 1, 0, "Browser", &browser);
    qmlRegisterUncreatableType<dv::br::BrowserSettings>("DriveVault.Browser.Backend", 1, 0, "BrowserSettings", QStringLiteral("owned by Browser"));
    qmlRegisterUncreatableType<dv::br::BookmarkStore>("DriveVault.Browser.Backend", 1, 0, "BookmarkStore", QStringLiteral("owned by Browser"));
    qmlRegisterUncreatableType<dv::br::HistoryStore>("DriveVault.Browser.Backend", 1, 0, "HistoryStore", QStringLiteral("owned by Browser"));
    qmlRegisterUncreatableType<dv::br::DownloadModel>("DriveVault.Browser.Backend", 1, 0, "DownloadModel", QStringLiteral("owned by Browser"));
    qmlRegisterUncreatableType<dv::br::MediaController>("DriveVault.Browser.Backend", 1, 0, "MediaController", QStringLiteral("owned by Browser"));
    qmlRegisterUncreatableType<dv::br::AdBlockController>("DriveVault.Browser.Backend", 1, 0, "AdBlockController", QStringLiteral("owned by Browser"));
    qmlRegisterUncreatableType<dv::br::ab::ListManager>("DriveVault.Browser.Backend", 1, 0, "ListManager", QStringLiteral("owned by AdBlockController"));
    engine.loadFromModule("DriveVault.Browser", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    QObject* root = engine.rootObjects().constFirst();
    session.enableLock();

    if (!session.locked() && parser.isSet(panelOpt))
        QMetaObject::invokeMethod(root, "showPanel", Q_ARG(QVariant, parser.value(panelOpt)));

    if (!session.locked() && parser.isSet(updateOpt)) {
        const QString file = parser.value(updateOpt);
        auto* lists = browser.adblock()->lists();
        auto finish = [&app, lists, file] {
            QJsonObject o{{QStringLiteral("updated"), lists->updatedCount()}, {QStringLiteral("failed"), lists->failedCount()}, {QStringLiteral("failures"), lists->failures()},
                          {QStringLiteral("totalRules"), lists->totalRules()}, {QStringLiteral("lists"), QJsonArray::fromVariantList(lists->status())}};
            QFile out(file);
            if (out.open(QIODevice::WriteOnly))
                out.write(QJsonDocument(o).toJson());
            app.exit(lists->failedCount() == 0 && lists->updatedCount() > 0 ? 0 : 1);
        };
        QObject::connect(lists, &dv::br::ab::ListManager::updateFinished, &app, [lists, finish, &app](const QString&) {
            if (lists->updatedCount() == 0) { finish(); return; }
            auto* once = new QObject(&app);
            QObject::connect(lists, &dv::br::ab::ListManager::reloaded, once, [finish, once] { once->deleteLater(); finish(); });
        });
        QTimer::singleShot(500, &app, [browserPtr = &browser] { browserPtr->adblock()->updateLists(); });
    }
    const int waitMs = parser.isSet(waitOpt) ? qBound(500, parser.value(waitOpt).toInt(), 120000) : 0;
    const bool inspect = parser.isSet(selfTestOpt) || parser.isSet(probeOpt) || parser.isSet(surveyOpt) || parser.isSet(pipTestOpt) || parser.isSet(mediaTestOpt);
    if (inspect && !session.locked()) {
        QObject* win = root->property("firstWindow").value<QObject*>();
        if (!win)
            return 3;
        const QString file = parser.isSet(selfTestOpt) ? parser.value(selfTestOpt) : parser.isSet(pipTestOpt) ? parser.value(pipTestOpt) : parser.isSet(mediaTestOpt) ? parser.value(mediaTestOpt) : parser.isSet(surveyOpt) ? parser.value(surveyOpt) : parser.value(probeOpt);
        const char* method = parser.isSet(selfTestOpt) ? "selfTest" : parser.isSet(pipTestOpt) ? "pipTest" : parser.isSet(mediaTestOpt) ? "mediaTest" : parser.isSet(surveyOpt) ? "survey" : "probe";
        const int delay = waitMs > 0 ? waitMs : 2500;
        QTimer::singleShot(delay, win, [win, method] { QMetaObject::invokeMethod(win, method); });         // the page has started loading
        auto* poll = new QTimer(&app);
        auto ticks = std::make_shared<int>(0);                  // shared state: the timer outlives this block (a reference to a local here was a dangling reference)
        const QString shotFile = parser.isSet(shotOpt) ? parser.value(shotOpt) : QString();                    // the picture of the page is taken right after the answer (not at the same time: the two disturbed each other)
        QObject::connect(poll, &QTimer::timeout, &app, [&app, win, file, poll, ticks, waitMs, shotFile] {
            const QString result = win->property("selfTestResult").toString();
            if (result.isEmpty() && ++*ticks < 120 + waitMs / 250)       // 30 s (and the wait before it)
                return;
            poll->stop();
            if (!shotFile.isEmpty())
                if (auto* qw = qobject_cast<QQuickWindow*>(win))
                    qw->grabWindow().save(shotFile);
            QFile out(file);
            bool allOk = !result.isEmpty();
            if (allOk)
                for (const QJsonValue& v : QJsonDocument::fromJson(result.toUtf8()).array())
                    allOk = allOk && v.toObject().value(QStringLiteral("ok")).toBool();
            if (out.open(QIODevice::WriteOnly))
                out.write(result.isEmpty() ? QByteArray("[]  (timed out)") : QJsonDocument::fromJson(result.toUtf8()).toJson());
            // close the window like a user does (its pages are taken down in order), then end
            QMetaObject::invokeMethod(win, "close");
            QTimer::singleShot(400, &app, [&app, allOk] { app.exit(allOk ? 0 : 1); });
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
        if (!inspect)                                           // with a probe / survey the picture is taken after its answer (see above)
            QTimer::singleShot(waitMs > 0 ? waitMs : 3500, &app, [&app, window, file] {
                const QImage img = window->grabWindow();
                app.exit(!img.isNull() && img.save(file) ? 0 : 2);
            });
    }
    const int result = app.exec();
    reopenWithProfile = session.restartRequested();
    return result;
}
