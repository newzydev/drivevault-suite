#include "ProjectFile.h"

#include "core/FileUtils.h"
#include "core/Logger.h"
#include "dv/core/Version.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace dv {

namespace {
constexpr int kMinDimension = 64;
constexpr int kMaxDimension = 16384;

QJsonObject trackToJson(const TrackInfo& t)
{
    return {{"id", t.id}, {"name", t.name}, {"kind", t.kind}, {"muted", t.muted}, {"locked", t.locked},
            {"visible", t.visible}, {"gainDb", t.gainDb}, {"pan", t.pan}, {"solo", t.solo}, {"lane", t.lane}};
}

QJsonObject mediaToJson(const MediaRef& m)
{
    const MediaInfo& i = m.info;
    return {{"id", m.id}, {"path", m.path}, {"relativePath", m.relativePath}, {"name", m.name}, {"kind", i.kind},
            {"durationUs", double(i.durationUs)}, {"hasVideo", i.hasVideo}, {"hasAudio", i.hasAudio},
            {"width", i.width}, {"height", i.height}, {"fpsNum", double(i.fpsNum)}, {"fpsDen", double(i.fpsDen)},
            {"variableFrameRate", i.variableFrameRate}, {"audioRate", i.audioRate}, {"audioChannels", i.audioChannels},
            {"videoCodec", i.videoCodec}, {"audioCodec", i.audioCodec}, {"fileSize", double(i.fileSize)},
            {"modifiedMs", double(i.modifiedMs)}};
}

bool mediaFromJson(const QJsonObject& o, MediaRef* out)
{
    MediaRef m;
    m.id = o.value("id").toString();
    m.path = o.value("path").toString();
    m.relativePath = o.value("relativePath").toString();
    m.name = o.value("name").toString();
    MediaInfo& i = m.info;
    i.kind = o.value("kind").toString();
    i.durationUs = qint64(o.value("durationUs").toDouble());
    i.hasVideo = o.value("hasVideo").toBool();
    i.hasAudio = o.value("hasAudio").toBool();
    i.width = o.value("width").toInt();
    i.height = o.value("height").toInt();
    i.fpsNum = qint64(o.value("fpsNum").toDouble());
    i.fpsDen = std::max<qint64>(1, qint64(o.value("fpsDen").toDouble(1)));
    i.variableFrameRate = o.value("variableFrameRate").toBool();
    i.audioRate = o.value("audioRate").toInt();
    i.audioChannels = o.value("audioChannels").toInt();
    i.videoCodec = o.value("videoCodec").toString();
    i.audioCodec = o.value("audioCodec").toString();
    i.fileSize = qint64(o.value("fileSize").toDouble());
    i.modifiedMs = qint64(o.value("modifiedMs").toDouble());
    const bool kindOk = i.kind == "video" || i.kind == "audio" || i.kind == "image";
    if (m.id.isEmpty() || m.name.isEmpty() || !kindOk || (m.path.isEmpty() && m.relativePath.isEmpty()))
        return false;
    *out = m;
    return true;
}

QJsonObject transitionToJson(const TransitionSpec& t)
{
    return {{"type", t.type}, {"frames", double(t.frames)}};
}

TransitionSpec transitionFromJson(const QJsonObject& o)
{
    TransitionSpec t;
    t.type = o.value("type").toString();
    t.frames = std::max<qint64>(0, qint64(o.value("frames").toDouble()));
    if (t.frames == 0)
        t.type.clear();
    return t;
}

QJsonObject clipToJson(const ClipData& c)
{
    QJsonObject o{{"id", c.id}, {"mediaId", c.mediaId}, {"trackId", c.trackId}, {"start", double(c.startFrame)},
                  {"duration", double(c.durationFrames)}, {"sourceIn", double(c.sourceInFrame)}, {"linkId", c.linkId}};
    if (c.type != QLatin1String("media"))
        o.insert("type", c.type);
    if (!c.params.isEmpty()) {
        QJsonObject params;
        for (auto it = c.params.cbegin(); it != c.params.cend(); ++it) {
            QJsonArray keys;
            for (const Keyframe& k : it->keys)
                keys.append(QJsonObject{{"f", double(k.frame)}, {"v", k.value}, {"i", k.interp}});
            params.insert(it.key(), QJsonObject{{"value", it->value}, {"keys", keys}});
        }
        o.insert("params", params);
    }
    if (c.isTitle()) {
        const TitleData& t = c.title;
        o.insert("title", QJsonObject{{"text", t.text}, {"font", t.font}, {"size", t.size}, {"color", t.color},
                                      {"strokeColor", t.strokeColor}, {"strokeWidth", t.strokeWidth}, {"shadow", t.shadow},
                                      {"bold", t.bold}, {"align", t.align}, {"x", t.x}, {"y", t.y}});
    }
    if (!c.lutPath.isEmpty())
        o.insert("lut", c.lutPath);
    if (!c.curves.isEmpty()) {
        QJsonObject curves;
        for (auto it = c.curves.cbegin(); it != c.curves.cend(); ++it) {
            QJsonArray pts;
            for (const QPointF& p : it.value())
                pts.append(QJsonArray{p.x(), p.y()});
            curves.insert(it.key(), pts);
        }
        o.insert("curves", curves);
    }
    if (c.transIn.active())
        o.insert("transIn", transitionToJson(c.transIn));
    if (c.transOut.active())
        o.insert("transOut", transitionToJson(c.transOut));
    if (c.fadeInFrames > 0)
        o.insert("fadeIn", double(c.fadeInFrames));
    if (c.fadeOutFrames > 0)
        o.insert("fadeOut", double(c.fadeOutFrames));
    return o;
}

bool clipFromJson(const QJsonObject& o, ClipData* out)
{
    ClipData c;
    c.id = o.value("id").toString();
    c.mediaId = o.value("mediaId").toString();
    c.trackId = o.value("trackId").toString();
    c.startFrame = qint64(o.value("start").toDouble(-1));
    c.durationFrames = qint64(o.value("duration").toDouble(0));
    c.sourceInFrame = qint64(o.value("sourceIn").toDouble(-1));
    c.linkId = o.value("linkId").toString();
    c.type = o.value("type").toString("media");
    if (c.type != QLatin1String("media") && c.type != QLatin1String("title"))
        return false;

    const QJsonObject params = o.value("params").toObject();
    for (auto it = params.begin(); it != params.end(); ++it) {
        if (!params::find(it.key()))
            continue;   // unknown parameter of a newer version: ignored
        const QJsonObject p = it.value().toObject();
        ParamTrack track;
        track.value = params::clamp(it.key(), p.value("value").toDouble(params::defaultOf(it.key())));
        for (const QJsonValue& kv : p.value("keys").toArray()) {
            const QJsonObject k = kv.toObject();
            Keyframe key;
            key.frame = qint64(k.value("f").toDouble());
            key.value = params::clamp(it.key(), k.value("v").toDouble());
            key.interp = k.value("i").toString("linear");
            track.keys.append(key);
        }
        std::sort(track.keys.begin(), track.keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.frame < b.frame; });
        c.params.insert(it.key(), track);
    }

