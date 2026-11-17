#include "AgentTools.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
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

// The text of a write_file / append_file call. The argument is called "text" (it sorts after "path", so the model sees and writes the path FIRST: a cut call
// then still says which file it was for); "content" is accepted too (older conversations, other models).
QString textArgument(const QJsonObject& args)
{
    return args.contains(QLatin1String("text")) ? args.value(QLatin1String("text")).toString() : args.value(QLatin1String("content")).toString();
}

// the last characters of a text file: a part of a big file tells the model where it ends, so that it need not read the file again
QString tailOf(const QString& path, int characters = 200)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    if (f.size() > 4000)
        f.seek(f.size() - 4000);
    const QString text = QString::fromUtf8(f.readAll());
    return text.right(characters);
}

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
    a.append(tool("read_file", QStringLiteral("Reads a text file (relative path). A long file is returned in parts (about 600 lines): the answer says which lines you got and the "
                                              "start_line to continue with. Use start_line / max_lines to read the part you need."),
                  QJsonObject{{"path", prop("string", QStringLiteral("relative path of the file"))},
                              {"start_line", prop("integer", QStringLiteral("first line to read, 1 based (default 1)"))},
                              {"max_lines", prop("integer", QStringLiteral("how many lines (default and most 600)"))}},
                  {QStringLiteral("path")}));
    a.append(tool("search_text", QStringLiteral("Finds a text (not case sensitive) in the text files of a folder and its sub folders; shows file, line and the line."),
                  QJsonObject{{"query", prop("string", QStringLiteral("the text to find"))}, {"path", prop("string", QStringLiteral("relative folder, default \".\""))}},
                  {QStringLiteral("query")}));
    a.append(tool("glob_files", QStringLiteral("Finds files by name pattern in the working folder and its sub folders, e.g. \"*.html\" (any folder) or \"src/**/*.cpp\"."),
                  QJsonObject{{"pattern", prop("string", QStringLiteral("the name pattern, * and ? are wildcards"))}}, {QStringLiteral("pattern")}));
    if (m_edits) {
        a.append(tool("edit_file", QStringLiteral("Changes a text file by replacing one exact piece of text with another (relative path). Prefer this to write_file for a change to an existing "
                                                  "file. old_string must match the file exactly, including spaces and line breaks, and must be unique in the file unless replace_all is true. "
                                                  "Read the file first. The old version is saved to .drivevault-backup."),
                      QJsonObject{{"path", prop("string", QStringLiteral("relative path of the file"))},
                                  {"old_string", prop("string", QStringLiteral("the exact text to replace"))},
                                  {"new_string", prop("string", QStringLiteral("the text that replaces it"))},
                                  {"replace_all", prop("boolean", QStringLiteral("replace every occurrence (default false)"))}},
                      {QStringLiteral("path"), QStringLiteral("old_string"), QStringLiteral("new_string")}));
        a.append(tool("write_file", QStringLiteral("Creates a text file or replaces its whole content (relative path). A replaced file is saved to .drivevault-backup first. "
                                                   "Your answer has a length limit: for a big file write only the first part (about 100 lines) here and add the rest with append_file."),
                      QJsonObject{{"path", prop("string", QStringLiteral("relative path of the file (give it first)"))}, {"text", prop("string", QStringLiteral("the whole new content (the first part of a big file)"))}},
                      {QStringLiteral("path"), QStringLiteral("text")}));
        a.append(tool("append_file", QStringLiteral("Adds text at the END of a text file (relative path; the file is created when it does not exist). Use it to build a big file in parts: "
                                                    "write_file for the first part, then append_file for each next part (about 100 lines each), each starting exactly where the file ends."),
                      QJsonObject{{"path", prop("string", QStringLiteral("relative path of the file (give it first)"))}, {"text", prop("string", QStringLiteral("the text to add at the end"))}},
                      {QStringLiteral("path"), QStringLiteral("text")}));
    }
    return a;
}

