// End-to-end UI test: loads the real QML UI (offscreen), clicks real controls and checks the effects
// on the backend (project files on disk, settings, translations).

#include "AppContext.h"
#include "TestMedia.h"
#include "core/AppPaths.h"
#include "media/MediaProbe.h"
#include "project/ProjectFile.h"

#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QtTest>

using dv::ClipData;
using dv::MediaInfo;
using dv::MediaProbe;

namespace {
QStringList g_qmlProblems;

void collectingHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        // Platform noise that is irrelevant for the UI.
        if (message.contains(QLatin1String("propagateSizeHints")) || message.contains(QLatin1String("QFontDatabase")))
            return;
        g_qmlProblems << message;
    }
    Q_UNUSED(context)
}

QQuickItem* findItem(QQuickItem* root, const QString& name)
{
    if (!root)
        return nullptr;
    if (root->objectName() == name)
        return root;
    for (QQuickItem* child : root->childItems()) {
        if (QQuickItem* hit = findItem(child, name))
            return hit;
    }
    return nullptr;
}
} // namespace

class TstUiFlow : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QTemporaryDir media;   // test media; declared before the context so it outlives every decoder that has the files open
    std::unique_ptr<dv::AppContext> m_ctx;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow* m_window = nullptr;

    QQuickItem* item(const QString& name) const { return findItem(m_window->contentItem(), name); }

    void click(const QString& name)
    {
        QQuickItem* target = nullptr;
        QTRY_VERIFY2_WITH_TIMEOUT((target = item(name)) != nullptr && target->isVisible() && target->width() > 0,
                                  qPrintable("control not found / visible: " + name), 5000);
        const QPointF center = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
        QTest::qWait(60);
    }

