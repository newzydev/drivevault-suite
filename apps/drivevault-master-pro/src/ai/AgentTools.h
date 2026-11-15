#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace dm::ai {

// The tools of the local agent. They work ONLY inside the folder the user chose: a path is relative to it, may not leave it (no "..", no drive, no ":" stream
// names, no link that points outside), and there is no tool that runs a program, deletes, renames or reaches the network. Reading is always possible; writing
// only when the user allowed edits, and a file that is overwritten is first copied to <folder>\.drivevault-backup.
class AgentTools {
public:
    AgentTools(const QString& folder, bool allowEdits);

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

    QJsonArray specs() const;                                         // the tools in the OpenAI "tools" format (write_file only when edits are allowed)
    Result run(const QString& name, const QJsonObject& args) const;
    static QString describe(const QString& name, const QJsonObject& args);   // "Read: notes.txt" for the activity line

    static constexpr qint64 kMaxRead = 60000;                          // bytes of one file shown to the model
    static constexpr qint64 kMaxWrite = 1000000;

private:
    QString resolve(const QString& relative, bool mustExist, QString* error) const;
    Result listFiles(const QJsonObject& args) const;
    Result readFile(const QJsonObject& args) const;
    Result searchText(const QJsonObject& args) const;
    Result writeFile(const QJsonObject& args) const;

    QString m_root;                   // canonical path of the folder
    bool m_edits;
};

} // namespace dm::ai
