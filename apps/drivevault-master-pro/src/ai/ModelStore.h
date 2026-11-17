#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace dm::ai {

// A language model of the catalogue: one file of a pinned revision with its size and SHA-256 (read from Hugging Face's own headers when the entry was written).
struct CatalogEntry {
    QString id;
    QString title;
    QString fileName;
    QString url;                // https://huggingface.co/<owner>/<repo>/resolve/<commit>/<file>
    QString sha256;
    qint64 size = 0;
    QString level;              // tiny | small | balanced | large: what the UI says about speed and memory
};

// A link the user pasted: only https://huggingface.co/<owner>/<repo>/(blob|resolve)/<revision>/<path>.gguf is accepted.
struct HfLink {
    bool ok = false;
    QString error;              // badLink | notHuggingFace | notGguf
    QString url;                // the resolve form
    QString fileName;
    QString repo;               // owner/repo
};

// What Hugging Face says about the file (the answer to a HEAD request: size, SHA-256 of the file, commit).
struct HfInfo {
    bool ok = false;
    QString error;              // http<status> | noChecksum | noSize
    qint64 size = 0;
    QString sha256;
    QString commit;
    QString pinnedUrl;          // the link with the commit in place of the branch, so the file cannot change under the checksum
};

struct InstalledModel {
    QString name;
    QString path;
    qint64 size = 0;
};

// The vision file ("mmproj") that lets a model look at pictures: a separate file of the same pinned revision, kept next to the model as <model>.mmproj.gguf.
struct VisionFile {
    QString fileName;           // Qwen3.5-4B-Q4_K_M.mmproj.gguf (empty = this model has none on offer)
    QString url;
    QString sha256;
    qint64 size = 0;
};

namespace ModelStore {

void setVisionFolder(const QString& folder);            // where the program saves vision files (the models folder in the user's data); searched after the model's own folder
QString visionFileFor(const QString& modelPath);        // the installed vision file of that model, or ""
VisionFile visionFileOf(const QString& modelFileName);  // what can be downloaded for a model file name (empty fileName = nothing)
QList<CatalogEntry> catalog();
HfLink parseHfLink(const QString& text);
// `headers` has lower-case names. A file of Hugging Face's LFS storage answers with x-linked-size / x-linked-etag / x-repo-commit.
HfInfo readHfHeaders(int status, const QMap<QString, QString>& headers, const HfLink& link);
QList<InstalledModel> installed(const QStringList& folders);        // *.gguf files, newest folder entries first, no duplicates by path

} // namespace ModelStore

} // namespace dm::ai
