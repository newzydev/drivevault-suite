#include "Security.h"

#include <QFileInfo>
#include <QTemporaryFile>

#include <qpdf/Pipeline.hh>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFWriter.hh>
#include <qpdf/QUtil.hh>

#include <optional>
#include <stdexcept>

namespace dp::tools {
namespace {

class FileOutput final : public Pipeline {
public:
    explicit FileOutput(QFile& file) : Pipeline("PDF output", nullptr), m_file(file) {}

    void write(const unsigned char* data, size_t len) override
    {
        if (m_file.write(reinterpret_cast<const char*>(data), qint64(len)) != qint64(len))
            throw std::runtime_error("write");
    }

    void finish() override
    {
        if (!m_file.flush())
            throw std::runtime_error("write");
    }

private:
    QFile& m_file;
};

void openPdf(std::optional<QPDF>& pdf, const QByteArray& filename, const QByteArray& password)
{
    pdf.emplace().setSuppressWarnings(true);
    try {
        pdf->processFile(filename.constData(), password.constData());
    } catch (const QPDFExc& ex) {
        // Legacy PDF encryption uses PDFDocEncoding for the supplied password. Convert only if lossless.
        std::string legacy;
        if (ex.getErrorCode() != qpdf_e_password || password.isEmpty()
            || !QUtil::utf8_to_pdf_doc(password.toStdString(), legacy)
            || legacy == password.toStdString() || legacy.find('\0') != std::string::npos)
            throw;
        pdf.emplace().setSuppressWarnings(true);
        pdf->processFile(filename.constData(), legacy.c_str());
        int revision = 0;
        int permissions = 0;
        if (!pdf->isEncrypted(revision, permissions) || revision >= 5)
            throw;
    }
}

QString rewrite(const QString& source, const QString& destination, const Protection* protection, const QByteArray& password)
{
    if (QFileInfo::exists(destination))
        return QStringLiteral("exists");
    if (!QFileInfo::exists(source))
        return QStringLiteral("file");

    const QByteArray sourceName = source.toUtf8();
    std::optional<QPDF> pdf;
    try {
        openPdf(pdf, sourceName, password);
        if (protection && pdf->isEncrypted())
            return QStringLiteral("alreadyProtected");
        if (!protection) {
            if (!pdf->isEncrypted())
                return QStringLiteral("notProtected");
            if (!pdf->ownerPasswordMatched()) {
                if (!pdf->userPasswordMatched())
                    return QStringLiteral("password");
                // If the file opens without a password, only its owner can remove restrictions.
                QPDF probe;
                probe.setSuppressWarnings(true);
                try {
                    probe.processFile(sourceName.constData(), "");
                    return QStringLiteral("ownerRequired");
                } catch (const QPDFExc& ex) {
                    if (ex.getErrorCode() != qpdf_e_password)
                        throw;
                }
            }
        }
    } catch (const QPDFExc& ex) {
        if (ex.getErrorCode() == qpdf_e_password)
            return protection ? QStringLiteral("alreadyProtected") : QStringLiteral("password");
        return QStringLiteral("format");
    } catch (const std::exception&) {
        return QStringLiteral("format");
    }

    QTemporaryFile out(QFileInfo(destination).absolutePath() + QStringLiteral("/.dv-security-XXXXXX"));
    if (!out.open())
        return QStringLiteral("write");
    try {
        FileOutput sink(out);
        QPDFWriter writer(*pdf);
        writer.setOutputPipeline(&sink);
        if (protection) {
            writer.setR6EncryptionParameters(protection->userPassword.toUtf8().constData(),
                protection->ownerPassword.toUtf8().constData(), true, protection->copy,
                protection->modify, protection->annotate, protection->modify, protection->modify,
                protection->print ? qpdf_r3p_full : qpdf_r3p_none, true);
        } else {
            writer.setPreserveEncryption(false);
        }
        writer.write();
    } catch (const std::exception&) {
        return QStringLiteral("write");
    }
    out.close();
    if (!out.rename(destination))
        return QFileInfo::exists(destination) ? QStringLiteral("exists") : QStringLiteral("write");
    out.setAutoRemove(false);
    return {};
}

} // namespace

QString protectPdf(const QString& source, const QString& destination, const Protection& options)
{
    const auto user = options.userPassword.toUtf8();
    const auto owner = options.ownerPassword.toUtf8();
    if (owner.isEmpty() || owner == user || owner.size() > 127 || user.size() > 127
        || owner.contains('\0') || user.contains('\0'))
        return QStringLiteral("invalidPasswords");
    return rewrite(source, destination, &options, {});
}

QString unlockPdf(const QString& source, const QString& destination, const QString& password)
{
    if (password.isEmpty())
        return QStringLiteral("ownerRequired");
    const auto encoded = password.toUtf8();
    if (encoded.size() > 127 || encoded.contains('\0'))
        return QStringLiteral("password");
    return rewrite(source, destination, nullptr, encoded);
}

} // namespace dp::tools
