#include "DocumentController.h"

#include "dp/Version.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QMutexLocker>
#include <QSettings>

namespace dp {

namespace {
constexpr int kMaxRecent = 10;

QString errorCode(pdf::OpenError e)
{
    switch (e) {
    case pdf::OpenError::Password: return QStringLiteral("password");
    case pdf::OpenError::Format: return QStringLiteral("format");
    case pdf::OpenError::File: return QStringLiteral("file");
    case pdf::OpenError::TooLarge: return QStringLiteral("tooLarge");
    default: return QStringLiteral("unknown");
    }
}
} // namespace

DocumentController::DocumentController(const QString& dataDir, QObject* parent)
    : QObject(parent)
    , m_dataDir(dataDir)
{
    QDir().mkpath(m_dataDir);
    QSettings s(m_dataDir + QStringLiteral("/settings.ini"), QSettings::IniFormat);
    m_recent = s.value(QStringLiteral("recentFiles")).toStringList();
    m_language = s.value(QStringLiteral("language")).toString();
    if (m_language != QLatin1String("th") && m_language != QLatin1String("en"))
        m_language = QLocale::system().language() == QLocale::Thai ? QStringLiteral("th") : QStringLiteral("en");
}

bool DocumentController::isOpen() const
{
    QMutexLocker lock(&m_mutex);
    return bool(m_doc);
}

int DocumentController::pageCount() const
{
    QMutexLocker lock(&m_mutex);
    return m_doc ? m_doc->pageCount() : 0;
}

QString DocumentController::fileName() const
{
    QMutexLocker lock(&m_mutex);
    return m_doc ? QFileInfo(m_doc->path()).fileName() : QString();
}

QString DocumentController::filePath() const
{
    QMutexLocker lock(&m_mutex);
    return m_doc ? m_doc->path() : QString();
}

QString DocumentController::version() const { return QString::fromLatin1(version::kVersion); }
QString DocumentController::buildNumber() const { return QString::fromLatin1(version::kBuildNumber); }

void DocumentController::setLanguage(const QString& lang)
{
    if ((lang != QLatin1String("th") && lang != QLatin1String("en")) || lang == m_language)
        return;
    m_language = lang;
    QSettings(m_dataDir + QStringLiteral("/settings.ini"), QSettings::IniFormat).setValue(QStringLiteral("language"), lang);
    emit languageChanged();
}

QString DocumentController::localPath(const QString& pathOrUrl) const
{
    if (pathOrUrl.startsWith(QLatin1String("file:")))
        return QUrl(pathOrUrl).toLocalFile();
    return pathOrUrl;
}

QString DocumentController::open(const QString& pathOrUrl, const QString& password)
{
    const QString path = QDir::toNativeSeparators(QFileInfo(localPath(pathOrUrl)).absoluteFilePath());
    pdf::OpenError error = pdf::OpenError::Unknown;
    auto doc = pdf::PdfDocument::open(path, password, &error);
    if (!doc)
        return errorCode(error);
    {
        QMutexLocker lock(&m_mutex);
        m_doc = doc;
        ++m_docId;
    }
    addRecent(path);
    emit documentChanged();
    return {};
}

void DocumentController::close()
{
    {
        QMutexLocker lock(&m_mutex);
        if (!m_doc)
            return;
        m_doc.reset();
        ++m_docId;
    }
    emit documentChanged();
}

double DocumentController::pageWidthPt(int page) const
{
    QMutexLocker lock(&m_mutex);
    return m_doc ? m_doc->pageSizePt(page).width() : 0.0;
}

double DocumentController::pageHeightPt(int page) const
{
    QMutexLocker lock(&m_mutex);
    return m_doc ? m_doc->pageSizePt(page).height() : 0.0;
}

std::shared_ptr<pdf::PdfDocument> DocumentController::documentFor(int id) const
{
    QMutexLocker lock(&m_mutex);
    return id == m_docId ? m_doc : nullptr;
}

void DocumentController::addRecent(const QString& path)
{
    m_recent.removeAll(path);
    m_recent.prepend(path);
    while (m_recent.size() > kMaxRecent)
        m_recent.removeLast();
    saveRecent();
}

void DocumentController::removeRecent(const QString& path)
{
    if (m_recent.removeAll(path) > 0)
        saveRecent();
}

void DocumentController::saveRecent()
{
    QSettings(m_dataDir + QStringLiteral("/settings.ini"), QSettings::IniFormat).setValue(QStringLiteral("recentFiles"), m_recent);
    emit recentFilesChanged();
}

} // namespace dp
