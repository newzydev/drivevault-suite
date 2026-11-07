#pragma once

#include <QLocale>
#include <QObject>
#include <QTranslator>
#include <QVariantList>

namespace dv {

class SettingsManager;

// Runtime language switching for the UI (th-TH / en-US).
//
// Source strings in code and QML are English; Thai comes from drivevault_th_TH.qm compiled from
// resources/translations/drivevault_th_TH.ts. After a switch the owner calls QQmlEngine::retranslate().
class LanguageManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentLanguage READ currentLanguage NOTIFY languageChanged FINAL)
    Q_PROPERTY(QVariantList languages READ languages CONSTANT FINAL)

public:
    explicit LanguageManager(SettingsManager* settings, QObject* parent = nullptr);

    QString currentLanguage() const { return m_current; }
    QVariantList languages() const;

    // Resolves "auto" (system language) to a supported code.
    static QString resolve(const QString& setting, const QLocale& system = QLocale::system());
    // Where compiled translations live; overridable for tests.
    void setTranslationPrefix(const QString& prefix) { m_prefix = prefix; }

    // Installs the translator for the language stored in settings.
    Q_INVOKABLE void apply();
    Q_INVOKABLE bool isLoaded(const QString& code) const;

signals:
    void languageChanged(const QString& code);

private:
    bool install(const QString& code);

    SettingsManager* m_settings;
    QTranslator m_translator;
    QString m_current;
    QString m_prefix = QStringLiteral(":/i18n");
};

} // namespace dv
