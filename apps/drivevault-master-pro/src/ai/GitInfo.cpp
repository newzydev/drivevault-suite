#include "GitInfo.h"

#include "ProcessUtil.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <functional>

namespace dm::ai {

GitInfo::GitInfo(QObject* parent)
    : QObject(parent)
{
    m_info = QVariantMap{{"state", "none"}, {"branch", QString()}, {"count", 0}, {"added", 0}, {"removed", 0}, {"files", QVariantList()}};
}

GitInfo::~GitInfo()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

QString GitInfo::gitExecutable()
{
    QString exe = QStandardPaths::findExecutable(QStringLiteral("git"));
    if (!exe.isEmpty())
        return exe;
    for (const QString& candidate : {qEnvironmentVariable("ProgramFiles") + QStringLiteral("/Git/cmd/git.exe"),
                                     qEnvironmentVariable("ProgramFiles(x86)") + QStringLiteral("/Git/cmd/git.exe"),
                                     qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Programs/Git/cmd/git.exe")})
        if (QFileInfo::exists(candidate))
            return candidate;
    return {};
}

// ---- parsing (pure) ----------------------------------------------------------------------------------------------------------------------
QVariantMap GitInfo::parseStatus(const QString& porcelain)
{
    QVariantMap out{{"state", "ok"}, {"branch", QString()}, {"detached", false}, {"ahead", 0}, {"behind", 0}};
    QVariantList files;
    int count = 0;
    for (const QString& raw : porcelain.split(QLatin1Char('\n'))) {
        QString line = raw;
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1String("## "))) {                          // "## main...origin/main [ahead 1, behind 2]", "## No commits yet on main", "## HEAD (no branch)"
            QString b = line.mid(3);
            if (b.startsWith(QLatin1String("No commits yet on ")))
                b = b.mid(18);
            else if (b.startsWith(QLatin1String("Initial commit on ")))
                b = b.mid(18);
            const int bracket = b.indexOf(QLatin1String(" ["));
            if (bracket > 0) {
                const QString ab = b.mid(bracket + 2);
                const auto grab = [&ab](const QString& word) {
                    const int i = ab.indexOf(word + QLatin1Char(' '));
                    return i < 0 ? 0 : ab.mid(i + word.size() + 1).section(QRegularExpression(QStringLiteral("[^0-9]")), 0, 0).toInt();
                };
                out["ahead"] = grab(QStringLiteral("ahead"));
                out["behind"] = grab(QStringLiteral("behind"));
                b = b.left(bracket);
            }
            const int dots = b.indexOf(QLatin1String("..."));
            if (dots > 0)
                b = b.left(dots);
            if (b.startsWith(QLatin1String("HEAD (no branch)"))) {
                out["detached"] = true;
                b = QStringLiteral("HEAD");
            }
            out["branch"] = b;
            continue;
        }
        if (line.size() < 4)
            continue;
        const QChar x = line.at(0), y = line.at(1);
        QString path = line.mid(3);
        const int arrow = path.indexOf(QLatin1String(" -> "));
        if (arrow >= 0)
            path = path.mid(arrow + 4);                                         // a rename: the new name
        if (path.startsWith(QLatin1Char('"')) && path.endsWith(QLatin1Char('"')) && path.size() >= 2)
            path = path.mid(1, path.size() - 2);
        QChar status;
        if (x == QLatin1Char('?') && y == QLatin1Char('?'))
            status = QLatin1Char('?');
        else if (x == QLatin1Char('U') || y == QLatin1Char('U') || (x == QLatin1Char('A') && y == QLatin1Char('A')) || (x == QLatin1Char('D') && y == QLatin1Char('D')))
            status = QLatin1Char('U');
        else
            status = x != QLatin1Char(' ') ? x : y;
        ++count;
        if (files.size() < 200)
            files << QVariantMap{{"path", path}, {"status", QString(status)}, {"added", -1}, {"removed", -1}};
    }
    out["files"] = files;
    out["count"] = count;
    out["added"] = 0;
    out["removed"] = 0;
    return out;
}

// `git diff --numstat HEAD`: "<added>\t<removed>\t<path>" (a binary file has "-"). The numbers are put on the files of the list and summed up.
void GitInfo::applyNumstat(QVariantMap& info, const QString& numstat)
{
    QVariantList files = info.value("files").toList();
    int added = 0, removed = 0;
    for (const QString& line : numstat.split(QLatin1Char('\n'))) {
        const QStringList p = line.trimmed().split(QLatin1Char('\t'));
        if (p.size() < 3)
            continue;
        bool okA = false, okR = false;
        const int a = p.at(0).toInt(&okA), r = p.at(1).toInt(&okR);
        if (!okA || !okR)
            continue;
        added += a;
        removed += r;
        const QString path = p.mid(2).join(QLatin1Char('\t'));
        for (int i = 0; i < files.size(); ++i) {
            QVariantMap f = files.at(i).toMap();
            if (f.value("path").toString() == path) {
                f["added"] = a;
                f["removed"] = r;
                files[i] = f;
                break;
            }
        }
    }
    info["files"] = files;
    info["added"] = added;
    info["removed"] = removed;
}

// ---- running git ----------------------------------------------------------------------------------------------------------------------------
void GitInfo::run(const QStringList& arguments, int generation, const std::function<void(int, const QString&, const QString&)>& done)
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
    auto* p = new QProcess(this);
    m_process = p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    p->setProcessEnvironment(env);
    p->setWorkingDirectory(m_folder);
    p->setProgram(gitExecutable());
    p->setArguments(QStringList{QStringLiteral("--no-optional-locks"), QStringLiteral("-c"), QStringLiteral("core.quotepath=false")} + arguments);
    hideConsole(p);
    auto* timer = new QTimer(p);                              // a git that hangs is stopped
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, p, [p] { p->kill(); });
    connect(p, &QProcess::finished, this, [this, p, generation, done](int code, QProcess::ExitStatus status) {
        const QString out = QString::fromUtf8(p->readAllStandardOutput().left(2000000));
        const QString err = QString::fromUtf8(p->readAllStandardError().left(4000));
        if (m_process == p)
            m_process = nullptr;
        p->deleteLater();
        if (generation == m_generation)
            done(status == QProcess::NormalExit ? code : -1, out, err);
    });
    connect(p, &QProcess::errorOccurred, this, [this, p, generation, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        if (m_process == p)
            m_process = nullptr;
        p->deleteLater();
        if (generation == m_generation)
            done(-1, {}, p->errorString());
    });
    p->start();
    timer->start(10000);
}

