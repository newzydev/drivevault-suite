#include "GoogleAccount.h"
#include "Hub.h"
#include "ai/AiService.h"
#include "ai/MarkdownModel.h"

#include <QCommandLineParser>
#include <functional>
#include <memory>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QUrl>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>
#include <QRegularExpression>
#include <QWebEngineProfile>
#include <qqml.h>

int main(int argc, char* argv[])
{
    QtWebEngineQuick::initialize();                           // the browser tab (Qt WebEngine): before the application object; nothing is started until the tab is opened
    QGuiApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("DriveVault"));
    app.setApplicationName(QStringLiteral("DriveVault Master Pro"));
    app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/DriveVault/Master/resources/icons/app.png")));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption dataDir(QStringLiteral("data-dir"), QStringLiteral("Keep settings in <dir> (tests, portable runs)."), QStringLiteral("dir"));
    QCommandLineOption lang(QStringLiteral("lang"), QStringLiteral("Language for this run: th or en."), QStringLiteral("code"));
    QCommandLineOption registry(QStringLiteral("registry-root"), QStringLiteral("Registry key below HKLM / HKCU that holds the Products (tests)."), QStringLiteral("key"));
    QCommandLineOption shot(QStringLiteral("screenshot"), QStringLiteral("Render the window to <file> (PNG) and exit (diagnostics)."), QStringLiteral("file"));
    QCommandLineOption size(QStringLiteral("size"), QStringLiteral("Window size WxH for --screenshot."), QStringLiteral("WxH"));
    QCommandLineOption list(QStringLiteral("list-products"), QStringLiteral("Write the programs found on this PC as JSON to <file> and exit (diagnostics, installer tests)."), QStringLiteral("file"));
    QCommandLineOption tab(QStringLiteral("tab"), QStringLiteral("Tab shown first: programs, ask or agent (diagnostics, --screenshot)."), QStringLiteral("name"));
    QCommandLineOption aiView(QStringLiteral("ai-view"), QStringLiteral("Open a part of the shown AI tab: settings, model (the model menu), effort (the effort menu) or conversation (the newest saved conversation) (diagnostics, --screenshot)."), QStringLiteral("part"));
    QCommandLineOption browserTest(QStringLiteral("browser-test"), QStringLiteral("With --screenshot: open the HTML <file> in the browser tab and run what the AI's browser tools do (open, read, look); results are written next to the screenshot (diagnostics)."), QStringLiteral("file"));
    QCommandLineOption agentTest(QStringLiteral("agent-test"), QStringLiteral("Run ONE agent job with the local model and write what happened to --agent-out (diagnostics): <prompt>, in --agent-folder, file changes applied at once, no commands."), QStringLiteral("prompt"));
    QCommandLineOption agentFolder(QStringLiteral("agent-folder"), QStringLiteral("Working folder of --agent-test."), QStringLiteral("dir"));
    QCommandLineOption agentOut(QStringLiteral("agent-out"), QStringLiteral("File that gets the result of --agent-test (JSON)."), QStringLiteral("file"));
    QCommandLineOption browserViewport(QStringLiteral("browser-viewport"), QStringLiteral("With --screenshot: open the HTML <file> and test it at other screen sizes like the AI does (browser_viewport: mobile, tablet, laptop, reset); a picture and the answers of each size go next to the screenshot (diagnostics)."), QStringLiteral("file"));
    QCommandLineOption browserInteract(QStringLiteral("browser-interact"), QStringLiteral("With --screenshot: open the HTML <file> and work it like the AI does (click, type, drag, scroll, tabs, live reload); the answers go next to the screenshot (diagnostics)."), QStringLiteral("file"));
    QCommandLineOption browserPreview(QStringLiteral("browser-preview"), QStringLiteral("With --screenshot: ask the browser to show the HTML <file> as if the AI had just written it (the live preview), then take the picture (diagnostics)."), QStringLiteral("file"));
    parser.addOptions({dataDir, lang, registry, shot, size, list, tab, aiView, browserTest, browserInteract, browserViewport, browserPreview, agentTest, agentFolder, agentOut});
    parser.process(app);

    for (const char* font : {":/qt/qml/DriveVault/Master/resources/fonts/Inter-Variable.ttf",
                             ":/qt/qml/DriveVault/Master/resources/fonts/NotoSansThai-Variable.ttf"})
        QFontDatabase::addApplicationFont(QString::fromLatin1(font));
    QFont f(QStringLiteral("Inter"));
    f.setFamilies({QStringLiteral("Inter"), QStringLiteral("Noto Sans Thai"), QStringLiteral("Leelawadee UI"), QStringLiteral("Segoe UI")});
    f.setPixelSize(13);
    app.setFont(f);

    dm::Hub::Options options;
    options.dataDir = parser.value(dataDir);
    if (parser.isSet(registry))
        options.registryRoot = parser.value(registry);
    dm::Hub hub(options);
    if (parser.isSet(lang))
        hub.setLanguage(parser.value(lang));
    hub.setStartCheckEnabled(!parser.isSet(shot) && !parser.isSet(list));      // diagnostics never use the network

    if (parser.isSet(list)) {
        QJsonArray out;
        for (const QVariant& v : hub.products())
            out.append(QJsonObject::fromVariantMap(v.toMap()));
        QFile listFile(parser.value(list));
        return listFile.open(QIODevice::WriteOnly) && listFile.write(QJsonDocument(out).toJson()) > 0 ? 0 : 2;
    }

    dm::ai::AiService ai(&hub);
    {                                                           // the name the engine gives itself, without "QtWebEngine/x.y.z": sign-in pages of some sites turn such a browser away
        QWebEngineProfile probe;
        ai.browserBridge()->setUserAgent(probe.httpUserAgent().replace(QRegularExpression(QStringLiteral(" QtWebEngine/[0-9.]+")), QString()));
    }
    ai.browserBridge()->setCompiledIn(true);                  // this program has the browser (the tests do not)
    ai.browserBridge()->setStoragePath(hub.dataDir() + QStringLiteral("/browser"));
    if (parser.isSet(dataDir))                                 // a test / diagnostic run keeps its downloads in its own data folder, never in the real Downloads folder
        ai.browserBridge()->setDownloadsFolder(hub.dataDir() + QStringLiteral("/downloads"));
    dv::account::GoogleAccount account;                       // the Google account of the whole suite (one shared file, see shared/auth)
    QQmlApplicationEngine engine;
    qmlRegisterSingletonInstance("DriveVault.Master.Backend", 1, 0, "Hub", &hub);
    qmlRegisterSingletonInstance("DriveVault.Master.Backend", 1, 0, "Account", &account);
    qmlRegisterSingletonInstance("DriveVault.Master.Backend", 1, 0, "Ai", &ai);
    qmlRegisterType<dm::ai::MarkdownModel>("DriveVault.Master.Backend", 1, 0, "MarkdownModel");      // one per answer on the page (the blocks of its Markdown)
    engine.loadFromModule("DriveVault.Master", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    if (parser.isSet(tab) && QStringList{QStringLiteral("programs"), QStringLiteral("ask"), QStringLiteral("agent")}.contains(parser.value(tab)))
        engine.rootObjects().constFirst()->setProperty("tab", parser.value(tab));
    else if (parser.value(tab) == QLatin1String("browser"))                       // diagnostics: the Agentic AI page with the browser open at its right
        QMetaObject::invokeMethod(engine.rootObjects().constFirst(), "showBrowser");
    if (parser.isSet(aiView))
        engine.rootObjects().constFirst()->setProperty("diagnosticView", parser.value(aiView));
    if (parser.isSet(agentTest) && parser.isSet(agentFolder) && parser.isSet(agentOut)) {
        // diagnostics: a real job of the agent page (the local model, the real browser view), no clicks. Polls until the job ended, then writes the messages (with their steps).
        auto* channel = ai.agentChannel();
        channel->setFolder(parser.value(agentFolder));
        channel->setPermission(QStringLiteral("edit"));
        const QString prompt = parser.value(agentTest);
        const QString outFile = parser.value(agentOut);
        auto* agentWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
        QTimer::singleShot(1500, &app, [&app, channel, prompt, outFile, agentWindow] {
            const QString started = channel->send(prompt);
            auto* poll = new QTimer(&app);
            auto* clock = new QElapsedTimer;
            clock->start();
            auto ticks = std::make_shared<int>(0);
            QObject::connect(poll, &QTimer::timeout, &app, [&app, channel, outFile, started, poll, clock, agentWindow, ticks] {
                if (++*ticks % 12 == 0 && agentWindow)                            // a picture of the window every 12 seconds: what the user would see while the AI works
                    agentWindow->grabWindow().save(outFile + QStringLiteral(".%1.png").arg(*ticks / 12, 2, 10, QLatin1Char('0')));
                if (started.isEmpty() && channel->busy() && clock->elapsed() < 25 * 60 * 1000)
                    return;
                QJsonObject out{{"started", started}, {"seconds", clock->elapsed() / 1000}, {"messages", QJsonArray::fromVariantList(channel->messages())}};
                QFile f(outFile);
                if (f.open(QIODevice::WriteOnly))
                    f.write(QJsonDocument(out).toJson());
                QCoreApplication::exit(started.isEmpty() ? 0 : 4);
            });
            poll->start(1000);
        });
    }
    if (parser.isSet(shot)) {
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
        if (parser.isSet(size) && window) {
            const QStringList wh = parser.value(size).split(QLatin1Char('x'));
            if (wh.size() == 2)
                window->resize(wh.at(0).toInt(), wh.at(1).toInt());
        }
        const QString file = parser.value(shot);
        const int delay = parser.isSet(browserInteract) || parser.isSet(browserViewport) ? 30000 : parser.isSet(browserTest) ? 9000 : parser.isSet(browserPreview) ? 6000 : 1500;
        if (parser.isSet(browserTest)) {              // diagnostics: what the AI's browser tools do with a real page (open -> read -> look), written next to the screenshot
            const QString page = QFileInfo(parser.value(browserTest)).absoluteFilePath();
            auto* bridge = ai.browserBridge();
            QTimer::singleShot(0, &app, [bridge, page, file] {
                bridge->request(QStringLiteral("open"), {{"url", QUrl::fromLocalFile(page).toString()}}, [bridge, file](const QVariantMap& opened) {
                    QFile o(file + QStringLiteral(".open.txt"));
                    if (o.open(QIODevice::WriteOnly))
                        o.write(QJsonDocument(QJsonObject::fromVariantMap(opened)).toJson());
                    bridge->request(QStringLiteral("read"), {}, [bridge, file](const QVariantMap& read) {
                        QFile r(file + QStringLiteral(".read.txt"));
                        if (r.open(QIODevice::WriteOnly))
                            r.write(read.value(QStringLiteral("text")).toString().toUtf8() + read.value(QStringLiteral("error")).toString().toUtf8());
                        bridge->request(QStringLiteral("look"), {{"scroll", QStringLiteral("top")}}, [file](const QVariantMap& look) {
                            QFile l(file + QStringLiteral(".look.jpg"));
                            if (l.open(QIODevice::WriteOnly))
                                l.write(QByteArray::fromBase64(look.value(QStringLiteral("image")).toString().toLatin1()));
                            QFile t(file + QStringLiteral(".look.txt"));
                            if (t.open(QIODevice::WriteOnly))
                                t.write(look.value(QStringLiteral("text")).toString().toUtf8() + look.value(QStringLiteral("error")).toString().toUtf8());
                        });
                    });
                });
            });
        }
        if (parser.isSet(browserPreview)) {
            const QString page = QFileInfo(parser.value(browserPreview)).absoluteFilePath();
            auto* bridge = ai.browserBridge();
            QTimer::singleShot(800, &app, [bridge, page] { bridge->previewFile(page); });
        }
        if (parser.isSet(browserInteract) || parser.isSet(browserViewport)) {          // diagnostics: open a test page and work it like the AI does (click, type, drag, scroll, tabs, live reload); every answer goes to <screenshot>.steps.txt
            const bool viewportRun = parser.isSet(browserViewport);
            const QString page = QFileInfo(parser.value(viewportRun ? browserViewport : browserInteract)).absoluteFilePath();
            auto* bridge = ai.browserBridge();
            struct Step { QString op; QVariantMap args; int waitAfterMs = 0; };
            const QString pageUrl = QUrl::fromLocalFile(page).toString();
            auto steps = std::make_shared<QList<Step>>(viewportRun ? QList<Step>{
                {QStringLiteral("open"), {{"url", pageUrl}}},
                {QStringLiteral("read"), {}},
                {QStringLiteral("SHOT"), {}, 0},
                {QStringLiteral("viewport"), {{"width", 390}, {"height", 844}, {"name", QStringLiteral("mobile")}}},
                {QStringLiteral("read"), {}},
                {QStringLiteral("look"), {{"scroll", QStringLiteral("top")}}},
                {QStringLiteral("click"), {{"target", QStringLiteral("Menu")}}},
                {QStringLiteral("SHOT"), {}, 0},
                {QStringLiteral("viewport"), {{"width", 820}, {"height", 1180}, {"name", QStringLiteral("tablet")}}},
                {QStringLiteral("read"), {}},
                {QStringLiteral("look"), {{"scroll", QStringLiteral("top")}}},
                {QStringLiteral("SHOT"), {}, 0},
                {QStringLiteral("viewport"), {{"reset", true}}},
                {QStringLiteral("read"), {}},
                {QStringLiteral("SHOT"), {}, 0},
            } : QList<Step>{
                {QStringLiteral("open"), {{"url", QUrl::fromLocalFile(page).toString()}}},
                {QStringLiteral("click"), {{"target", QStringLiteral("Go")}}},
                {QStringLiteral("SHOT"), {}, 0},                                          // a picture of the window while the cursor is still shown
                {QStringLiteral("click"), {{"target", QStringLiteral("#go")}}},
                {QStringLiteral("type"), {{"target", QStringLiteral("#name")}, {"text", QStringLiteral("Somchai")}}},
                {QStringLiteral("drag"), {{"from", QStringLiteral("#handle")}, {"to", QStringLiteral("#track")}}},
                {QStringLiteral("scroll"), {{"direction", QStringLiteral("bottom")}}},
                {QStringLiteral("read"), {}},
                {QStringLiteral("tab"), {{"action", QStringLiteral("list")}, {"index", 0}}},
                {QStringLiteral("MODIFY"), {}, 3000},                                     // change a file of the page on disk: the tab must follow it
                {QStringLiteral("read"), {}},
            });
            auto log = std::make_shared<QString>();
            auto shots = std::make_shared<int>(0);
            auto at = std::make_shared<int>(0);
            auto next = std::make_shared<std::function<void()>>();
            *next = [=, &app]() {
                if (*at >= steps->size()) {
                    QFile f(file + QStringLiteral(".steps.txt"));
                    if (f.open(QIODevice::WriteOnly))
                        f.write(log->toUtf8());
                    return;
                }
                const Step s = steps->at((*at)++);
                if (s.op == QLatin1String("SHOT")) {
                    const QImage image = window ? window->grabWindow() : QImage();
                    image.save(file + (viewportRun ? QStringLiteral(".shot%1.png").arg(++*shots) : QStringLiteral(".cursor.png")));
                    *log += QStringLiteral("== picture of the window saved\n");
                    QTimer::singleShot(0, &app, [next] { (*next)(); });
                    return;
                }
                if (s.op == QLatin1String("MODIFY")) {
                    QFile p(page);
                    if (p.open(QIODevice::ReadOnly)) {
                        QByteArray data = p.readAll();
                        p.close();
                        data.replace("Interact test</h1>", "Interact test CHANGED-BY-FILE</h1>");
                        if (p.open(QIODevice::WriteOnly | QIODevice::Truncate))
                            p.write(data);
                    }
                    *log += QStringLiteral("== file changed on disk\n");
                    QTimer::singleShot(s.waitAfterMs, &app, [next] { (*next)(); });
                    return;
                }
                bridge->request(s.op, s.args, [=, &app](const QVariantMap& r) {
                    QString text = r.value(QStringLiteral("text")).toString() + r.value(QStringLiteral("error")).toString();
                    if (s.op == QLatin1String("read"))
                        text = text.left(viewportRun ? 330 : 500);
                    if (s.op == QLatin1String("look")) {                                         // the picture the model would get
                        QFile l(file + QStringLiteral(".look%1.jpg").arg(++*shots));
                        if (l.open(QIODevice::WriteOnly))
                            l.write(QByteArray::fromBase64(r.value(QStringLiteral("image")).toString().toLatin1()));
                    }
                    *log += QStringLiteral("== %1 %2\n%3\n").arg(s.op, r.value(QStringLiteral("ok")).toBool() ? QStringLiteral("ok") : QStringLiteral("ERROR"), text);
                    QTimer::singleShot(200, &app, [next] { (*next)(); });
                });
            };
            QTimer::singleShot(0, &app, [next] { (*next)(); });
        }
        QTimer::singleShot(delay, &app, [window, file] {
            const QImage image = window ? window->grabWindow() : QImage();
            QCoreApplication::exit(!image.isNull() && image.save(file) ? 0 : 3);
        });
    }
    return app.exec();
}
