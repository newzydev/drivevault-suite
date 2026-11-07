#include "Hub.h"

#include "Version.h"
#include "WinUtil.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QVariantMap>

#include <memory>

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

// Release tags are "<slug>-v<x.y.z>"; the slug of every catalog program. Programs this hub does not know have no online updates.
QString slugForKey(const QString& key)
{
    if (key.compare(QLatin1String("VideoEditorPro"), Qt::CaseInsensitive) == 0) return QStringLiteral("video-editor");
    if (key.compare(QLatin1String("ImageEditorPro"), Qt::CaseInsensitive) == 0) return QStringLiteral("image-editor");
    if (key.compare(QLatin1String("AudioEditorPro"), Qt::CaseInsensitive) == 0) return QStringLiteral("audio-editor");
    return {};
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
        // online release (only known after the user pressed "Check for updates"): offered when it is newer than the installed
        // program and newer than any setup file that is already on this PC
        const auto online = m_online.constFind(slugForKey(p.value("key").toString()));
        const bool hasOnline = online != m_online.constEnd() && !p.value("planned").toBool();
        const QString localVersion = inst.value("version").toString();
        const bool newerThanLocal = hasOnline && (inst.isEmpty() || win::compareVersions(online->version, localVersion) > 0);
        p["onlineVersion"] = hasOnline ? online->version : QString();
        p["onlineUpdate"] = installed && newerThanLocal && win::compareVersions(online->version, reg->version) > 0;
        p["onlineInstall"] = !installed && newerThanLocal;
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
        if (!m_cleanupFile.isEmpty()) {          // a downloaded setup file is only kept when it did not work
            if (result->first == 0)
                QFile::remove(m_cleanupFile);
            m_cleanupFile.clear();
        }
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

QNetworkAccessManager* Hub::net()
{
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    return m_net;
}

namespace {
QNetworkRequest makeRequest(const QString& url, const QString& accept)
{
    QNetworkRequest r{QUrl(url)};
    r.setRawHeader("User-Agent", QByteArray("DriveVaultMasterPro/") + QByteArray(version::kVersion));
    if (!accept.isEmpty())
        r.setRawHeader("Accept", accept.toLatin1());
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);   // never https -> http
    r.setTransferTimeout(30000);                                                                         // 30 s without data
    return r;
}
} // namespace

QString Hub::checkForUpdates()
{
    if (m_busy || m_checking)
        return tr2(QStringLiteral("Another task is still running."), QStringLiteral("à¸¡à¸µà¸‡à¸²à¸™à¸­à¸·à¹ˆà¸™à¸à¸³à¸¥à¸±à¸‡à¸—à¸³à¸‡à¸²à¸™à¸­à¸¢à¸¹à¹ˆ"));
    m_checking = true;
    emit checkingChanged();
    setMessage(tr2(QStringLiteral("Checking GitHub for new versionsâ€¦"), QStringLiteral("à¸à¸³à¸¥à¸±à¸‡à¸•à¸£à¸§à¸ˆà¸«à¸²à¹€à¸§à¸­à¸£à¹Œà¸Šà¸±à¸™à¹ƒà¸«à¸¡à¹ˆà¸ˆà¸²à¸ GitHubâ€¦")));
    QNetworkReply* reply = net()->get(makeRequest(m_options.releasesUrl, QStringLiteral("application/vnd.github+json")));
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_checking = false;
        emit checkingChanged();
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 403 || status == 429)
                setMessage(tr2(QStringLiteral("GitHub is limiting requests from this connection. Try again in an hour."),
                               QStringLiteral("GitHub à¸ˆà¸³à¸à¸±à¸”à¸ˆà¸³à¸™à¸§à¸™à¸„à¸³à¸‚à¸­à¸ˆà¸²à¸à¸à¸²à¸£à¹€à¸Šà¸·à¹ˆà¸­à¸¡à¸•à¹ˆà¸­à¸™à¸µà¹‰ à¸¥à¸­à¸‡à¹ƒà¸«à¸¡à¹ˆà¸­à¸µà¸à¸„à¸£à¸±à¹‰à¸‡à¹ƒà¸™à¸­à¸µà¸à¸›à¸£à¸°à¸¡à¸²à¸“à¸«à¸™à¸¶à¹ˆà¸‡à¸Šà¸±à¹ˆà¸§à¹‚à¸¡à¸‡")));
            else
                setMessage(tr2(QStringLiteral("Could not check for updates: %1"), QStringLiteral("à¸•à¸£à¸§à¸ˆà¸«à¸²à¸­à¸±à¸›à¹€à¸”à¸•à¹„à¸¡à¹ˆà¸ªà¸³à¹€à¸£à¹‡à¸ˆ: %1")).arg(reply->errorString()));
            emit checkFinished(false);
            return;
        }
        m_online = releases::parse(reply->readAll());
        m_onlineChecked = true;
        refresh();
        int updates = 0;
        for (const QVariant& v : m_products)
            if (v.toMap().value("onlineUpdate").toBool())
                ++updates;
        setMessage(updates > 0 ? tr2(QStringLiteral("%1 update(s) available."), QStringLiteral("à¸¡à¸µà¸­à¸±à¸›à¹€à¸”à¸• %1 à¸£à¸²à¸¢à¸à¸²à¸£")).arg(updates)
                               : tr2(QStringLiteral("The installed programs are up to date."), QStringLiteral("à¹‚à¸›à¸£à¹à¸à¸£à¸¡à¸—à¸µà¹ˆà¸•à¸´à¸”à¸•à¸±à¹‰à¸‡à¹€à¸›à¹‡à¸™à¸£à¸¸à¹ˆà¸™à¸¥à¹ˆà¸²à¸ªà¸¸à¸”à¹à¸¥à¹‰à¸§")));
        emit checkFinished(true);
    });
    return {};
}