    if (c.isTitle()) {
        const QJsonObject t = o.value("title").toObject();
        TitleData& d = c.title;
        d.text = t.value("text").toString();
        d.font = t.value("font").toString(d.font);
        d.size = std::clamp(t.value("size").toInt(d.size), 6, 600);
        d.color = t.value("color").toString(d.color);
        d.strokeColor = t.value("strokeColor").toString(d.strokeColor);
        d.strokeWidth = std::clamp(t.value("strokeWidth").toDouble(), 0.0, 60.0);
        d.shadow = t.value("shadow").toBool();
        d.bold = t.value("bold").toBool(true);
        d.align = t.value("align").toString(d.align);
        d.x = std::clamp(t.value("x").toDouble(0.5), -1.0, 2.0);
        d.y = std::clamp(t.value("y").toDouble(0.5), -1.0, 2.0);
    }
    c.lutPath = o.value("lut").toString();
    const QJsonObject curves = o.value("curves").toObject();
    for (auto it = curves.begin(); it != curves.end(); ++it) {
        QList<QPointF> pts;
        for (const QJsonValue& pv : it.value().toArray()) {
            const QJsonArray p = pv.toArray();
            if (p.size() == 2)
                pts.append(QPointF(std::clamp(p[0].toDouble(), 0.0, 1.0), std::clamp(p[1].toDouble(), 0.0, 1.0)));
        }
        if (pts.size() >= 2)
            c.curves.insert(it.key(), pts);
    }
    c.transIn = transitionFromJson(o.value("transIn").toObject());
    c.transOut = transitionFromJson(o.value("transOut").toObject());
    c.fadeInFrames = std::max<qint64>(0, qint64(o.value("fadeIn").toDouble()));
    c.fadeOutFrames = std::max<qint64>(0, qint64(o.value("fadeOut").toDouble()));
    *out = c;
    return true;
}
} // namespace

QList<TrackInfo> ProjectFile::defaultTracks()
{
    TrackInfo v2{"V2", "V2", "video"};
    TrackInfo v1{"V1", "V1", "video"};
    TrackInfo a1{"A1", "A1", "audio"};
    TrackInfo a2{"A2", "A2", "audio"};
    return {v2, v1, a1, a2};
}

