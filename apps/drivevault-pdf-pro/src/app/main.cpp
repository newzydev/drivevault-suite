#include "DocumentController.h"
#include "PageImageProvider.h"
#include "Compressor.h"
#include "EditorModel.h"
#include "FormFiller.h"
#include "OverlayImageProvider.h"
#include "PageListModel.h"
#include "ThaiOcr.h"
#include "pdfengine/PdfiumRuntime.h"

#include "dp/Version.h"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QSysInfo>
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
    QCommandLineOption diagnosticsOption(QStringLiteral("diagnostics-json"),
        QStringLiteral("Write local build diagnostics to a new JSON file and exit."), QStringLiteral("file"));
    parser.addOptions({dataDirOption, langOption, passwordOption, screenshotOption, sizeOption, diagnosticsOption});
    parser.process(app);

    if (parser.isSet(diagnosticsOption)) {
        const QByteArray data = QJsonDocument(QJsonObject{
            {"version", QCoreApplication::applicationVersion()},
            {"build", QString::fromLatin1(dp::version::kBuildNumber)},
            {"qt", QString::fromLatin1(qVersion())},
            {"os", QSysInfo::productType()},
            {"osVersion", QSysInfo::productVersion()},
            {"architecture", QSysInfo::currentCpuArchitecture()},
            {"thaiOcrFilesPresent", dp::thaiOcrAvailable()}
        }).toJson(QJsonDocument::Indented);
        QFile file(parser.value(diagnosticsOption));
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return 3;
        if (file.write(data) != data.size() || !file.flush()) {
            file.remove();
            return 3;
        }
        return 0;
    }

    loadFonts();
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    const QString dataDir = parser.isSet(dataDirOption)
        ? parser.value(dataDirOption)
        : QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/DriveVault/") + QString::fromLatin1(dp::version::kDataFolder);
    dp::DocumentController controller(dataDir);
    if (parser.isSet(langOption))
        controller.setLanguage(parser.value(langOption));

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
    if (parser.isSet(screenshotOption))
        engine.setInitialProperties({{QStringLiteral("rememberWindow"), false}});
    engine.addImageProvider(QStringLiteral("pdf"), new dp::PageImageProvider(&controller));
    engine.addImageProvider(QStringLiteral("ovl"), new dp::OverlayImageProvider(&controller, &editor));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("DriveVault.PdfPro.UI", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    const QStringList files = parser.positionalArguments();
    if (!files.isEmpty()) {
        // Use the same password prompt and error handling as Open PDF and drag-and-drop.
        if (!window || !QMetaObject::invokeMethod(window, "openPath",
                Q_ARG(QVariant, QVariant(files.constFirst())),
                Q_ARG(QVariant, QVariant(parser.value(passwordOption)))))
            return -1;
    }
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
