#pragma once

#include <QString>

class QQuickWindow;

namespace dv {

class PlaybackEngine;
class ProjectManager;

// Diagnostics (--ui-bench <report.txt>): drives the real window of the open project - wheel scrolling, Ctrl + wheel zoom at a fixed mouse position,
// playback - and writes how smooth it was (frames swapped, frame interval average / 95th percentile / worst) and whether the picture under the mouse
// stayed in place while zooming. Runs on the machine's real renderer. Returns the exit code.
int runUiBench(QQuickWindow* window, ProjectManager* projects, PlaybackEngine* playback, const QString& reportFile);

} // namespace dv
