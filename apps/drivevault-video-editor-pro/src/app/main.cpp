#include "AppContext.h"

#include "core/AppPaths.h"
#include "core/Logger.h"
#include "dv/core/Version.h"
#include "platform/CrashHandler.h"

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
#include <QTimer>

namespace {

// The UI scale has to be known before QGuiApplication exists, so the stored value is peeked at
// directly. The full, validated load happens later in SettingsManager.
void applyStoredUiScale()
{
    if (qEnvironmentVariableIsSet("QT_SCALE_FACTOR"))
        return;
    QFile f(dv::paths::settingsFile());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject values = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("values")).toObject();
    const double scale = values.value(QStringLiteral("uiScale")).toDouble(1.0);
    if (scale >= 0.75 && scale <= 2.0 && qAbs(scale - 1.0) > 0.001)
        qputenv("QT_SCALE_FACTOR", QByteArray::number(scale));
}

void loadFonts()
{
    for (const char* file : {":/fonts/Inter-Variable.ttf", ":/fonts/NotoSansThai-Variable.ttf"}) {
        if (QFontDatabase::addApplicationFont(QString::fromLatin1(file)) < 0)
            qCWarning(lcApp) << "Cannot load bundled font" << file;
    }
    QFont font(QStringLiteral("Inter"));
    font.setFamilies({QStringLiteral("Inter"), QStringLiteral("Noto Sans Thai"), QStringLiteral("Segoe UI"),
                      QStringLiteral("Leelawadee UI")});
    font.setPixelSize(13);
    QGuiApplication::setFont(font);
}

} // namespace

