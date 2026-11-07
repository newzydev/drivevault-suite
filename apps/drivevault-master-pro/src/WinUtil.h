#pragma once

#include <QString>

namespace dm::win {

// Version resource of an .exe / .dll ("ProductName", "ProductVersion", "FileVersion" ...); empty when there is none.
QString versionInfo(const QString& file, const QString& key);

// Starts a program the way Explorer does (a program that needs administrator rights asks for it) and waits for it to end.
// Returns the exit code, or -1 and an error text when it could not be started.
int runAndWait(const QString& file, const QString& arguments, const QString& workingDir, QString* error);

// "1.2.3.4" style comparison: <0, 0, >0.
int compareVersions(const QString& a, const QString& b);

} // namespace dm::win
