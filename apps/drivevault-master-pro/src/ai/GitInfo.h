#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <functional>

class QProcess;

namespace dm::ai {

// What git says about the working folder of the agent page: the branch and the changed files (with how many lines were added / removed). READ ONLY: it runs
// `git --no-optional-locks status --porcelain=v1 -b` and `git --no-optional-locks diff --numstat HEAD` with fixed arguments in the folder (no user text on the
// command line, no prompt, no lock on the index), never anything that changes the repository. Needs `git` on this PC (PATH or the usual Git for Windows folders).
class GitInfo : public QObject {
    Q_OBJECT
public:
    explicit GitInfo(QObject* parent = nullptr);
    ~GitInfo() override;

    // {state: none | checking | ok | notRepo | noGit | error, branch, detached, ahead, behind, count, added, removed, files: [{path, status, added, removed}], error}
    // status: M modified, A added, D deleted, R renamed, ? new (not tracked), U conflict. `files` holds at most 200.
    QVariantMap info() const { return m_info; }
    void refresh(const QString& folder);                      // asynchronous: changed() comes when the answer is there
    static QString gitExecutable();                           // "" when git is not found

    // pure and tested
    static QVariantMap parseStatus(const QString& porcelain);
    static void applyNumstat(QVariantMap& info, const QString& numstat);

signals:
    void changed();

private:
    void run(const QStringList& arguments, int generation, const std::function<void(int, const QString&, const QString&)>& done);
    void finishWith(const QVariantMap& info);

    QVariantMap m_info;
    QString m_folder;
    int m_generation = 0;                                     // bumps with every refresh: an older answer is dropped
    QProcess* m_process = nullptr;
};

} // namespace dm::ai
