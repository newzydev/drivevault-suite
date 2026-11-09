#include "settings/SettingsManager.h"

#include <QFile>
#include <QJSEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using dv::SettingsManager;

class TstSettings : public QObject {
    Q_OBJECT
private:
    static void writeFile(const QString& path, const QByteArray& data)
    {
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(data);
    }

private slots:
    void defaultsWhenNoFile()
    {
        QTemporaryDir dir;
        SettingsManager s;
        QVERIFY(s.load(dir.filePath("settings.json")));
        QCOMPARE(s.get("theme").toString(), QStringLiteral("dark"));
        QCOMPARE(s.get("accentColor").toString(), QStringLiteral("#8B5CF6"));
        QCOMPARE(s.get("language").toString(), QStringLiteral("auto"));
        QCOMPARE(s.get("autoSaveMinutes").toInt(), 5);
        QVERIFY(!s.restartRequired());
    }
    void roundTripPersistsValuesAndState()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("settings.json");
        {
            SettingsManager s;
            s.load(file);
            QVERIFY(s.set("theme", "light"));
            QVERIFY(s.set("language", "th-TH"));
            QVERIFY(s.set("accentColor", "#14b8a6"));
            QVERIFY(s.set("autoSaveMinutes", 12));
            QVERIFY(s.set("snapping", false));
            s.setState("window", QVariantMap{{"w", 1200}, {"h", 800}});
            QVERIFY(s.save());
        }
        SettingsManager s2;
        QVERIFY(s2.load(file));
        QCOMPARE(s2.get("theme").toString(), QStringLiteral("light"));
        QCOMPARE(s2.get("language").toString(), QStringLiteral("th-TH"));
        QCOMPARE(s2.get("accentColor").toString(), QStringLiteral("#14B8A6")); // normalised
        QCOMPARE(s2.get("autoSaveMinutes").toInt(), 12);
        QCOMPARE(s2.get("snapping").toBool(), false);
        QCOMPARE(s2.state("window").toMap().value("w").toInt(), 1200);
    }
    void stateGivenAsAJavaScriptObjectIsSaved()
    {
        // QML passes an object as a QJSValue inside the QVariant: it used to be written as null (window size / maximized never remembered)
        QTemporaryDir dir;
        const QString file = dir.filePath("settings.json");
        {
            QJSEngine engine;
            QJSValue o = engine.newObject();
            o.setProperty("w", 1280);
            o.setProperty("h", 760);
            o.setProperty("maximized", true);
            QJSValue list = engine.newArray(2);
            list.setProperty(0, 3);
            list.setProperty(1, 4);
            SettingsManager s;
            s.load(file);
            s.setState("window", QVariant::fromValue(o));
            s.setState("sizes", QVariant::fromValue(list));
            QVERIFY(s.save());
        }
        SettingsManager s2;
        QVERIFY(s2.load(file));
        const QVariantMap w = s2.state("window").toMap();
        QCOMPARE(w.value("w").toInt(), 1280);
        QCOMPARE(w.value("h").toInt(), 760);
        QVERIFY(w.value("maximized").toBool());
        QCOMPARE(s2.state("sizes").toList().size(), 2);
    }
    void rejectsInvalidValues()
    {
        QTemporaryDir dir;
        SettingsManager s;
        s.load(dir.filePath("s.json"));
        QVERIFY(!s.set("theme", "neon"));
        QVERIFY(!s.set("accentColor", "not-a-colour"));
        QVERIFY(!s.set("nonexistent", 1));
        QVERIFY(!s.set("snapping", "yes"));
        QVERIFY(!s.set("autoSaveMinutes", "abc"));
        QCOMPARE(s.get("theme").toString(), QStringLiteral("dark"));
    }
    void clampsNumbersToRange()
    {
        QTemporaryDir dir;
        SettingsManager s;
        s.load(dir.filePath("s.json"));
        QVERIFY(s.set("autoSaveMinutes", 9999));
        QCOMPARE(s.get("autoSaveMinutes").toInt(), 60);
        QVERIFY(s.set("autoSaveMinutes", -5));
        QCOMPARE(s.get("autoSaveMinutes").toInt(), 1);
        QVERIFY(s.set("uiScale", 10.0));
        QCOMPARE(s.get("uiScale").toDouble(), 2.0);
    }
    void corruptFileFallsBackToDefaultsAndKeepsCopy()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("settings.json");
        writeFile(file, "{ this is not json");
        SettingsManager s;
        QVERIFY(!s.load(file));
        QCOMPARE(s.get("theme").toString(), QStringLiteral("dark"));
        QVERIFY(QFile::exists(file + ".corrupt"));
    }
    void invalidStoredValueIsReplacedByDefault()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("settings.json");
        writeFile(file, R"({"schemaVersion":1,"values":{"theme":"purple","autoSaveMinutes":"x","snapping":false,"bogus":1}})");
        SettingsManager s;
        QVERIFY(s.load(file));
        QCOMPARE(s.get("theme").toString(), QStringLiteral("dark"));
        QCOMPARE(s.get("autoSaveMinutes").toInt(), 5);
        QCOMPARE(s.get("snapping").toBool(), false);
    }
    void changeNotificationsAndRestartFlag()
    {
        QTemporaryDir dir;
        SettingsManager s;
        s.load(dir.filePath("s.json"));
        QSignalSpy changed(&s, &SettingsManager::valueChanged);
        QSignalSpy restart(&s, &SettingsManager::restartRequiredChanged);
        s.set("theme", "light");
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().first().toString(), QStringLiteral("theme"));
        s.set("theme", "light"); // unchanged -> no signal
        QCOMPARE(changed.count(), 1);
        QCOMPARE(restart.count(), 0);
        s.set("uiScale", 1.25);
        QVERIFY(s.restartRequired());
        QCOMPARE(restart.count(), 1);
    }
    void resetToDefaults()
    {
        QTemporaryDir dir;
        SettingsManager s;
        s.load(dir.filePath("s.json"));
        s.set("theme", "light");
        s.resetToDefaults();
        QCOMPARE(s.get("theme").toString(), QStringLiteral("dark"));
    }
    void debouncedSaveWritesFile()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("s.json");
        SettingsManager s;
        s.load(file);
        s.set("theme", "light");
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(file), 3000);
    }
};

QTEST_MAIN(TstSettings)
#include "tst_settings.moc"
