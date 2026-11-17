#include "ModelStore.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

namespace dm::ai {
namespace ModelStore {

QList<CatalogEntry> catalog()
{
    // Qwen3.5 (Qwen team, Apache-2.0), GGUF conversion of unsloth; Q4_K_M. Sizes, commits and SHA-256 are the values Hugging Face reports (checked 2026-11-15).
    auto entry = [](const char* id, const char* title, const char* repo, const char* file, const char* commit, qint64 size, const char* sha, const char* level) {
        CatalogEntry e;
        e.id = QLatin1String(id);
        e.title = QLatin1String(title);
        e.fileName = QLatin1String(file);
        e.url = QStringLiteral("https://huggingface.co/%1/resolve/%2/%3").arg(QLatin1String(repo), QLatin1String(commit), QLatin1String(file));
        e.sha256 = QLatin1String(sha);
        e.size = size;
        e.level = QLatin1String(level);
        return e;
    };
    return {
        entry("qwen35-08b", "Qwen3.5 0.8B", "unsloth/Qwen3.5-0.8B-GGUF", "Qwen3.5-0.8B-Q4_K_M.gguf", "6ab461498e2023f6e3c1baea90a8f0fe38ab64d0", 532517120LL,
              "bd258782e35f7f458f8aced1adc053e6e92e89bc735ba3be89d38a06121dc517", "tiny"),
        entry("qwen35-2b", "Qwen3.5 2B", "unsloth/Qwen3.5-2B-GGUF", "Qwen3.5-2B-Q4_K_M.gguf", "f6d5376be1edb4d416d56da11e5397a961aca8ae", 1280835840LL,
              "aaf42c8b7c3cab2bf3d69c355048d4a0ee9973d48f16c731c0520ee914699223", "small"),
        entry("qwen35-4b", "Qwen3.5 4B", "unsloth/Qwen3.5-4B-GGUF", "Qwen3.5-4B-Q4_K_M.gguf", "e87f176479d0855a907a41277aca2f8ee7a09523", 2740937888LL,
              "00fe7986ff5f6b463e62455821146049db6f9313603938a70800d1fb69ef11a4", "balanced"),
        // Typhoon 2.5 Qwen3 4B (SCB 10X, Apache-2.0): the official GGUF, Thai-focused with function calling (values from Hugging Face's headers, 2026-11-15).
        // Not tested in this program: the default and the tested model is Qwen3.5 4B.
        entry("typhoon25-4b", "Typhoon 2.5 Qwen3 4B (Thai)", "typhoon-ai/typhoon2.5-qwen3-4b-gguf", "typhoon2.5-qwen3-4b-q4_k_m.gguf",
              "386874d02430a544367cb0878687f8983751bbd2", 2497276128LL, "b365302922688f3a3c9ac8e3c00ab97a152cac0cdbf4eb5a734ecb483ae3e511", "thai"),
        entry("qwen35-9b", "Qwen3.5 9B", "unsloth/Qwen3.5-9B-GGUF", "Qwen3.5-9B-Q4_K_M.gguf", "3885219b6810b007914f3a7950a8d1b469d598a5", 5680522464LL,
              "03b74727a860a56338e042c4420bb3f04b2fec5734175f4cb9fa853daf52b7e8", "large"),
    };
}

HfLink parseHfLink(const QString& text)
{
    HfLink r;
    const QUrl u(text.trimmed());
    if (!u.isValid() || u.scheme() != QLatin1String("https") || u.host().isEmpty()) {
        r.error = QStringLiteral("badLink");
        return r;
    }
    if (u.host() != QLatin1String("huggingface.co") || u.port() != -1 || !u.userInfo().isEmpty()) {
        r.error = QStringLiteral("notHuggingFace");
        return r;
    }
    const QStringList parts = u.path().split(QLatin1Char('/'), Qt::SkipEmptyParts);     // owner repo blob|resolve revision path...
    static const QRegularExpression name(QStringLiteral("^[A-Za-z0-9._-]+$"));
    if (parts.size() < 5 || (parts.at(2) != QLatin1String("blob") && parts.at(2) != QLatin1String("resolve"))
        || !name.match(parts.at(0)).hasMatch() || !name.match(parts.at(1)).hasMatch() || !name.match(parts.at(3)).hasMatch()) {
        r.error = QStringLiteral("badLink");
        return r;
    }
    for (int i = 4; i < parts.size(); ++i) {
        if (parts.at(i) == QLatin1String("..") || parts.at(i) == QLatin1String(".") || parts.at(i).contains(QLatin1Char('\\'))) {
            r.error = QStringLiteral("badLink");
            return r;
        }
    }
    const QString file = parts.last();
    static const QRegularExpression fileName(QStringLiteral("^[A-Za-z0-9._()+-]+\\.gguf$"), QRegularExpression::CaseInsensitiveOption);
    if (!fileName.match(file).hasMatch()) {
        r.error = QStringLiteral("notGguf");
        return r;
    }
    r.ok = true;
    r.repo = parts.at(0) + QLatin1Char('/') + parts.at(1);
    r.fileName = file;
    r.url = QStringLiteral("https://huggingface.co/%1/%2/resolve/%3/%4").arg(parts.at(0), parts.at(1), parts.at(3), QStringList(parts.mid(4)).join(QLatin1Char('/')));
    return r;
}

HfInfo readHfHeaders(int status, const QMap<QString, QString>& headers, const HfLink& link)
{
    HfInfo r;
    if (status != 200 && status != 302 && status != 307) {
        r.error = QStringLiteral("http%1").arg(status);
        return r;
    }
    bool ok = false;
    r.size = headers.value(QStringLiteral("x-linked-size")).toLongLong(&ok);
    if (!ok || r.size <= 0) {
        r.error = QStringLiteral("noSize");
        return r;
    }
    QString etag = headers.value(QStringLiteral("x-linked-etag")).trimmed();
    etag.remove(QLatin1Char('"'));
    static const QRegularExpression sha(QStringLiteral("^[0-9a-f]{64}$"));
    if (!sha.match(etag).hasMatch()) {
        r.error = QStringLiteral("noChecksum");
        return r;
    }
    r.sha256 = etag;
    r.commit = headers.value(QStringLiteral("x-repo-commit")).trimmed();
    static const QRegularExpression commit(QStringLiteral("^[0-9a-f]{40}$"));
    r.pinnedUrl = link.url;
    if (commit.match(r.commit).hasMatch()) {
        const QStringList parts = QUrl(link.url).path().split(QLatin1Char('/'), Qt::SkipEmptyParts);
        r.pinnedUrl = QStringLiteral("https://huggingface.co/%1/%2/resolve/%3/%4").arg(parts.at(0), parts.at(1), r.commit, QStringList(parts.mid(4)).join(QLatin1Char('/')));
    }
    r.ok = true;
    return r;
}

QList<InstalledModel> installed(const QStringList& folders)
{
    QList<InstalledModel> out;
    QSet<QString> seen;
    for (const QString& folder : folders) {
        for (const QFileInfo& f : QDir(folder).entryInfoList({QStringLiteral("*.gguf")}, QDir::Files, QDir::Name)) {
            if (f.fileName().contains(QLatin1String("mmproj"), Qt::CaseInsensitive))
                continue;                                    // the vision file of a model (<model>.mmproj.gguf or the original mmproj-F16.gguf) is not a model
            const QString path = QDir::cleanPath(f.absoluteFilePath());
            const QString key = f.fileName().toLower() + QLatin1Char('|') + QString::number(f.size());
            if (f.size() < 1000000 || seen.contains(key))
                continue;                                    // a stub is not a model; the same file in two folders is listed once
            seen.insert(key);
            out.append({f.fileName(), path, f.size()});
        }
    }
    return out;
}

static QString g_visionFolder;

void setVisionFolder(const QString& folder) { g_visionFolder = folder; }

QString visionFileFor(const QString& modelPath)
{
    if (modelPath.isEmpty())
        return {};
    const QFileInfo m(modelPath);
    for (const QString& folder : {m.absolutePath(), g_visionFolder}) {       // next to the model, else in the user's models folder (the model may be in Program Files)
        if (folder.isEmpty())
            continue;
        const QString path = folder + QLatin1Char('/') + m.completeBaseName() + QStringLiteral(".mmproj.gguf");
        if (QFileInfo::exists(path) && QFileInfo(path).size() > 1000000)
            return path;
    }
    return {};
}

// Only the Qwen3.5 4B file has been checked (size and SHA-256 read from Hugging Face's headers on 2026-11-17); the other models get no offer.
VisionFile visionFileOf(const QString& modelFileName)
{
    VisionFile v;
    if (modelFileName.compare(QLatin1String("Qwen3.5-4B-Q4_K_M.gguf"), Qt::CaseInsensitive) != 0)
        return v;
    v.fileName = QStringLiteral("Qwen3.5-4B-Q4_K_M.mmproj.gguf");
    v.url = QStringLiteral("https://huggingface.co/unsloth/Qwen3.5-4B-GGUF/resolve/e87f176479d0855a907a41277aca2f8ee7a09523/mmproj-F16.gguf");
    v.sha256 = QStringLiteral("cd88edcf8d031894960bb0c9c5b9b7e1fea6ebee02b9f7ce925a00d12891f864");
    v.size = 672423616LL;
    return v;
}

} // namespace ModelStore
} // namespace dm::ai
