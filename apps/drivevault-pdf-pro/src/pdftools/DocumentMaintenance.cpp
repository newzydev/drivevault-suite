#include "DocumentMaintenance.h"
#include "pdfengine/PdfDocument.h"

#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFEmbeddedFileDocumentHelper.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFFileSpecObjectHelper.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFWriter.hh>

#include <algorithm>
#include <array>

namespace dp::tools {
namespace {

constexpr std::array fields{"/Title", "/Author", "/Keywords"};
constexpr std::array cleanupKeys{"/Metadata", "/PieceInfo", "/Thumb", "/AF", "/EmbeddedFiles", "/EF", "/RF",
                                "/JavaScript", "/JS", "/AA", "/OpenAction"};
constexpr std::array externalActions{"/JavaScript", "/Launch", "/SubmitForm", "/ImportData", "/GoToR", "/GoToE", "/URI"};

void open(QPDF& pdf, const QString& source, const QString& password, bool recover)
{
    pdf.setSuppressWarnings(true);
    pdf.setAttemptRecovery(recover);
    pdf.processFile(source.toUtf8().constData(), password.toUtf8().constData());
}

QString errorCode(const QPDFExc& e)
{
    return e.getErrorCode() == qpdf_e_password ? QStringLiteral("password") : QStringLiteral("format");
}

QVariantMap properties(QPDF& pdf, bool inspect = false)
{
    QVariantMap result;
    const auto info = pdf.getTrailer().getKey("/Info");
    for (const auto key : fields) {
        const auto value = info.isDictionary() ? info.getKey(key) : QPDFObjectHandle::newNull();
        result.insert(QString::fromLatin1(key + 1), value.isString() ? QString::fromStdString(value.getUTF8Value()) : QString());
    }
    QVariantList attachments;
    for (const auto& [key, spec] : QPDFEmbeddedFileDocumentHelper(pdf).getEmbeddedFiles()) {
        // Keep the exact name-tree key as bytes even if its display name is not UTF-8.
        attachments.append(QVariantMap{{"id", QString::fromLatin1(QByteArray::fromStdString(key).toHex())},
                                      {"name", QString::fromStdString(spec->getFilename())}});
    }
    result.insert("attachments", attachments);
    if (inspect) {
        QSet<QString> fontNames;
        for (const auto& object : pdf.getAllObjects()) {
            if (!object.isDictionary() || !object.getKey("/Type").isNameAndEquals("/Font"))
                continue;
            const auto name = object.getKey("/BaseFont");
            if (name.isName())
                fontNames.insert(QString::fromStdString(name.getName()).mid(1));
        }
        QStringList fonts = fontNames.values();
        fonts.sort(Qt::CaseInsensitive);
        result.insert("fonts", fonts);
        result.insert("pdfVersion", QString::fromStdString(pdf.getPDFVersion()));
        result.insert("pageCount", int(QPDFPageDocumentHelper(pdf).getAllPages().size()));
        result.insert("encrypted", pdf.isEncrypted());
    }
    return result;
}

bool privateEntries(QPDF& pdf, bool remove)
{
    bool found = false;
    const auto strip = [&](QPDFObjectHandle object, const char* key) {
        if (!object.hasKey(key))
            return;
        found = true;
        if (remove)
            object.removeKey(key);
    };
    strip(pdf.getTrailer(), "/Info");
    for (const auto key : {"/AcroForm", "/Outlines", "/PageLabels", "/StructTreeRoot", "/MarkInfo"})
        strip(pdf.getRoot(), key);
    for (auto page : QPDFPageDocumentHelper(pdf).getAllPages())
        strip(page.getObjectHandle(), "/Annots");
    for (auto object : pdf.getAllObjects()) {
        if (object.isStreamOfType("/EmbeddedFile") || object.isStreamOfType("/Metadata")) {
            found = true;
            if (remove)
                pdf.replaceObject(object.getObjGen(), QPDFObjectHandle::newNull());
            continue;
        }
        if (object.isStream())
            object = object.getDict();
        if (object.isDictionary() || object.isArray()) {
            // Also visit inline dictionaries such as the catalog's /Names or resource dictionaries.
            // Indirect objects are handled by getAllObjects(), so cycles are not traversed.
            QList<QPDFObjectHandle> pending{object};
            while (!pending.isEmpty()) {
                auto current = pending.takeLast();
                if (current.isArray()) {
                    for (const auto& entry : current.getArrayAsVector())
                        if (!entry.isIndirect() && (entry.isArray() || entry.isDictionary()))
                            pending.append(entry);
                } else {
                    const auto actionType = current.getKey("/S");
                    if (actionType.isName() && std::ranges::any_of(externalActions, [&](const char* action) {
                            return actionType.isNameAndEquals(action);
                        })) {
                        found = true;
                        if (remove)
                            for (const auto& key : current.getKeys())
                                current.removeKey(key);
                        continue;
                    }
                    if (current.hasKey("/EF") || current.hasKey("/RF"))
                        for (const auto key : {"/F", "/UF", "/DOS", "/Mac", "/Unix", "/Desc"})
                            strip(current, key);
                    for (const auto key : cleanupKeys)
                        strip(current, key);
                    for (const auto& key : current.getKeys()) {
                        auto child = current.getKey(key);
                        if (!child.isIndirect() && (child.isArray() || child.isDictionary()))
                            pending.append(child);
                    }
                }
            }
        }
    }
    return found;
}

} // namespace

QVariantMap inspectDocument(const QString& source, const QString& password)
{
    try {
        QPDF pdf;
        open(pdf, source, password, false);
        return properties(pdf, true);
    } catch (const QPDFExc& e) {
        return {{"error", errorCode(e)}};
    } catch (const std::exception&) {
        return {{"error", QStringLiteral("format")}};
    }
}

QString maintainDocument(const QString& source, const QString& password, const QString& destination,
                         const QString& mode, const QVariantMap& edits, const QStringList& removeAttachments)
{
    if (!QFileInfo(source).isFile())
        return QStringLiteral("file");
    if (QFileInfo::exists(destination))
        return QStringLiteral("exists");
    const QTemporaryDir stage(QFileInfo(destination).absolutePath() + QStringLiteral("/.dv-maintain-XXXXXX"));
    if (!stage.isValid())
        return QStringLiteral("write");
    const auto temporary = stage.filePath(QStringLiteral("copy.pdf"));
    try {
        QPDF pdf;
        open(pdf, source, password, mode == QLatin1String("repair"));
        const bool encrypted = pdf.isEncrypted();
        if (encrypted && !pdf.ownerPasswordMatched())
            return QStringLiteral("ownerRequired");
        const auto count = QPDFPageDocumentHelper(pdf).getAllPages().size();
        if (!count)
            return QStringLiteral("format");
        if (mode == QLatin1String("clean")) {
            privateEntries(pdf, true);
        } else if (mode == QLatin1String("metadata")) {
            auto info = pdf.getTrailer().getKey("/Info");
            if (!info.isDictionary()) {
                info = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
                pdf.getTrailer().replaceKey("/Info", info);
            }
            bool changed = false;
            for (const auto key : fields) {
                const QString name = QString::fromLatin1(key + 1);
                if (!edits.contains(name))
                    continue;
                const auto value = edits.value(name).toString();
                if (value.size() > 4096)
                    return QStringLiteral("maintenanceOptions");
                if (value.isEmpty())
                    info.removeKey(key);
                else
                    info.replaceKey(key, QPDFObjectHandle::newUnicodeString(value.toStdString()));
                changed = true;
            }
            if (changed)
                pdf.getRoot().removeKey("/Metadata");
            QPDFEmbeddedFileDocumentHelper attachments(pdf);
            for (const auto& id : removeAttachments) {
                const auto bytes = QByteArray::fromHex(id.toLatin1());
                if (bytes.toHex() != id.toLatin1() || !attachments.removeEmbeddedFile(bytes.toStdString()))
                    return QStringLiteral("maintenanceOptions");
            }
        } else if (mode != QLatin1String("repair")) {
            return QStringLiteral("maintenanceOptions");
        }
        const auto path = temporary.toUtf8();
        try {
            QPDFWriter writer(pdf, path.constData());
            writer.setPreserveEncryption(encrypted);
            writer.write();
        } catch (const std::exception&) {
            return QStringLiteral("write");
        }
        QPDF check;
        open(check, temporary, password, false);
        if (QPDFPageDocumentHelper(check).getAllPages().size() != count || check.isEncrypted() != encrypted
            || !pdf::PdfDocument::open(temporary, password, nullptr) || !check.getWarnings().empty())
            return QStringLiteral("maintenanceVerify");
        if (mode == QLatin1String("clean") && privateEntries(check, false))
            return QStringLiteral("maintenanceVerify");
        if (mode == QLatin1String("metadata")) {
            const auto actual = properties(check);
            for (const auto key : fields) {
                const auto name = QString::fromLatin1(key + 1);
                if (edits.contains(name) && actual.value(name).toString() != edits.value(name).toString())
                    return QStringLiteral("maintenanceVerify");
            }
            for (const auto& attachment : actual.value("attachments").toList())
                if (removeAttachments.contains(attachment.toMap().value("id").toString()))
                    return QStringLiteral("maintenanceVerify");
        }
    } catch (const QPDFExc& e) {
        return errorCode(e);
    } catch (const std::exception&) {
        return QStringLiteral("format");
    }
    if (!QFile::rename(temporary, destination))
        return QFileInfo::exists(destination) ? QStringLiteral("exists") : QStringLiteral("write");
    return {};
}

} // namespace dp::tools
