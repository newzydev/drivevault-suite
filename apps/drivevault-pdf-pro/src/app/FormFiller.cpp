#include "FormFiller.h"

#include "DocumentController.h"
#include "pdftools/Forms.h"

#include <QDir>
#include <QFileInfo>
#include <QRunnable>
#include <QThreadPool>

namespace dp {

FormFiller::FormFiller(DocumentController* controller, QObject* parent)
    : QObject(parent), m_controller(controller)
{
    connect(controller, &DocumentController::documentChanged, this, [this] { clear(); });
}

FormFiller::~FormFiller()
{
    QThreadPool::globalInstance()->waitForDone(15000);
}

void FormFiller::clear()
{
    if (m_busy)
        return;
    m_loaded = false;
    m_hasForm = false;
    m_xfa = false;
    m_fields.clear();
    m_error.clear();
    emit changed();
}

QString FormFiller::load()
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
        const tools::FormRead r = tools::readFormFields(src.path, src.password);
        QVariantList list;
        for (const tools::FormField& f : r.fields)
            list.append(QVariantMap{{"name", f.name},         {"type", f.type},           {"value", f.value},          {"checked", f.checked},
                                    {"options", f.options},   {"readOnly", f.readOnly},   {"multiline", f.multiline},  {"page", f.page},
                                    {"maxLength", f.maxLength}});
        QMetaObject::invokeMethod(this, [this, r, list] {
            m_busy = false;
            m_loaded = r.ok;
            m_hasForm = r.hasForm;
            m_xfa = r.xfa;
            m_fields = list;
            m_error = r.error;
            emit changed();
        }, Qt::QueuedConnection);
    }));
    return {};
}

QString FormFiller::save(const QVariantMap& values, const QString& outPath, bool overwrite)
{
    const DocumentController::SourceInfo src = m_controller->currentSource();
    if (!src.doc)
        return QStringLiteral("noDocument");
    if (m_busy)
        return QStringLiteral("busy");
    if (values.isEmpty())
        return QStringLiteral("noValues");
    const QString path = QDir::toNativeSeparators(QFileInfo(outPath).absoluteFilePath());
    if (QFileInfo(path) == QFileInfo(src.path))
        return QStringLiteral("sameFile");
    if (!overwrite && QFileInfo::exists(path))
        return QStringLiteral("exists");
    QMap<QString, QVariant> map;
    for (auto it = values.begin(); it != values.end(); ++it)
        map.insert(it.key(), it.value());
    m_busy = true;
    emit changed();
    QThreadPool::globalInstance()->start(QRunnable::create([this, src, map, path, overwrite] {
        const tools::FormFill r = tools::fillForm(src.path, src.password, path, map, overwrite);
        QMetaObject::invokeMethod(this, [this, r, path] {
            m_busy = false;
            emit changed();
            emit saveFinished(r.ok, r.error, r.detail, r.filled, r.skipped, r.warnings, path);
        }, Qt::QueuedConnection);
    }));
    return {};
}

} // namespace dp
