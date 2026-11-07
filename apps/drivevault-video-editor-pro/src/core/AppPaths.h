#pragma once

#include <QString>

namespace dv::paths {

// Redirects every per-user directory below `root` (used by tests and portable runs).
// Pass an empty string to restore the default locations.
void setOverrideRoot(const QString& root);

QString dataDir();         // %LOCALAPPDATA%/DriveVault/VideoEditorPro
QString logsDir();         // <dataDir>/logs
QString autosaveDir();     // <dataDir>/autosave
QString defaultCacheDir(); // <dataDir>/cache
QString settingsFile();    // <dataDir>/settings.json
QString recentFile();      // <dataDir>/recent-projects.json
QString defaultProjectsDir(); // Documents/DriveVault Projects
QString defaultExportDir();   // Videos/DriveVault Exports

// Creates `path` (and parents) if needed. Returns false on failure.
bool ensureDir(const QString& path);

} // namespace dv::paths