QString AgentTools::describe(const QString& name, const QJsonObject& args)
{
    QString detail = args.value(QLatin1String("path")).toString();
    if (name == QLatin1String("search_text"))
        detail = args.value(QLatin1String("query")).toString();
    else if (name == QLatin1String("glob_files"))
        detail = args.value(QLatin1String("pattern")).toString();
    detail = detail.simplified().left(100);
    const QString label = name == QLatin1String("list_files") ? QStringLiteral("List")
                          : name == QLatin1String("read_file") ? QStringLiteral("Read")
                          : name == QLatin1String("search_text") ? QStringLiteral("Search")
                          : name == QLatin1String("glob_files") ? QStringLiteral("Find files")
                          : name == QLatin1String("edit_file") ? QStringLiteral("Edit")
                          : name == QLatin1String("append_file") ? QStringLiteral("Append")
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
    if (name == QLatin1String("glob_files"))
        return globFiles(args);
    if (name == QLatin1String("write_file"))
        return writeFile(args);
    if (name == QLatin1String("edit_file"))
        return editFile(args);
    if (name == QLatin1String("append_file"))
        return appendFile(args);
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
    if (f.size() > kMaxFile)
        return {QStringLiteral("%1 is too big to read (%2 bytes); use search_text to find the part you need").arg(QFileInfo(path).fileName()).arg(f.size()), true};
    const QByteArray data = f.readAll();
    if (looksBinary(data.left(4096)))
        return {QStringLiteral("%1 is not a text file").arg(QFileInfo(path).fileName()), true};
    // a part of a long file: start_line (1 based) and max_lines; the answer says where to go on
    QStringList lines = QString::fromUtf8(data).split(QLatin1Char('\n'));
    if (data.endsWith('\n') && !lines.isEmpty())
        lines.removeLast();                                            // the empty piece after the last line break
    const int total = qMax(1, int(lines.size()));
    const int start = qBound(1, args.value(QLatin1String("start_line")).toInt(1), total);
    const int most = qBound(1, args.value(QLatin1String("max_lines")).toInt(kMaxLines), kMaxLines);
    QString text;
    int end = start - 1;
    while (end < lines.size() && end - start + 1 < most && text.size() < kMaxRead) {
        text += lines.at(end) + QLatin1Char('\n');
        ++end;
    }
    if (start == 1 && end >= lines.size())
        return {QString::fromUtf8(data), false};                       // the whole file, as it is
    QString note = QStringLiteral("[lines %1-%2 of %3]").arg(start).arg(end).arg(total);
    if (end < total)
        note += QStringLiteral(" Read on with start_line=%1.").arg(end + 1);
    return {note + QLatin1Char('\n') + text, false};
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
    const QString content = textArgument(args);
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
    const QString content = textArgument(args);
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
    return save(path, content);
}

AgentTools::Result AgentTools::save(const QString& path, const QString& content) const
{
    const QFileInfo info(path);
    QString note;
    if (info.exists()) {
        const QString rel = QDir(m_root).relativeFilePath(path);
        QString flat = rel;
        flat.replace(QLatin1Char('/'), QLatin1Char('_'));
        const QString backupDir = m_root + QStringLiteral("/.drivevault-backup");
        QDir().mkpath(backupDir);
        const QString backup = QStringLiteral("%1/%2.%3.bak").arg(backupDir, flat, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
        if (!QFile::copy(path, backup))
            return {QStringLiteral("the old version could not be saved, so the file was not changed"), true};
        note = QStringLiteral(" The old version is saved in .drivevault-backup.");
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly) || out.write(content.toUtf8()) < 0 || !out.commit())
        return {QStringLiteral("the file could not be written"), true};
    QString ends;
    if (content.size() > 600)                                         // a first part of a big file: where it ends (the next part goes on there)
        ends = QStringLiteral("\nThe file ends with:\n<<<\n%1\n>>>").arg(tailOf(path));
    return {QStringLiteral("Wrote %1 bytes to %2.%3%4").arg(content.toUtf8().size()).arg(QDir(m_root).relativeFilePath(path), note, ends), false};
}