int main(int argc, char* argv[])
{
    // Pre-scan only --data-dir, which must be honoured before any path is resolved.
    for (int i = 1; i + 1 < argc; ++i) {
        if (QByteArray(argv[i]) == "--data-dir")
            dv::paths::setOverrideRoot(QString::fromLocal8Bit(argv[i + 1]));
    }

    applyStoredUiScale();

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("VideoEditorPro"));
    QGuiApplication::setApplicationDisplayName(QString::fromLatin1(dv::version::kProductName));
    QGuiApplication::setOrganizationName(QString::fromLatin1(dv::version::kOrganization));
    QGuiApplication::setApplicationVersion(QString::fromLatin1(dv::version::kVersion));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QString::fromLatin1(dv::version::kProductName));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("project"), QStringLiteral("Project file (.dvproj) to open."));
    QCommandLineOption dataDirOption(QStringLiteral("data-dir"), QStringLiteral("Store settings, logs and recents in <dir>."), QStringLiteral("dir"));
    QCommandLineOption screenshotOption(QStringLiteral("screenshot"), QStringLiteral("Render the UI to <file> (PNG) and exit (diagnostics)."), QStringLiteral("file"));
    QCommandLineOption screenOption(QStringLiteral("screen"), QStringLiteral("Screen for --screenshot: home, editor, settings, newproject, about."), QStringLiteral("name"), QStringLiteral("home"));
    QCommandLineOption pageOption(QStringLiteral("page"), QStringLiteral("Settings page for --screen settings."), QStringLiteral("page"), QStringLiteral("general"));
    QCommandLineOption workspaceOption(QStringLiteral("workspace"), QStringLiteral("Workspace for --screen editor."), QStringLiteral("name"), QStringLiteral("edit"));
    QCommandLineOption langOption(QStringLiteral("lang"), QStringLiteral("Override UI language for this run: th-TH or en-US."), QStringLiteral("code"));
    QCommandLineOption sizeOption(QStringLiteral("size"), QStringLiteral("Window size WxH for --screenshot."), QStringLiteral("WxH"));
    QCommandLineOption exportOption(QStringLiteral("export"), QStringLiteral("Render the opened project to <file> (.mp4, .mov or .mkv) without showing the UI, then exit."), QStringLiteral("file"));
    QCommandLineOption presetOption(QStringLiteral("preset"), QStringLiteral("Resolution preset for --export: 720p, 1080p, 1440p or 4k."), QStringLiteral("name"), QStringLiteral("1080p"));
    QCommandLineOption selectOption(QStringLiteral("select-clip"), QStringLiteral("Select the clip with this index (0 = first) before --screenshot (diagnostics)."), QStringLiteral("index"));
    parser.addOptions({dataDirOption, screenshotOption, screenOption, pageOption, workspaceOption, langOption, sizeOption, exportOption, presetOption, selectOption});
    parser.process(app);

    dv::logging::init();
    dv::crash::install();
    qCInfo(lcApp) << dv::version::kProductName << dv::version::kVersion << "build" << dv::version::kBuildNumber
                  << "- Qt" << qVersion();

    loadFonts();
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    dv::AppContext ctx;
    ctx.settings.load();
    if (parser.isSet(langOption)) {
        const QString lang = parser.value(langOption);
        ctx.settings.set(QStringLiteral("language"), lang == QLatin1String("th") ? QStringLiteral("th-TH")
                                                     : lang == QLatin1String("en") ? QStringLiteral("en-US") : lang);
    }
    ctx.recents.load();
    ctx.language.apply();
    ctx.trimCache();
    ctx.registerQmlTypes();
    dv::AppController& controller = ctx.controller;
    dv::ProjectManager& projects = ctx.projects;
    dv::SettingsManager& settings = ctx.settings;

    QQmlApplicationEngine engine;
    ctx.attach(&engine);
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] {
        qCCritical(lcUi) << "Failed to create the main window";
        QCoreApplication::exit(-1);
    }, Qt::QueuedConnection);

    engine.loadFromModule("DriveVault.UI", "Main");
    if (engine.rootObjects().isEmpty()) {
        dv::logging::shutdown();
        return -1;
    }

    // Open a project passed on the command line (also used by the file association later).
    if (!parser.positionalArguments().isEmpty()) {
        const QString error = projects.openProject(parser.positionalArguments().constFirst());
        if (error.isEmpty())
            controller.setScreen(QStringLiteral("editor"));
        else
            qCWarning(lcApp) << "Cannot open command line project:" << error;
    }


    // Headless export: DriveVaultVideoEditorPro.exe project.dvproj --export out.mp4 [--preset 1080p]
    if (parser.isSet(exportOption)) {
        auto* win = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
        if (win)
            win->hide();
        if (!projects.isOpen()) {
            qCCritical(lcApp) << "--export needs a project file to open";
            dv::logging::shutdown();
            return 2;
        }
        const QFileInfo target(parser.value(exportOption));
        const QString error = ctx.exporter.start(target.completeBaseName(), target.absolutePath(), target.suffix().toLower(),
                                                 parser.value(presetOption), 1920, 1080, 75, 192, QString());
        if (!error.isEmpty()) {
            qCCritical(lcApp) << "Export not started:" << error;
            dv::logging::shutdown();
            return 2;
        }
        ctx.exporter.waitForFinished(30 * 60 * 1000);
        const bool ok = ctx.exporter.state() == QLatin1String("done");
        qCInfo(lcApp) << "Export" << (ok ? "finished" : "FAILED") << ctx.exporter.outputPath() << ctx.exporter.message()
                      << "encoder" << ctx.exporter.encoderUsed();
        settings.save();
        dv::logging::shutdown();
        return ok ? 0 : 1;
    }

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());

    if (parser.isSet(screenshotOption) && window) {
        const QString screen = parser.value(screenOption);
        controller.setWorkspace(parser.value(workspaceOption));
        if (parser.isSet(selectOption)) {
            const QList<dv::ClipData> clips = projects.timeline()->clips();
            const int index = parser.value(selectOption).toInt();
            if (index >= 0 && index < clips.size())
                projects.timeline()->select(clips.at(index).id);
        }
        if (screen == QLatin1String("editor"))
            controller.setScreen(QStringLiteral("editor"));
        else if (screen == QLatin1String("settings")) {
            controller.setSettingsPage(parser.value(pageOption));
            controller.setSettingsOpen(true);
        } else if (screen == QLatin1String("newproject"))
            controller.setNewProjectOpen(true);
        else if (screen == QLatin1String("about"))
            controller.setAboutOpen(true);
        if (parser.isSet(sizeOption)) {
            const QStringList wh = parser.value(sizeOption).split(QLatin1Char('x'));
            if (wh.size() == 2)
                window->resize(wh.at(0).toInt(), wh.at(1).toInt());
        }
        const QString file = parser.value(screenshotOption);
        QTimer::singleShot(2500, &app, [window, file] {
            const QImage image = window->grabWindow();
            const bool ok = !image.isNull() && image.save(file);
            qCInfo(lcApp) << "Screenshot" << file << (ok ? "saved" : "FAILED") << image.size();
            QCoreApplication::exit(ok ? 0 : 3);
        });
    }

    const int rc = app.exec();
    settings.save();
    qCInfo(lcApp) << "Exit code" << rc;
    dv::logging::shutdown();
    return rc;
}