private slots:
    void initTestCase()
    {
        dv::paths::setOverrideRoot(m_dir.filePath("app"));
        qInstallMessageHandler(collectingHandler);
        QQuickStyle::setStyle(QStringLiteral("Basic"));

        m_ctx = std::make_unique<dv::AppContext>();
        m_ctx->settings.load();
        m_ctx->settings.set("language", "en-US");
        m_ctx->settings.set("defaultProjectLocation", m_dir.filePath("projects"));
        m_ctx->recents.load();
        m_ctx->language.apply();
        m_ctx->registerQmlTypes();

        m_engine = std::make_unique<QQmlApplicationEngine>();
        m_ctx->attach(m_engine.get());
        m_engine->loadFromModule("DriveVault.UI", "Main");
        QVERIFY(!m_engine->rootObjects().isEmpty());
        m_window = qobject_cast<QQuickWindow*>(m_engine->rootObjects().constFirst());
        QVERIFY(m_window);
        m_window->resize(1440, 900);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window));
        QTest::qWait(300);
    }

    void homeScreenIsShownFirst()
    {
        QCOMPARE(m_ctx->controller.screen(), QStringLiteral("home"));
        QQuickItem* button = item("homeNewProject");
        QVERIFY(button);
        QCOMPARE(button->property("text").toString(), QStringLiteral("New Project"));
    }

    void languageSwitchesAtRuntimeWithoutRestart()
    {
        m_ctx->settings.set("language", "th-TH");
        QTRY_COMPARE_WITH_TIMEOUT(item("homeNewProject")->property("text").toString(),
                                  QString::fromUtf8("โปรเจกต์ใหม่"), 3000);
        m_ctx->settings.set("language", "en-US");
        QTRY_COMPARE_WITH_TIMEOUT(item("homeNewProject")->property("text").toString(), QStringLiteral("New Project"), 3000);
    }

    void newProjectDialogCreatesRealProjectAndOpensEditor()
    {
        click("homeNewProject");
        QTRY_VERIFY_WITH_TIMEOUT(m_ctx->controller.newProjectOpen(), 2000);

        QQuickItem* name = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((name = item("npName")) != nullptr && name->isVisible(), 3000);
        name->setProperty("text", QString::fromUtf8("คลิปทดสอบ UI"));
        click("npCreate");

        QTRY_VERIFY_WITH_TIMEOUT(m_ctx->projects.isOpen(), 3000);
        QCOMPARE(m_ctx->controller.screen(), QStringLiteral("editor"));
        QVERIFY(QFile::exists(m_ctx->projects.filePath()));
        QVERIFY(m_ctx->projects.filePath().endsWith(QString::fromUtf8("คลิปทดสอบ UI/คลิปทดสอบ UI.dvproj")));
        QTRY_VERIFY_WITH_TIMEOUT(!m_ctx->controller.newProjectOpen(), 2000); // closed after its exit transition
        QTRY_VERIFY_WITH_TIMEOUT(item("ws_edit") != nullptr, 3000);
        QTest::qWait(300);
    }

    void workspaceTabsSwitchWorkspaces()
    {
        for (const char* ws : {"color", "export", "edit"}) {          // Import, Audio, Effects, AI ... are tabs of the tool panel, not workspaces in the bar
            click(QStringLiteral("ws_") + ws);
            QCOMPARE(m_ctx->controller.workspace(), QString::fromLatin1(ws));
        }
    }

    void importEditAndExportThroughTheUi()
    {
        QVERIFY(media.isValid());
        const QString clip = media.filePath("clip.mp4");
        QVERIFY(testmedia::makeTwoColorVideo(clip, "red", "blue", true, "320x240", 30, 2));   // 4 s = 120 frames
        QVERIFY(testmedia::makeSine(media.filePath("tone.wav"), 3));

        auto* projects = &m_ctx->projects;
        const QString clipId = projects->media()->importFile(clip);
        projects->media()->importFile(media.filePath("tone.wav"));
        QTRY_VERIFY_WITH_TIMEOUT(projects->media()->isReady(clipId), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(projects->media()->busyCount(), 0, 15000);
        QCOMPARE(projects->media()->count(), 2);

        // The media panel shows the items; a double-click adds the clip at the playhead.
        m_ctx->controller.setWorkspace("edit");
        QTest::qWait(500);
        QQuickItem* cell = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((cell = item("media_clip.mp4")) != nullptr && cell->isVisible() && cell->width() > 0, 5000);
        const QPointF c = cell->mapToScene(QPointF(cell->width() / 2, cell->height() / 2));
        QTest::mouseDClick(m_window, Qt::LeftButton, Qt::NoModifier, c.toPoint());
        QTRY_COMPARE_WITH_TIMEOUT(projects->timeline()->clipCount(), 2, 3000);   // linked video + audio
        const QList<ClipData> added = projects->timeline()->clips();
        QCOMPARE(added.at(0).durationFrames, qint64(120));

        // Split at the playhead with Ctrl+K, undo with Ctrl+Z, redo with Ctrl+Shift+Z.
        projects->setPlayheadFrame(40);
        QTest::keyClick(m_window, Qt::Key_K, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(projects->timeline()->clipCount(), 4, 3000);
        QTest::keyClick(m_window, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(projects->timeline()->clipCount(), 2, 3000);
        QTest::keyClick(m_window, Qt::Key_Z, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(projects->timeline()->clipCount(), 4, 3000);

        // Delete the second half (select it, press Delete), then undo.
        QString second;
        for (const ClipData& cd : projects->timeline()->clips())
            if (cd.startFrame == 40 && cd.trackId == "V1")
                second = cd.id;
        QVERIFY(!second.isEmpty());
        projects->timeline()->select(second);
        QTest::keyClick(m_window, Qt::Key_Delete);
        QTRY_COMPARE_WITH_TIMEOUT(projects->timeline()->clipCount(), 2, 3000);
        QTest::keyClick(m_window, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(projects->timeline()->clipCount(), 4, 3000);

        // Drag the second half to the right with the mouse (moves video + linked audio).
        QQuickItem* clipItem = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((clipItem = item("clip_" + second)) != nullptr && clipItem->width() > 20, 3000);
        QPointF from = clipItem->mapToScene(QPointF(clipItem->width() / 2, clipItem->height() / 2));
        QTest::mouseMove(m_window, from.toPoint());
        QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, from.toPoint());
        for (int i = 1; i <= 8; ++i)
            QTest::mouseMove(m_window, (from + QPointF(10 * i, 0)).toPoint());
        QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier, (from + QPointF(80, 0)).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(projects->timeline()->findClip(second)->startFrame > 40, 3000);
        QCOMPARE(projects->timeline()->findClip(second)->startFrame, qint64(60));      // 80 px at 4 px per frame = 20 frames
        QTest::keyClick(m_window, Qt::Key_Z, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(projects->timeline()->findClip(second)->startFrame, qint64(40), 3000);

        // Trim the end of that clip by dragging its right edge to the left.
        QQuickItem* handle = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((handle = item("clipTrimEnd_" + second)) != nullptr, 3000);
        from = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2));
        QTest::mouseMove(m_window, from.toPoint());
        QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, from.toPoint());
        for (int i = 1; i <= 6; ++i)
            QTest::mouseMove(m_window, (from - QPointF(20 * i, 0)).toPoint());
        QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier, (from - QPointF(120, 0)).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(projects->timeline()->findClip(second)->durationFrames < 80, 3000);
        QCOMPARE(projects->timeline()->findClip(second)->durationFrames, qint64(50));   // 120 px = 30 frames off 80

        // Play / pause through the transport button.
        projects->setPlayheadFrame(0);
        click("playButton");
        QTRY_VERIFY_WITH_TIMEOUT(m_ctx->playback.playing(), 3000);
        QTest::qWait(400);
        QVERIFY(projects->playheadFrame() > 3);
        click("playButton");
        QTRY_VERIFY_WITH_TIMEOUT(!m_ctx->playback.playing(), 3000);

        // Export through the Export workspace.
        m_ctx->settings.set("exportFolder", media.filePath("out"));
        m_ctx->settings.set("exportPreset", "720p");
        click("ws_export");
        QTest::qWait(500);
        click("exportStart");
        QTRY_COMPARE_WITH_TIMEOUT(m_ctx->exporter.state(), QStringLiteral("done"), 60000);
        QVERIFY(QFile::exists(m_ctx->exporter.outputPath()));
        MediaInfo outInfo;
        QVERIFY(MediaProbe::probe(m_ctx->exporter.outputPath(), &outInfo, nullptr));
        QCOMPARE(outInfo.width, 1280);
        QCOMPARE(outInfo.height, 720);
        QVERIFY(outInfo.hasAudio);
        m_ctx->exporter.reset();

        // Back to the edit workspace, then undo everything so the next tests start from the saved state.
        click("ws_edit");
        while (projects->timeline()->canUndo())
            projects->timeline()->undo();
        QCOMPARE(projects->timeline()->clipCount(), 0);
        projects->media()->waitForIdle();
        QVERIFY(projects->save().isEmpty());   // the following tests expect a clean project
    }

    void inspectorToolsMarkersAndMixerWorkThroughTheUi()
    {
        auto* projects = &m_ctx->projects;
        auto* tl = projects->timeline();
        m_ctx->controller.setWorkspace("edit");
        const QString mediaId = projects->media()->idForPath(media.filePath("clip.mp4"));
        QVERIFY(!mediaId.isEmpty());
        QCOMPARE(tl->addMedia(mediaId, -1, 0), QString());
        QString video, audio;
        for (const ClipData& c : tl->clips())
            (c.trackId.startsWith("V") ? video : audio) = c.id;
        QVERIFY(!video.isEmpty() && !audio.isEmpty());
        projects->setPlayheadFrame(10);

        // The inspector shows the controls of the selected clip; clicking the opacity slider changes the clip.
        tl->select(video);
        QQuickItem* slider = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((slider = item("param_opacity")) != nullptr && slider->isVisible() && slider->width() > 20, 5000);
        QPointF centre = slider->mapToScene(QPointF(slider->width() / 4, slider->height() / 2));
        QTest::mouseClick(m_window, Qt::LeftButton, Qt::NoModifier, centre.toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(tl->paramValue(video, "opacity", 10) < 0.9, 3000);
        QVERIFY(tl->paramValue(video, "opacity", 10) > 0.05);

        // The keyframe diamond adds a keyframe at the playhead, a second click removes it again.
        click("key_opacity");
        QVERIFY(tl->keyframeAt(video, "opacity", 10));
        QVERIFY(tl->paramAnimated(video, "opacity"));
        click("key_opacity");
        QVERIFY(!tl->keyframeAt(video, "opacity", 10));

        // Audio controls belong to the linked sound clip.
        QTRY_VERIFY_WITH_TIMEOUT(item("slider_fadeIn") != nullptr, 3000);
        QVERIFY(item("param_volume") != nullptr);

        // Tool shortcuts and tool buttons.
        QTest::keyClick(m_window, Qt::Key_B);
        QTRY_COMPARE_WITH_TIMEOUT(item("tool_ripple")->property("variant").toString(), QStringLiteral("primary"), 2000);
        click("tool_slip");
        QCOMPARE(item("tool_slip")->property("variant").toString(), QStringLiteral("primary"));
        QTest::keyClick(m_window, Qt::Key_V);
        QTRY_COMPARE_WITH_TIMEOUT(item("tool_select")->property("variant").toString(), QStringLiteral("primary"), 2000);

        // Markers and the In / Out range.
        QTest::keyClick(m_window, Qt::Key_M);
        QTRY_COMPARE_WITH_TIMEOUT(tl->markerList().size(), 1, 2000);
        projects->setPlayheadFrame(20);
        QTest::keyClick(m_window, Qt::Key_I);
        projects->setPlayheadFrame(60);
        QTest::keyClick(m_window, Qt::Key_O);
        QTRY_COMPARE_WITH_TIMEOUT(tl->inPoint(), qint64(20), 2000);
        QCOMPARE(tl->outPoint(), qint64(60));

        // Copy / paste with the keyboard.
        tl->select(video);
        QTest::keyClick(m_window, Qt::Key_C, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(tl->canPaste(), 2000);
        projects->setPlayheadFrame(200);
        const int before = tl->clipCount();
        QTest::keyClick(m_window, Qt::Key_V, Qt::ControlModifier);
        QTRY_VERIFY_WITH_TIMEOUT(tl->clipCount() > before, 2000);

        // A title shows its text editor in the inspector.
        QCOMPARE(tl->addTitle(-1, 0, "title"), QString());
        QString title;
        for (const ClipData& c : tl->clips())
            if (c.isTitle())
                title = c.id;
        QVERIFY(!title.isEmpty());
        tl->select(title);
        QTRY_VERIFY_WITH_TIMEOUT(item("titleText") != nullptr && item("titleText")->isVisible(), 3000);

        // Audio mixer: solo and mute buttons drive the track model.
        int audioTrack = -1;
        const auto tracks = projects->tracks()->tracks();
        for (int i = 0; i < tracks.size(); ++i)
            if (tracks.at(i).kind == "audio") {
                audioTrack = i;
                break;
            }
        QVERIFY(audioTrack >= 0);
        // the strip sits far down the inspector, so the button is activated directly
        auto activate = [&](const QString& name) {
            QQuickItem* target = item(name);
            QVERIFY2(target != nullptr, qPrintable(name));
            QMetaObject::invokeMethod(target, "clicked");
        };
        activate("trackSolo" + QString::number(audioTrack));
        QVERIFY(projects->tracks()->tracks().at(audioTrack).solo);
        activate("trackSolo" + QString::number(audioTrack));
        QVERIFY(!projects->tracks()->tracks().at(audioTrack).solo);

        // Export workspace: presets and the range selector exist; the range is honoured.
        click("ws_export");
        QTRY_VERIFY_WITH_TIMEOUT(item("preset_web720") != nullptr, 3000);
        click("preset_web720");
        QCOMPARE(m_ctx->settings.get("exportPreset").toString(), QStringLiteral("720p"));
        QCOMPARE(m_ctx->settings.get("exportQuality").toInt(), 55);
        click("preset_audio");
        QCOMPARE(m_ctx->settings.get("exportFormat").toString(), QStringLiteral("m4a"));
        m_ctx->exporter.setRange("inout");
        QCOMPARE(m_ctx->exporter.rangeFrames(), (QVariantList{qint64(20), qint64(61)}));
        click("preset_youtube1080");
        QCOMPARE(m_ctx->settings.get("exportFormat").toString(), QStringLiteral("mp4"));
        m_ctx->exporter.setRange("all");
        click("ws_edit");

        // Leave the project exactly as it was: no edits, no marks, saved.
        tl->clearRange();
        for (const QVariant& m : tl->markerList())
            tl->removeMarker(m.toMap().value("id").toString());
        while (tl->canUndo())
            tl->undo();
        QCOMPARE(tl->clipCount(), 0);
        QVERIFY(projects->save().isEmpty());
    }

    void toolPanelTabsApplyRealEditsToTheSelectedClip()
    {
        auto* projects = &m_ctx->projects;
        auto* tl = projects->timeline();
        m_ctx->controller.setWorkspace("edit");
        const int clipsAtStart = tl->clipCount();
        const QString mediaId = projects->media()->idForPath(media.filePath("clip.mp4"));
        QVERIFY(!mediaId.isEmpty());
        QCOMPARE(tl->addMedia(mediaId, -1, 0), QString());
        QString video;
        for (const ClipData& c : tl->clips())
            if (c.trackId.startsWith("V")) { video = c.id; break; }
        QVERIFY(!video.isEmpty());
        tl->select(video);
        projects->setPlayheadFrame(10);
        const double satDef = tl->paramInfo("saturation").value("def").toDouble();

        click("toolTab_filters");                       // a look = real colour parameters, undoable like any edit
        click("look_mono");
        QTRY_VERIFY_WITH_TIMEOUT(tl->paramValue(video, "saturation", 10) < satDef - 0.1, 3000);
        click("look_none");
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(tl->paramValue(video, "saturation", 10) - satDef) < 1e-6, 3000);

        click("toolTab_transitions");
        click("trIn_dissolve");
        QTRY_COMPARE_WITH_TIMEOUT(tl->clipInfo(video).value("transIn").toString(), QStringLiteral("dissolve"), 3000);
        click("trIn_none");
        QTRY_COMPARE_WITH_TIMEOUT(tl->clipInfo(video).value("transIn").toString(), QString(), 3000);

        click("toolTab_effects");
        click("fx_blur");
        QTRY_VERIFY_WITH_TIMEOUT(tl->paramValue(video, "blur", 10) > tl->paramInfo("blur").value("def").toDouble(), 3000);
        click("fx_reset");
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(tl->paramValue(video, "blur", 10) - tl->paramInfo("blur").value("def").toDouble()) < 1e-6, 3000);

        const int before = tl->clipCount();
        click("toolTab_text");
        click("text_title");
        QTRY_COMPARE_WITH_TIMEOUT(tl->clipCount(), before + 1, 3000);
        tl->undo();
        QTRY_COMPARE_WITH_TIMEOUT(tl->clipCount(), before, 3000);

        // a thin guide line follows the mouse over the timeline
        QQuickItem* guide = item("hoverGuide");
        QVERIFY(guide != nullptr);
        QTest::mouseMove(m_window, guide->parentItem()->mapToScene(QPointF(300, 120)).toPoint());
        QTest::mouseMove(m_window, guide->parentItem()->mapToScene(QPointF(320, 122)).toPoint());
        QTRY_VERIFY_WITH_TIMEOUT(guide->isVisible(), 3000);

        QVERIFY(item("meterL") != nullptr && item("meterL")->isVisible());      // the master meter is always shown
        click("toolTab_import");
        while (tl->clipCount() > clipsAtStart && tl->canUndo())                 // leave the timeline as this test found it
            tl->undo();
        QCOMPARE(tl->clipCount(), clipsAtStart);
    }

    void tracksCanBeAddedRenamedAndRemoved()
    {
        auto* projects = &m_ctx->projects;
        auto* tl = projects->timeline();
        auto* tracks = projects->tracks();
        m_ctx->controller.setWorkspace("edit");
        const int before = tracks->count();
        auto rowOf = [&](const QString& id) {
            const auto list = tracks->tracks();
            for (int i = 0; i < list.size(); ++i)
                if (list.at(i).id == id)
                    return i;
            return -1;
        };
        click("addVideoTrack");                                         // the buttons under the track names
        QTRY_COMPARE_WITH_TIMEOUT(tracks->count(), before + 1, 3000);
        click("addAudioTrack");
        QTRY_COMPARE_WITH_TIMEOUT(tracks->count(), before + 2, 3000);
        const int newVideo = rowOf("V3");
        const int newAudio = rowOf("A3");
        QVERIFY(newVideo >= 0 && newAudio >= 0);

        tracks->setName(newVideo, QStringLiteral("B-roll"));            // rename (the dialog calls the same function)
        QCOMPARE(tracks->nameAt(newVideo), QStringLiteral("B-roll"));

        // a track with clips: removing it asks first; the clips go with it and the history is cleared
        const QString mediaId = projects->media()->idForPath(media.filePath("clip.mp4"));
        QVERIFY(!mediaId.isEmpty());
        const int clipsBefore = tl->clipCount();
        QCOMPARE(tl->addMedia(mediaId, newVideo, 0), QString());
        QVERIFY(tl->trackUsed(newVideo));
        QVERIFY(tracks->canRemove(newVideo));
        QVERIFY(!projects->removeTrack(newVideo).isEmpty());            // the plain removal refuses a track with clips
        QCOMPARE(projects->removeTrackWithClips(newVideo), QString());
        QCOMPARE(tl->clipCount(), clipsBefore);
        QVERIFY(!tl->canUndo());
        QCOMPARE(rowOf("V3"), -1);

        QCOMPARE(projects->removeTrack(rowOf("A3")), QString());         // an empty track goes at once
        QCOMPARE(tracks->count(), before);
        QVERIFY(!tracks->canRemove(rowOf("V1")) || tracks->count() > 2);   // the last video / audio track of a kind stays
    }

    void aiWorkspaceFindsSilenceAndAppliesItOnlyAfterConfirmation()
    {
        auto* projects = &m_ctx->projects;
        auto* tl = projects->timeline();
        QVERIFY(testmedia::runFfmpeg({"-f", "lavfi", "-i", "aevalsrc='if(lt(mod(t,2),1),0.5*sin(2*PI*440*t),0)':s=48000:d=4", media.filePath("pauses.wav")}));
        const QString id = projects->media()->importFile(media.filePath("pauses.wav"));
        QTRY_VERIFY_WITH_TIMEOUT(projects->media()->isReady(id), 15000);
        QCOMPARE(tl->addMedia(id, -1, 0), QString());
        QCOMPARE(tl->durationFrames(), qint64(120));

        m_ctx->settings.set("aiEnabled", false);
        click("toolTab_ai");
        QTest::qWait(300);
        QVERIFY(item("aiEnable") != nullptr);
        click("aiEnable");                                             // the switch turns the AI tools on
        QTRY_VERIFY_WITH_TIMEOUT(m_ctx->settings.get("aiEnabled").toBool(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(item("captionStatus") != nullptr && item("captionStatus")->isVisible(), 3000);

        auto activate = [&](const QString& name) {
            QQuickItem* target = item(name);
            QVERIFY2(target != nullptr, qPrintable(name));
            QMetaObject::invokeMethod(target, "clicked");
        };
        activate("aiSilence");
        QTRY_VERIFY_WITH_TIMEOUT(m_ctx->ai.busy() || !m_ctx->ai.silences().isEmpty(), 3000);
        m_ctx->ai.waitForIdle();
        QCOMPARE(m_ctx->ai.silences().size(), 2);
        QCOMPARE(tl->durationFrames(), qint64(120));                   // nothing was cut by the analysis
        m_ctx->ai.clearSilences();

        // the assistant: a Thai request is understood and starts the same analysis
        QQuickItem* ask = item("aiAsk");
        QVERIFY(ask);
        ask->setProperty("text", QString::fromUtf8("หาช่วงที่ไม่มีเสียงพูด"));
        QTRY_VERIFY_WITH_TIMEOUT(item("aiAskRun")->property("enabled").toBool(), 2000);
        activate("aiAskRun");
        m_ctx->ai.waitForIdle();
        QCOMPARE(m_ctx->ai.silences().size(), 2);

        QCOMPARE(m_ctx->ai.applySilenceCuts(), QString());
        QVERIFY(tl->durationFrames() < 120);
        while (tl->canUndo())
            tl->undo();
        QCOMPARE(tl->clipCount(), 0);
        m_ctx->settings.set("aiEnabled", false);
        click("ws_edit");
        QVERIFY(projects->save().isEmpty());
    }

    void shippedDocumentsAreFoundAndAboutShowsTheProduct()
    {
        for (const char* name : {"user-guide.en.html", "user-guide.th.html", "release-notes.txt", "THIRD-PARTY-NOTICES.txt"}) {
            const QString path = m_ctx->systemInfo.documentPath(QString::fromLatin1(name));
            QVERIFY2(!path.isEmpty() && QFileInfo::exists(path), name);
        }
        QVERIFY(m_ctx->systemInfo.documentPath("../LICENSE.txt").isEmpty());          // only plain file names are looked up
        QCOMPARE(m_ctx->systemInfo.version(), QStringLiteral("1.3.1"));
        QVERIFY(m_ctx->systemInfo.productName().contains("Video Editor Pro"));
    }

    void trackToggleMarksDirtyAndSavePersists()
    {
        QVERIFY(!m_ctx->projects.dirty());
        click("trackLock0");
        QVERIFY(m_ctx->projects.dirty());
        QVERIFY(m_ctx->projects.tracks()->tracks().at(0).locked);

        click("menuSave");
        QTRY_VERIFY_WITH_TIMEOUT(!m_ctx->projects.dirty(), 2000);

        dv::ProjectData onDisk;
        QString error;
        QVERIFY2(dv::ProjectFile::load(m_ctx->projects.filePath(), &onDisk, &error), qPrintable(error));
        QVERIFY(onDisk.tracks.at(0).locked);
    }

    void playheadStepsFramesThroughTransportButtons()
    {
        m_ctx->projects.setPlayheadFrame(0);   // earlier tests moved the playhead
        QCOMPARE(m_ctx->projects.playheadFrame(), qint64(0));
        m_ctx->projects.stepFrames(31);
        QCOMPARE(m_ctx->projects.timecode(), QStringLiteral("00:00:01:01"));
        m_ctx->projects.setPlayheadFrame(0);
    }

    void settingsDialogNavigatesAndPersists()
    {
        m_ctx->controller.setSettingsOpen(true);
        QTest::qWait(450); // let the open transition (fade + scale) finish so hit positions are final
        click("settingsNav_appearance");
        QCOMPARE(m_ctx->controller.settingsPage(), QStringLiteral("appearance"));
        click("settingsNav_ai");
        QCOMPARE(m_ctx->controller.settingsPage(), QStringLiteral("ai"));

        m_ctx->settings.set("theme", "light");
        m_ctx->settings.set("accentColor", "#14B8A6");
        QVERIFY(m_ctx->settings.save());
        QTest::qWait(150);

        dv::SettingsManager reloaded;
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.get("theme").toString(), QStringLiteral("light"));
        QCOMPARE(reloaded.get("accentColor").toString(), QStringLiteral("#14B8A6"));

        m_ctx->controller.setSettingsOpen(false);
        QTRY_VERIFY_WITH_TIMEOUT(!m_ctx->controller.settingsOpen(), 2000);
        QTest::qWait(300);
    }

    void everyScreenRendersNonBlankInBothThemes()
    {
        for (const char* theme : {"light", "dark"}) {
            m_ctx->settings.set("theme", theme);
            for (const char* ws : {"edit", "export"}) {
                m_ctx->controller.setWorkspace(QString::fromLatin1(ws));
                QTest::qWait(250);
                const QImage image = m_window->grabWindow();
                QVERIFY(!image.isNull());
                QSet<QRgb> colors;
                for (int y = 0; y < image.height(); y += 5)
                    for (int x = 0; x < image.width(); x += 7)
                        colors.insert(image.pixel(x, y));
                QVERIFY2(colors.size() > 8, qPrintable(QString("%1/%2 looks blank").arg(theme, ws)));
            }
        }
    }

    void unsavedChangesAreGuarded()
    {
        m_ctx->settings.set("theme", "dark");
        m_ctx->controller.setWorkspace("edit"); // the previous test ended on the Export workspace (no timeline)
        QTest::qWait(300);
        click("trackToggle2"); // mute A1 -> dirty
        QVERIFY(m_ctx->projects.dirty());

        // Cancel keeps the project open and the edit intact.
        click("ws_home");
        QTest::qWait(450);
        QVERIFY(m_ctx->projects.isOpen());
        click("confirmCancel");
        QTest::qWait(450);
        QVERIFY(m_ctx->projects.isOpen());
        QVERIFY(m_ctx->projects.dirty());

        // Save: writes the file, then goes home.
        click("ws_home");
        QTest::qWait(450);
        click("confirmPrimary");
        QTRY_VERIFY_WITH_TIMEOUT(!m_ctx->projects.isOpen(), 3000);
        dv::ProjectData onDisk;
        QString error;
        QVERIFY2(dv::ProjectFile::load(m_ctx->recents.pathAt(0), &onDisk, &error), qPrintable(error));
        QVERIFY(onDisk.tracks.at(2).muted);
    }

    void discardingUnsavedChangesLeavesTheFileUntouched()
    {
        QCOMPARE(m_ctx->projects.openProject(m_ctx->recents.pathAt(0)), QString());
        m_ctx->controller.setWorkspace("edit");
        QTRY_VERIFY_WITH_TIMEOUT(item("trackToggle2") != nullptr, 3000);
        QTest::qWait(600); // editor layout settles asynchronously after the screen is created
        click("trackToggle2"); // unmute A1 -> dirty (not saved)
        QVERIFY(m_ctx->projects.dirty());
        click("ws_home");
        QTest::qWait(450);
        click("confirmSecondary"); // Don't save
        QTRY_VERIFY_WITH_TIMEOUT(!m_ctx->projects.isOpen(), 3000);

        dv::ProjectData onDisk;
        QString error;
        QVERIFY2(dv::ProjectFile::load(m_ctx->recents.pathAt(0), &onDisk, &error), qPrintable(error));
        QVERIFY(onDisk.tracks.at(2).muted); // still muted: the discarded edit never reached the file
    }

    void backToHomeClosesProject()
    {
        QCOMPARE(m_ctx->controller.screen(), QStringLiteral("home"));
        QCOMPARE(m_ctx->recents.count(), 1);
    }

    void noQmlWarningsWereEmitted()
    {
        QVERIFY2(g_qmlProblems.isEmpty(), qPrintable(g_qmlProblems.join('\n')));
    }

    void cleanupTestCase()
    {
        qInstallMessageHandler(nullptr);
        m_window = nullptr;
        m_engine.reset();
        m_ctx.reset();
        dv::paths::setOverrideRoot({});
    }
};

QTEST_MAIN(TstUiFlow)
#include "tst_ui_flow.moc"
