#include "FormData.h"

#include <QFile>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

namespace dp::tools {

namespace {

const QString kNs = QStringLiteral("http://ns.adobe.com/xfdf/");

struct Node {
    QString name;
    QString value;
    bool hasValue = false;
    QList<Node> children;
};

QString valueText(const FormField& f, const QMap<QString, QVariant>& overrides)
{
    const auto it = overrides.constFind(f.name);
    if (f.type == "checkbox") {
        const bool on = it != overrides.constEnd() ? it->toBool() : f.checked;
        return on ? QStringLiteral("Yes") : QStringLiteral("Off");
    }
    return it != overrides.constEnd() ? it->toString() : f.value;
}

// Puts a dotted name into the tree.
void insert(Node& root, const QStringList& parts, const QString& value)
{
    Node* cur = &root;
    for (const QString& part : parts) {
        Node* next = nullptr;
        for (Node& c : cur->children)
            if (c.name == part && !c.hasValue)
                next = &c;
        if (!next) {
            cur->children.append(Node{part, {}, false, {}});
            next = &cur->children.last();
        }
        cur = next;
    }
    cur->value = value;
    cur->hasValue = true;
}

void writeNode(QXmlStreamWriter& w, const Node& n)
{
    w.writeStartElement(QStringLiteral("field"));
    w.writeAttribute(QStringLiteral("name"), n.name);
    if (n.hasValue)
        w.writeTextElement(QStringLiteral("value"), n.value);
    for (const Node& c : n.children)
        writeNode(w, c);
    w.writeEndElement();
}

void collect(QXmlStreamReader& r, const QString& prefix, QList<QPair<QString, QString>>& out)
{
    // positioned on a <field> start element
    const QString name = r.attributes().value(QStringLiteral("name")).toString();
    const QString full = prefix.isEmpty() ? name : prefix + QLatin1Char('.') + name;
    QString value;
    bool hasValue = false;
    while (r.readNextStartElement()) {
        if (r.name() == QLatin1String("field")) {
            collect(r, full, out);
        } else if (r.name() == QLatin1String("value")) {
            if (!hasValue)
                value = r.readElementText();
            else
                r.skipCurrentElement();
            hasValue = true;
        } else {
            r.skipCurrentElement();
        }
    }
    if (hasValue && !name.isEmpty())
        out.append({full, value});
}

} // namespace

QString writeXfdf(const QString& outFile, const QList<FormField>& fields, const QMap<QString, QVariant>& overrides)
{
    Node root;
    for (const FormField& f : fields) {
        if (f.type == "button" || f.type == "signature")
            continue;
        insert(root, f.name.split(QLatin1Char('.')), valueText(f, overrides));
    }
    QSaveFile file(outFile);
    if (!file.open(QIODevice::WriteOnly))
        return QStringLiteral("write");
    QXmlStreamWriter w(&file);
    w.setAutoFormatting(true);
    w.writeStartDocument();
    w.writeStartElement(QStringLiteral("xfdf"));
    w.writeDefaultNamespace(kNs);
    w.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
    w.writeStartElement(QStringLiteral("fields"));
    for (const Node& c : root.children)
        writeNode(w, c);
    w.writeEndElement();
    w.writeEndElement();
    w.writeEndDocument();
    if (w.hasError())
        return QStringLiteral("write");
    return file.commit() ? QString() : QStringLiteral("write");
}

FormDataRead readXfdf(const QString& path, const QList<FormField>& fields)
{
    FormDataRead out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        out.error = QStringLiteral("file");
        return out;
    }
    QXmlStreamReader r(&file);
    QList<QPair<QString, QString>> found;
    if (!r.readNextStartElement() || r.name() != QLatin1String("xfdf")) {
        out.error = QStringLiteral("format");
        return out;
    }
    while (r.readNextStartElement()) {
        if (r.name() == QLatin1String("fields")) {
            while (r.readNextStartElement()) {
                if (r.name() == QLatin1String("field"))
                    collect(r, {}, found);
                else
                    r.skipCurrentElement();
            }
        } else {
            r.skipCurrentElement();
        }
    }
    if (r.hasError()) {
        out.error = QStringLiteral("format");
        return out;
    }
    QMap<QString, const FormField*> byName;
    for (const FormField& f : fields)
        byName.insert(f.name, &f);
    for (const auto& [name, value] : found) {
        const FormField* f = byName.value(name, nullptr);
        if (!f) {
            out.unknown.append(name);
        } else if (f->readOnly) {
            out.skipped.append(name + QStringLiteral(": readonly"));
        } else if (f->type == "text") {
            out.values.insert(name, value);
        } else if (f->type == "checkbox") {
            const QString v = value.trimmed();
            out.values.insert(name, !(v.isEmpty() || v.compare(QLatin1String("Off"), Qt::CaseInsensitive) == 0 || v == QLatin1String("0")
                                      || v.compare(QLatin1String("false"), Qt::CaseInsensitive) == 0));
        } else if (f->type == "choice" || f->type == "radio") {
            if (value.isEmpty() && f->type == "radio")
                out.values.insert(name, QString());
            else if (f->options.contains(value))
                out.values.insert(name, value);
            else
                out.skipped.append(name + QStringLiteral(": option"));
        } else {
            out.skipped.append(name + QStringLiteral(": unsupported"));
        }
    }
    out.ok = true;
    return out;
}

} // namespace dp::tools
