#include "ExportController.h"

#include "core/FileUtils.h"
#include "core/Logger.h"
#include "project/ProjectManager.h"
#include "timeline/TimelineModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFileInfo>
#include <QPointer>
#include <QThread>
#include <QUrl>

namespace dv {

ExportController::ExportController(ProjectManager* project, QObject* parent)
    : QObject(parent)
    , m_project(project)
{
    connect(m_project, &ProjectManager::snapshotChanged, this, [this] {
        const auto snap = m_project->snapshot();
        const bool audio = snap && Exporter::hasAudibleAudio(*snap);
        if (audio != m_hasAudio) {
            m_hasAudio = audio;
            emit hasAudioChanged();
        }
    });
}

ExportController::~ExportController()
{
    if (m_thread) {
        m_cancel = true;
        m_thread->wait(30000);
    }
}

QSize ExportController::outputSize(const QString& preset, int customWidth, int customHeight) const
{
    if (preset == QLatin1String("custom"))
        return QSize(std::max(16, customWidth & ~1), std::max(16, customHeight & ~1));

    QSize box(1920, 1080);
    if (preset == QLatin1String("720p"))
        box = QSize(1280, 720);
    else if (preset == QLatin1String("1440p"))
        box = QSize(2560, 1440);
    else if (preset == QLatin1String("4k"))
        box = QSize(3840, 2160);
    const QSize project(std::max(2, m_project->width()), std::max(2, m_project->height()));
    if (project.height() > project.width())
        box.transpose();
    const QSize fit = project.scaled(box, Qt::KeepAspectRatio);
    return QSize(std::max(16, fit.width() & ~1), std::max(16, fit.height() & ~1));
}

QString ExportController::outputFilePath(const QString& name, const QString& folderOrUrl, const QString& container) const
{
    QString folder = ProjectManager::toLocalPath(folderOrUrl).trimmed();
    if (folder.isEmpty())
        folder = QDir::homePath();
    QString base = fileutils::sanitizeFileName(name);
    if (base.isEmpty())
        base = QStringLiteral("export");
    return QDir::cleanPath(folder + QLatin1Char('/') + base + QLatin1Char('.') + container);
}

bool ExportController::fileExists(const QString& path) const
{
    return QFileInfo::exists(path);
}

void ExportController::setRange(const QString& range)
{
    const QString r = range == QLatin1String("inout") ? range : QStringLiteral("all");
    if (r == m_range)
        return;
    m_range = r;
    emit rangeChanged();
}

QVariantList ExportController::rangeFrames() const
{
    const auto snap = m_project->snapshot();
    const qint64 total = snap ? snap->durationFrames : 0;
    qint64 a = 0, b = total;
    if (m_range == QLatin1String("inout")) {
        const TimelineModel* tl = m_project->timeline();
        if (tl->inPoint() >= 0)
            a = std::min(tl->inPoint(), total);
        if (tl->outPoint() >= 0)
            b = std::min(tl->outPoint() + 1, total);   // the Out mark is an inclusive frame
        if (b <= a) {
            a = 0;
            b = total;
        }
    }
    return {a, b};
}

QVariantList ExportController::builtinPresets() const
{
    auto p = [](const char* id, const QString& label, const QString& description, const char* format, const char* preset, int quality, int audio) {
        return QVariant(QVariantMap{{"id", QString::fromLatin1(id)}, {"label", label}, {"description", description},
                                    {"format", QString::fromLatin1(format)}, {"preset", QString::fromLatin1(preset)},
                                    {"quality", quality}, {"audioKbps", audio}});
    };
    return {
        p("youtube1080", tr("YouTube 1080p"), tr("MP4, 1080p, high quality"), "mp4", "1080p", 80, 192),
        p("youtube4k", tr("YouTube 4K"), tr("MP4, 2160p, high quality"), "mp4", "4k", 85, 256),
        p("social", tr("Instagram / TikTok"), tr("MP4, 1080p, balanced - keeps the project aspect ratio"), "mp4", "1080p", 70, 192),
        p("web720", tr("Web 720p (small)"), tr("MP4, 720p, small file"), "mp4", "720p", 55, 128),
        p("archive", tr("Archive (high quality)"), tr("MKV, 1440p, highest quality"), "mkv", "1440p", 100, 320),
        p("audio", tr("Audio only"), tr("M4A (AAC) without a picture"), "m4a", "1080p", 75, 256),
    };
}

QString ExportController::exportFrame(const QString& name, const QString& folderOrUrl, qint64 frame)
{
    const SnapshotPtr snap = m_project->snapshot();
    if (!m_project->isOpen() || !snap || snap->durationFrames <= 0)
        return tr("The timeline is empty. Add clips before exporting.");
    if (fileutils::sanitizeFileName(name).isEmpty())
        return tr("Please enter a valid file name.");
    const QString path = outputFilePath(name, folderOrUrl, QStringLiteral("png"));
    const QString error = Exporter::exportFrame(*snap, frame, QSize(m_project->width() & ~1, m_project->height() & ~1), path);
    if (error.isEmpty()) {
        m_output = path;
        emit stateChanged();
    }
    return error;
}

QString ExportController::estimatedSize(const QString& preset, int customWidth, int customHeight, int quality, int audioKbps, const QString& container) const
{
    const auto snap = m_project->snapshot();
    if (!snap || snap->durationFrames <= 0)
        return {};
    const QSize s = outputSize(preset, customWidth, customHeight);
    const QVariantList r = rangeFrames();
    const double seconds = double(r.at(1).toLongLong() - r.at(0).toLongLong()) / std::max(1.0, snap->fps.toDouble());
    const double kbps = (container == QLatin1String("m4a") ? 0 : Exporter::defaultBitrateKbps(s.width(), s.height(), snap->fps.toDouble(), quality))
                        + (Exporter::hasAudibleAudio(*snap) ? audioKbps : 0);
    const double mb = kbps * seconds / 8.0 / 1000.0;
    return mb >= 1000 ? QStringLiteral("%1 GB").arg(mb / 1000.0, 0, 'f', 1) : QStringLiteral("%1 MB").arg(mb, 0, 'f', mb < 10 ? 1 : 0);
}

void ExportController::detectEncoders()
{
    if (m_detecting || !m_encoders.isEmpty())
        return;
    m_detecting = true;
    emit encodersChanged();
    QPointer<ExportController> self(this);
    QThread* t = QThread::create([self] {
        const QList<EncoderInfo> found = Exporter::availableEncoders();
        QVariantList list;
        for (const EncoderInfo& e : found)
            list.append(QVariantMap{{"id", e.id}, {"label", e.label}, {"hardware", e.hardware}});
        if (self)
            QMetaObject::invokeMethod(self.data(), [self, list] {
                if (!self)
                    return;
                self->m_encoders = list;
                self->m_detecting = false;
                emit self->encodersChanged();
            }, Qt::QueuedConnection);
    });
    connect(t, &QThread::finished, t, &QObject::deleteLater);
    t->start();
}

void ExportController::setState(const QString& state, const QString& message)
{
    m_state = state;
    m_message = message;
    emit stateChanged();
}

QString ExportController::start(const QString& name, const QString& folderOrUrl, const QString& container, const QString& preset,
                                int customWidth, int customHeight, int quality, int audioKbps, const QString& encoderId)
{
    if (m_state == QLatin1String("running"))
        return tr("An export is already running.");
    if (!m_project->isOpen())
        return tr("There is no open project.");
    const SnapshotPtr snap = m_project->snapshot();
    if (!snap || snap->clips.isEmpty())
        return tr("The timeline is empty. Add clips before exporting.");
    if (const QStringList offline = Exporter::offlineMedia(*snap); !offline.isEmpty())
        return tr("Some media files are missing: %1. Relink them first.").arg(offline.join(QStringLiteral(", ")));
    if (fileutils::sanitizeFileName(name).isEmpty())
        return tr("Please enter a valid file name.");

    const QString folder = ProjectManager::toLocalPath(folderOrUrl).trimmed();
    if (folder.isEmpty())
        return tr("Please choose an output folder.");
    if (!QDir().mkpath(folder))
        return tr("Cannot create the output folder.");

    ExportSettings s;
    s.outputPath = outputFilePath(name, folder, container);
    s.container = container;
    const QSize size = outputSize(preset, customWidth, customHeight);
    s.width = size.width();
    s.height = size.height();
    s.quality = quality;
    s.audioBitrateKbps = audioKbps;
    s.encoder = encoderId.isEmpty() ? QStringLiteral("auto") : encoderId;
    s.audioOnly = container == QLatin1String("m4a");
    if (s.audioOnly && !Exporter::hasAudibleAudio(*snap))
        return tr("The sequence has no audible audio to export.");
    const QVariantList r = rangeFrames();
    s.startFrame = r.at(0).toLongLong();
    s.endFrame = r.at(1).toLongLong();

    m_cancel = false;
    m_done = 0;
    m_total = s.endFrame - s.startFrame;
    m_fps = 0;
    m_eta.clear();
    m_result.clear();
    m_encoder.clear();
    m_output = s.outputPath;
    m_lastEmitMs = 0;
    m_clock.start();
    setState(QStringLiteral("running"));
    emit progressChanged();

    QPointer<ExportController> self(this);
    m_thread = QThread::create([self, snap, s, this] {
        const ExportResult result = Exporter::run(snap, s, [self, this](qint64 done, qint64 total) {
            if (!self)
                return;
            const qint64 now = m_clock.elapsed();
            if (done != total && now - m_lastEmitMs < 100)
                return;
            m_lastEmitMs = now;
            QMetaObject::invokeMethod(self.data(), [self, done, total, now] {
                if (!self)
                    return;
                self->m_done = done;
                self->m_total = total;
                const double seconds = double(now) / 1000.0;
                self->m_fps = seconds > 0 ? double(done) / seconds : 0;
                const double remaining = self->m_fps > 0 ? double(total - done) / self->m_fps : 0;
                const int r = int(std::lround(remaining));
                self->m_eta = QStringLiteral("%1:%2").arg(r / 60).arg(r % 60, 2, 10, QLatin1Char('0'));
                emit self->progressChanged();
            }, Qt::QueuedConnection);
        }, m_cancel);
        if (self)
            QMetaObject::invokeMethod(self.data(), [self, result] {
                if (self)
                    self->onFinished(result);
            }, Qt::QueuedConnection);
    });
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
    return {};
}

void ExportController::onFinished(const ExportResult& r)
{
    m_thread = nullptr;
    m_encoder = r.encoder;
    m_fps = r.throughputFps;
    if (r.ok) {
        m_done = m_total;
        const ExportValidation& v = r.validation;
        m_result = {{"ok", true}, {"encoder", r.encoder}, {"seconds", r.seconds}, {"throughputFps", r.throughputFps},
                    {"width", v.width}, {"height", v.height}, {"fps", v.fps}, {"durationMs", v.durationUs / 1000},
                    {"hasAudio", v.hasAudio}, {"audioRate", v.audioRate}, {"videoCodec", v.videoCodec},
                    {"audioCodec", v.audioCodec}, {"fileSize", v.fileSize}};
        emit progressChanged();
        qCInfo(lcProject) << "Export finished:" << r.outputPath << r.encoder << r.throughputFps << "fps";
        setState(QStringLiteral("done"));
    } else if (r.cancelled) {
        setState(QStringLiteral("cancelled"), tr("Export cancelled. No file was written."));
    } else {
        qCWarning(lcProject) << "Export failed:" << r.error;
        setState(QStringLiteral("failed"), r.error);
    }
    emit finished(r.ok);
}

void ExportController::cancel()
{
    if (m_state == QLatin1String("running"))
        m_cancel = true;
}

void ExportController::reset()
{
    if (m_state == QLatin1String("running"))
        return;
    m_done = 0;
    m_total = 0;
    m_result.clear();
    setState(QStringLiteral("idle"));
    emit progressChanged();
}

void ExportController::waitForFinished(int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (m_state == QLatin1String("running") && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
}

} // namespace dv
