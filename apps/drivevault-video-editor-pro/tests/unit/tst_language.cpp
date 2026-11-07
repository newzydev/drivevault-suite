#include "localization/LanguageManager.h"
#include "settings/SettingsManager.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace dv;

class TstLanguage : public QObject {
    Q_OBJECT
private slots:
    void resolveExplicitAndAuto()
    {
        QCOMPARE(LanguageManager::resolve("th-TH"), QStringLiteral("th-TH"));
        QCOMPARE(LanguageManager::resolve("en-US"), QStringLiteral("en-US"));
        QCOMPARE(LanguageManager::resolve("auto", QLocale(QLocale::Thai, QLocale::Thailand)), QStringLiteral("th-TH"));
        QCOMPARE(LanguageManager::resolve("auto", QLocale(QLocale::German, QLocale::Germany)), QStringLiteral("en-US"));
        QCOMPARE(LanguageManager::resolve("garbage", QLocale(QLocale::English)), QStringLiteral("en-US"));
    }
    void switchingLanguageEmitsAndFallsBackWithoutQm()
    {
        QTemporaryDir dir;
        SettingsManager settings;
        settings.load(dir.filePath("s.json"));
        LanguageManager lm(&settings);
        lm.setTranslationPrefix(dir.path()); // no .qm here: Thai load must fail gracefully

        QSignalSpy spy(&lm, &LanguageManager::languageChanged);
        settings.set("language", "en-US");
        QCOMPARE(lm.currentLanguage(), QStringLiteral("en-US"));
        QVERIFY(spy.count() >= 1);
        settings.set("language", "th-TH");
        QCOMPARE(lm.currentLanguage(), QStringLiteral("th-TH"));
    }
    void listsBothLanguages()
    {
        SettingsManager settings;
        LanguageManager lm(&settings);
        QCOMPARE(lm.languages().size(), 2);
    }
};

QTEST_MAIN(TstLanguage)
#include "tst_language.moc"
