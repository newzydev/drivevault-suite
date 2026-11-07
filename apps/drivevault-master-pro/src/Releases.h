#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>

namespace dm::releases {

// One published program release found in the GitHub "list releases" answer.
struct Release {
    QString slug;        // "drivevault-video-editor-pro"  (the apps/ folder name, from the tag  <slug>-v<major.minor.patch>)
    QString version;     // "1.0.1"
    QString tag;         // "drivevault-video-editor-pro-v1.0.1"
    QString assetName;   // "DriveVault-Video-Editor-Pro-Setup-x64.exe"
    QString assetUrl;
    QString hashUrl;     // the "<assetName>.sha256" asset of the same release
    QString pageUrl;     // the release page (html_url)
    qint64 size = 0;
};

// Parses the JSON array of `GET /repos/<owner>/<repo>/releases`. Only real releases count: no drafts, no pre-releases, a tag
// `<slug>-v<x.y.z>`, one `.exe` asset and the `.exe.sha256` asset next to it. Returns the highest version of every slug.
QMap<QString, Release> parse(const QByteArray& json);

// Reads the expected hash from a `.sha256` file ("<hex> *<file name>" as written by package.ps1, or just "<hex>").
// Returns 64 lower-case hex digits, or "" when the text is not a SHA-256 for that file name.
QString parseSha256(const QString& text, const QString& fileName);

// Only addresses that start with the configured prefix are downloaded (the official release download address).
bool urlAllowed(const QString& url, const QString& prefix);

} // namespace dm::releases
