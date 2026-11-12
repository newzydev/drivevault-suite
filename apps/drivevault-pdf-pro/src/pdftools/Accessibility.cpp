#include "Accessibility.h"

#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>
#include <qpdf/QPDFObjGen.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFSystemError.hh>
#include <qpdf/QPDFWriter.hh>

#include <QFile>
#include <QFileInfo>

#include <set>
#include <string>

namespace dp::tools {

namespace {

QString fromPdf(const QPDFObjectHandle& h)
{
    if (!h.isString())
        return {};
    return QString::fromUtf8(h.getUTF8Value().c_str()).trimmed();
}

struct FigureCount {
    int figures = 0;
    int withoutAlt = 0;
    int visited = 0;
};

void walkStructure(QPDFObjectHandle node, FigureCount& c, std::set<QPDFObjGen>& seen, int depth)
{
    if (depth > 200 || c.visited > 200000 || !node)
        return;
    if (node.isArray()) {
        for (int i = 0; i < node.getArrayNItems(); ++i)
            walkStructure(node.getArrayItem(i), c, seen, depth + 1);
        return;
    }
    if (!node.isDictionary())
        return;
    if (node.isIndirect() && !seen.insert(node.getObjGen()).second)
        return;
    ++c.visited;
    QPDFObjectHandle s = node.getKey("/S");
    if (s.isName() && s.getName() == "/Figure") {
        ++c.figures;
        if (fromPdf(node.getKey("/Alt")).isEmpty() && fromPdf(node.getKey("/ActualText")).isEmpty())
            ++c.withoutAlt;
    }
    QPDFObjectHandle k = node.getKey("/K");
    if (k.isArray() || k.isDictionary())
        walkStructure(k, c, seen, depth + 1);
}

AccessCheck make(const char* id, const char* status, int count = 0, const QString& detail = {})
{
    AccessCheck c;
    c.id = QString::fromLatin1(id);
    c.status = QString::fromLatin1(status);
    c.count = count;
    c.detail = detail;
    return c;
}

QString errorOf(const QPDFExc& e)
{
    return e.getErrorCode() == qpdf_e_password ? QStringLiteral("password") : QStringLiteral("format");
}

} // namespace

AccessReport checkAccessibility(const QString& file, const QString& password)
{
    AccessReport out;
    if (!QFileInfo::exists(file)) {
        out.error = QStringLiteral("file");
        return out;
    }
    try {
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        pdf.processFile(file.toUtf8().constData(), password.isEmpty() ? nullptr : password.toUtf8().constData());
        out.pages = int(QPDFPageDocumentHelper(pdf).getAllPages().size());
        QPDFObjectHandle root = pdf.getRoot();

        // tagged structure
        QPDFObjectHandle markInfo = root.getKey("/MarkInfo");
        const bool marked = markInfo.isDictionary() && markInfo.getKey("/Marked").isBool() && markInfo.getKey("/Marked").getBoolValue();
        QPDFObjectHandle structRoot = root.getKey("/StructTreeRoot");
        const bool tagged = marked && structRoot.isDictionary();
        out.checks << make("tagged", tagged ? "pass" : "fail");

        // figures with a description
        if (tagged) {
            FigureCount fc;
            std::set<QPDFObjGen> seen;
            walkStructure(structRoot.getKey("/K"), fc, seen, 0);
            out.checks << (fc.figures == 0 ? make("figureAlt", "na") : make("figureAlt", fc.withoutAlt == 0 ? "pass" : "fail", fc.withoutAlt));
        } else {
            out.checks << make("figureAlt", "na");
        }

        // language and title
        const QString lang = fromPdf(root.getKey("/Lang"));
        out.checks << make("language", lang.isEmpty() ? "fail" : "pass", 0, lang);
        QPDFObjectHandle info = pdf.getTrailer().getKey("/Info");
        const QString title = info.isDictionary() ? fromPdf(info.getKey("/Title")) : QString();
        out.checks << make("title", title.isEmpty() ? "fail" : "pass", 0, title);
        QPDFObjectHandle vp = root.getKey("/ViewerPreferences");
        const bool showTitle = vp.isDictionary() && vp.getKey("/DisplayDocTitle").isBool() && vp.getKey("/DisplayDocTitle").getBoolValue();
        out.checks << make("displayTitle", showTitle ? "pass" : "warn");

        // bookmarks help in a long file
        if (out.pages >= 10) {
            QPDFObjectHandle outlines = root.getKey("/Outlines");
            const bool has = outlines.isDictionary() && outlines.hasKey("/First");
            out.checks << make("bookmarks", has ? "pass" : "warn");
        } else {
            out.checks << make("bookmarks", "na");
        }

        // form fields need a tool tip (the name a screen reader says)
        {
            QPDFAcroFormDocumentHelper afdh(pdf);
            int fields = 0, missing = 0;
            for (QPDFFormFieldObjectHelper f : afdh.getFormFields()) {
                if (f.getFieldType() == "/Sig")
                    continue;
                ++fields;
                if (f.getInheritableFieldValueAsString("/TU").empty())
                    ++missing;
            }
            out.checks << (fields == 0 ? make("fieldTips", "na") : make("fieldTips", missing == 0 ? "pass" : "fail", missing));
        }

        // the protection must allow screen readers
        if (pdf.isEncrypted())
            out.checks << make("security", pdf.allowAccessibility() ? "pass" : "fail");
        else
            out.checks << make("security", "na");

        // a declared PDF/A part (read from the XMP metadata; not validated)
        {
            int part = 0;
            QPDFObjectHandle meta = root.getKey("/Metadata");
            if (meta.isStream()) {
                std::shared_ptr<Buffer> data = meta.getStreamData();
                const QByteArray xmp(reinterpret_cast<const char*>(data->getBuffer()), int(data->getSize()));
                const int at = xmp.indexOf("pdfaid:part");
                if (at >= 0) {
                    for (int i = at + 11; i < xmp.size() && i < at + 40; ++i)
                        if (xmp[i] >= '1' && xmp[i] <= '9') {
                            part = xmp[i] - '0';
                            break;
                        }
                }
            }
            out.checks << (part > 0 ? make("pdfa", "info", part) : make("pdfa", "na"));
        }
        out.ok = true;
    } catch (const QPDFExc& e) {
        out.error = errorOf(e);
    } catch (const QPDFSystemError&) {
        out.error = QStringLiteral("file");
    } catch (const std::exception&) {
        out.error = QStringLiteral("unknown");
    }
    return out;
}

AccessFix setTitleAndLanguage(const QString& file, const QString& password, const QString& outFile, const QString& title, const QString& language, bool overwrite)
{
    AccessFix result;
    const QString newTitle = title.trimmed();
    const QString newLang = language.trimmed();
    if (newTitle.isEmpty() && newLang.isEmpty()) {
        result.error = QStringLiteral("nothing");
        return result;
    }
    if (!QFileInfo::exists(file)) {
        result.error = QStringLiteral("file");
        return result;
    }
    if (!overwrite && QFileInfo::exists(outFile)) {
        result.error = QStringLiteral("exists");
        return result;
    }
    const QString tmp = outFile + QStringLiteral(".part.tmp");
    try {
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        pdf.processFile(file.toUtf8().constData(), password.isEmpty() ? nullptr : password.toUtf8().constData());
        if (pdf.isEncrypted())
            result.warnings << QStringLiteral("encrypted");
        QPDFObjectHandle root = pdf.getRoot();
        if (!newLang.isEmpty())
            root.replaceKey("/Lang", QPDFObjectHandle::newString(newLang.toLatin1().toStdString()));
        if (!newTitle.isEmpty()) {
            QPDFObjectHandle info = pdf.getTrailer().getKey("/Info");
            if (!info.isDictionary()) {
                info = pdf.makeIndirectObject(QPDFObjectHandle::newDictionary());
                pdf.getTrailer().replaceKey("/Info", info);
            }
            info.replaceKey("/Title", QPDFObjectHandle::newUnicodeString(newTitle.toUtf8().toStdString()));
            QPDFObjectHandle vp = root.getKey("/ViewerPreferences");
            if (!vp.isDictionary()) {
                vp = QPDFObjectHandle::newDictionary();
                root.replaceKey("/ViewerPreferences", vp);
            }
            vp.replaceKey("/DisplayDocTitle", QPDFObjectHandle::newBool(true));
        }
        QFile::remove(tmp);
        const QByteArray outputName = tmp.toUtf8();
        QPDFWriter writer(pdf, outputName.constData());
        writer.setObjectStreamMode(qpdf_o_generate);
        writer.setStreamDataMode(qpdf_s_compress);
        writer.write();
    } catch (const QPDFExc& e) {
        QFile::remove(tmp);
        result.error = errorOf(e);
        result.detail = QString::fromUtf8(e.what());
        return result;
    } catch (const QPDFSystemError& e) {
        QFile::remove(tmp);
        result.error = QStringLiteral("write");
        result.detail = QString::fromUtf8(e.what());
        return result;
    } catch (const std::exception& e) {
        QFile::remove(tmp);
        result.error = QStringLiteral("unknown");
        result.detail = QString::fromUtf8(e.what());
        return result;
    }
    if (overwrite)
        QFile::remove(outFile);
    if (!QFile::rename(tmp, outFile)) {
        QFile::remove(tmp);
        result.error = QStringLiteral("write");
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace dp::tools
