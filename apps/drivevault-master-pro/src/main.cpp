#include "Hub.h"

#include <QCommandLineParser>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTimer>
#include <qqml.h>

int main(int argc, char* argv[])
{
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
    parser.addOptions({dataDir, lang, registry, shot, size, list});
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

    if (parser.isSet(list)) {
        QJsonArray out;
        for (const QVariant& v : hub.products())
            out.append(QJsonObject::fromVariantMap(v.toMap()));
        QFile listFile(parser.value(list));
        return listFile.open(QIODevice::WriteOnly) && listFile.write(QJsonDocument(out).toJson()) > 0 ? 0 : 2;
    }

    QQmlApplicationEngine engine;
    qmlRegisterSingletonInstance("DriveVault.Master.Backend", 1, 0, "Hub", &hub);
    engine.loadFromModule("DriveVault.Master", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;

    if (parser.isSet(shot)) {
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
        if (parser.isSet(size) && window) {
            const QStringList wh = parser.value(size).split(QLatin1Char('x'));
            if (wh.size() == 2)
                window->resize(wh.at(0).toInt(), wh.at(1).toInt());
        }
        const QString file = parser.value(shot);
        QTimer::singleShot(1500, &app, [window, file] {
            const QImage image = window ? window->grabWindow() : QImage();
            QCoreApplication::exit(!image.isNull() && image.save(file) ? 0 : 3);
        });
    }
    return app.exec();
}
