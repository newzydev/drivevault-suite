#include "AgentTools.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace dm::ai {

namespace {

QJsonObject prop(const char* type, const QString& description)
{
    return QJsonObject{{"type", QLatin1String(type)}, {"description", description}};
}

QJsonObject tool(const char* name, const QString& description, const QJsonObject& properties, const QStringList& required)
{
    QJsonArray req;
    for (const QString& r : required)
        req.append(r);
    return QJsonObject{{"type", "function"},
                       {"function", QJsonObject{{"name", QLatin1String(name)}, {"description", description},
                                                {"parameters", QJsonObject{{"type", "object"}, {"properties", properties}, {"required", req}}}}}};
}

bool looksBinary(const QByteArray& head) { return head.contains('\0'); }

bool skippedFolder(const QString& name)
{
    return name == QLatin1String(".git") || name == QLatin1String("node_modules") || name == QLatin1String(".drivevault-backup");
}

} // namespace

AgentTools::AgentTools(const QString& folder, bool allowEdits)
    : m_root(QFileInfo(folder).canonicalFilePath())
    , m_edits(allowEdits)
{
}

QJsonArray AgentTools::specs() const
{
    QJsonArray a;
    a.append(tool("list_files", QStringLiteral("Lists the files and folders in a folder (relative to the working folder, \".\" is the working folder)."),
                  QJsonObject{{"path", prop("string", QStringLiteral("relative folder, default \".\""))}}, {}));
    a.append(tool("read_file", QStringLiteral("Reads a text file (relative path). Long files are cut."),
                  QJsonObject{{"path", prop("string", QStringLiteral("relative path of the file"))}}, {QStringLiteral("path")}));
    a.append(tool("search_text", QStringLiteral("Finds a text (not case sensitive) in the text files of a folder and its sub folders; shows file, line and the line."),
                  QJsonObject{{"query", prop("string", QStringLiteral("the text to find"))}, {"path", prop("string", QStringLiteral("relative folder, default \".\""))}},
                  {QStringLiteral("query")}));
    if (m_edits)
        a.append(tool("write_file", QStringLiteral("Creates a text file or replaces its whole content (relative path). A replaced file is saved to .drivevault-backup first."),
                      QJsonObject{{"path", prop("string", QStringLiteral("relative path of the file"))}, {"content", prop("string", QStringLiteral("the whole new content"))}},
                      {QStringLiteral("path"), QStringLiteral("content")}));
    return a;
}

QString AgentTools::describe(const QString& name, const QJsonObject& args)
{
    QString detail = args.value(QLatin1String("path")).toString();
    if (name == QLatin1String("search_text"))
        detail = args.value(QLatin1String("query")).toString();
    detail = detail.simplified().left(100);
    const QString label = name == QLatin1String("list_files") ? QStringLiteral("List")
                          : name == QLatin1String("read_file") ? QStringLiteral("Read")
                          : name == QLatin1String("search_text") ? QStringLiteral("Search")
                          : name == QLatin1String("write_file") ? QStringLiteral("Write") : name;
    return detail.isEmpty() ? label : label + QStringLiteral(": ") + detail;
}

QString AgentTools::resolve(const QString& relative, bool mustExist, QString* error) const
{
    if (m_root.isEmpty()) {
        *error = QStringLiteral("the working folder does not exist");
        return {};
    }
    QString rel = relative.trimmed();
    if (rel.isEmpty())
        rel = QStringLiteral(".");
    rel.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (rel.startsWith(QLatin1Char('/')) || rel.contains(QLatin1Char(':')) || rel.contains(QLatin1String("..")) || rel.contains(QChar(0))) {
        *error = QStringLiteral("the path must be inside the working folder (no absolute path, no \"..\")");
        return {};
    }
    const QString full = QDir::cleanPath(m_root + QLatin1Char('/') + rel);
    const QFileInfo info(full);
    // the real place (a link may point elsewhere): the file itself, or for a new file its folder
    const QString real = info.exists() ? info.canonicalFilePath() : QFileInfo(info.absolutePath()).canonicalFilePath();
    const QString realCmp = real.toLower(), rootCmp = m_root.toLower();
    if (real.isEmpty() || (realCmp != rootCmp && !realCmp.startsWith(rootCmp + QLatin1Char('/')))) {
        *error = mustExist ? QStringLiteral("not found: %1").arg(relative) : QStringLiteral("the folder of this path does not exist inside the working folder");
        return {};
    }
    if (mustExist && !info.exists()) {
        *error = QStringLiteral("not found: %1").arg(relative);
        return {};
    }
    return full;
}

AgentTools::Result AgentTools::run(const QString& name, const QJsonObject& args) const
{
    if (name == QLatin1String("list_files"))
        return listFiles(args);
    if (name == QLatin1String("read_file"))
        return readFile(args);
    if (name == QLatin1String("search_text"))
        return searchText(args);
    if (name == QLatin1String("write_file"))
        return writeFile(args);
    return {QStringLiteral("unknown tool: %1").arg(name), true};
}

AgentTools::Result AgentTools::listFiles(const QJsonObject& args) const
{
    QString err;
    const QString dir = resolve(args.value(QLatin1String("path")).toString(QStringLiteral(".")), true, &err);
    if (dir.isEmpty())
        return {err, true};
    const QFileInfo info(dir);
    if (!info.isDir())
        return {QStringLiteral("not a folder: %1").arg(info.fileName()), true};
    QStringList lines;
    const QFileInfoList list = QDir(dir).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
    for (const QFileInfo& f : list) {
        if (lines.size() >= 200)
            break;
        lines << (f.isDir() ? f.fileName() + QLatin1Char('/') : QStringLiteral("%1  (%2 bytes)").arg(f.fileName()).arg(f.size()));
    }
    if (lines.isEmpty())
        return {QStringLiteral("(empty folder)"), false};
    if (list.size() > lines.size())
        lines << QStringLiteral("... %1 more").arg(list.size() - lines.size());
    return {lines.join(QLatin1Char('\n')), false};
}