void GitInfo::finishWith(const QVariantMap& info)
{
    m_info = info;
    emit changed();
}

void GitInfo::refresh(const QString& folder)
{
    ++m_generation;
    const int gen = m_generation;
    m_folder = folder;
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
        finishWith(QVariantMap{{"state", "none"}, {"branch", QString()}, {"count", 0}, {"added", 0}, {"removed", 0}, {"files", QVariantList()}});
        return;
    }
    if (gitExecutable().isEmpty()) {
        finishWith(QVariantMap{{"state", "noGit"}, {"branch", QString()}, {"count", 0}, {"added", 0}, {"removed", 0}, {"files", QVariantList()}});
        return;
    }
    run({QStringLiteral("status"), QStringLiteral("--porcelain=v1"), QStringLiteral("-b"), QStringLiteral("--untracked-files=normal")}, gen,
        [this, gen](int code, const QString& out, const QString& err) {
            if (code != 0) {
                const bool notRepo = err.contains(QLatin1String("not a git repository"), Qt::CaseInsensitive);
                finishWith(QVariantMap{{"state", notRepo ? "notRepo" : "error"}, {"branch", QString()}, {"count", 0}, {"added", 0}, {"removed", 0},
                                       {"files", QVariantList()}, {"error", err.simplified().left(200)}});
                return;
            }
            QVariantMap info = parseStatus(out);
            if (info.value("count").toInt() == 0) {                               // nothing changed: no second question
                finishWith(info);
                return;
            }
            run({QStringLiteral("diff"), QStringLiteral("--numstat"), QStringLiteral("HEAD")}, gen, [this, info](int code2, const QString& out2, const QString&) mutable {
                if (code2 == 0)                                                   // (a repository without a commit has no HEAD: the numbers stay unknown)
                    applyNumstat(info, out2);
                finishWith(info);
            });
        });
}

} // namespace dm::ai
