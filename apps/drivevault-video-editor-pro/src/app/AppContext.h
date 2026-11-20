#pragma once

#include "AppController.h"
#include "ai/AiController.h"
#include "export/ExportController.h"
#include "export/ProxyManager.h"
#include "localization/LanguageManager.h"
#include "mcp/McpServer.h"
#include "playback/PlaybackEngine.h"
#include "platform/SystemInfo.h"
#include "platform/WindowsIntegration.h"
#include "project/ProjectManager.h"
#include "project/RecentProjectsModel.h"
#include "settings/SettingsManager.h"
#include "GoogleAccount.h"

#include <QObject>

class QQmlEngine;

namespace dv {

// Owns every backend object the QML UI talks to and exposes them to QML as the
// "DriveVault.Backend" module. Shared by the application and the UI integration tests.
class AppContext : public QObject {
    Q_OBJECT
public:
    explicit AppContext(QObject* parent = nullptr);

    SettingsManager settings;
    LanguageManager language;
    RecentProjectsModel recents;
    ProjectManager projects;
    PlaybackEngine playback;
    ExportController exporter;
    AiController ai;
    McpServer mcp;
    ProxyManager proxies;
    AppController controller;
    SystemInfo systemInfo;
    WindowsIntegration windowsIntegration;
    account::GoogleAccount account;     // the Google account of the whole suite (one shared file, see shared/auth)

    // The recent projects follow the signed-in Google account (see the .cpp); called by load() and when the account changes.
    void applyAccountScope();
    // Loads settings + recents and installs the stored UI language. Call once, before the UI exists.
    void load();
    // Keeps the thumbnail / waveform cache below the configured size limit.
    void trimCache();
    // Registers the backend singletons. Call once per process, before loading QML.
    void registerQmlTypes();
    // Makes the engine re-translate when the language changes.
    void attach(QQmlEngine* engine);
};

} // namespace dv