// ---- edit_file: one exact piece of text replaced ----------------------------------------------------------------------------------------
// The tolerant second try of edit_file: the lines of `oldText` are compared with the lines of the file after trimming each line. One match (or every match with
// replace_all): those lines are replaced by `newText`. No match: `*error` names the nearest line of the file, so the model can copy it exactly.
bool AgentTools::replaceIgnoringIndentation(const QString& content, const QString& oldText, const QString& newText, bool all, QString* out, QString* error)
{
    QStringList want = oldText.split(QLatin1Char('\n'));
    for (QString& w : want)
        w = w.trimmed();
    while (!want.isEmpty() && want.first().isEmpty())
        want.removeFirst();
    while (!want.isEmpty() && want.last().isEmpty())
        want.removeLast();
    if (want.isEmpty())
        return false;
    const bool crlf = content.contains(QLatin1String("\r\n"));
    QStringList lines = content.split(QLatin1Char('\n'));
    QStringList trimmed;
    for (const QString& l : std::as_const(lines))
        trimmed << l.trimmed();
    QList<int> hits;
    for (int i = 0; i + want.size() <= trimmed.size(); ++i) {
        bool same = true;
        for (int k = 0; k < want.size() && same; ++k)
            same = trimmed.at(i + k) == want.at(k);
        if (same) {
            hits << i;
            i += int(want.size()) - 1;                                  // matches do not overlap
        }
    }
    if (hits.isEmpty()) {
        const QStringList words = want.first().split(QRegularExpression(QStringLiteral("\\W+")), Qt::SkipEmptyParts);
        int best = 0, bestLine = -1;
        for (int i = 0; i < trimmed.size(); ++i) {
            int score = 0;
            for (const QString& w : words)
                if (w.size() > 2 && trimmed.at(i).contains(w))
                    ++score;
            if (score > best) {
                best = score;
                bestLine = i;
            }
        }
        if (bestLine >= 0)
            *error = QStringLiteral("old_string was not found. The nearest line of the file is line %1: %2 - read that part of the file (read_file with start_line) and copy it exactly")
                         .arg(bestLine + 1).arg(trimmed.at(bestLine).left(200));
        return false;
    }
    if (hits.size() > 1 && !all) {
        *error = QStringLiteral("old_string matches %1 places (ignoring indentation). Add more text around it so that it is unique, or set replace_all to true").arg(hits.size());
        return false;
    }
    QString replacement = newText;
    replacement.remove(QLatin1Char('\r'));
    if (crlf)
        replacement.replace(QLatin1Char('\n'), QLatin1String("\r\n"));
    // the indentation of the first replaced line is kept for the new text when the model sent it without
    for (int h = int(hits.size()) - 1; h >= 0; --h) {
        const int first = hits.at(h), last = first + int(want.size()) - 1;
        QString indent;
        for (const QChar c : lines.at(first)) {
            if (c == QLatin1Char(' ') || c == QLatin1Char('\t'))
                indent += c;
            else
                break;
        }
        QString text = replacement;
        bool anyIndented = false;                                       // the model sent its own indentation: keep it; else every line gets the indentation of the first replaced line
        for (const QString& l : text.split(QLatin1Char('\n')))
            anyIndented = anyIndented || l.startsWith(QLatin1Char(' ')) || l.startsWith(QLatin1Char('\t'));
        if (!indent.isEmpty() && !text.isEmpty() && !anyIndented) {
            QStringList withIndent;
            for (const QString& l : text.split(QLatin1Char('\n')))
                withIndent << (l.trimmed().isEmpty() ? l : indent + l);
            text = withIndent.join(QLatin1Char('\n'));
        }
        const bool cr = lines.at(last).endsWith(QLatin1Char('\r'));
        QStringList part = lines.mid(0, first);
        part << text + (cr ? QStringLiteral("\r") : QString());
        part << lines.mid(last + 1);
        lines = part;
    }
    *out = lines.join(QLatin1Char('\n'));
    return true;
}