AgentTools::Result AgentTools::readFile(const QJsonObject& args) const
{
    QString err;
    const QString path = resolve(args.value(QLatin1String("path")).toString(), true, &err);
    if (path.isEmpty())
        return {err, true};
    QFile f(path);
    if (!QFileInfo(path).isFile() || !f.open(QIODevice::ReadOnly))
        return {QStringLiteral("cannot read: %1").arg(QFileInfo(path).fileName()), true};
    const QByteArray data = f.read(kMaxRead + 1);
    if (looksBinary(data.left(4096)))
        return {QStringLiteral("%1 is not a text file").arg(QFileInfo(path).fileName()), true};
    QString text = QString::fromUtf8(data.left(kMaxRead));
    if (data.size() > kMaxRead)
        text += QStringLiteral("\n... (the file is longer; only the first %1 bytes are shown)").arg(kMaxRead);
    return {text, false};
}

AgentTools::Result AgentTools::searchText(const QJsonObject& args) const
{
    const QString query = args.value(QLatin1String("query")).toString().trimmed();
    if (query.isEmpty())
        return {QStringLiteral("query is empty"), true};
    QString err;
    const QString dir = resolve(args.value(QLatin1String("path")).toString(QStringLiteral(".")), true, &err);
    if (dir.isEmpty())
        return {err, true};
    if (!QFileInfo(dir).isDir())
        return {QStringLiteral("not a folder"), true};
    QStringList hits;
    int scanned = 0;
    QDirIterator it(dir, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext() && hits.size() < 40 && scanned < 2000) {
        const QString file = it.next();
        const QString rel = QDir(m_root).relativeFilePath(file);
        bool skip = false;
        for (const QString& part : rel.split(QLatin1Char('/')))
            skip = skip || skippedFolder(part);
        if (skip || it.fileInfo().size() > 1000000)
            continue;
        ++scanned;
        QFile f(file);
        if (!f.open(QIODevice::ReadOnly))
            continue;
        if (looksBinary(f.peek(4096)))
            continue;
        int lineNo = 0;
        while (!f.atEnd() && hits.size() < 40) {
            const QString line = QString::fromUtf8(f.readLine()).trimmed();
            ++lineNo;
            if (line.contains(query, Qt::CaseInsensitive))
                hits << QStringLiteral("%1:%2: %3").arg(rel).arg(lineNo).arg(line.left(200));
        }
    }
    if (hits.isEmpty())
        return {QStringLiteral("no match"), false};
    return {hits.join(QLatin1Char('\n')), false};
}

AgentTools::WriteInfo AgentTools::inspectWrite(const QJsonObject& args) const
{
    WriteInfo w;
    if (!m_edits) {
        w.error = QStringLiteral("editing is not allowed in this mode");
        return w;
    }
    const QString content = args.value(QLatin1String("content")).toString();
    w.bytes = content.toUtf8().size();
    if (w.bytes > kMaxWrite) {
        w.error = QStringLiteral("the content is too long (limit %1 bytes)").arg(kMaxWrite);
        return w;
    }
    QString err;
    const QString path = resolve(args.value(QLatin1String("path")).toString(), false, &err);
    if (path.isEmpty()) {
        w.error = err;
        return w;
    }
    const QFileInfo info(path);
    if (info.isDir() || info.fileName().isEmpty() || skippedFolder(info.fileName())) {
        w.error = QStringLiteral("that name is not allowed");
        return w;
    }
    w.ok = true;
    w.path = QDir(m_root).relativeFilePath(path);
    w.exists = info.exists();
    w.preview = content.left(800);
    return w;
}

AgentTools::Result AgentTools::writeFile(const QJsonObject& args) const
{
    if (!m_edits)
        return {QStringLiteral("editing is not allowed: the user chose read only"), true};
    const QString content = args.value(QLatin1String("content")).toString();
    if (content.toUtf8().size() > kMaxWrite)
        return {QStringLiteral("the content is too long (limit %1 bytes)").arg(kMaxWrite), true};
    QString err;
    const QString path = resolve(args.value(QLatin1String("path")).toString(), false, &err);
    if (path.isEmpty())
        return {err, true};
    const QFileInfo info(path);
    if (info.isDir())
        return {QStringLiteral("that path is a folder"), true};
    if (info.fileName().isEmpty() || skippedFolder(info.fileName()))
        return {QStringLiteral("that name is not allowed"), true};
    QString note;
    if (info.exists()) {
        const QString rel = QDir(m_root).relativeFilePath(path);
        QString flat = rel;
        flat.replace(QLatin1Char('/'), QLatin1Char('_'));
        const QString backupDir = m_root + QStringLiteral("/.drivevault-backup");
        QDir().mkpath(backupDir);
        const QString backup = QStringLiteral("%1/%2.%3.bak").arg(backupDir, flat, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")));
        if (!QFile::copy(path, backup))
            return {QStringLiteral("the old version could not be saved, so the file was not changed"), true};
        note = QStringLiteral(" The old version is saved in .drivevault-backup.");
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(content.toUtf8()) < 0 || !out.commit())
        return {QStringLiteral("the file could not be written"), true};
    return {QStringLiteral("Wrote %1 bytes to %2.%3").arg(content.toUtf8().size()).arg(QDir(m_root).relativeFilePath(path), note), false};
}

} // namespace dm::ai
