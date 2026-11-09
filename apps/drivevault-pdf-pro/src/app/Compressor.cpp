#include "Compressor.h"

#include "DocumentController.h"
#include "pdftools/Compress.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRunnable>
#include <QThreadPool>
#include <QUuid>

namespace dp {

Compressor::Compressor(DocumentController* controller, QObject* parent)
    : QObject(parent), m_controller(controller)
{
}

Compressor::~Compressor()
{
    if (m_cancel)
        m_cancel->store(true);
    QThreadPool::globalInstance()->waitForDone(15000);
    discard();
}

QString Compressor::baseName() const
{
    return m_name.isEmpty() ? QStringLiteral("compressed") : m_name + QStringLiteral("-compressed");
}

QVariantMap Compressor::preset(const QString& level) const
{
    if (level == QLatin1String("high"))
        return {{"dpi", 110}, {"quality", 65}, {"strip", true}, {"linearize", false}};
    if (level == QLatin1String("medium"))
        return {{"dpi", 150}, {"quality", 80}, {"strip", true}, {"linearize", false}};
    return {{"dpi", 0}, {"quality", 0}, {"strip", false}, {"linearize", false}};          // low: structure only
}

QString Compressor::start(const QVariantMap& options)
{
    const DocumentController::SourceInfo src = m_controller->currentSource();
    if (!src.doc)
        return QStringLiteral("noDocument");
    if (m_busy)
        return QStringLiteral("busy");
    discard();

    tools::CompressOptions o;
    o.imageDpi = qBound(0, options.value(QStringLiteral("dpi")).toInt(), 1200);
    o.jpegQuality = qBound(0, options.value(QStringLiteral("quality")).toInt(), 100);
    o.stripExtras = options.value(QStringLiteral("strip")).toBool();
    o.linearize = options.value(QStringLiteral("linearize")).toBool();

    m_tempFile = QDir::tempPath() + QStringLiteral("/drivevault-pdfpro-") + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".pdf");
    m_sourcePath = src.path;
    m_password = src.password;
    m_name = QFileInfo(src.path).completeBaseName();
    m_cancel = std::make_shared<std::atomic_bool>(false);
    auto cancel = m_cancel;
    m_busy = true;
    m_progress = 0.0;
    m_result.clear();
    emit changed();
    const QString tmp = m_tempFile;
    QThreadPool::globalInstance()->start(QRunnable::create([this, src, o, tmp, cancel] {
        const tools::CompressResult r = tools::compressFile(src.path, src.password, tmp, o, [this, cancel](int done, int total) {
            QMetaObject::invokeMethod(this, [this, done, total] { m_progress = total > 0 ? double(done) / total : 0.0; emit changed(); }, Qt::QueuedConnection);
            return !cancel->load();
        });
        QMetaObject::invokeMethod(this, [this, r, src, tmp] {
            m_busy = false;
            m_progress = r.ok ? 1.0 : m_progress;
            QVariantMap map{{"written", r.written},         {"inBytes", double(r.inBytes)},     {"outBytes", double(r.outBytes)},
                            {"images", r.images},           {"changed", r.imagesChanged},       {"downsampled", r.imagesDownsampled},
                            {"skipped", r.imagesSkipped},   {"warnings", r.warnings}};
            m_result = map;
            if (r.ok && r.written) {
                // open the smaller copy so that a page can be compared with the original
                pdf::OpenError err = pdf::OpenError::None;
                if (auto doc = pdf::PdfDocument::open(tmp, src.password, &err)) {
                    m_beforeId = src.id;
                    m_afterId = m_controller->registerExtra(doc);
                }
            }
            if (m_afterId == 0)
                QFile::remove(tmp);
            emit changed();
            emit finished(r.ok, r.error);
        }, Qt::QueuedConnection);
    }));
    return {};
}

void Compressor::cancel()
{
    if (m_cancel)
        m_cancel->store(true);
}

QString Compressor::saveAs(const QString& outPath, bool overwrite)
{
    if (m_afterId == 0 || m_tempFile.isEmpty() || !QFileInfo::exists(m_tempFile))
        return QStringLiteral("noResult");
    const QString path = QDir::toNativeSeparators(QFileInfo(outPath).absoluteFilePath());
    if (QFileInfo(path) == QFileInfo(m_sourcePath))
        return QStringLiteral("sameFile");                 // the original is never replaced by its own smaller copy
    if (!overwrite && QFileInfo::exists(path))
        return QStringLiteral("exists");
    const QString part = path + QStringLiteral(".part.tmp");
    QFile::remove(part);
    if (!QFile::copy(m_tempFile, part))
        return QStringLiteral("write");
    if (overwrite)
        QFile::remove(path);
    if (!QFile::rename(part, path)) {
        QFile::remove(part);
        return QStringLiteral("write");
    }
    return {};
}

void Compressor::discard()
{
    if (m_afterId != 0) {
        m_controller->unregisterExtra(m_afterId);
        m_afterId = 0;
        m_beforeId = 0;
    }
    if (!m_tempFile.isEmpty()) {
        QFile::remove(m_tempFile);
        m_tempFile.clear();
    }
    if (!m_result.isEmpty() && !m_busy) {
        m_result.clear();
        emit changed();
    }
}

} // namespace dp