bool AgentTools::applyEdit(const QJsonObject& args, QString* fullPath, QString* oldContent, QString* newContent, QString* error) const
{
    if (!m_edits) {
        *error = QStringLiteral("editing is not allowed in this mode");
        return false;
    }
    const QString oldText = args.value(QLatin1String("old_string")).toString();
    const QString newText = args.value(QLatin1String("new_string")).toString();
    if (oldText.isEmpty()) {
        *error = QStringLiteral("old_string is empty (to create a file use write_file)");
        return false;
    }
    if (oldText == newText) {
        *error = QStringLiteral("old_string and new_string are the same");
        return false;
    }
    QString err;
    const QString path = resolve(args.value(QLatin1String("path")).toString(), true, &err);
    if (path.isEmpty()) {
        *error = err;
        return false;
    }
    const QFileInfo info(path);
    if (!info.isFile() || skippedFolder(info.fileName())) {
        *error = QStringLiteral("that is not a file you may change");
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("cannot read: %1").arg(info.fileName());
        return false;
    }
    const QByteArray data = f.readAll();
    f.close();
    if (data.size() > kMaxWrite || looksBinary(data.left(4096))) {
        *error = QStringLiteral("%1 is too long or not a text file").arg(info.fileName());
        return false;
    }
    const QString content = QString::fromUtf8(data);
    qsizetype count = content.count(oldText);
    if (count == 0) {
        // A small model often gets the indentation or the line breaks (CRLF) wrong: try again line by line, ignoring the spaces at the start and end of each line.
        QString out;
        if (replaceIgnoringIndentation(content, oldText, newText, args.value(QLatin1String("replace_all")).toBool(), &out, error)) {
            if (out.toUtf8().size() > kMaxWrite) {
                *error = QStringLiteral("the result would be too long (limit %1 bytes)").arg(kMaxWrite);
                return false;
            }
            *fullPath = path;
            *oldContent = content;
            *newContent = out;
            return true;
        }
        if (error->isEmpty())
            *error = QStringLiteral("old_string was not found in %1. Read the file again and copy the text exactly (spaces and line breaks too)").arg(info.fileName());
        return false;
    }
    const bool all = args.value(QLatin1String("replace_all")).toBool();
    if (count > 1 && !all) {
        *error = QStringLiteral("old_string appears %1 times in %2. Add more text around it so that it is unique, or set replace_all to true").arg(count).arg(info.fileName());
        return false;
    }
    QString out = content;
    if (all)
        out.replace(oldText, newText);
    else
        out.replace(content.indexOf(oldText), oldText.size(), newText);
    if (out.toUtf8().size() > kMaxWrite) {
        *error = QStringLiteral("the result would be too long (limit %1 bytes)").arg(kMaxWrite);
        return false;
    }
    *fullPath = path;
    *oldContent = content;
    *newContent = out;
    return true;
}

AgentTools::WriteInfo AgentTools::inspectEdit(const QJsonObject& args) const
{
    WriteInfo w;
    QString path, before, after;
    if (!applyEdit(args, &path, &before, &after, &w.error))
        return w;
    auto lines = [](const QString& text, const QString& mark) {
        QStringList out;
        for (const QString& l : text.split(QLatin1Char('\n')))
            out << mark + l;
        return out.join(QLatin1Char('\n'));
    };
    w.ok = true;
    w.path = QDir(m_root).relativeFilePath(path);
    w.exists = true;
    w.bytes = after.toUtf8().size();
    w.preview = (lines(args.value(QLatin1String("old_string")).toString(), QStringLiteral("- ")) + QLatin1Char('\n')
                 + lines(args.value(QLatin1String("new_string")).toString(), QStringLiteral("+ "))).left(1600);
    return w;
}

