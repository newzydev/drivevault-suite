#include "UiBench.h"

#include "core/Logger.h"
#include "playback/PlaybackEngine.h"
#include "project/ProjectManager.h"
#include "timeline/TimelineModel.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QMetaObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTextStream>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>

namespace dv {

namespace {
QQuickItem* findByName(QQuickItem* root, const QString& name)
{
    if (!root)
        return nullptr;
    if (root->objectName() == name)
        return root;
    for (QQuickItem* child : root->childItems())
        if (QQuickItem* hit = findByName(child, name))
            return hit;
    return nullptr;
}

void wait(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void sendWheel(QQuickWindow* window, QPointF at, int deltaY, Qt::KeyboardModifiers modifiers)
{
    QWheelEvent ev(at, window->mapToGlobal(at.toPoint()), QPoint(), QPoint(0, deltaY), Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(window, &ev);
}

struct Stats {
    QString name;
    int frames = 0;
    double seconds = 0;
    double avgMs = 0, p95Ms = 0, maxMs = 0;
    double stepAvgMs = 0, stepMaxMs = 0;       // time the program itself needed to handle one step (events, bindings) before the next frame can start
};

// Runs `step(i)` `steps` times, `stepMs` apart, and measures the frames that the window swapped meanwhile.
Stats measure(QQuickWindow* window, const QString& name, int steps, int stepMs, const std::function<void(int)>& step)
{
    QVector<qint64> swaps;
    QElapsedTimer clock;
    clock.start();
    const QMetaObject::Connection conn = QObject::connect(window, &QQuickWindow::frameSwapped, window, [&] { swaps.append(clock.nsecsElapsed()); });
    QVector<double> costs;
    for (int i = 0; i < steps; ++i) {
        QElapsedTimer cost;
        cost.start();
        step(i);
        QCoreApplication::processEvents();
        costs.append(cost.nsecsElapsed() / 1e6);
        wait(stepMs);
    }
    QObject::disconnect(conn);
    Stats s;
    s.name = name;
    if (!costs.isEmpty()) {
        double sum = 0;
        for (double c : std::as_const(costs)) {
            sum += c;
            s.stepMaxMs = std::max(s.stepMaxMs, c);
        }
        s.stepAvgMs = sum / costs.size();
    }
    s.seconds = clock.nsecsElapsed() / 1e9;
    s.frames = int(swaps.size());
    QVector<double> gaps;
    for (int i = 1; i < swaps.size(); ++i)
        gaps.append((swaps[i] - swaps[i - 1]) / 1e6);
    if (!gaps.isEmpty()) {
        double sum = 0;
        for (double g : std::as_const(gaps))
            sum += g;
        s.avgMs = sum / gaps.size();
        std::sort(gaps.begin(), gaps.end());
        s.p95Ms = gaps[std::min<qsizetype>(gaps.size() - 1, qsizetype(gaps.size() * 0.95))];
        s.maxMs = gaps.last();
    }
    return s;
}
} // namespace

int runUiBench(QQuickWindow* window, ProjectManager* projects, PlaybackEngine* playback, const QString& reportFile)
{
    QString report;
    QTextStream out(&report);
    out << "DriveVault Video Editor Pro - UI bench\n";
    out << "renderer: " << (window->rendererInterface() ? int(window->rendererInterface()->graphicsApi()) : -1) << " (QSGRendererInterface::GraphicsApi)\n";
    out << "window: " << window->width() << " x " << window->height() << "\n";
    if (!projects->isOpen()) {
        out << "ERROR: open a project first\n";
        QFile f(reportFile);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write(report.toUtf8());
        return 2;
    }
    out << "project: " << projects->name() << ", clips " << projects->timeline()->clipCount() << ", " << projects->timeline()->durationFrames() << " frames at "
        << projects->nominalFps() << " fps\n";

    wait(5000);                                                       // let the screen settle (thumbnails, first frames)
    QQuickItem* panel = findByName(window->contentItem(), QStringLiteral("timelinePanel"));
    QQuickItem* lanes = findByName(window->contentItem(), QStringLiteral("timelineLanes"));
    if (!panel || !lanes) {
        out << "ERROR: the timeline is not on screen (workspace Edit, tab Timeline)\n";
        QFile f(reportFile);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write(report.toUtf8());
        return 2;
    }
    const QPointF mouse = lanes->mapToScene(QPointF(lanes->width() * 0.55, lanes->height() * 0.5));
    auto frameUnderMouse = [&] {
        return (lanes->property("contentX").toDouble() + (mouse.x() - lanes->mapToScene(QPointF(0, 0)).x())) / panel->property("pxPerFrame").toDouble();
    };

    QList<Stats> all;
    all << measure(window, QStringLiteral("idle (1 s)"), 1, 1000, [](int) {});
    // plain wheel: the timeline scrolls sideways
    all << measure(window, QStringLiteral("scroll sideways (60 wheel steps, 40 ms apart)"), 60, 40, [&](int) { sendWheel(window, mouse, -120, Qt::NoModifier); });
    for (int i = 0; i < 60; ++i)
        sendWheel(window, mouse, 120, Qt::NoModifier);
    wait(300);

    // Ctrl + wheel at a fixed mouse position: the frame under the mouse must stay under the mouse
    const double startFrame = frameUnderMouse();
    double worstDriftFrames = 0;
    double worstDriftPx = 0;
    all << measure(window, QStringLiteral("zoom in and out (24 steps in, 24 steps out, 60 ms apart)"), 48, 60, [&](int i) {
        sendWheel(window, mouse, i < 24 ? 120 : -120, Qt::ControlModifier);
        QCoreApplication::processEvents();
        const double now = frameUnderMouse();
        const double px = panel->property("pxPerFrame").toDouble();
        worstDriftFrames = std::max(worstDriftFrames, std::abs(now - startFrame));
        worstDriftPx = std::max(worstDriftPx, std::abs(now - startFrame) * px);
    });
    out << "zoom: frame under the mouse before " << startFrame << ", worst drift " << worstDriftFrames << " frames = " << worstDriftPx << " px, zoom now "
        << panel->property("pxPerFrame").toDouble() << " px/frame (min " << panel->property("zoomMin").toDouble() << ", max " << panel->property("zoomMax").toDouble() << ")\n";

    // all the way out and all the way in
    for (int i = 0; i < 120; ++i)
        sendWheel(window, mouse, -120, Qt::ControlModifier);
    wait(500);
    out << "all the way out: " << panel->property("pxPerFrame").toDouble() << " px/frame; the project ends at x = "
        << projects->timeline()->durationFrames() * panel->property("pxPerFrame").toDouble() << " of " << lanes->width() << " px\n";
    for (int i = 0; i < 160; ++i)
        sendWheel(window, mouse, 120, Qt::ControlModifier);
    wait(2500);                                                       // frames decode in the background
    out << "all the way in: " << panel->property("pxPerFrame").toDouble() << " px/frame\n";
    all << measure(window, QStringLiteral("scroll at the deepest zoom (30 wheel steps)"), 30, 60, [&](int) { sendWheel(window, mouse, -120, Qt::NoModifier); });
    for (int i = 0; i < 160; ++i)
        sendWheel(window, mouse, -120, Qt::ControlModifier);
    wait(500);

    // playback
    playback->goToStart();
    wait(300);
    all << measure(window, QStringLiteral("playback (4 s)"), 1, 4000, [&](int) { playback->play(); });
    out << "playback engine: " << playback->playbackFps() << " fps, dropped frames " << playback->droppedFrames() << ", render " << playback->renderMs() << " ms per frame\n";
    playback->pause();
    out << "playhead after playback: frame " << projects->playheadFrame() << "\n";

    out << "\nscenario                                                          frames   fps   avg ms  p95 ms  worst ms  step avg  step max   (step = ms the program needs to handle one input)\n";
    for (const Stats& s : std::as_const(all))
        out << QString::asprintf("%-62s %6d %5.1f %8.1f %7.1f %8.1f %9.1f %9.1f\n", qPrintable(s.name), s.frames, s.seconds > 0 ? s.frames / s.seconds : 0.0, s.avgMs, s.p95Ms,
                                 s.maxMs, s.stepAvgMs, s.stepMaxMs);
    out.flush();
    QFile f(reportFile);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
        f.write(report.toUtf8());
    qCInfo(lcApp) << "UI bench written to" << reportFile;
    return 0;
}

} // namespace dv
