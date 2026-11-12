#include "SignatureManager.h"

#include "DocumentController.h"
#include "pdftools/Signature.h"

#include <QDir>
#include <QFileInfo>
#include <QRunnable>
#include <QThreadPool>
#include <QUrl>

namespace dp {

SignatureManager::SignatureManager(DocumentController* controller, QObject* parent)
    : QObject(parent), m_controller(controller)
{
    connect(controller, &DocumentController::documentChanged, this, [this] { clear(); });
}

SignatureManager::~SignatureManager()
{
    QThreadPool::globalInstance()->waitForDone(15000);
}

void SignatureManager::clear()
{
    if (m_busy)
        return;
    m_loaded = false;
    m_signatures.clear();
    m_error.clear();
    emit changed();
}

QString SignatureManager::check()
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
        QString error;
        const QList<tools::SignatureInfo> list = tools::verifySignatures(src.path, src.password, &error);
        QVariantList out;
        for (const tools::SignatureInfo& s : list)
            out.append(QVariantMap{{"field", s.fieldName}, {"signer", s.signer}, {"reason", s.reason}, {"location", s.location}, {"time", s.signingTime},
                                   {"intact", s.intact}, {"wholeFile", s.coversWholeFile}, {"trust", s.trust}, {"error", s.error}});
        QMetaObject::invokeMethod(this, [this, out, error] {
            m_busy = false;
            m_loaded = error.isEmpty();
            m_signatures = out;
            m_error = error;
            emit changed();
        }, Qt::QueuedConnection);
    }));
    return {};
}

QString SignatureManager::sign(const QVariantMap& options, const QString& outPath, bool overwrite)
{
    const DocumentController::SourceInfo src = m_controller->currentSource();
    if (!src.doc)
        return QStringLiteral("noDocument");
    if (m_busy)
        return QStringLiteral("busy");
    const QUrl pfxUrl(options.value(QStringLiteral("pfx")).toString());
    const QString pfx = pfxUrl.isLocalFile() ? pfxUrl.toLocalFile() : options.value(QStringLiteral("pfx")).toString();
    if (pfx.isEmpty() || !QFileInfo::exists(pfx))
        return QStringLiteral("noCertificate");
    const QString path = QDir::toNativeSeparators(QFileInfo(outPath).absoluteFilePath());
    if (QFileInfo(path) == QFileInfo(src.path))
        return QStringLiteral("sameFile");
    if (!overwrite && QFileInfo::exists(path))
        return QStringLiteral("exists");
    tools::SignOptions o;
    o.pfxPath = pfx;
    o.pfxPassword = options.value(QStringLiteral("pfxPassword")).toString();
    o.name = options.value(QStringLiteral("name")).toString();
    o.reason = options.value(QStringLiteral("reason")).toString();
    o.location = options.value(QStringLiteral("location")).toString();
    m_busy = true;
    emit changed();
    QThreadPool::globalInstance()->start(QRunnable::create([this, src, o, path, overwrite] {
        // a protected file needs its password: the signing code works on a plain file, so it refuses (the window says why)
        const tools::SignResult r = tools::signPdf(src.path, path, o, overwrite);
        QMetaObject::invokeMethod(this, [this, r, path] {
            m_busy = false;
            emit changed();
            emit signFinished(r.ok, r.error, r.detail, r.signer, path);
        }, Qt::QueuedConnection);
    }));
    return {};
}

} // namespace dp