void Hub::failDownload(const QString& why)
{
    m_busy = false;
    m_progress = -1;
    emit busyChanged();
    emit downloadProgressChanged();
    setMessage(why);
    emit downloadFinished(false);
}

QString Hub::downloadAndInstall(const QString& id)
{
    if (m_busy || m_checking)
        return tr2(QStringLiteral("Another task is still running."), QStringLiteral("à¸¡à¸µà¸‡à¸²à¸™à¸­à¸·à¹ˆà¸™à¸à¸³à¸¥à¸±à¸‡à¸—à¸³à¸‡à¸²à¸™à¸­à¸¢à¸¹à¹ˆ"));
    const QVariantMap p = productById(id);
    const auto it = m_online.constFind(slugForKey(p.value("key").toString()));
    if (it == m_online.constEnd())
        return tr2(QStringLiteral("No online version is known. Press \"Check for updates\" first."),
                   QStringLiteral("à¸¢à¸±à¸‡à¹„à¸¡à¹ˆà¸—à¸£à¸²à¸šà¹€à¸§à¸­à¸£à¹Œà¸Šà¸±à¸™à¸­à¸­à¸™à¹„à¸¥à¸™à¹Œ à¸à¸”à¸›à¸¸à¹ˆà¸¡ \"à¸•à¸£à¸§à¸ˆà¸«à¸²à¸­à¸±à¸›à¹€à¸”à¸•\" à¸à¹ˆà¸­à¸™"));
    const releases::Release rel = *it;
    if (!releases::urlAllowed(rel.assetUrl, m_options.downloadPrefix) || !releases::urlAllowed(rel.hashUrl, m_options.downloadPrefix))
        return tr2(QStringLiteral("The release points to an address that is not the official download address. Nothing was downloaded."),
                   QStringLiteral("à¹„à¸Ÿà¸¥à¹Œà¸‚à¸­à¸‡à¸£à¸¸à¹ˆà¸™à¸™à¸µà¹‰à¹„à¸¡à¹ˆà¹„à¸”à¹‰à¸­à¸¢à¸¹à¹ˆà¸—à¸µà¹ˆà¸—à¸µà¹ˆà¸­à¸¢à¸¹à¹ˆà¸”à¸²à¸§à¸™à¹Œà¹‚à¸«à¸¥à¸”à¸—à¸²à¸‡à¸à¸²à¸£ à¸ˆà¸¶à¸‡à¹„à¸¡à¹ˆà¹„à¸”à¹‰à¸”à¸²à¸§à¸™à¹Œà¹‚à¸«à¸¥à¸”"));
    if (rel.assetName.contains(QLatin1Char('/')) || rel.assetName.contains(QLatin1Char('\\')) || rel.assetName.contains(QLatin1String("..")))
        return tr2(QStringLiteral("The release has an invalid file name."), QStringLiteral("à¸Šà¸·à¹ˆà¸­à¹„à¸Ÿà¸¥à¹Œà¸‚à¸­à¸‡à¸£à¸¸à¹ˆà¸™à¸™à¸µà¹‰à¹„à¸¡à¹ˆà¸–à¸¹à¸à¸•à¹‰à¸­à¸‡"));

    m_busy = true;
    m_progress = 0;
    emit busyChanged();
    emit downloadProgressChanged();
    const QString name = p.value("name").toString();
    setMessage(tr2(QStringLiteral("Getting the checksum of %1 %2â€¦"), QStringLiteral("à¸à¸³à¸¥à¸±à¸‡à¸­à¹ˆà¸²à¸™à¸„à¹ˆà¸²à¸•à¸£à¸§à¸ˆà¸ªà¸­à¸šà¸‚à¸­à¸‡ %1 %2â€¦")).arg(name, rel.version));
    QNetworkReply* hashReply = net()->get(makeRequest(rel.hashUrl, QString()));
    connect(hashReply, &QNetworkReply::finished, this, [this, hashReply, rel, name] {
        hashReply->deleteLater();
        if (hashReply->error() != QNetworkReply::NoError)
            return failDownload(tr2(QStringLiteral("Could not download the checksum file: %1"), QStringLiteral("à¸”à¸²à¸§à¸™à¹Œà¹‚à¸«à¸¥à¸”à¹„à¸Ÿà¸¥à¹Œà¸„à¹ˆà¸²à¸•à¸£à¸§à¸ˆà¸ªà¸­à¸šà¹„à¸¡à¹ˆà¸ªà¸³à¹€à¸£à¹‡à¸ˆ: %1")).arg(hashReply->errorString()));
        const QString expected = releases::parseSha256(QString::fromUtf8(hashReply->read(4096)), rel.assetName);
        if (expected.isEmpty())
            return failDownload(tr2(QStringLiteral("The checksum file of this release is not valid. Nothing was installed."),
                                    QStringLiteral("à¹„à¸Ÿà¸¥à¹Œà¸„à¹ˆà¸²à¸•à¸£à¸§à¸ˆà¸ªà¸­à¸šà¸‚à¸­à¸‡à¸£à¸¸à¹ˆà¸™à¸™à¸µà¹‰à¹„à¸¡à¹ˆà¸–à¸¹à¸à¸•à¹‰à¸­à¸‡ à¸ˆà¸¶à¸‡à¹„à¸¡à¹ˆà¹„à¸”à¹‰à¸•à¸´à¸”à¸•à¸±à¹‰à¸‡")));
        startAssetDownload(rel, name, expected);
    });
    return {};
}

