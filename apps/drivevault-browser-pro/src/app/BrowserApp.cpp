#include "BrowserApp.h"
#include <QImage>
#include <QQuickItemGrabResult>

#include "UrlUtil.h"
#include "Version.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPrintDialog>
#include <QPrinter>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSet>
#include <QVariantMap>
#include <QtWebEngineCore/QWebEngineCookieStore>
#include <QtWebEngineCore/QWebEnginePermission>
#include <QtWebEngineCore/qtwebenginecoreglobal.h>
#include <QtWebEngineQuick/QQuickWebEngineProfile>
#include <fpdfview.h>

namespace dv::br {

BrowserApp::BrowserApp(const QString& dataDir, QQuickWebEngineProfile* profile, AdBlockInterceptor* interceptor, StandInHandler* standIns, QObject* parent)
    : QObject(parent), m_dataDir(dataDir), m_profile(profile),
      m_settings(dataDir + QStringLiteral("/settings.json")), m_bookmarks(dataDir + QStringLiteral("/bookmarks.json")),
      m_shortcuts(dataDir + QStringLiteral("/shortcuts.json")),
      m_history(dataDir + QStringLiteral("/history.jsonl")),
      m_adblock(&m_settings, interceptor, standIns, dataDir, QCoreApplication::applicationDirPath() + QStringLiteral("/filters")),
      m_media(interceptor, &m_downloads)
{
    if (!QFileInfo::exists(dataDir + QStringLiteral("/shortcuts.json"))) {
        m_shortcuts.add(QStringLiteral("Google"), QStringLiteral("https://www.google.com/"), QStringLiteral("other"));
        for (const QVariant& entry : m_bookmarks.items()) {
            const QVariantMap item = entry.toMap();
            const QString url = item.value(QStringLiteral("url")).toString();
            if (url.compare(QStringLiteral("https://www.google.com"), Qt::CaseInsensitive) != 0)
                m_shortcuts.add(item.value(QStringLiteral("title")).toString(), url, QStringLiteral("other"));
        }
    }
    m_media.setUserAgent(profile ? profile->httpUserAgent() : QString());
    QFile library(QStringLiteral(":/qt/qml/DriveVault/Browser/resources/adblock-page.js"));       // the script that runs inside pages (hiding, scriptlets)
    if (library.open(QIODevice::ReadOnly))
        m_adblock.setPageLibrary(QString::fromUtf8(library.readAll()));
    auto readResource = [](const char* name) {
        QFile f(QStringLiteral(":/qt/qml/DriveVault/Browser/resources/") + QString::fromLatin1(name));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };
    m_pipOn = readResource("pip-on.js");
    m_pipOff = readResource("pip-off.js");
}

QString BrowserApp::version() const { return QString::fromLatin1(version::kVersion); }

QString BrowserApp::engineInfo() const
{
    return QStringLiteral("Qt %1 · Chromium %2").arg(QString::fromLatin1(qVersion()), QString::fromLatin1(qWebEngineChromiumVersion()));
}

QUrl BrowserApp::resolve(const QString& typed) const { return resolveInput(typed, m_settings.searchEngine()); }
QString BrowserApp::prettyUrl(const QString& url) const { return dv::br::prettyUrl(url); }
QString BrowserApp::hostOf(const QString& url) const { return dv::br::hostOf(url); }
bool BrowserApp::isGoogleSignInPage(const QUrl& url) const { return dv::br::isGoogleSignInPage(url); }

bool BrowserApp::hasIconPixels(QObject* grabResult) const
{
    const auto* result = qobject_cast<QQuickItemGrabResult*>(grabResult);
    if (!result)
        return false;
    const QImage image = result->image().convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < image.height(); ++y) {
        const auto* pixels = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(pixels[x]) > 0)
                return true;
    }
    return false;
}

QVariantList BrowserApp::suggest(const QString& text, int limit) const
{
    QVariantList out;
    const QString q = text.trimmed();
    if (q.isEmpty())
        return out;
    QSet<QString> seen;
    auto push = [&](const QString& title, const QString& url, const char* kind) {
        if (out.size() >= limit || seen.contains(url))
            return;
        seen.insert(url);
        out.append(QVariantMap{{QStringLiteral("title"), title}, {QStringLiteral("url"), url}, {QStringLiteral("kind"), QString::fromLatin1(kind)}});
    };
    for (const QVariant& v : m_bookmarks.items()) {
        const QVariantMap m = v.toMap();
        if (m.value(QStringLiteral("title")).toString().contains(q, Qt::CaseInsensitive) || m.value(QStringLiteral("url")).toString().contains(q, Qt::CaseInsensitive))
            push(m.value(QStringLiteral("title")).toString(), m.value(QStringLiteral("url")).toString(), "bookmark");
    }
    for (const QVariant& v : m_history.search(q, 30)) {
        const QVariantMap m = v.toMap();
        push(m.value(QStringLiteral("title")).toString(), m.value(QStringLiteral("url")).toString(), "history");
    }
    return out;
}

