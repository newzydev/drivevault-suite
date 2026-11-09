#include "Forms.h"

#include <qpdf/Constants.h>
#include <qpdf/QPDF.hh>
#include <qpdf/QPDFAcroFormDocumentHelper.hh>
#include <qpdf/QPDFAnnotationObjectHelper.hh>
#include <qpdf/QPDFExc.hh>
#include <qpdf/QPDFFormFieldObjectHelper.hh>
#include <qpdf/QPDFObjGen.hh>
#include <qpdf/QPDFObjectHandle.hh>
#include <qpdf/QPDFPageDocumentHelper.hh>
#include <qpdf/QPDFSystemError.hh>
#include <qpdf/QPDFWriter.hh>

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <map>

namespace dp::tools {

namespace {

constexpr int kReadOnly = 1;
constexpr int kMultiline = 1 << 12;

QString fromUtf8(const std::string& s) { return QString::fromUtf8(s.data(), int(s.size())); }

// the names of the "on" appearance states of the widgets of a field (checkbox: one, radio group: one per button)
QStringList onStates(QPDFAcroFormDocumentHelper& afdh, QPDFFormFieldObjectHelper& f, std::vector<QPDFAnnotationObjectHelper>* widgets = nullptr)
{
    QStringList out;
    for (QPDFAnnotationObjectHelper a : afdh.getAnnotationsForField(f)) {
        if (widgets)
            widgets->push_back(a);
        QPDFObjectHandle ap = a.getAppearanceDictionary();
        QPDFObjectHandle n = ap.isDictionary() ? ap.getKey("/N") : QPDFObjectHandle::newNull();
        QString on;
        if (n.isDictionary())
            for (const std::string& key : n.getKeys())
                if (key != "/Off") {
                    on = fromUtf8(key.substr(1));
                    break;
                }
        out << on;
    }
    return out;
}

// the fields once each, in file order; a radio group's buttons are widgets of one field (the parent), not fields of their own
struct Group {
    QString name;
    std::vector<QPDFFormFieldObjectHelper> members;     // a radio group arrives as one entry per button
};
std::vector<Group> uniqueFields(QPDFAcroFormDocumentHelper& afdh)
{
    std::vector<Group> out;
    for (QPDFFormFieldObjectHelper f : afdh.getFormFields()) {
        const QString n = fromUtf8(f.getFullyQualifiedName());
        if (n.isEmpty())
            continue;
        auto have = std::find_if(out.begin(), out.end(), [&](const Group& g) { return g.name == n; });
        if (have == out.end())
            out.push_back({n, {f}});
        else
            have->members.push_back(f);
    }
    return out;
}

QString errorOf(const QPDFExc& e) { return e.getErrorCode() == qpdf_e_password ? QStringLiteral("password") : QStringLiteral("format"); }

} // namespace

FormRead readFormFields(const QString& file, const QString& password)
{
    FormRead out;
    try {
        QPDF pdf;
        pdf.setSuppressWarnings(true);
        pdf.processFile(file.toUtf8().constData(), password.isEmpty() ? nullptr : password.toUtf8().constData());
        QPDFAcroFormDocumentHelper afdh(pdf);
        out.ok = true;
        out.hasForm = afdh.hasAcroForm();
        if (!out.hasForm)
            return out;
        out.xfa = pdf.getRoot().getKey("/AcroForm").getKey("/XFA").isInitialized() && !pdf.getRoot().getKey("/AcroForm").getKey("/XFA").isNull();
        std::map<QPDFObjGen, int> pageOf;
        int index = 0;
        for (QPDFPageObjectHelper page : QPDFPageDocumentHelper(pdf).getAllPages()) {
            for (QPDFAnnotationObjectHelper a : afdh.getWidgetAnnotationsForPage(page)) {
                QPDFFormFieldObjectHelper f = afdh.getFieldForAnnotation(a);
                if (!f.isNull())
                    pageOf.emplace(f.getObjectHandle().getObjGen(), index);
            }
            ++index;
        }
        for (Group& group : uniqueFields(afdh)) {
            QPDFFormFieldObjectHelper f = group.members.front();
            const QString fieldName = group.name;
            FormField ff;
            ff.name = fieldName;
            if (ff.name.isEmpty())
                continue;
            const int flags = f.getFlags();
            ff.readOnly = (flags & kReadOnly) != 0;
            if (auto it = pageOf.find(f.getObjectHandle().getObjGen()); it != pageOf.end())
                ff.page = it->second;
            const std::string ft = f.getFieldType();
            QPDFObjectHandle maxLen = f.getObjectHandle().getKey("/MaxLen");
            ff.maxLength = maxLen.isInteger() ? int(maxLen.getIntValue()) : 0;
            if (ft == "/Tx") {
                ff.type = QStringLiteral("text");
                ff.value = fromUtf8(f.getValueAsString());
                ff.multiline = (flags & kMultiline) != 0;
            } else if (ft == "/Ch") {
                ff.type = QStringLiteral("choice");
                ff.value = fromUtf8(f.getValueAsString());
                for (const std::string& c : f.getChoices())
                    ff.options << fromUtf8(c);
            } else if (ft == "/Btn") {
                if (f.isCheckbox()) {
                    ff.type = QStringLiteral("checkbox");
                    ff.checked = f.isChecked();
                } else if (f.isRadioButton()) {
                    ff.type = QStringLiteral("radio");
                    QStringList on;
                    for (QPDFFormFieldObjectHelper& m : group.members)
                        on << onStates(afdh, m);
                    for (const QString& s : on)
                        if (!s.isEmpty() && !ff.options.contains(s))
                            ff.options << s;
                    QPDFObjectHandle v = f.getValue();
                    ff.value = v.isName() && v.getName() != "/Off" ? fromUtf8(v.getName().substr(1)) : QString();
                } else {
                    ff.type = QStringLiteral("button");
                }
            } else if (ft == "/Sig") {
                ff.type = QStringLiteral("signature");
            } else {
                continue;
            }
            out.fields.append(ff);
        }
    } catch (const QPDFExc& e) {
        out = FormRead();
        out.error = errorOf(e);
    } catch (const QPDFSystemError&) {
        out = FormRead();
        out.error = QStringLiteral("file");
    } catch (const std::exception&) {
        out = FormRead();
        out.error = QStringLiteral("unknown");
    }
    return out;
}

FormFill fillForm(const QString& file, const QString& password, const QString& outFile, const QMap<QString, QVariant>& values, bool overwrite)
{
    FormFill result;
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
        QPDFAcroFormDocumentHelper afdh(pdf);
        if (!afdh.hasAcroForm()) {
            result.error = QStringLiteral("noForm");
            return result;
        }
        std::map<QString, Group> byName;
        for (Group& g : uniqueFields(afdh))
            byName.emplace(g.name, g);
        for (auto it = values.begin(); it != values.end(); ++it) {
            const QString& name = it.key();
            const auto found = byName.find(name);
            if (found == byName.end()) {
                result.skipped << name + QStringLiteral(": unknown");
                continue;
            }
            QPDFFormFieldObjectHelper f = found->second.members.front();
            if (f.getFlags() & kReadOnly) {
                result.skipped << name + QStringLiteral(": readonly");
                continue;
            }
            const std::string ft = f.getFieldType();
            if (ft == "/Tx") {
                const QString text = it.value().toString();
                bool latin = true;
                for (const QChar c : text)
                    if (c.unicode() > 0xFF && c != QLatin1Char('\n'))
                        latin = false;
                if (!latin) {
                    result.skipped << name + QStringLiteral(": unicode");
                    continue;
                }
                f.setV(text.toUtf8().toStdString(), true);
                ++result.filled;
            } else if (ft == "/Ch") {
                const QString text = it.value().toString();
                const std::vector<std::string> choices = f.getChoices();
                bool known = choices.empty();
                for (const std::string& c : choices)
                    known = known || fromUtf8(c) == text;
                if (!known && !text.isEmpty()) {
                    result.skipped << name + QStringLiteral(": option");
                    continue;
                }
                f.setV(text.toUtf8().toStdString(), true);
                ++result.filled;
            } else if (ft == "/Btn" && (f.isCheckbox() || f.isRadioButton())) {
                std::vector<QPDFAnnotationObjectHelper> widgets;
                QStringList on;
                for (QPDFFormFieldObjectHelper& m : found->second.members)
                    on << onStates(afdh, m, &widgets);
                QString want;                                  // the state that is on ("" = all off)
                if (f.isCheckbox()) {
                    if (it.value().toBool())
                        for (const QString& s : on)
                            if (!s.isEmpty()) {
                                want = s;
                                break;
                            }
                    if (it.value().toBool() && want.isEmpty()) {
                        result.skipped << name + QStringLiteral(": unsupported");
                        continue;
                    }
                } else {
                    want = it.value().toString();
                    if (!want.isEmpty() && !on.contains(want)) {
                        result.skipped << name + QStringLiteral(": option");
                        continue;
                    }
                }
                const std::string state = want.isEmpty() ? "/Off" : "/" + want.toUtf8().toStdString();
                for (QPDFFormFieldObjectHelper& m : found->second.members) {
                    m.setV(QPDFObjectHandle::newName(state), false);
                    QPDFObjectHandle parent = m.getObjectHandle().getKey("/Parent");
                    if (parent.isDictionary())
                        parent.replaceKey("/V", QPDFObjectHandle::newName(state));      // a radio group keeps its value in the parent
                }
                for (size_t i = 0; i < widgets.size(); ++i) {
                    const std::string wanted = (!want.isEmpty() && on.value(int(i)) == want) ? state : std::string("/Off");
                    widgets[i].getObjectHandle().replaceKey("/AS", QPDFObjectHandle::newName(f.isCheckbox() ? state : wanted));
                }
                ++result.filled;
            } else {
                result.skipped << name + QStringLiteral(": unsupported");
            }
        }
        afdh.generateAppearancesIfNeeded();

        QFile::remove(tmp);
        QPDFWriter writer(pdf, tmp.toUtf8().constData());
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
        result.error = QFileInfo::exists(file) ? QStringLiteral("write") : QStringLiteral("file");
        result.detail = QString::fromUtf8(e.what());
        return result;
    } catch (const std::exception& e) {
        QFile::remove(tmp);
        result.error = QStringLiteral("unknown");
        result.detail = QString::fromUtf8(e.what());
        return result;
    }
    if (!result.error.isEmpty())
        return result;
    if (overwrite)
        QFile::remove(outFile);
    if (!QFile::rename(tmp, outFile)) {
        QFile::remove(tmp);
        result.error = QStringLiteral("write");
        return result;
    }
    result.ok = true;
    result.bytes = QFileInfo(outFile).size();
    return result;
}

} // namespace dp::tools
