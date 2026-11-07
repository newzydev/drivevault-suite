#pragma once

#include "project/ProjectData.h"

#include <QCoreApplication>
#include <QJsonObject>

namespace dv {

// Reading / writing / validating the .dvproj JSON document.
class ProjectFile {
    Q_DECLARE_TR_FUNCTIONS(dv::ProjectFile)

public:
    static constexpr const char* kFormat = "drivevault-project";
    static constexpr const char* kExtension = "dvproj";

    static ProjectData makeDefault(const QString& name, int width, int height,
                                   const timecode::FrameRate& rate, int sampleRate);
    static QList<TrackInfo> defaultTracks();

    // Returns an empty string when the settings are acceptable, otherwise a user facing message.
    static QString validate(const QString& name, int width, int height, const timecode::FrameRate& rate, int sampleRate);

    static QJsonObject toJson(const ProjectData& data);
    // Applies migrations (older -> current) and validates. Newer versions are rejected.
    static bool fromJson(const QJsonObject& root, ProjectData* out, QString* error);

    static bool load(const QString& path, ProjectData* out, QString* error);
    // Atomic write; the previous file is kept as "<path>.bak".
    static bool save(const QString& path, const ProjectData& data, QString* error);

    static bool hasProjectExtension(const QString& path);
};

} // namespace dv
