#include "core/AppPaths.h"
#include "project/ProjectManager.h"
#include "project/RecentProjectsModel.h"
#include "settings/SettingsManager.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace dv;

class TstProjectManager : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_root;
    std::unique_ptr<SettingsManager> m_settings;
    std::unique_ptr<RecentProjectsModel> m_recents;
    std::unique_ptr<ProjectManager> m_pm;

private slots:
    void init()
    {
        paths::setOverrideRoot(m_root.filePath(QString::number(QRandomGenerator::global()->generate())));
        m_settings = std::make_unique<SettingsManager>();
        m_settings->load();
        m_recents = std::make_unique<RecentProjectsModel>(m_settings.get());
        m_recents->load();
        m_pm = std::make_unique<ProjectManager>(m_settings.get(), m_recents.get());
    }
    void cleanup()
    {
        m_pm.reset();
        m_recents.reset();
        m_settings.reset();
        paths::setOverrideRoot({});
    }

    void createsProjectFolderAndFile()
    {
        const QString folder = m_root.filePath("projects");
        QCOMPARE(m_pm->createProject("Demo: Clip", folder, 1920, 1080, "29.97", 48000), QString());
        QVERIFY(m_pm->isOpen());
        QVERIFY(!m_pm->dirty());
        QCOMPARE(m_pm->frameRateLabel(), QStringLiteral("29.97"));
        QCOMPARE(m_pm->nominalFps(), 30);
        QVERIFY(QFile::exists(m_pm->filePath()));
        QVERIFY(m_pm->filePath().endsWith("Demo Clip/Demo Clip.dvproj"));
        QCOMPARE(m_recents->count(), 1);
    }
    void refusesToOverwriteExistingProject()
    {
        const QString folder = m_root.filePath("projects");
        QCOMPARE(m_pm->createProject("A", folder, 1920, 1080, "30", 48000), QString());
        m_pm->closeProject();
        QVERIFY(!m_pm->createProject("A", folder, 1920, 1080, "30", 48000).isEmpty());
    }
    void rejectsBadInput()
    {
        const QString folder = m_root.filePath("projects");
        QVERIFY(!m_pm->createProject("", folder, 1920, 1080, "30", 48000).isEmpty());
        QVERIFY(!m_pm->createProject("CON", folder, 1920, 1080, "30", 48000).isEmpty());
        QVERIFY(!m_pm->createProject("ok", folder, 5, 1080, "30", 48000).isEmpty());
        QVERIFY(!m_pm->createProject("ok", folder, 1920, 1080, "abc", 48000).isEmpty());
        QVERIFY(!m_pm->createProject("ok", "", 1920, 1080, "30", 48000).isEmpty());
        QVERIFY(!m_pm->isOpen());
    }
    void trackEditsMarkDirtyAndPersist()
    {
        const QString folder = m_root.filePath("projects");
        QCOMPARE(m_pm->createProject("Tracks", folder, 1280, 720, "25", 44100), QString());
        QSignalSpy dirty(m_pm.get(), &ProjectManager::dirtyChanged);
        m_pm->tracks()->toggle(0, "muted");
        m_pm->tracks()->toggle(1, "locked");
        QVERIFY(m_pm->dirty());
        QCOMPARE(dirty.count(), 1);
        QCOMPARE(m_pm->save(), QString());
        QVERIFY(!m_pm->dirty());

        const QString path = m_pm->filePath();
        m_pm->closeProject();
        QVERIFY(!m_pm->isOpen());
        QCOMPARE(m_pm->openProject(path), QString());
        QCOMPARE(m_pm->width(), 1280);
        QCOMPARE(m_pm->sampleRate(), 44100);
        QVERIFY(m_pm->tracks()->tracks().at(0).muted);
        QVERIFY(m_pm->tracks()->tracks().at(1).locked);
        QVERIFY(!m_pm->dirty());
    }
    void saveAsWritesNewFileAndAddsExtension()
    {
        QCOMPARE(m_pm->createProject("Orig", m_root.filePath("projects"), 1920, 1080, "30", 48000), QString());
        const QString target = m_root.filePath("copy/Copy of Orig");
        QCOMPARE(m_pm->saveAs(target), QString());
        QVERIFY(QFile::exists(target + ".dvproj"));
        QCOMPARE(m_pm->filePath(), target + ".dvproj");
    }
    void openRejectsWrongExtensionAndGarbage()
    {
        QFile f(m_root.filePath("bad.dvproj"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{}");
        f.close();
        QVERIFY(!m_pm->openProject(f.fileName()).isEmpty());
        QVERIFY(!m_pm->openProject(m_root.filePath("x.txt")).isEmpty());
        QVERIFY(!m_pm->openProject(m_root.filePath("missing.dvproj")).isEmpty());
        QVERIFY(!m_pm->isOpen());
    }
    void playheadStepsAndFormats()
    {
        QCOMPARE(m_pm->createProject("PH", m_root.filePath("projects"), 1920, 1080, "30", 48000), QString());
        m_pm->setPlayheadFrame(95);
        QCOMPARE(m_pm->timecode(), QStringLiteral("00:00:03:05"));
        m_pm->stepFrames(-100);
        QCOMPARE(m_pm->playheadFrame(), qint64(0)); // clamped at zero
    }
    void autosaveDoesNotTouchProjectFileAndOffersRecovery()
    {
        QCOMPARE(m_pm->createProject("Auto", m_root.filePath("projects"), 1920, 1080, "30", 48000), QString());
        const QString path = m_pm->filePath();
        QFile original(path);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const QByteArray before = original.readAll();
        original.close();

        m_pm->tracks()->toggle(0, "muted");
        m_pm->autosaveNow();

        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), before); // project file untouched
        original.close();

        QTest::qWait(1100); // make the autosave strictly newer than the project file
        m_pm->autosaveNow();

        ProjectManager second(m_settings.get(), m_recents.get()); // simulates a restart after a crash
        QCOMPARE(second.openProject(path), QString());
        QVERIFY(second.hasRecovery());
        QCOMPARE(second.restoreRecovery(), QString());
        QVERIFY(second.dirty());
        QVERIFY(second.tracks()->tracks().at(0).muted);
        QVERIFY(!second.hasRecovery());
    }
    void recentsRoundTripAndMissingFiles()
    {
        QCOMPARE(m_pm->createProject("One", m_root.filePath("projects"), 1920, 1080, "30", 48000), QString());
        const QString first = m_pm->filePath();
        QCOMPARE(m_pm->createProject("Two", m_root.filePath("projects"), 1920, 1080, "30", 48000), QString());
        QCOMPARE(m_recents->count(), 2);
        QCOMPARE(m_recents->data(m_recents->index(0), RecentProjectsModel::NameRole).toString(), QStringLiteral("Two"));

        RecentProjectsModel reloaded(m_settings.get());
        reloaded.load();
        QCOMPARE(reloaded.count(), 2);

        QVERIFY(QFile::remove(first));
        QCOMPARE(reloaded.data(reloaded.index(1), RecentProjectsModel::ExistsRole).toBool(), false);
        reloaded.removeAt(1);
        QCOMPARE(reloaded.count(), 1);
    }
    void recentsRespectLimit()
    {
        m_settings->set("recentProjectsLimit", 3);
        for (int i = 0; i < 6; ++i)
            m_recents->add(m_root.filePath(QString("p%1.dvproj").arg(i)), QString("P%1").arg(i));
        QCOMPARE(m_recents->count(), 3);
        QCOMPARE(m_recents->data(m_recents->index(0), RecentProjectsModel::NameRole).toString(), QStringLiteral("P5"));
    }
};

QTEST_MAIN(TstProjectManager)
#include "tst_projectmanager.moc"
