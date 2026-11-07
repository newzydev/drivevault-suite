#include "LanguageManager.h"

#include "core/Logger.h"
#include "settings/SettingsManager.h"

#include <QCoreApplication>
#include <QVariantMap>

namespace dv {

LanguageManager::LanguageManager(SettingsManager* settings, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
{
    connect(m_settings, &SettingsManager::valueChanged, this, [this](const QString& key) {
        if (key == QLatin1String("language"))
            apply();
    });
}

QVariantList LanguageManager::languages() const
{
    // Native names are intentionally not translated so users can always find their language.
    return {
        QVariantMap{{"code", "th-TH"}, {"name", QStringLiteral("ไทย")}},
        QVariantMap{{"code", "en-US"}, {"name", QStringLiteral("English")}},
    };
}

QString LanguageManager::resolve(const QString& setting, const QLocale& system)
{
    if (setting == QLatin1String("th-TH") || setting == QLatin1String("en-US"))
        return setting;
    return system.language() == QLocale::Thai ? QStringLiteral("th-TH") : QStringLiteral("en-US");
}

bool LanguageManager::install(const QString& code)
{
    QCoreApplication::removeTranslator(&m_translator);
    if (code == QLatin1String("en-US")) {
        // English strings are the source text; nothing to load.
        return true;
    }
    const QString base = QStringLiteral("drivevault_") + QString(code).replace(QLatin1Char('-'), QLatin1Char('_'));
    if (!m_translator.load(base, m_prefix, QString(), QStringLiteral(".qm"))) {
        qCWarning(lcApp) << "Translation not found for" << code << "in" << m_prefix;
        return false;
    }
    QCoreApplication::installTranslator(&m_translator);
    return true;
}

void LanguageManager::apply()
{
    const QString code = resolve(m_settings->get(QStringLiteral("language")).toString());
    const bool ok = install(code);
    if (!ok && code != QLatin1String("en-US"))
        qCWarning(lcApp) << "Falling back to English UI";

    QLocale::setDefault(QLocale(code == QLatin1String("th-TH") ? QLocale::Thai : QLocale::English,
                                code == QLatin1String("th-TH") ? QLocale::Thailand : QLocale::UnitedStates));
    if (m_current != code || !ok) {
        m_current = code;
        qCInfo(lcApp) << "UI language:" << code;
        emit languageChanged(code);
    }
}

bool LanguageManager::isLoaded(const QString& code) const
{
    return m_current == code;
}

} // namespace dv
