#pragma once

#include "PdfTools.h"

#include <QString>
#include <QStringList>

namespace dp::tools {

// What to do to the file. The structure is always rewritten (object streams, strongest deflate, objects nobody refers to are dropped);
// the rest is optional. Text and lines are never turned into pictures: only embedded images are touched.
struct CompressOptions {
    int imageDpi = 0;            // 0 = do not reduce the resolution; otherwise images shown at more than this (+15 %) are reduced to it
    int jpegQuality = 0;         // 0 = do not change how images are stored; 1..100 = photos are stored as JPEG with this quality
    bool stripExtras = false;    // remove the XML metadata, page thumbnails and editor private data (the document information stays)
    bool linearize = false;      // "fast web view"
    qint64 minImageBytes = 20 * 1024;   // images stored smaller than this are left alone
};

struct CompressResult {
    bool ok = false;
    bool cancelled = false;
    QString error;               // "" | "password" | "file" | "format" | "write" | "unknown" | "noJpeg" | "cancelled"
    QString detail;
    bool written = false;        // false when the result would not be smaller: then no file was written ("noGain")
    qint64 inBytes = 0;
    qint64 outBytes = 0;
    int images = 0;              // images in the file
    int imagesChanged = 0;       // images that were stored smaller
    int imagesDownsampled = 0;   // of those, images whose resolution was reduced
    int imagesSkipped = 0;       // images left as they are (too small, unsupported format, or the new version was not smaller)
    QStringList warnings;        // "encrypted": the new file keeps the protection of the source
};

// Writes a smaller copy of `inFile` to `outFile` (replacing `outFile`; the caller picks a temporary name). The source is never changed. Progress is
// (images done, images total). When the result is not smaller than the source nothing is written and `written` is false.
CompressResult compressFile(const QString& inFile, const QString& password, const QString& outFile, const CompressOptions& options, const Progress& progress = {});

} // namespace dp::tools
