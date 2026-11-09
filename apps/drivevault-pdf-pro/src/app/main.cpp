#include "DocumentController.h"
#include "PageImageProvider.h"
#include "Compressor.h"
#include "EditorModel.h"
#include "FormFiller.h"
#include "OverlayImageProvider.h"
#include "PageListModel.h"
#include "pdfengine/PdfiumRuntime.h"

#include "dp/Version.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTimer>
#include <QtQml/qqml.h>

namespace {

void loadFonts()
{
    for (const char* file : {":/fonts/Inter-Variable.ttf", ":/fonts/NotoSansThai-Variable.ttf"})
        QFontDatabase::addApplicationFont(QString::fromLatin1(file));
    QFont font(QStringLiteral("Inter"));
    font.setFamilies({QStringLiteral("Inter"), QStringLiteral("Noto Sans Thai"), QStringLiteral("Segoe UI"), QStringLiteral("Leelawadee UI")});
    font.setPixelSize(13);
    QGuiApplication::setFont(font);
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    const auto pdfiumShutdown = qScopeGuard(dp::pdf::shutdownPdfium);   // After controllers, renderers and their jobs.
    QGuiApplication::setApplicationName(QStringLiteral("PdfPro"));
    QGuiApplication::setApplicationDisplayName(QString::fromLatin1(dp::version::kProductName));
    QGuiApplication::setOrganizationName(QString::fromLatin1(dp::version::kOrganization));
    QGuiApplication::setApplicationVersion(QString::fromLatin1(dp::version::kVersion));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QString::fromLatin1(dp::version::kProductName));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("file"), QStringLiteral("PDF file to open."));
    QCommandLineOption dataDirOption(QStringLiteral("data-dir"), QStringLiteral("Store settings in <dir>."), QStringLiteral("dir"));
    QCommandLineOption langOption(QStringLiteral("lang"), QStringLiteral("Language for this run: th or en."), QStringLiteral("code"));
    QCommandLineOption passwordOption(QStringLiteral("password"), QStringLiteral("Password for the file to open (diagnostics)."), QStringLiteral("text"));
    QCommandLineOption screenshotOption(QStringLiteral("screenshot"), QStringLiteral("Render the window to <file> (PNG) and exit (diagnostics)."), QStringLiteral("file"));
    QCommandLineOption sizeOption(QStringLiteral("size"), QStringLiteral("Window size WxH for --screenshot."), QStringLiteral("WxH"));
    parser.addOptions({dataDirOption, langOption, passwordOption, screenshotOption, sizeOption});
    parser.process(app);

    loadFonts();
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    const QString dataDir = parser.isSet(dataDirOption)
        ? parser.value(dataDirOption)
        : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/DriveVault/") + QString::fromLatin1(dp::version::kDataFolder);
    dp::DocumentController controller(dataDir);
    if (parser.isSet(langOption))
        controller.setLanguage(parser.value(langOption));

    if (!parser.positionalArguments().isEmpty())
        controller.open(parser.positionalArguments().constFirst(), parser.value(passwordOption));

    dp::PageListModel organizer(&controller);
    dp::Compressor compressor(&controller);
    dp::EditorModel editor(&controller);
    dp::FormFiller forms(&controller);
    qmlRegisterSingletonInstance("DriveVault.PdfPro.Backend", 1, 0, "Doc", &controller);
    qmlRegisterSingletonInstance("DriveVault.PdfPro.Backend", 1, 0, "Organizer", &organizer);
    qmlRegisterSingletonInstance("DriveVault.PdfPro.Backend", 1, 0, "Compressor", &compressor);
    qmlRegisterSingletonInstance("DriveVault.PdfPro.Backend", 1, 0, "Editor", &editor);
    qmlRegisterSingletonInstance("DriveVault.PdfPro.Backend", 1, 0, "Forms", &forms);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("pdf"), new dp::PageImageProvider(&controller));
    engine.addImageProvider(QStringLiteral("ovl"), new dp::OverlayImageProvider(&controller, &editor));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("DriveVault.PdfPro.UI", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    if (window && parser.isSet(sizeOption)) {
        const QStringList wh = parser.value(sizeOption).split(QLatin1Char('x'));
        if (wh.size() == 2)
            window->resize(wh.at(0).toInt(), wh.at(1).toInt());
    }
    // Diagnostics: write a picture of the window and exit (the pages are rendered by then).
    if (window && parser.isSet(screenshotOption)) {
        const QString file = parser.value(screenshotOption);
        QTimer::singleShot(3500, &app, [window, file] {
            const bool ok = window->grabWindow().save(file);
            QCoreApplication::exit(ok ? 0 : 3);
        });
    }

    const int code = app.exec();
    controller.close();
    return code;
}