void Hub::startAssetDownload(const releases::Release& rel, const QString& productName, const QString& expectedHash)
{
    const QString dir = QDir::cleanPath(m_dataDir + QStringLiteral("/installers"));
    QDir().mkpath(dir);
    const QString target = dir + QLatin1Char('/') + rel.assetName;
    auto file = std::make_shared<QFile>(target + QStringLiteral(".part"));
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate))
        return failDownload(tr2(QStringLiteral("Could not write to %1."), QStringLiteral("à¹€à¸‚à¸µà¸¢à¸™à¹„à¸Ÿà¸¥à¹Œà¸—à¸µà¹ˆ %1 à¹„à¸¡à¹ˆà¹„à¸”à¹‰")).arg(dir));
    setMessage(tr2(QStringLiteral("Downloading %1 %2â€¦"), QStringLiteral("à¸à¸³à¸¥à¸±à¸‡à¸”à¸²à¸§à¸™à¹Œà¹‚à¸«à¸¥à¸” %1 %2â€¦")).arg(productName, rel.version));
    QNetworkReply* reply = net()->get(makeRequest(rel.assetUrl, QStringLiteral("application/octet-stream")));
    connect(reply, &QNetworkReply::readyRead, this, [reply, file] { file->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, rel](qint64 received, qint64 total) {
        const qint64 whole = total > 0 ? total : rel.size;
        m_progress = whole > 0 ? qBound(0.0, double(received) / double(whole), 1.0) : 0.0;
        emit downloadProgressChanged();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, rel, productName, expectedHash, target] {
        reply->deleteLater();
        file->write(reply->readAll());
        file->close();
        const QString partName = file->fileName();
        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(partName);
            return failDownload(tr2(QStringLiteral("Download failed: %1"), QStringLiteral("à¸”à¸²à¸§à¸™à¹Œà¹‚à¸«à¸¥à¸”à¹„à¸¡à¹ˆà¸ªà¸³à¹€à¸£à¹‡à¸ˆ: %1")).arg(reply->errorString()));
        }
        QFile check(partName);
        QCryptographicHash sha(QCryptographicHash::Sha256);
        if (!check.open(QIODevice::ReadOnly) || !sha.addData(&check)) {
            QFile::remove(partName);
            return failDownload(tr2(QStringLiteral("The downloaded file could not be read."), QStringLiteral("à¸­à¹ˆà¸²à¸™à¹„à¸Ÿà¸¥à¹Œà¸—à¸µà¹ˆà¸”à¸²à¸§à¸™à¹Œà¹‚à¸«à¸¥à¸”à¹„à¸¡à¹ˆà¹„à¸”à¹‰")));
        }
        check.close();
        if (QString::fromLatin1(sha.result().toHex()) != expectedHash) {
            QFile::remove(partName);
            return failDownload(tr2(QStringLiteral("The checksum (SHA-256) of the download does not match. The file was deleted and nothing was installed."),
                                    QStringLiteral("à¸„à¹ˆà¸²à¸•à¸£à¸§à¸ˆà¸ªà¸­à¸š (SHA-256) à¸‚à¸­à¸‡à¹„à¸Ÿà¸¥à¹Œà¸—à¸µà¹ˆà¸”à¸²à¸§à¸™à¹Œà¹‚à¸«à¸¥à¸”à¹„à¸¡à¹ˆà¸•à¸£à¸‡à¸à¸±à¸™ à¸¥à¸šà¹„à¸Ÿà¸¥à¹Œà¹à¸¥à¹‰à¸§à¹à¸¥à¸°à¹„à¸¡à¹ˆà¹„à¸”à¹‰à¸•à¸´à¸”à¸•à¸±à¹‰à¸‡")));
        }
        QFile::remove(target);
        if (!QFile::rename(partName, target)) {
            QFile::remove(partName);
            return failDownload(tr2(QStringLiteral("Could not save the downloaded file."), QStringLiteral("à¸šà¸±à¸™à¸—à¸¶à¸à¹„à¸Ÿà¸¥à¹Œà¸—à¸µà¹ˆà¸”à¸²à¸§à¸™à¹Œà¹‚à¸«à¸¥à¸”à¹„à¸¡à¹ˆà¹„à¸”à¹‰")));
        }
        // the file must be what the release says it is
        const QString company = win::versionInfo(target, QStringLiteral("CompanyName"));
        const QString product = win::versionInfo(target, QStringLiteral("ProductName"));
        QString v = win::versionInfo(target, QStringLiteral("ProductVersion"));
        if (v.isEmpty())
            v = win::versionInfo(target, QStringLiteral("FileVersion"));
        if (company.compare(QLatin1String("DriveVault"), Qt::CaseInsensitive) != 0 || product.compare(productName, Qt::CaseInsensitive) != 0
            || win::compareVersions(v, rel.version) != 0) {
            QFile::remove(target);
            return failDownload(tr2(QStringLiteral("The downloaded file is not %1 %2. It was deleted."), QStringLiteral("à¹„à¸Ÿà¸¥à¹Œà¸—à¸µà¹ˆà¸”à¸²à¸§à¸™à¹Œà¹‚à¸«à¸¥à¸”à¹„à¸¡à¹ˆà¹ƒà¸Šà¹ˆ %1 %2 à¸¥à¸šà¹„à¸Ÿà¸¥à¹Œà¹à¸¥à¹‰à¸§")).arg(productName, rel.version));
        }
        m_busy = false;
        m_progress = -1;
        emit busyChanged();
        emit downloadProgressChanged();
        emit downloadFinished(true);
        m_cleanupFile = target;
        const QString error = installFromFile(target);
        if (!error.isEmpty()) {
            m_cleanupFile.clear();
            setMessage(error);
        }
    });
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
