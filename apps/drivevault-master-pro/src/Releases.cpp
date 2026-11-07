#include "Releases.h"

#include "WinUtil.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace dm::releases {

QMap<QString, Release> parse(const QByteArray& json)
{
    QMap<QString, Release> best;
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray())
        return best;
    static const QRegularExpression tagPattern(QStringLiteral("^([a-z][a-z0-9-]*)-v(\\d+\\.\\d+\\.\\d+)$"));
    for (const QJsonValue& rv : doc.array()) {
        const QJsonObject r = rv.toObject();
        if (r.value(QStringLiteral("draft")).toBool() || r.value(QStringLiteral("prerelease")).toBool())
            continue;
        const QString tag = r.value(QStringLiteral("tag_name")).toString();
        const QRegularExpressionMatch m = tagPattern.match(tag);
        if (!m.hasMatch())
            continue;
        Release rel;
        rel.slug = m.captured(1);
        rel.version = m.captured(2);
        rel.tag = tag;
        rel.pageUrl = r.value(QStringLiteral("html_url")).toString();
        QMap<QString, QJsonObject> assets;
        for (const QJsonValue& av : r.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject a = av.toObject();
            assets.insert(a.value(QStringLiteral("name")).toString(), a);
        }
        for (auto it = assets.constBegin(); it != assets.constEnd(); ++it) {
            if (!it.key().endsWith(QLatin1String(".exe"), Qt::CaseInsensitive) || !assets.contains(it.key() + QStringLiteral(".sha256")))
                continue;
            rel.assetName = it.key();
            rel.assetUrl = it.value().value(QStringLiteral("browser_download_url")).toString();
            rel.hashUrl = assets.value(it.key() + QStringLiteral(".sha256")).value(QStringLiteral("browser_download_url")).toString();
            rel.size = qint64(it.value().value(QStringLiteral("size")).toDouble());
            break;
        }
        if (rel.assetUrl.isEmpty() || rel.hashUrl.isEmpty())
            continue;
        const auto cur = best.constFind(rel.slug);
        if (cur == best.constEnd() || win::compareVersions(rel.version, cur->version) > 0)
            best.insert(rel.slug, rel);
    }
    return best;
}

QString parseSha256(const QString& text, const QString& fileName)
{
    static const QRegularExpression pattern(QStringLiteral("^\\s*([0-9a-fA-F]{64})(?:\\s+\\*?(.+?))?\\s*$"));
    for (const QString& line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch m = pattern.match(line);
        if (!m.hasMatch())
            continue;
        const QString named = m.captured(2).trimmed();
        if (named.isEmpty() || named.compare(fileName, Qt::CaseInsensitive) == 0)
            return m.captured(1).toLower();
    }
    return {};
}

bool urlAllowed(const QString& url, const QString& prefix)
{
    return !prefix.isEmpty() && url.startsWith(prefix, Qt::CaseInsensitive);
}

} // namespace dm::releases