AgentTools::Result AgentTools::editFile(const QJsonObject& args) const
{
    QString path, before, after, err;
    if (!applyEdit(args, &path, &before, &after, &err))
        return {err, true};
    const Result r = save(path, after);
    if (r.error)
        return r;
    const bool all = args.value(QLatin1String("replace_all")).toBool();
    return {QStringLiteral("Edited %1 (%2).%3").arg(QDir(m_root).relativeFilePath(path), all ? QStringLiteral("every occurrence") : QStringLiteral("1 place"),
                                                    QStringLiteral(" The old version is saved in .drivevault-backup.")), false};
}

// ---- append_file: a big file is built in parts ------------------------------------------------------------------------------------------
AgentTools::WriteInfo AgentTools::inspectAppend(const QJsonObject& args) const
{
    WriteInfo w;
    if (!m_edits) {
        w.error = QStringLiteral("editing is not allowed in this mode");
        return w;
    }
    const QString content = textArgument(args);
    if (content.isEmpty()) {
        w.error = QStringLiteral("content is empty");
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
    w.bytes = (info.exists() ? info.size() : 0) + content.toUtf8().size();
    if (w.bytes > kMaxWrite) {
        w.error = QStringLiteral("the file would be too long (limit %1 bytes)").arg(kMaxWrite);
        return w;
    }
    w.ok = true;
    w.path = QDir(m_root).relativeFilePath(path);
    w.exists = info.exists();
    w.preview = content.left(800);
    return w;
}

AgentTools::Result AgentTools::appendFile(const QJsonObject& args) const
{
    const WriteInfo w = inspectAppend(args);
    if (!w.ok)
        return {w.error, true};
    QString err;
    const QString path = resolve(args.value(QLatin1String("path")).toString(), false, &err);
    if (path.isEmpty())
        return {err, true};
    const QByteArray data = textArgument(args).toUtf8();
    QFile f(path);                                                   // only text is added: what was there is never changed, so no backup copy is needed
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append) || f.write(data) != data.size())
        return {QStringLiteral("the file could not be written"), true};
    f.close();
    return {QStringLiteral("Added %1 bytes to %2; the file is now %3 bytes. It ends with:\n<<<\n%4\n>>>").arg(data.size()).arg(w.path).arg(w.bytes).arg(tailOf(path)), false};
}

// ---- glob_files --------------------------------------------------------------------------------------------------------------------------
AgentTools::Result AgentTools::globFiles(const QJsonObject& args) const
{
    QString pattern = args.value(QLatin1String("pattern")).toString().trimmed();
    pattern.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (pattern.isEmpty())
        return {QStringLiteral("pattern is empty"), true};
    if (pattern.startsWith(QLatin1String("**/")))
        pattern = pattern.mid(3);
    if (m_root.isEmpty())
        return {QStringLiteral("the working folder does not exist"), true};
    const bool byName = !pattern.contains(QLatin1Char('/'));                         // "*.html": the file name, in any folder
    const QRegularExpression re(QRegularExpression::wildcardToRegularExpression(pattern, QRegularExpression::NonPathWildcardConversion),
                                QRegularExpression::CaseInsensitiveOption);
    QStringList hits;
    int scanned = 0;
    QDirIterator it(m_root, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext() && hits.size() < 200 && scanned < 20000) {
        const QString file = it.next();
        const QString rel = QDir(m_root).relativeFilePath(file);
        bool skip = false;
        for (const QString& part : rel.split(QLatin1Char('/')))
            skip = skip || skippedFolder(part);
        if (skip)
            continue;
        ++scanned;
        if (re.match(byName ? it.fileInfo().fileName() : rel).hasMatch())
            hits << rel;
    }
    if (hits.isEmpty())
        return {QStringLiteral("no file matches"), false};
    hits.sort();
    return {hits.join(QLatin1Char('\n')), false};
}

} // namespace dm::ai
