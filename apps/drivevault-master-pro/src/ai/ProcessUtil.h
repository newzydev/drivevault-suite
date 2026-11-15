#pragma once

#include <QString>

class QProcess;

namespace dm::ai {

// Starts `process` without a console window (call before start()).
void hideConsole(QProcess* process);
// Puts a started process in a Windows job that kills it when this program ends, however it ends (a crash too).
void killWithThisProgram(QProcess* process);
// Looks an executable up on PATH (with the PATHEXT endings); "" when it is not found.
QString findOnPath(const QString& name);

} // namespace dm::ai
