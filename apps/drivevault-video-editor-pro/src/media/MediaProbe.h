#pragma once

#include "core/MediaTypes.h"

#include <QCoreApplication>

namespace dv {

class MediaProbe {
    Q_DECLARE_TR_FUNCTIONS(dv::MediaProbe)
public:
    // Validates and describes a media file. Returns false with a user-facing message when the file
    // is missing, unreadable, corrupted, or has nothing the editor can use.
    static bool probe(const QString& path, MediaInfo* out, QString* error);

    // "FFmpeg 8.1.3 (libavcodec 62.28.103)" for the status bar / About page.
    static QString engineDescription();
    // Image extensions treated as stills.
    static bool looksLikeImage(const QString& path);
};

} // namespace dv
