#include "PageScript.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace dv::br::ab {

QString buildPageScript(const QString& libraryJs, const PageCosmetics& page, const QString& genericClasses, const QString& genericIds, bool defend)
{
    const bool generic = !page.noGeneric && (!genericClasses.isEmpty() || !genericIds.isEmpty());
    if (page.empty() && !generic && !defend)
        return {};
    QJsonObject cfg;
    cfg.insert(QStringLiteral("sel"), QJsonArray::fromStringList(page.noCosmetic ? QStringList() : page.selectors));
    cfg.insert(QStringLiteral("css"), QJsonArray::fromStringList(page.noCosmetic ? QStringList() : page.styles));
    QJsonArray sl;
    for (const ScriptletCall& c : page.scriptlets)
        sl.append(QJsonArray{c.name, QJsonArray::fromStringList(c.args)});
    cfg.insert(QStringLiteral("sl"), sl);
    cfg.insert(QStringLiteral("goff"), QJsonArray::fromStringList(page.genericOff));
    cfg.insert(QStringLiteral("gen"), generic && !page.noCosmetic);
    cfg.insert(QStringLiteral("def"), defend);
    if (generic && !page.noCosmetic) {
        cfg.insert(QStringLiteral("cls"), genericClasses);
        cfg.insert(QStringLiteral("ids"), genericIds);
    }
    const QString json = QString::fromUtf8(QJsonDocument(cfg).toJson(QJsonDocument::Compact));
    // the JSON text is also a valid JavaScript expression, except that U+2028 / U+2029 must be escaped in older engines
    QString safe = json;
    safe.replace(QChar(0x2028), QStringLiteral("\\u2028")).replace(QChar(0x2029), QStringLiteral("\\u2029"));
    return QStringLiteral("(function(){var CFG=") + safe + QStringLiteral(";") + libraryJs + QStringLiteral("\n})();");
}

} // namespace dv::br::ab