QString BrowserApp::localPath(const QUrl& url) const { return url.toLocalFile(); }
qint64 BrowserApp::fileSize(const QString& path) const
{
    const QFileInfo fi(path);
    return fi.exists() ? fi.size() : -1;
}

void BrowserApp::openWindow(bool privateMode, const QString& url) { emit windowRequested(privateMode, url); }

QStringList BrowserApp::loadSession() const
{
    QFile f(m_dataDir + QStringLiteral("/session.json"));
    QStringList urls;
    if (f.open(QIODevice::ReadOnly))
        for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).array())
            if (BookmarkStore::allowedUrl(v.toString()))
                urls << v.toString();
    return urls;
}

void BrowserApp::saveSession(const QStringList& urls) const
{
    QJsonArray arr;
    for (const QString& u : urls)
        if (BookmarkStore::allowedUrl(u))
            arr.append(u);
    QSaveFile f(m_dataDir + QStringLiteral("/session.json"));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

void BrowserApp::copyText(const QString& text) const
{
    if (QClipboard* c = QGuiApplication::clipboard())
        c->setText(text);
}

void BrowserApp::clearBrowsingData(bool history, bool cache)
{
    if (history)
        m_history.clear();
    if (cache && m_profile)
        m_profile->clearHttpCache();
}

int BrowserApp::printPdf(const QByteArray& pdf, const QString& title)
{
    if (pdf.isEmpty())
        return -1;
    if (m_printing)
        return 0;
    const QScopedValueRollback printing(m_printing, true);
    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(title);
    QPrintDialog dialog(&printer);
    dialog.setOption(QAbstractPrintDialog::PrintPageRange, false);
    if (dialog.exec() != QDialog::Accepted)
        return 0;

    FPDF_InitLibrary();
    const auto document = FPDF_LoadMemDocument64(pdf.constData(), size_t(pdf.size()), nullptr);
    QPainter painter;
    const int pageCount = document ? FPDF_GetPageCount(document) : 0;
    bool ok = pageCount > 0 && painter.begin(&printer);
    for (int i = 0; ok && i < pageCount; ++i) {
        if (i > 0 && !printer.newPage()) {
            ok = false;
            break;
        }
        const auto page = FPDF_LoadPage(document, i);
        const QSizeF size(page ? FPDF_GetPageWidthF(page) : 0, page ? FPDF_GetPageHeightF(page) : 0);
        if (size.isEmpty()) {
            if (page) FPDF_ClosePage(page);
            ok = false;
            break;
        }
        const QRectF area = printer.pageRect(QPrinter::DevicePixel);
        const QSizeF fitted = size.scaled(area.size(), Qt::KeepAspectRatio);
        const QSize pixels = fitted.toSize().scaled(QSize(3508, 3508), Qt::KeepAspectRatio).boundedTo(fitted.toSize());
        QImage image(pixels, QImage::Format_RGB32);
        const auto bitmap = image.isNull() ? nullptr
            : FPDFBitmap_CreateEx(image.width(), image.height(), FPDFBitmap_BGRx, image.bits(), int(image.bytesPerLine()));
        if (bitmap) {
            image.fill(Qt::white);
            FPDF_RenderPageBitmap(bitmap, page, 0, 0, image.width(), image.height(), 0, FPDF_PRINTING | FPDF_ANNOT);
            FPDFBitmap_Destroy(bitmap);
            painter.drawImage(QRectF(QPointF((area.width() - fitted.width()) / 2, (area.height() - fitted.height()) / 2), fitted), image);
        } else {
            ok = false;
        }
        FPDF_ClosePage(page);
    }
    if (!ok && painter.isActive())
        printer.abort();
    if (painter.isActive() && !painter.end())
        ok = false;
    if (document)
        FPDF_CloseDocument(document);
    FPDF_DestroyLibrary();
    return ok ? 1 : -1;
}

void BrowserApp::clearCookies()
{
    m_profile->cookieStore()->deleteAllCookies();
}

QUrl BrowserApp::chromeProfilesFolder() const
{
    const QString localData = qEnvironmentVariable("LOCALAPPDATA");
    if (localData.isEmpty())
        return {};
    const QString folder = localData + QStringLiteral("/Google/Chrome/User Data");
    return QFileInfo(folder).isDir() ? QUrl::fromLocalFile(folder) : QUrl();
}

QStringList BrowserApp::permissionOrigins() const
{
    QSet<QString> origins;
    for (const auto& permission : m_profile->listAllPermissions()) {
        if (permission.state() == QWebEnginePermission::State::Granted
            || permission.state() == QWebEnginePermission::State::Denied)
            origins.insert(permission.origin().toString());
    }
    QStringList result = origins.values();
    result.sort(Qt::CaseInsensitive);
    return result;
}

void BrowserApp::resetPermissions(const QUrl& origin)
{
    for (const auto& permission : m_profile->listPermissionsForOrigin(origin))
        permission.reset();
}

} // namespace dv::br