ProjectData ProjectFile::makeDefault(const QString& name, int width, int height,
                                     const timecode::FrameRate& rate, int sampleRate)
{
    ProjectData d;
    d.name = name.trimmed();
    d.width = width;
    d.height = height;
    d.frameRate = rate;
    d.sampleRate = sampleRate;
    d.createdAt = QDateTime::currentDateTimeUtc();
    d.modifiedAt = d.createdAt;
    d.tracks = defaultTracks();
    return d;
}

QString ProjectFile::validate(const QString& name, int width, int height, const timecode::FrameRate& rate, int sampleRate)
{
    if (name.trimmed().isEmpty())
        return tr("Project name cannot be empty.");
    if (name.trimmed().size() > 120)
        return tr("Project name is too long (maximum 120 characters).");
    if (width < kMinDimension || height < kMinDimension || width > kMaxDimension || height > kMaxDimension)
        return tr("Resolution must be between %1 and %2 pixels.").arg(kMinDimension).arg(kMaxDimension);
    if (!rate.isValid() || rate.toDouble() > 240.0)
        return tr("Frame rate is not valid.");
    if (sampleRate < 8000 || sampleRate > 192000)
        return tr("Audio sample rate is not valid.");
    return {};
}

QJsonObject ProjectFile::toJson(const ProjectData& d)
{
    QJsonArray tracks;
    for (const TrackInfo& t : d.tracks)
        tracks.append(trackToJson(t));
    QJsonArray media;
    for (const MediaRef& m : d.media)
        media.append(mediaToJson(m));
    QJsonArray clips;
    for (const ClipData& c : d.clips)
        clips.append(clipToJson(c));
    QJsonArray markers;
    for (const MarkerData& m : d.markers)
        markers.append(QJsonObject{{"id", m.id}, {"frame", double(m.frame)}, {"name", m.name}, {"color", m.color}});

    return {
        {"format", QString::fromLatin1(kFormat)},
        {"schemaVersion", version::kProjectSchemaVersion},
        {"appVersion", QString::fromLatin1(version::kVersion)},
        {"name", d.name},
        {"createdAt", d.createdAt.toUTC().toString(Qt::ISODateWithMs)},
        {"modifiedAt", d.modifiedAt.toUTC().toString(Qt::ISODateWithMs)},
        {"settings", QJsonObject{{"width", d.width},
                                 {"height", d.height},
                                 {"frameRateNum", double(d.frameRate.num)},
                                 {"frameRateDen", double(d.frameRate.den)},
                                 {"sampleRate", d.sampleRate}}},
        {"media", media},
        {"timeline", QJsonObject{{"tracks", tracks}, {"clips", clips}, {"markers", markers},
                                 {"inPoint", double(d.inPoint)}, {"outPoint", double(d.outPoint)},
                                 {"masterGainDb", d.masterGainDb}}},
    };
}

