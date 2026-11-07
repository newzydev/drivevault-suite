#include "Hub.h"

#include "Version.h"
#include "WinUtil.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QVariantMap>

#include <algorithm>

namespace dm {

namespace {

struct CatalogEntry {
    const char* id;
    const char* key;          // registry sub key below Products
    const char* name;
    const char* accent;
    bool planned;
    const char16_t* descEn;
    const char16_t* descTh;
};

// The DriveVault suite as it is planned. Only programs with planned == false exist today.
const CatalogEntry kCatalog[] = {
    {"drivevault.videoeditor", "VideoEditorPro", "DriveVault Video Editor Pro", "#8B5CF6", false,
     u"Professional video editing: multitrack timeline, colour, effects, titles, audio mixing and offline AI tools.",
     u"ตัดต่อวิดีโอระดับมืออาชีพ: ไทม์ไลน์หลายแทร็ก สี เอฟเฟกต์ ชื่อเรื่อง มิกซ์เสียง และเครื่องมือ AI แบบออฟไลน์"},
    {"drivevault.imageeditor", "ImageEditorPro", "DriveVault Image Editor Pro", "#3B82F6", true,
     u"Photo and graphics editing. Planned - not available yet.",
     u"ตัดต่อและแต่งภาพ อยู่ในแผน - ยังไม่พร้อมใช้งาน"},
    {"drivevault.audioeditor", "AudioEditorPro", "DriveVault Audio Editor Pro", "#10B981", true,
     u"Audio recording, editing and mastering. Planned - not available yet.",
     u"บันทึก ตัดต่อ และมาสเตอร์เสียง อยู่ในแผน - ยังไม่พร้อมใช้งาน"},
};

struct Registered {
    QString key, hive, displayName, version, installDir, executable, manifest, accent, uninstaller, publisher;
    bool exeExists = false;
};

QList<Registered> readRegistry(const QString& root)
{
    QList<Registered> out;
    for (const QString& hive : {QStringLiteral("HKEY_LOCAL_MACHINE"), QStringLiteral("HKEY_CURRENT_USER")}) {
        QSettings s(QStringLiteral("%1\\%2\\Products").arg(hive, root), QSettings::NativeFormat);
        for (const QString& key : s.childGroups()) {
            s.beginGroup(key);
            Registered r;
            r.key = key;
            r.hive = hive;
            r.displayName = s.value(QStringLiteral("DisplayName")).toString();
            r.version = s.value(QStringLiteral("Version")).toString();
            r.installDir = s.value(QStringLiteral("InstallDir")).toString();
            r.executable = s.value(QStringLiteral("Executable")).toString();
            r.manifest = s.value(QStringLiteral("Manifest")).toString();
            r.accent = s.value(QStringLiteral("AccentColor")).toString();
            r.uninstaller = s.value(QStringLiteral("Uninstaller")).toString();
            r.publisher = s.value(QStringLiteral("Publisher")).toString();
            r.exeExists = !r.executable.isEmpty() && QFileInfo::exists(r.executable);
            s.endGroup();
            out.append(r);
        }
    }
    return out;
}

QString toLocal(const QString& pathOrUrl)
{
    return pathOrUrl.startsWith(QLatin1String("file:")) ? QUrl(pathOrUrl).toLocalFile() : pathOrUrl;
}

} // namespace

Hub::Hub(const Options& options, QObject* parent)
    : QObject(parent)
    , m_options(options)
{
    m_appDir = options.appDir.isEmpty() ? QCoreApplication::applicationDirPath() : options.appDir;
    m_dataDir = options.dataDir.isEmpty()
                    ? QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/DriveVault/MasterPro")
                    : options.dataDir;
    QDir().mkpath(m_dataDir);
    QSettings s(m_dataDir + QStringLiteral("/settings.ini"), QSettings::IniFormat);
    m_language = s.value(QStringLiteral("language")).toString();
    if (m_language != QLatin1String("th") && m_language != QLatin1String("en"))
        m_language = QLocale::system().language() == QLocale::Thai ? QStringLiteral("th") : QStringLiteral("en");
    refresh();
}

Hub::~Hub()
{
    if (m_thread)
        m_thread->wait(10000);
}

QString Hub::version() const { return QString::fromLatin1(version::kVersion); }
QString Hub::buildNumber() const { return QString::fromLatin1(version::kBuildNumber); }

void Hub::setLanguage(const QString& language)
{
    if ((language != QLatin1String("th") && language != QLatin1String("en")) || language == m_language)
        return;
    m_language = language;
    QSettings(m_dataDir + QStringLiteral("/settings.ini"), QSettings::IniFormat).setValue(QStringLiteral("language"), m_language);
    emit languageChanged();
    refresh();
}

void Hub::setMessage(const QString& m)
{
    m_message = m;
    emit messageChanged();
}

QStringList Hub::userFolders() const
{
    return QSettings(m_dataDir + QStringLiteral("/settings.ini"), QSettings::IniFormat).value(QStringLiteral("installerFolders")).toStringList();
}

void Hub::saveFolders(const QStringList& folders)
{
    QSettings(m_dataDir + QStringLiteral("/settings.ini"), QSettings::IniFormat).setValue(QStringLiteral("installerFolders"), folders);
}

QStringList Hub::installerFolders() const
{
    QStringList dirs{QDir::cleanPath(m_appDir + QStringLiteral("/installers")), QDir::cleanPath(m_dataDir + QStringLiteral("/installers"))};
    for (const QString& f : userFolders())
        if (!dirs.contains(QDir::cleanPath(f)))
            dirs << QDir::cleanPath(f);
    return dirs;
}

QVariantList Hub::scanInstallers() const
{
    QVariantList found;
    for (const QString& dir : installerFolders()) {
        for (const QFileInfo& fi : QDir(dir).entryInfoList({QStringLiteral("*.exe")}, QDir::Files)) {
            const QString path = fi.absoluteFilePath();
            if (win::versionInfo(path, QStringLiteral("CompanyName")).compare(QLatin1String("DriveVault"), Qt::CaseInsensitive) != 0)
                continue;
            const QString product = win::versionInfo(path, QStringLiteral("ProductName"));
            if (product.isEmpty() || product.compare(QLatin1String("DriveVault Master Pro"), Qt::CaseInsensitive) == 0)
                continue;
            QString v = win::versionInfo(path, QStringLiteral("ProductVersion"));
            if (v.isEmpty())
                v = win::versionInfo(path, QStringLiteral("FileVersion"));
            found.append(QVariantMap{{"path", path}, {"product", product}, {"version", v}});
        }
    }
    return found;
}

void Hub::refresh()
{
    const bool th = m_language == QLatin1String("th");
    const QList<Registered> regs = readRegistry(m_options.registryRoot);
    const QVariantList installers = scanInstallers();

    auto bestInstaller = [&](const QString& name) {
        QVariantMap best;
        for (const QVariant& v : installers) {
            const QVariantMap m = v.toMap();
            if (m.value("product").toString().compare(name, Qt::CaseInsensitive) != 0)
                continue;
            if (best.isEmpty() || win::compareVersions(m.value("version").toString(), best.value("version").toString()) > 0)
                best = m;
        }
        return best;
    };
    auto fill = [&](QVariantMap& p, const Registered* reg, const QString& name) {
        const QVariantMap inst = bestInstaller(name);
        p["installerPath"] = inst.value("path").toString();
        p["installerVersion"] = inst.value("version").toString();
        const bool installed = reg && reg->exeExists;
        p["installed"] = installed;
        p["version"] = reg ? reg->version : QString();
        p["installDir"] = reg ? reg->installDir : QString();
        p["executable"] = reg ? reg->executable : QString();
        p["uninstaller"] = reg ? reg->uninstaller : QString();
        p["scope"] = reg ? (reg->hive.contains(QLatin1String("LOCAL_MACHINE")) ? QStringLiteral("allUsers") : QStringLiteral("currentUser")) : QString();
        p["updateAvailable"] = installed && !inst.isEmpty() && win::compareVersions(inst.value("version").toString(), reg->version) > 0;
        p["canInstall"] = !inst.isEmpty() && !installed;
        p["canUninstall"] = installed && !reg->uninstaller.isEmpty() && QFileInfo::exists(reg->uninstaller);
        p["broken"] = reg && !reg->exeExists;
    };

    QVariantList list;
    QStringList usedKeys;
    for (const CatalogEntry& c : kCatalog) {
        QVariantMap p{{"id", QString::fromLatin1(c.id)}, {"key", QString::fromLatin1(c.key)}, {"name", QString::fromLatin1(c.name)},
                      {"accent", QString::fromLatin1(c.accent)}, {"planned", c.planned},
                      {"description", QString::fromUtf16(th ? c.descTh : c.descEn)}};
        const Registered* pick = nullptr;
        for (const Registered& r : regs)
            if (r.key.compare(QLatin1String(c.key), Qt::CaseInsensitive) == 0 && (!pick || (r.exeExists && !pick->exeExists)))
                pick = &r;
        usedKeys << QString::fromLatin1(c.key).toLower();
        fill(p, pick, QString::fromLatin1(c.name));
        p["status"] = p.value("installed").toBool() ? "installed" : p.value("broken").toBool() ? "broken" : c.planned ? "comingSoon" : "notInstalled";
        list.append(p);
    }
    // programs this hub does not know yet (a newer product, or a third-party tool that follows the contract)
    for (const Registered& r : regs) {
        if (usedKeys.contains(r.key.toLower()))
            continue;
        usedKeys << r.key.toLower();
        QString name = r.displayName.isEmpty() ? r.key : r.displayName;
        QString id = QStringLiteral("registry.") + r.key.toLower();
        if (QFile f(r.manifest); !r.manifest.isEmpty() && f.open(QIODevice::ReadOnly)) {
            const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            if (!o.value("id").toString().isEmpty())
                id = o.value("id").toString();
        }
        QVariantMap p{{"id", id}, {"key", r.key}, {"name", name}, {"accent", r.accent.isEmpty() ? QStringLiteral("#6B7280") : r.accent},
                      {"planned", false}, {"description", r.publisher.isEmpty() ? QString() : r.publisher}};
        fill(p, &r, name);
        p["status"] = p.value("installed").toBool() ? "installed" : "broken";
        list.append(p);
    }
    m_products = list;
    emit productsChanged();
}

QVariantMap Hub::productById(const QString& id) const
{
    for (const QVariant& v : m_products)
        if (v.toMap().value("id").toString() == id)
            return v.toMap();
    return {};
}

QString Hub::launch(const QString& id)
{
    const QVariantMap p = productById(id);
    if (!p.value("installed").toBool())
        return tr2(QStringLiteral("This program is not installed."), QStringLiteral("ยังไม่ได้ติดตั้งโปรแกรมนี้"));
    const QString exe = p.value("executable").toString();
    if (!QProcess::startDetached(exe, {}, QFileInfo(exe).absolutePath()))
        return tr2(QStringLiteral("The program could not be started."), QStringLiteral("เปิดโปรแกรมไม่ได้"));
    setMessage(tr2(QStringLiteral("Started %1."), QStringLiteral("เปิด %1 แล้ว")).arg(p.value("name").toString()));
    return {};
}

QString Hub::openFolder(const QString& id)
{
    const QVariantMap p = productById(id);
    const QString dir = p.value("installDir").toString();
    if (dir.isEmpty() || !QFileInfo(dir).isDir())
        return tr2(QStringLiteral("The folder was not found."), QStringLiteral("ไม่พบโฟลเดอร์"));
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    return {};
}

QString Hub::run(const QString& file, const QString& arguments, const QString& workDir, const QString& startMessage, const QString& doneMessage)
{
    if (m_busy)
        return tr2(QStringLiteral("Another task is still running."), QStringLiteral("มีงานอื่นกำลังทำงานอยู่"));
    m_busy = true;
    emit busyChanged();
    setMessage(startMessage);
    auto result = std::make_shared<std::pair<int, QString>>(-1, QString());
    QPointer<Hub> self(this);
    m_thread = QThread::create([file, arguments, workDir, result] {
        QString error;
        result->first = win::runAndWait(file, arguments, workDir, &error);
        result->second = error;
    });
    connect(m_thread, &QThread::finished, this, [this, result, doneMessage] {
        m_thread = nullptr;
        m_busy = false;
        m_lastExit = result->first;
        emit busyChanged();
        setMessage(result->second.isEmpty()
                       ? (result->first == 0 ? doneMessage
                                             : tr2(QStringLiteral("Finished with exit code %1."), QStringLiteral("จบการทำงานด้วยรหัส %1")).arg(result->first))
                       : result->second);
        refresh();
        emit taskFinished(result->first);
    }, Qt::QueuedConnection);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
    return {};
}

QString Hub::uninstall(const QString& id)
{
    const QVariantMap p = productById(id);
    if (!p.value("canUninstall").toBool())
        return tr2(QStringLiteral("This program cannot be uninstalled from here (no uninstaller was registered)."),
                   QStringLiteral("ถอนการติดตั้งจากที่นี่ไม่ได้ (ไม่พบตัวถอนที่ลงทะเบียนไว้)"));
    const QString uninstaller = p.value("uninstaller").toString();
    return run(uninstaller, m_silent ? QStringLiteral("/VERYSILENT /SUPPRESSMSGBOXES /NORESTART") : QString(), QFileInfo(uninstaller).absolutePath(),
               tr2(QStringLiteral("Uninstalling %1…"), QStringLiteral("กำลังถอนการติดตั้ง %1…")).arg(p.value("name").toString()),
               tr2(QStringLiteral("%1 was uninstalled."), QStringLiteral("ถอนการติดตั้ง %1 แล้ว")).arg(p.value("name").toString()));
}

QString Hub::installFromFile(const QString& fileOrUrl)
{
    const QString path = toLocal(fileOrUrl);
    const QFileInfo fi(path);
    if (!fi.isFile() || fi.suffix().compare(QLatin1String("exe"), Qt::CaseInsensitive) != 0)
        return tr2(QStringLiteral("Choose a DriveVault setup file (.exe)."), QStringLiteral("เลือกไฟล์ติดตั้ง DriveVault (.exe)"));
    // The hub only starts setup programs that identify themselves as DriveVault products. (It does not verify signatures: the
    // installers are not signed yet.)
    if (win::versionInfo(path, QStringLiteral("CompanyName")).compare(QLatin1String("DriveVault"), Qt::CaseInsensitive) != 0)
        return tr2(QStringLiteral("This file is not a DriveVault setup program."), QStringLiteral("ไฟล์นี้ไม่ใช่ตัวติดตั้งของ DriveVault"));
    const QString product = win::versionInfo(path, QStringLiteral("ProductName"));
    return run(path, m_silent ? QStringLiteral("/VERYSILENT /SUPPRESSMSGBOXES /NORESTART") : QString(), fi.absolutePath(),
               tr2(QStringLiteral("Installing %1…"), QStringLiteral("กำลังติดตั้ง %1…")).arg(product),
               tr2(QStringLiteral("%1 is installed."), QStringLiteral("ติดตั้ง %1 แล้ว")).arg(product));
}

QString Hub::installProduct(const QString& id)
{
    const QVariantMap p = productById(id);
    const QString setup = p.value("installerPath").toString();
    if (setup.isEmpty())
        return tr2(QStringLiteral("No setup file for this program was found in the installer folders."),
                   QStringLiteral("ไม่พบไฟล์ติดตั้งของโปรแกรมนี้ในโฟลเดอร์ติดตั้ง"));
    return installFromFile(setup);
}

QString Hub::addInstallerFolder(const QString& folderOrUrl)
{
    const QString dir = QDir::cleanPath(toLocal(folderOrUrl));
    if (!QFileInfo(dir).isDir())
        return tr2(QStringLiteral("The folder was not found."), QStringLiteral("ไม่พบโฟลเดอร์"));
    QStringList f = userFolders();
    if (!f.contains(dir)) {
        f << dir;
        saveFolders(f);
    }
    refresh();
    return {};
}

QString Hub::removeInstallerFolder(const QString& folder)
{
    QStringList f = userFolders();
    f.removeAll(QDir::cleanPath(folder));
    saveFolders(f);
    refresh();
    return {};
}

QString Hub::openInstallersFolder()
{
    const QString dir = QDir::cleanPath(m_dataDir + QStringLiteral("/installers"));
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
    return {};
}

QString Hub::documentPath(const QString& fileName) const
{
    if (fileName.isEmpty() || fileName.contains(QLatin1Char('/')) || fileName.contains(QLatin1Char('\\')))
        return {};
    for (const QString& dir : {m_appDir + QStringLiteral("/licenses"), m_appDir + QStringLiteral("/docs"), m_appDir + QStringLiteral("/../docs"),
                               m_appDir + QStringLiteral("/../../docs")}) {
        const QString p = QDir(dir).filePath(fileName);
        if (QFileInfo::exists(p))
            return QDir::cleanPath(p);
    }
    return {};
}

bool Hub::openDocument(const QString& fileName) const
{
    const QString p = documentPath(fileName);
    return !p.isEmpty() && QDesktopServices::openUrl(QUrl::fromLocalFile(p));
}

void Hub::waitForIdle(int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (m_busy && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents();
}

} // namespace dm
