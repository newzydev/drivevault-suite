#include "AppContext.h"

#include "core/AppPaths.h"
#include "media/MediaCache.h"
#include "playback/FrameView.h"
#include "timeline/TimelineViewModel.h"

#include <QDir>
#include <QFile>
#include <QQmlEngine>
#include <qqml.h>

namespace dv {

AppContext::AppContext(QObject* parent)
    : QObject(parent)
    , language(&settings)
    , recents(&settings)
    , projects(&settings, &recents)
    , playback(&projects, &settings)
    , exporter(&projects)
    , ai(&projects, &settings)
    , mcp(&projects, &ai, &settings)
    , proxies(&projects, &settings)
{
    // a sign-in or sign-out (here or in another DriveVault program) changes whose recent projects are shown
    connect(&account, &account::GoogleAccount::changed, this, &AppContext::applyAccountScope);
}

void AppContext::load()
{
    settings.load();
    applyAccountScope();
    language.apply();
    trimCache();
}

void AppContext::applyAccountScope() { recents.useAccount(account.scopeKey()); }

void AppContext::trimCache()
{
    QString dir = settings.get(QStringLiteral("cacheLocation")).toString();
    if (dir.isEmpty())
        dir = paths::defaultCacheDir();
    const qint64 limit = qint64(settings.get(QStringLiteral("cacheLimitMb")).toInt()) * 1024 * 1024;
    mediacache::trim(dir, limit);
}

void AppContext::registerQmlTypes()
{
    constexpr const char* uri = "DriveVault.Backend";
    qmlRegisterSingletonInstance(uri, 1, 0, "AppSettings", &settings);
    qmlRegisterSingletonInstance(uri, 1, 0, "Localization", &language);
    qmlRegisterSingletonInstance(uri, 1, 0, "Recents", &recents);
    qmlRegisterSingletonInstance(uri, 1, 0, "Projects", &projects);
    qmlRegisterSingletonInstance(uri, 1, 0, "App", &controller);
    qmlRegisterSingletonInstance(uri, 1, 0, "SysInfo", &systemInfo);
    qmlRegisterSingletonInstance(uri, 1, 0, "WinIntegration", &windowsIntegration);
    qmlRegisterSingletonInstance(uri, 1, 0, "Playback", &playback);
    qmlRegisterSingletonInstance(uri, 1, 0, "ExportJob", &exporter);
    qmlRegisterSingletonInstance(uri, 1, 0, "Ai", &ai);
    qmlRegisterSingletonInstance(uri, 1, 0, "Mcp", &mcp);
    qmlRegisterSingletonInstance(uri, 1, 0, "Proxies", &proxies);
    qmlRegisterSingletonInstance(uri, 1, 0, "Account", &account);
    qmlRegisterType<FrameView>(uri, 1, 0, "FrameView");
    qmlRegisterType<WaveformItem>(uri, 1, 0, "WaveformItem");
    qmlRegisterType<FrameStripItem>(uri, 1, 0, "FrameStripItem");
    qmlRegisterType<TimelineViewModel>(uri, 1, 0, "TimelineViewModel");
    qmlRegisterUncreatableType<TimelineModel>(uri, 1, 0, "TimelineModel", QStringLiteral("owned by Projects"));
    qmlRegisterUncreatableType<MediaLibrary>(uri, 1, 0, "MediaLibrary", QStringLiteral("owned by Projects"));    qmlRegisterUncreatableType<TrackListModel>(uri, 1, 0, "TrackListModel", QStringLiteral("owned by Projects"));
}

void AppContext::attach(QQmlEngine* engine)
{
    connect(&language, &LanguageManager::languageChanged, engine, [this, engine](const QString&) {
        engine->retranslate();
        recents.refresh();
    });
}

} // namespace dv
