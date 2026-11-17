#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QUrl>

#include "Version.h"

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("DriveVault Remote Desktop Pro"));
    QGuiApplication::setOrganizationName(QStringLiteral("DriveVault"));
    QGuiApplication::setApplicationVersion(QString::fromLatin1(rd::version::kVersion));
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    engine.loadFromModule("DriveVault.RemoteDesktop", "Main");
    return app.exec();
}
