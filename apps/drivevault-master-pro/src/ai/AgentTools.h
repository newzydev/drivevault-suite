#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace dm::ai {

// The file tools of the local agent. They work ONLY inside the folder the user chose: a path is relative to it, may not leave it (no "..", no drive, no ":" stream
// names, no link that points outside), and none of them deletes, renames or reaches the network (running a program is a separate tool, see CommandRunner, and
// always waits for the user). Reading and finding files is always possible; write_file / edit_file only when the user allowed edits, and a file that is
// changed is first copied to <folder>\.drivevault-backup.
class AgentTools {
public:
    AgentTools(const QString& folder, bool allowEdits);
    void setAllowEdits(bool allow) { m_edits = allow; }               // a plan the user approved turns editing on

    struct Result {
        QString text;                 // what the model sees
        bool error = false;
    };

    // What a write_file call would do, without doing it (the user is asked before it happens in the "ask" mode).
    struct WriteInfo {
        bool ok = false;
        QString error;
        QString path;                 // relative to the folder
        bool exists = false;          // it would replace a file
        qint64 bytes = 0;
        QString preview;              // the beginning of the new content
    };
    WriteInfo inspectWrite(const QJsonObject& args) const;
    // The same for edit_file: `preview` is a small diff ("- old line" / "+ new line"), `bytes` the size the file would have.
    WriteInfo inspectEdit(const QJsonObject& args) const;
    // The same for append_file: `preview` is the text that would be added, `bytes` the size the file would have.
    WriteInfo inspectAppend(const QJsonObject& args) const;
    static bool changesFiles(const QString& tool)
    {
        return tool == QLatin1String("write_file") || tool == QLatin1String("edit_file") || tool == QLatin1String("append_file");
    }

    QJsonArray specs() const;                                         // the tools in the OpenAI "tools" format (write_file / edit_file only when edits are allowed)
    Result run(const QString& name, const QJsonObject& args) const;
    static QString describe(const QString& name, const QJsonObject& args);   // "Read: notes.txt" for the activity line

    static constexpr qint64 kMaxRead = 24000;                          // characters of one read_file answer (a long file is read in parts)
    static constexpr int kMaxLines = 600;                              // lines of one read_file answer
    static constexpr qint64 kMaxFile = 3000000;                        // a bigger file is not read at all
    static constexpr qint64 kMaxWrite = 1000000;

private:
    QString resolve(const QString& relative, bool mustExist, QString* error) const;
    Result listFiles(const QJsonObject& args) const;
    Result readFile(const QJsonObject& args) const;
    Result searchText(const QJsonObject& args) const;
    Result writeFile(const QJsonObject& args) const;
    Result editFile(const QJsonObject& args) const;
    Result appendFile(const QJsonObject& args) const;
    Result globFiles(const QJsonObject& args) const;
    Result save(const QString& fullPath, const QString& content) const;      // backup of the old file, then an atomic write
    // the edit applied to the file: the new content, or an error text
    bool applyEdit(const QJsonObject& args, QString* fullPath, QString* oldContent, QString* newContent, QString* error) const;
public:
    // The second try of edit_file (see the .cpp): lines compared after trimming. Pure, tested.
    static bool replaceIgnoringIndentation(const QString& content, const QString& oldText, const QString& newText, bool all, QString* out, QString* error);
private:

    QString m_root;                   // canonical path of the folder
    bool m_edits;
};

} // namespace dm::ai