bool ProjectFile::fromJson(const QJsonObject& root, ProjectData* out, QString* error)
{
    auto fail = [&](const QString& message) {
        if (error)
            *error = message;
        return false;
    };

    if (root.value(QStringLiteral("format")).toString() != QLatin1String(kFormat))
        return fail(tr("This is not a DriveVault project file."));

    const int fileVersion = root.value(QStringLiteral("schemaVersion")).toInt(0);
    if (fileVersion < 1)
        return fail(tr("The project file has no valid version information."));
    if (fileVersion > version::kProjectSchemaVersion)
        return fail(tr("This project was created by a newer version of DriveVault Video Editor Pro "
                       "(file format %1). Please update the application.").arg(fileVersion));
    // Migrations: v1 has no media / clips, v2 has no parameters, titles, transitions, mixer settings or markers.
    // Every added field has a neutral default, so older files simply read with those defaults and are rewritten
    // as the current version on the next save.

    ProjectData d;
    d.name = root.value(QStringLiteral("name")).toString();
    d.createdAt = QDateTime::fromString(root.value(QStringLiteral("createdAt")).toString(), Qt::ISODateWithMs);
    d.modifiedAt = QDateTime::fromString(root.value(QStringLiteral("modifiedAt")).toString(), Qt::ISODateWithMs);
    if (!d.createdAt.isValid())
        d.createdAt = QDateTime::currentDateTimeUtc();
    if (!d.modifiedAt.isValid())
        d.modifiedAt = d.createdAt;

    const QJsonObject s = root.value(QStringLiteral("settings")).toObject();
    d.width = s.value(QStringLiteral("width")).toInt();
    d.height = s.value(QStringLiteral("height")).toInt();
    d.frameRate = {qint64(s.value(QStringLiteral("frameRateNum")).toDouble()),
                   qint64(s.value(QStringLiteral("frameRateDen")).toDouble())};
    d.sampleRate = s.value(QStringLiteral("sampleRate")).toInt();

    if (const QString problem = validate(d.name, d.width, d.height, d.frameRate, d.sampleRate); !problem.isEmpty())
        return fail(tr("The project file is damaged: %1").arg(problem));

    const QJsonObject timeline = root.value(QStringLiteral("timeline")).toObject();
    d.masterGainDb = std::clamp(timeline.value(QStringLiteral("masterGainDb")).toDouble(0.0), -60.0, 12.0);   // absent in older files
    for (const QJsonValue& v : timeline.value(QStringLiteral("tracks")).toArray()) {
        const QJsonObject o = v.toObject();
        TrackInfo t;
        t.id = o.value(QStringLiteral("id")).toString();
        t.name = o.value(QStringLiteral("name")).toString(t.id);
        t.kind = o.value(QStringLiteral("kind")).toString();
        t.muted = o.value(QStringLiteral("muted")).toBool(false);
        t.locked = o.value(QStringLiteral("locked")).toBool(false);
        t.visible = o.value(QStringLiteral("visible")).toBool(true);
        t.gainDb = std::clamp(o.value(QStringLiteral("gainDb")).toDouble(0.0), -60.0, 12.0);
        t.pan = std::clamp(o.value(QStringLiteral("pan")).toDouble(0.0), -1.0, 1.0);
        t.solo = o.value(QStringLiteral("solo")).toBool(false);
        t.lane = std::clamp(o.value(QStringLiteral("lane")).toInt(1), 0, 2);       // older project files have no lane height
        const bool kindOk = t.kind == QLatin1String("video") || t.kind == QLatin1String("audio") || t.kind == QLatin1String("subtitle");
        if (t.id.isEmpty() || !kindOk)
            return fail(tr("The project file is damaged: invalid track definition."));
        d.tracks.append(t);
    }
    if (d.tracks.isEmpty())
        d.tracks = defaultTracks();

    for (const QJsonValue& v : timeline.value(QStringLiteral("markers")).toArray()) {
        const QJsonObject o = v.toObject();
        MarkerData m;
        m.id = o.value("id").toString();
        m.frame = std::max<qint64>(0, qint64(o.value("frame").toDouble()));
        m.name = o.value("name").toString();
        m.color = o.value("color").toString(m.color);
        if (!m.id.isEmpty())
            d.markers.append(m);
    }
    d.inPoint = qint64(timeline.value("inPoint").toDouble(-1));
    d.outPoint = qint64(timeline.value("outPoint").toDouble(-1));

    QSet<QString> mediaIds;
    for (const QJsonValue& v : root.value(QStringLiteral("media")).toArray()) {
        MediaRef m;
        if (!mediaFromJson(v.toObject(), &m) || mediaIds.contains(m.id))
            return fail(tr("The project file is damaged: invalid media entry."));
        mediaIds.insert(m.id);
        d.media.append(m);
    }
    QSet<QString> trackIds;
    for (const TrackInfo& t : d.tracks)
        trackIds.insert(t.id);
    QSet<QString> clipIds;
    for (const QJsonValue& v : timeline.value(QStringLiteral("clips")).toArray()) {
        ClipData c;
        if (!clipFromJson(v.toObject(), &c) || c.id.isEmpty() || clipIds.contains(c.id) || !trackIds.contains(c.trackId)
            || (!c.isTitle() && !mediaIds.contains(c.mediaId)) || c.startFrame < 0 || c.durationFrames <= 0 || c.sourceInFrame < 0)
            return fail(tr("The project file is damaged: invalid clip definition."));
        clipIds.insert(c.id);
        d.clips.append(c);
    }

    *out = d;
    return true;
}

bool ProjectFile::load(const QString& path, ProjectData* out, QString* error)
{
    QFile f(path);
    if (!f.exists()) {
        if (error)
            *error = tr("The file was not found: %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = tr("Cannot open the file: %1").arg(f.errorString());
        return false;
    }
    if (f.size() > 64 * 1024 * 1024) {
        if (error)
            *error = tr("The project file is unexpectedly large.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error)
            *error = tr("The project file is damaged and cannot be read (%1).").arg(parseError.errorString());
        return false;
    }
    return fromJson(doc.object(), out, error);
}

bool ProjectFile::save(const QString& path, const ProjectData& data, QString* error)
{
    return fileutils::writeAtomic(path, QJsonDocument(toJson(data)).toJson(QJsonDocument::Indented), error, true);
}

bool ProjectFile::hasProjectExtension(const QString& path)
{
    return QFileInfo(path).suffix().compare(QLatin1String(kExtension), Qt::CaseInsensitive) == 0;
}

} // namespace dv
