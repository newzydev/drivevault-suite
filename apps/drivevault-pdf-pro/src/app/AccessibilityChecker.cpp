#include "AccessibilityChecker.h"

#include "DocumentController.h"
#include "pdftools/Accessibility.h"

#include <QDir>
#include <QFileInfo>
#include <QRunnable>
#include <QThreadPool>

namespace dp {

AccessibilityChecker::AccessibilityChecker(DocumentController* controller, QObject* parent)
    : QObject(parent), m_controller(controller)
{
    connect(controller, &DocumentController::documentChanged, this, [this] { clear(); });
}

AccessibilityChecker::~AccessibilityChecker()
{
    QThreadPool::globalInstance()->waitForDone(15000);
}

void AccessibilityChecker::clear()
{
    if (m_busy)
        return;
    m_loaded = false;
    m_checks.clear();
    m_error.clear();
    m_title.clear();
    m_language.clear();
    emit changed();
}

QString AccessibilityChecker::check()
{
    const DocumentController::SourceInfo src = m_controller->currentSource();
    if (!src.doc)
        return QStringLiteral("noDocument");
    if (m_busy)
        return QStringLiteral("busy");
    m_busy = true;
    m_loaded = false;
    emit changed();
    QThreadPool::globalInstance()->start(QRunnable::create([this, src] {
        const tools::AccessReport r = tools::checkAccessibility(src.path, src.password);
        QVariantList list;
        QString title, language;
        for (const tools::AccessCheck& c : r.checks) {
            list.append(QVariantMap{{"id", c.id}, {"status", c.status}, {"count", c.count}, {"detail", c.detail}});
            if (c.id == QLatin1String("title"))
                title = c.detail;
            else if (c.id == QLatin1String("language"))
                language = c.detail;
        }
        QMetaObject::invokeMethod(this, [this, r, list, title, language] {
            m_busy = false;
            m_loaded = r.ok;
            m_checks = list;
            m_error = r.error;
            m_title = title;
            m_language = language;
            emit changed();
        }, Qt::QueuedConnection);
    }));
    return {};
}

QString AccessibilityChecker::saveInfo(const QString& title, const QString& language, const QString& outPath, bool overwrite)
{
    const DocumentController::SourceInfo src = m_controller->currentSource();
    if (!src.doc)
        return QStringLiteral("noDocument");
    if (m_busy)
        return QStringLiteral("busy");
    if (title.trimmed().isEmpty() && language.trimmed().isEmpty())
        return QStringLiteral("nothing");
    const QString path = QDir::toNativeSeparators(QFileInfo(outPath).absoluteFilePath());
    if (QFileInfo(path) == QFileInfo(src.path))
        return QStringLiteral("sameFile");
    if (!overwrite && QFileInfo::exists(path))
        return QStringLiteral("exists");
    m_busy = true;
    emit changed();
    QThreadPool::globalInstance()->start(QRunnable::create([this, src, title, language, path, overwrite] {
        const tools::AccessFix r = tools::setTitleAndLanguage(src.path, src.password, path, title, language, overwrite);
        QMetaObject::invokeMethod(this, [this, r, path] {
            m_busy = false;
            emit changed();
            emit saveFinished(r.ok, r.error, r.detail, path);
        }, Qt::QueuedConnection);
    }));
    return {};
}

} // namespace dp
