#include "Hub.h"
#include "ai/AiService.h"

#include <QCommandLineParser>
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
    parser.addOptions({dataDir, lang, registry, shot, size, list, tab, aiView, browserTest, agentTest, agentFolder, agentOut});
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
    ai.browserBridge()->setCompiledIn(true);                  // this program has the browser (the tests do not)
    ai.browserBridge()->setStoragePath(hub.dataDir() + QStringLiteral("/browser"));
    QQmlApplicationEngine engine;
    qmlRegisterSingletonInstance("DriveVault.Master.Backend", 1, 0, "Hub", &hub);
    qmlRegisterSingletonInstance("DriveVault.Master.Backend", 1, 0, "Ai", &ai);
    engine.loadFromModule("DriveVault.Master", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    if (parser.isSet(tab) && QStringList{QStringLiteral("programs"), QStringLiteral("ask"), QStringLiteral("agent"), QStringLiteral("browser")}.contains(parser.value(tab)))
        engine.rootObjects().constFirst()->setProperty("tab", parser.value(tab));
    if (parser.isSet(aiView))
        engine.rootObjects().constFirst()->setProperty("diagnosticView", parser.value(aiView));
    if (parser.isSet(agentTest) && parser.isSet(agentFolder) && parser.isSet(agentOut)) {
        // diagnostics: a real job of the agent page (the local model, the real browser view), no clicks. Polls until the job ended, then writes the messages (with their steps).
        auto* channel = ai.agentChannel();
        channel->setFolder(parser.value(agentFolder));
        channel->setPermission(QStringLiteral("edit"));
        const QString prompt = parser.value(agentTest);
        const QString outFile = parser.value(agentOut);
        QTimer::singleShot(1500, &app, [&app, channel, prompt, outFile] {
            const QString started = channel->send(prompt);
            auto* poll = new QTimer(&app);
            auto* clock = new QElapsedTimer;
            clock->start();
            QObject::connect(poll, &QTimer::timeout, &app, [&app, channel, outFile, started, poll, clock] {
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
        const int delay = parser.isSet(browserTest) ? 9000 : 1500;
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
        QTimer::singleShot(delay, &app, [window, file] {
            const QImage image = window ? window->grabWindow() : QImage();
            QCoreApplication::exit(!image.isNull() && image.save(file) ? 0 : 3);
        });
    }
    return app.exec();
}
