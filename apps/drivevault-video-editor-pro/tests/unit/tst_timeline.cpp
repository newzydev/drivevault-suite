#include "timeline/TimelineModel.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace dv;

class TstTimeline : public QObject {
    Q_OBJECT
    std::unique_ptr<TimelineModel> m;

    QList<ClipData> sorted() const
    {
        QList<ClipData> v = m->clips();
        std::sort(v.begin(), v.end(), [](const ClipData& a, const ClipData& b) {
            return std::tie(a.trackId, a.startFrame) < std::tie(b.trackId, b.startFrame);
        });
        return v;
    }
    QList<ClipData> onTrack(const QString& track) const
    {
        QList<ClipData> v;
        for (const ClipData& c : sorted())
            if (c.trackId == track)
                v << c;
        return v;
    }

private slots:
    void init()
    {
        m = std::make_unique<TimelineModel>();
        m->setFrameRate({30, 1});
        m->setTracks({{"V2", "V2", "video", false, false, true}, {"V1", "V1", "video", false, false, true},
                      {"A1", "A1", "audio", false, false, true}, {"A2", "A2", "audio", false, false, true}});
        m->setMediaLookup([](const QString& id) -> std::optional<MediaLite> {
            if (id == "v")   return MediaLite{"clip.mp4", "video", 10'000'000, true, true, true};       // 300 frames
            if (id == "vs")  return MediaLite{"silent.mp4", "video", 4'000'000, true, false, true};     // 120 frames
            if (id == "a")   return MediaLite{"tone.wav", "audio", 5'000'000, false, true, true};       // 150 frames
            if (id == "i")   return MediaLite{"pic.png", "image", 0, true, false, true};                // 150 frames (5 s)
            if (id == "off") return MediaLite{"gone.mp4", "video", 1'000'000, true, true, false};
            return std::nullopt;
        });
    }

    void addVideoCreatesLinkedVideoAndAudioPair()
    {
        QCOMPARE(m->addMedia("v", -1, 0), QString());
        QCOMPARE(m->clipCount(), 2);
        const QList<ClipData> v1 = onTrack("V1");
        const QList<ClipData> a1 = onTrack("A1");
        QCOMPARE(v1.size(), 1);
        QCOMPARE(a1.size(), 1);
        QCOMPARE(v1[0].durationFrames, qint64(300));
        QVERIFY(!v1[0].linkId.isEmpty());
        QCOMPARE(v1[0].linkId, a1[0].linkId);
        QCOMPARE(m->durationFrames(), qint64(300));
        QCOMPARE(m->selectedCount(), 2);   // the new pair is selected
    }
    void mediaKindsGoToTheRightTracks()
    {
        QCOMPARE(m->addMedia("a", -1, 0), QString());
        QCOMPARE(onTrack("A1").size(), 1);
        QCOMPARE(m->addMedia("i", -1, 0), QString());
        QCOMPARE(onTrack("V1").size(), 1);
        QCOMPARE(onTrack("V1")[0].durationFrames, qint64(150));
        QCOMPARE(m->addMedia("vs", -1, 400), QString());
        QCOMPARE(onTrack("A1").size(), 1);     // silent video has no audio clip
        QCOMPARE(m->clipCount(), 3);
    }
    void rejectsOfflineUnknownAndLockedTargets()
    {
        QVERIFY(!m->addMedia("off", -1, 0).isEmpty());
        QVERIFY(!m->addMedia("nope", -1, 0).isEmpty());
        m->setTracks({{"V2", "V2", "video", false, true, true}, {"V1", "V1", "video", false, true, true},
                      {"A1", "A1", "audio", false, false, true}, {"A2", "A2", "audio", false, false, true}});
        QVERIFY(!m->addMedia("vs", -1, 0).isEmpty());   // every video track locked
        QCOMPARE(m->clipCount(), 0);
    }
    void occupiedSpaceIsSkipped()
    {
        QCOMPARE(m->addMedia("v", -1, 0), QString());
        QCOMPARE(m->addMedia("v", -1, 0), QString());
        QCOMPARE(onTrack("V1").size(), 2);
        QCOMPARE(onTrack("V1")[1].startFrame, qint64(300));
        QCOMPARE(onTrack("A1")[1].startFrame, qint64(300));
        // Explicit upper track: no collision there, so the requested start is kept.
        QCOMPARE(m->addMedia("vs", 0, 10), QString());
        QCOMPARE(onTrack("V2")[0].startFrame, qint64(10));
    }

    void splitDividesBothLinkedClipsAndKeepsSourceOffsets()
    {
        m->addMedia("v", -1, 0);
        QCOMPARE(m->splitAt(100), QString());
        QCOMPARE(m->clipCount(), 4);
        const QList<ClipData> v = onTrack("V1");
        const QList<ClipData> a = onTrack("A1");
        QCOMPARE(v[0].durationFrames, qint64(100));
        QCOMPARE(v[1].startFrame, qint64(100));
        QCOMPARE(v[1].durationFrames, qint64(200));
        QCOMPARE(v[1].sourceInFrame, qint64(100));
        QCOMPARE(a[1].sourceInFrame, qint64(100));
        QCOMPARE(v[0].linkId, a[0].linkId);
        QCOMPARE(v[1].linkId, a[1].linkId);
        QVERIFY(v[0].linkId != v[1].linkId);
    }
    void splitAtEdgesOrEmptySpaceDoesNothing()
    {
        m->addMedia("v", -1, 0);
        QVERIFY(!m->splitAt(0).isEmpty());
        QVERIFY(!m->splitAt(300).isEmpty());
        QVERIFY(!m->splitAt(5000).isEmpty());
        QCOMPARE(m->clipCount(), 2);
    }
    void splitUsesSelectionOtherwiseAllClipsUnderThePlayhead()
    {
        m->addMedia("v", -1, 0);
        m->addMedia("vs", 0, 0);   // V2 clip under the same frames
        m->clearSelection();
        QCOMPARE(m->splitAt(60), QString());
        QCOMPARE(m->clipCount(), 6);   // V1+A1 pair and the V2 clip were all split
        m->undo();
        QCOMPARE(m->clipCount(), 3);
        m->select(onTrack("V2")[0].id);
        QCOMPARE(m->splitAt(60), QString());
        QCOMPARE(m->clipCount(), 4);   // only the selected clip was split
    }

    void undoRedoRestoreExactState()
    {
        m->addMedia("v", -1, 0);
        const QList<ClipData> afterAdd = sorted();
        m->splitAt(100);
        const QList<ClipData> afterSplit = sorted();
        m->select(onTrack("V1")[1].id);
        m->deleteSelected();
        QCOMPARE(m->clipCount(), 2);
        QVERIFY(m->canUndo());
        m->undo();
        QCOMPARE(sorted(), afterSplit);
        m->undo();
        QCOMPARE(sorted(), afterAdd);
        m->undo();
        QCOMPARE(m->clipCount(), 0);
        QVERIFY(!m->canUndo());
        QVERIFY(m->canRedo());
        m->redo();
        m->redo();
        QCOMPARE(sorted(), afterSplit);
        QCOMPARE(m->undoText(), QStringLiteral("Split clip"));
    }
    void editedSignalFiresForEditsAndUndo()
    {
        QSignalSpy edited(m.get(), &TimelineModel::edited);
        m->addMedia("v", -1, 0);
        m->undo();
        m->redo();
        QCOMPARE(edited.count(), 3);
    }

    void moveShiftsLinkedPartnersAndChangesOnlyTheDraggedTrack()
    {
        m->addMedia("v", -1, 0);
        const QString vid = onTrack("V1")[0].id;
        QCOMPARE(m->moveClip(vid, 50, 0), QString());   // up to V2
        QCOMPARE(onTrack("V2").size(), 1);
        QCOMPARE(onTrack("V2")[0].startFrame, qint64(50));
        QCOMPARE(onTrack("A1")[0].startFrame, qint64(50));   // partner moved in time, stayed on A1
        QCOMPARE(onTrack("V1").size(), 0);
        m->undo();
        QCOMPARE(onTrack("V1")[0].startFrame, qint64(0));
        QCOMPARE(onTrack("A1")[0].startFrame, qint64(0));
    }
    void moveRefusesOverlapAndLockedTracks()
    {
        m->addMedia("vs", 1, 0);                    // V1: 0..120
        m->addMedia("vs", 1, 120);                  // V1: 120..240
        const QString second = onTrack("V1")[1].id;
        QVERIFY(!m->moveClip(second, 60, 1).isEmpty());   // would overlap the first clip
        QCOMPARE(onTrack("V1")[1].startFrame, qint64(120));
        QVERIFY(!m->moveClip(second, -10, 1).isEmpty());
        m->setTracks({{"V2", "V2", "video", false, false, true}, {"V1", "V1", "video", false, true, true},
                      {"A1", "A1", "audio", false, false, true}, {"A2", "A2", "audio", false, false, true}});
        QVERIFY(!m->moveClip(second, 500, 1).isEmpty());
        QVERIFY(!m->deleteSelected().isEmpty() || m->selectedCount() == 0);
    }
    void clampMoveFindsNearestFreePositionAndSnaps()
    {
        m->addMedia("vs", 1, 0);                     // V1: 0..120
        m->addMedia("vs", 1, 300);                   // V1: 300..420
        const QString second = onTrack("V1")[1].id;
        // Dragging the second clip left into the first one: pushed back to the first clip's end.
        QCOMPARE(m->clampMove(second, 60, 1, false, 0).value("start").toLongLong(), qint64(120));
        // Dragging over the free gap keeps the position.
        QCOMPARE(m->clampMove(second, 200, 1, false, 0).value("start").toLongLong(), qint64(200));
        // Beyond the left edge of the timeline.
        QCOMPARE(m->clampMove(second, -500, 1, false, 0).value("start").toLongLong(), qint64(120));
        // Snapping to the playhead (within 5 frames).
        m->setPlayheadFrame(210);
        const QVariantMap snapped = m->clampMove(second, 207, 1, true, 5);
        QCOMPARE(snapped.value("start").toLongLong(), qint64(210));
        QVERIFY(snapped.value("snapped").toBool());
        // Wrong-kind target track is ignored.
        QCOMPARE(m->clampMove(second, 200, 2, false, 0).value("trackIndex").toInt(), 1);
    }

    void trimIsLimitedByMediaAndNeighbours()
    {
        m->addMedia("vs", 1, 0);                      // media = 120 frames
        const QString id = onTrack("V1")[0].id;
        QCOMPARE(m->trimEnd(id, 60), QString());
        QCOMPARE(onTrack("V1")[0].durationFrames, qint64(60));
        QCOMPARE(m->trimEnd(id, 5000), QString());    // cannot exceed the media length
        QCOMPARE(onTrack("V1")[0].durationFrames, qint64(120));
        QCOMPARE(m->clampTrimEnd(id, 1, false, 0), qint64(1));
        QCOMPARE(m->clampTrimEnd(id, -50, false, 0), qint64(1));      // keeps at least one frame
        QCOMPARE(m->clampTrimStart(id, -20, false, 0), qint64(0));    // sourceIn is 0: nothing to reveal on the left

        QCOMPARE(m->trimStart(id, 30), QString());
        QCOMPARE(onTrack("V1")[0].startFrame, qint64(30));
        QCOMPARE(onTrack("V1")[0].sourceInFrame, qint64(30));
        QCOMPARE(onTrack("V1")[0].durationFrames, qint64(90));
        QCOMPARE(m->trimStart(id, 0), QString());     // reveals the trimmed frames again
        QCOMPARE(onTrack("V1")[0].startFrame, qint64(0));
        QCOMPARE(onTrack("V1")[0].durationFrames, qint64(120));

        // A neighbour on the same track blocks extending.
        m->trimEnd(id, 50);
        m->addMedia("vs", 1, 50);
        QCOMPARE(m->clampTrimEnd(id, 100, false, 0), qint64(50));
    }
    void trimAppliesToLinkedPartners()
    {
        m->addMedia("v", -1, 0);
        const QString vid = onTrack("V1")[0].id;
        QCOMPARE(m->trimEnd(vid, 120), QString());
        QCOMPARE(onTrack("A1")[0].durationFrames, qint64(120));
        QCOMPARE(m->trimStart(vid, 20), QString());
        QCOMPARE(onTrack("A1")[0].startFrame, qint64(20));
        QCOMPARE(onTrack("A1")[0].sourceInFrame, qint64(20));
    }
    void stillImagesCanBeExtendedBeyondTheirDefaultLength()
    {
        m->addMedia("i", 1, 0);
        const QString id = onTrack("V1")[0].id;
        QCOMPARE(m->trimEnd(id, 900), QString());
        QCOMPARE(onTrack("V1")[0].durationFrames, qint64(900));
    }
    void trimIsRefusedOnLockedTracks()
    {
        m->addMedia("vs", 1, 0);
        const QString id = onTrack("V1")[0].id;
        m->setTracks({{"V2", "V2", "video", false, false, true}, {"V1", "V1", "video", false, true, true},
                      {"A1", "A1", "audio", false, false, true}, {"A2", "A2", "audio", false, false, true}});
        QVERIFY(!m->trimEnd(id, 10).isEmpty());
        QVERIFY(!m->trimStart(id, 10).isEmpty());
        QVERIFY(!m->splitAt(10).isEmpty());
        QCOMPARE(onTrack("V1")[0].durationFrames, qint64(120));
    }

    void deleteAndRippleDelete()
    {
        m->addMedia("vs", 1, 0);                      // V1: 0..120
        m->addMedia("vs", 1, 120);                    // V1: 120..240
        m->addMedia("vs", 1, 240);                    // V1: 240..360
        m->select(onTrack("V1")[1].id);
        QCOMPARE(m->deleteSelected(), QString());
        QCOMPARE(onTrack("V1").size(), 2);
        QCOMPARE(onTrack("V1")[1].startFrame, qint64(240));   // plain delete leaves a gap
        m->undo();
        m->select(onTrack("V1")[1].id);
        QCOMPARE(m->rippleDeleteSelected(), QString());
        QCOMPARE(onTrack("V1").size(), 2);
        QCOMPARE(onTrack("V1")[1].startFrame, qint64(120));   // later clip closed the gap
        m->undo();
        QCOMPARE(onTrack("V1")[2].startFrame, qint64(240));
    }
    void selectionTreatsLinkedClipsAsOneAndSupportsToggle()
    {
        m->addMedia("v", -1, 0);
        m->addMedia("vs", 0, 0);
        m->clearSelection();
        m->select(onTrack("V1")[0].id);
        QCOMPARE(m->selectedCount(), 2);                // video + its audio
        m->select(onTrack("V2")[0].id, true);
        QCOMPARE(m->selectedCount(), 3);
        m->select(onTrack("V1")[0].id, true);           // toggles the pair off
        QCOMPARE(m->selectedCount(), 1);
        m->selectAll();
        QCOMPARE(m->selectedCount(), 3);
        m->clearSelection();
        QCOMPARE(m->selectedCount(), 0);
    }
    void deletedClipsLeaveTheSelection()
    {
        m->addMedia("v", -1, 0);
        QCOMPARE(m->selectedCount(), 2);
        m->deleteSelected();
        QCOMPARE(m->selectedCount(), 0);
        m->undo();
        QCOMPARE(m->selectedCount(), 0);   // restored clips are not selected automatically
    }
    void loadReplacesEverythingAndKeepsIdsUnique()
    {
        m->addMedia("v", -1, 0);
        const QList<ClipData> saved = m->clips();
        m->load({});
        QCOMPARE(m->clipCount(), 0);
        QVERIFY(!m->canUndo());
        m->load(saved);
        QCOMPARE(m->clipCount(), 2);
        m->addMedia("v", -1, 0);
        QSet<QString> ids;
        for (const ClipData& c : m->clips())
            ids.insert(c.id);
        QCOMPARE(ids.size(), 4);   // new ids never collide with loaded ones
    }
    void queriesAndInfo()
    {
        m->addMedia("v", -1, 30);
        QVERIFY(m->mediaUsed("v"));
        QVERIFY(!m->mediaUsed("a"));
        QVERIFY(!m->clipAt(1, 40).isEmpty());
        QVERIFY(m->clipAt(1, 10).isEmpty());
        const QVariantMap info = m->clipInfo(onTrack("V1")[0].id);
        QCOMPARE(info.value("name").toString(), QStringLiteral("clip.mp4"));
        QCOMPARE(info.value("startTimecode").toString(), QStringLiteral("00:00:01:00"));
        QCOMPARE(info.value("durationTimecode").toString(), QStringLiteral("00:00:10:00"));
        QVERIFY(info.value("linked").toBool());
        QCOMPARE(m->mediaFrames("a"), qint64(150));
    }
    // ======================= Phase 3: parameters, keyframes, effects ===============================
    void keyframeInterpolation()
    {
        ParamTrack t;
        t.keys = {{0, 0.0, "linear"}, {30, 1.0, "ease"}, {60, 0.0, "hold"}, {90, 5.0, "linear"}};
        QCOMPARE(t.at(-10), 0.0);                      // before the first key
        QCOMPARE(t.at(15), 0.5);                       // linear
        QVERIFY(t.at(40) > 0.5 && t.at(40) < 1.0);     // ease: from key 30 (1.0) down to key 60 (0.0), smoothstep
        QCOMPARE(t.at(60), 0.0);
        QCOMPARE(t.at(75), 0.0);                       // hold keeps the value of the key on the left
        QCOMPARE(t.at(500), 5.0);                      // after the last key
        ParamTrack c{0.7, {}};
        QCOMPARE(c.at(123), 0.7);
        QVERIFY(qFuzzyCompare(interpolate(0, 10, 0.5, "ease") + 1, 5.0 + 1));   // smoothstep is symmetric
    }
    void parametersAreClampedStoredSparsely()
    {
        m->addMedia("vs", 1, 0);
        const QString id = onTrack("V1")[0].id;
        QVERIFY(m->setParam(id, "nonsense", 1, 0).size() > 0);
        QCOMPARE(m->setParam(id, "opacity", 5.0, 0), QString());          // clamped to 1.0 == default -> not stored
        QVERIFY(m->findClip(id)->params.isEmpty());
        QCOMPARE(m->setParam(id, "opacity", 0.25, 0), QString());
        QCOMPARE(m->paramValue(id, "opacity", 10), 0.25);
        QCOMPARE(m->paramValue(id, "brightness", 10), 0.0);               // untouched -> default
        QVERIFY(m->paramsModified(id, "transform"));
        QVERIFY(!m->paramsModified(id, "color"));
        QCOMPARE(m->resetParams(id, "transform"), QString());
        QVERIFY(m->findClip(id)->params.isEmpty());
    }
    void sliderDragIsOneUndoStep()
    {
        m->addMedia("vs", 1, 0);
        const QString id = onTrack("V1")[0].id;
        const int before = m->undoStack()->count();
        for (int i = 1; i <= 20; ++i)
            m->setParam(id, "contrast", i / 40.0, 0);
        QCOMPARE(m->undoStack()->count(), before + 1);
        QCOMPARE(m->paramValue(id, "contrast", 0), 0.5);
        m->undo();
        QCOMPARE(m->paramValue(id, "contrast", 0), 0.0);                  // back to the value before the drag
        m->redo();
        QCOMPARE(m->paramValue(id, "contrast", 0), 0.5);
        m->setParam(id, "saturation", 0.5, 0);                            // another parameter: a separate step
        QCOMPARE(m->undoStack()->count(), before + 2);
    }
    void keyframesAnimateAndToggle()
    {
        m->addMedia("vs", 1, 100);                                        // clip starts at frame 100
        const QString id = onTrack("V1")[0].id;
        QCOMPARE(m->toggleKeyframe(id, "opacity", 100), QString());       // key at clip frame 0 with value 1.0
        QVERIFY(m->paramAnimated(id, "opacity"));
        QVERIFY(m->keyframeAt(id, "opacity", 100));
        QCOMPARE(m->setParam(id, "opacity", 0.0, 160), QString());        // animated parameter: writes a key at clip frame 60
        QCOMPARE(m->keyframes(id, "opacity").size(), 2);
        QCOMPARE(m->paramValue(id, "opacity", 130), 0.5);                 // halfway
        QCOMPARE(m->paramValue(id, "opacity", 100), 1.0);
        QCOMPARE(m->setKeyInterpolation(id, "opacity", 100, "hold"), QString());
        QCOMPARE(m->paramValue(id, "opacity", 130), 1.0);
        QCOMPARE(m->toggleKeyframe(id, "opacity", 160), QString());       // removes the key at 60
        QCOMPARE(m->toggleKeyframe(id, "opacity", 100), QString());       // removes the last one -> constant again
        QVERIFY(!m->paramAnimated(id, "opacity"));
        QVERIFY(m->findClip(id)->params.isEmpty());
        m->undo();
        QVERIFY(m->paramAnimated(id, "opacity") || m->keyframes(id, "opacity").size() >= 1);
    }
    void keyframesStayRelativeWhenTheClipMoves()
    {
        m->addMedia("vs", 1, 0);
        const QString id = onTrack("V1")[0].id;
        m->toggleKeyframe(id, "posX", 0);
        m->setParam(id, "posX", 0.5, 60);
        QCOMPARE(m->moveClip(id, 200, 1), QString());
        QCOMPARE(m->paramValue(id, "posX", 200), 0.0);
        QCOMPARE(m->paramValue(id, "posX", 260), 0.5);
        const auto before = m->findClip(id)->params;
        QCOMPARE(m->splitAt(230), QString());                             // both halves keep their parameters
        QCOMPARE(onTrack("V1").size(), 2);
        QCOMPARE(onTrack("V1")[1].params, before);
    }
    void curvesLutAndFades()
    {
        m->addMedia("v", -1, 0);
        const QString vid = onTrack("V1")[0].id;
        const QString aud = onTrack("A1")[0].id;
        QVERIFY(!m->setCurve(vid, "alpha", {}).isEmpty());
        QCOMPARE(m->setCurve(vid, "master", QVariantList{QVariantList{0.0, 0.0}, QVariantList{0.5, 0.7}, QVariantList{1.0, 1.0}}), QString());
        QCOMPARE(m->curve(vid, "master").size(), 3);
        QCOMPARE(m->setCurve(vid, "master", QVariantList{QVariantList{0.0, 0.0}, QVariantList{1.0, 1.0}}), QString());   // identity = reset
        QVERIFY(m->curve(vid, "master").isEmpty());
        QVERIFY(!m->setLut(vid, "C:/does/not/exist.cube").isEmpty());
        QCOMPARE(m->setFades(aud, 30, 40), QString());
        QCOMPARE(m->findClip(aud)->fadeInFrames, qint64(30));
        QCOMPARE(m->setFades(aud, 250, 250), QString());
        QVERIFY(m->findClip(aud)->fadeInFrames + m->findClip(aud)->fadeOutFrames <= m->findClip(aud)->durationFrames);
    }
    void transitionsAreValidatedAndClamped()
    {
        m->addMedia("vs", 1, 0);
        const QString id = onTrack("V1")[0].id;
        QVERIFY(!m->setTransition(id, "out", "zoomOut", 10).isEmpty());      // not an out transition
        QVERIFY(!m->setTransition(id, "sideways", "fade", 10).isEmpty());
        QCOMPARE(m->setTransition(id, "in", "dissolve", 30), QString());
        QCOMPARE(m->findClip(id)->transIn.frames, qint64(30));
        QCOMPARE(m->setTransition(id, "in", "dissolve", 9999), QString());
        QCOMPARE(m->findClip(id)->transIn.frames, qint64(120));            // clamped to the clip length
        QCOMPARE(m->setTransition(id, "out", "dip", 15), QString());
        QCOMPARE(m->index(0).data(TimelineModel::TransOutRole).toString(), QStringLiteral("dip"));
        QCOMPARE(m->setTransition(id, "in", "", 0), QString());
        QVERIFY(!m->findClip(id)->transIn.active());
    }

    // ======================= Phase 3: titles and subtitles ========================================
    void titlesAreGeneratedClips()
    {
        QCOMPARE(m->addTitle(-1, 10, "lowerThird"), QString());
        QCOMPARE(m->clipCount(), 1);
        const ClipData c = m->clips()[0];
        QVERIFY(c.isTitle());
        QCOMPARE(c.trackId, QStringLiteral("V2"));                          // top-most unlocked video track
        QCOMPARE(c.durationFrames, qint64(150));
        QCOMPARE(c.title.align, QStringLiteral("left"));
        QVERIFY(m->index(0).data(TimelineModel::OfflineRole).toBool() == false);
        QCOMPARE(m->setTitle(c.id, {{"text", QString::fromUtf8("สวัสดี\nโลก")}}), QString());
        QCOMPARE(m->titleInfo(c.id).value("text").toString(), QString::fromUtf8("สวัสดี\nโลก"));
        QCOMPARE(m->index(0).data(TimelineModel::NameRole).toString(), QString::fromUtf8("สวัสดี"));
        QCOMPARE(m->setTitle(c.id, {{"size", 9999}, {"color", "not a colour"}, {"align", "diagonal"}}), QString());
        QCOMPARE(m->titleInfo(c.id).value("size").toInt(), 600);            // clamped
        QVERIFY(m->titleInfo(c.id).value("color").toString().startsWith('#'));   // invalid colour ignored
        QCOMPARE(m->titleInfo(c.id).value("align").toString(), QStringLiteral("left"));
        // Titles stretch freely and survive trimming / splitting.
        QCOMPARE(m->trimEnd(c.id, 900), QString());
        QCOMPARE(m->findClip(c.id)->durationFrames, qint64(890));
        QCOMPARE(m->splitAt(300), QString());
        QCOMPARE(m->clipCount(), 2);
        QVERIFY(m->clips()[1].isTitle());
    }
    void subtitlesNeedASubtitleTrackAndRoundTripThroughSrt()
    {
        QVERIFY(!m->addSubtitle(0, 30, "x").isEmpty());                     // no subtitle track yet
        m->setTracks({{"S1", "S1", "subtitle"}, {"V2", "V2", "video"}, {"V1", "V1", "video"}, {"A1", "A1", "audio"}, {"A2", "A2", "audio"}});
        QCOMPARE(m->addSubtitle(30, 45, "Hello"), QString());
        QCOMPARE(m->addSubtitle(120, 30, QString::fromUtf8("สองบรรทัด\nบรรทัดที่สอง")), QString());
        QCOMPARE(m->subtitleCount(), 2);

        QTemporaryDir dir;
        const QString srt = dir.filePath("out.srt");
        QCOMPARE(m->exportSrt(srt), QString());
        QFile f(srt);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(f.readAll());
        QVERIFY(text.startsWith("1\n00:00:01,000 --> 00:00:02,500\nHello\n"));
        QVERIFY(text.contains("2\n00:00:04,000 --> 00:00:05,000\n"));
        QVERIFY(text.contains(QString::fromUtf8("สองบรรทัด\nบรรทัดที่สอง")));

        m->load({});
        QCOMPARE(m->importSrt(srt), QString());
        QCOMPARE(m->subtitleCount(), 2);
        QCOMPARE(m->clips()[0].startFrame, qint64(30));
        QCOMPARE(m->clips()[0].durationFrames, qint64(45));
        QCOMPARE(m->clips()[1].title.text, QString::fromUtf8("สองบรรทัด\nบรรทัดที่สอง"));
        m->undo();
        QCOMPARE(m->subtitleCount(), 0);                                    // one undo step for the whole import
    }
    void srtImportToleratesRealWorldFiles()
    {
        m->setTracks({{"S1", "S1", "subtitle"}, {"V1", "V1", "video"}, {"A1", "A1", "audio"}});
        QTemporaryDir dir;
        QFile f(dir.filePath("win.srt"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray("\xEF\xBB\xBF" "1\r\n00:00:01,000 --> 00:00:03,000\r\nfirst\r\n\r\n"
                           "2\r\n00:00:02,500 --> 00:00:04,000\r\noverlaps the first\r\n\r\n"
                           "garbage block without a time\r\n\r\n"
                           "3\r\n00:00:10.5 --> 00:00:11.5\r\nstyle <i>tags</i> stay as text\r\n"));
        f.close();
        QCOMPARE(m->importSrt(f.fileName()), QString());
        QCOMPARE(m->subtitleCount(), 3);
        const QList<ClipData> cues = m->clips();
        QVERIFY(cues[1].startFrame >= cues[0].endFrame());                  // the overlap was resolved
        QCOMPARE(cues[2].startFrame, qint64(315));                          // 10.5 s * 30 fps
        QVERIFY(!m->importSrt(dir.filePath("missing.srt")).isEmpty());
        QFile empty(dir.filePath("empty.srt"));
        QVERIFY(empty.open(QIODevice::WriteOnly));
        empty.close();
        QVERIFY(!m->importSrt(empty.fileName()).isEmpty());
    }

    // ======================= Phase 3: markers, range, clipboard, grouping =========================
    void markersRangeAndNavigation()
    {
        QSignalSpy changed(m.get(), &TimelineModel::markersChanged);
        const QString a = m->addMarker(100, "Intro");
        m->addMarker(40);
        QCOMPARE(m->markerData().size(), 2);
        QCOMPARE(m->markerData()[0].frame, qint64(40));                     // kept sorted
        QCOMPARE(m->nextMarker(40), qint64(100));
        QCOMPARE(m->nextMarker(100), qint64(-1));
        QCOMPARE(m->prevMarker(100), qint64(40));
        QCOMPARE(m->prevMarker(40), qint64(-1));
        m->renameMarker(a, QString::fromUtf8("เริ่มต้น"));
        QCOMPARE(m->markerData()[1].name, QString::fromUtf8("เริ่มต้น"));
        m->removeMarker(a);
        QCOMPARE(m->markerData().size(), 1);
        QVERIFY(changed.count() >= 4);

        m->setInPoint(50);
        m->setOutPoint(200);
        QCOMPARE(m->inPoint(), qint64(50));
        QCOMPARE(m->outPoint(), qint64(200));
        m->setInPoint(300);                                                 // in behind out clears the out point
        QCOMPARE(m->outPoint(), qint64(-1));
        m->clearRange();
        QCOMPARE(m->inPoint(), qint64(-1));
    }
    void markersSnapOnLoad()
    {
        m->setMarkers({{"k7", 10, "a", "#fff"}, {"k3", 5, "b", "#fff"}}, 20, 90);
        QCOMPARE(m->markerData()[0].id, QStringLiteral("k3"));
        QCOMPARE(m->inPoint(), qint64(20));
        QCOMPARE(m->outPoint(), qint64(90));
        QVERIFY(m->addMarker(1) != "k7");                                   // new ids never collide with loaded ones
    }
    void copyCutPasteKeepsParametersAndLinks()
    {
        m->addMedia("v", -1, 0);
        const QString vid = onTrack("V1")[0].id;
        m->setParam(vid, "opacity", 0.4, 0);
        QCOMPARE(m->copySelected(), QString());
        QVERIFY(m->canPaste());
        QCOMPARE(m->paste(500), QString());
        QCOMPARE(m->clipCount(), 4);
        const QList<ClipData> v = onTrack("V1");
        QCOMPARE(v[1].startFrame, qint64(500));
        QCOMPARE(v[1].param("opacity", 0), 0.4);
        QCOMPARE(onTrack("A1")[1].startFrame, qint64(500));
        QCOMPARE(v[1].linkId, onTrack("A1")[1].linkId);
        QVERIFY(v[1].linkId != v[0].linkId);                                // a new link group
        QCOMPARE(m->selectedCount(), 2);
        // Pasting onto occupied space lands behind the occupants (0..300 and 500..800 are taken, a 300-frame clip fits after 800).
        QCOMPARE(m->paste(0), QString());
        QCOMPARE(onTrack("V1")[2].startFrame, qint64(800));
        m->undo();
        m->undo();
        QCOMPARE(m->clipCount(), 2);
        // Cut = copy + delete
        m->selectAll();
        QCOMPARE(m->cutSelected(), QString());
        QCOMPARE(m->clipCount(), 0);
        QCOMPARE(m->paste(60), QString());
        QCOMPARE(onTrack("V1")[0].startFrame, qint64(60));
    }
    void groupAndUngroupChangeWhatEditsTogether()
    {
        m->addMedia("vs", 1, 0);
        m->addMedia("vs", 0, 0);                                            // V2
        QVERIFY(!m->groupSelected().isEmpty() || m->selectedCount() < 2);
        m->selectAll();
        QCOMPARE(m->groupSelected(), QString());
        QCOMPARE(onTrack("V1")[0].linkId, onTrack("V2")[0].linkId);
        m->clearSelection();
        m->select(onTrack("V1")[0].id);
        QCOMPARE(m->selectedCount(), 2);                                    // selecting one selects the group
        QCOMPARE(m->moveClip(onTrack("V1")[0].id, 30, 1), QString());
        QCOMPARE(onTrack("V2")[0].startFrame, qint64(30));                  // the other member moved with it
        QCOMPARE(m->ungroupSelected(), QString());
        QVERIFY(onTrack("V1")[0].linkId.isEmpty());
        m->clearSelection();
        m->select(onTrack("V1")[0].id);
        QCOMPARE(m->selectedCount(), 1);
    }
    void multiSelectionMovesTogether()
    {
        m->addMedia("vs", 1, 0);                                            // V1 0..120
        m->addMedia("vs", 1, 200);                                          // V1 200..320
        m->selectAll();
        const QString first = onTrack("V1")[0].id;
        const QVariantMap r = m->clampMove(first, 40, 1, false, 0);
        QCOMPARE(r.value("start").toLongLong(), qint64(40));
        QCOMPARE(m->moveClip(first, 40, 1), QString());
        QCOMPARE(onTrack("V1")[0].startFrame, qint64(40));
        QCOMPARE(onTrack("V1")[1].startFrame, qint64(240));                 // moved by the same 40 frames
        // The group may not leave the timeline.
        QCOMPARE(m->clampMove(first, -500, 1, false, 0).value("start").toLongLong(), qint64(0));
        // A multi-selection stays on its tracks.
        QCOMPARE(m->clampMove(first, 40, 0, false, 0).value("trackIndex").toInt(), 1);
    }

    // ======================= Phase 3: ripple / roll / slip / slide ================================
    void rippleTrimShiftsEverythingBehind()
    {
        m->addMedia("vs", 1, 0);                                            // 0..120
        m->addMedia("vs", 1, 120);                                          // 120..240
        m->addMedia("vs", 1, 240);                                          // 240..360
        const QString a = onTrack("V1")[0].id;
        QCOMPARE(m->rippleTrimEnd(a, 90), QString());                       // shorten by 30
        QCOMPARE(onTrack("V1")[0].durationFrames, qint64(90));
        QCOMPARE(onTrack("V1")[1].startFrame, qint64(90));                  // followers closed the gap
        QCOMPARE(onTrack("V1")[2].startFrame, qint64(210));
        m->undo();
        QCOMPARE(onTrack("V1")[1].startFrame, qint64(120));
        // Head trim: the clip keeps its start, loses source frames, followers move left.
        QCOMPARE(m->rippleTrimStart(a, 20), QString());
        QCOMPARE(onTrack("V1")[0].startFrame, qint64(0));
        QCOMPARE(onTrack("V1")[0].sourceInFrame, qint64(20));
        QCOMPARE(onTrack("V1")[0].durationFrames, qint64(100));
        QCOMPARE(onTrack("V1")[1].startFrame, qint64(100));
        // Extending beyond the media is refused (clamped to the available frames).
        QCOMPARE(m->rippleTrimEnd(a, 5000), QString());
        QCOMPARE(onTrack("V1")[0].sourceInFrame + onTrack("V1")[0].durationFrames, qint64(120));
    }
    void rollMovesTheCutOnly()
    {
        m->addMedia("v", 1, 0);                                             // 0..300 (media 300 frames)
        m->trimEnd(onTrack("V1")[0].id, 100);                               // 0..100, audio linked
        m->clearSelection();
        m->addMedia("v", 1, 100);                                           // 100..400 behind it
        m->trimStart(onTrack("V1")[1].id, 150);                             // 150..400? start moves: this leaves a gap
        m->undo();
        // Rebuild a clean adjacent pair on V1 only.
        m->load({});
        m->addMedia("vs", 1, 0);                                            // 0..120
        m->trimEnd(onTrack("V1")[0].id, 60);                                // 0..60
        m->addMedia("vs", 1, 60);                                           // 60..180
        m->trimStart(onTrack("V1")[1].id, 90);                              // 90..180 (sourceIn 30) -> gap 60..90
        m->undo();                                                          // adjacent again: 60..180, sourceIn 0
        const QString a = onTrack("V1")[0].id;
        const qint64 total = onTrack("V1")[1].endFrame();
        QCOMPARE(m->rollEdit(a, 80), QString());
        QCOMPARE(onTrack("V1")[0].durationFrames, qint64(80));
        QCOMPARE(onTrack("V1")[1].startFrame, qint64(80));
        QCOMPARE(onTrack("V1")[1].durationFrames, qint64(100));
        QCOMPARE(onTrack("V1")[1].sourceInFrame, qint64(20));
        QCOMPARE(onTrack("V1")[1].endFrame(), total);                       // the outer edges did not move
        QCOMPARE(m->rollEdit(a, 100000), QString());                        // clamped, never overlaps
        QVERIFY(onTrack("V1")[1].durationFrames >= 1);
        QVERIFY(!m->rollEdit(onTrack("V1")[1].id, 10).isEmpty());          // nothing behind the last clip
    }
    void slipChangesOnlyTheSourceWindow()
    {
        m->addMedia("vs", 1, 0);                                            // media 120 frames
        const QString id = onTrack("V1")[0].id;
        m->trimEnd(id, 60);
        QCOMPARE(m->slip(id, 40), QString());
        QCOMPARE(onTrack("V1")[0].startFrame, qint64(0));
        QCOMPARE(onTrack("V1")[0].durationFrames, qint64(60));
        QCOMPARE(onTrack("V1")[0].sourceInFrame, qint64(40));
        QCOMPARE(m->slip(id, 1000), QString());                             // clamped to the end of the media
        QCOMPARE(onTrack("V1")[0].sourceInFrame, qint64(60));
        QCOMPARE(m->slip(id, -1000), QString());                            // clamped to the start
        QCOMPARE(onTrack("V1")[0].sourceInFrame, qint64(0));
        m->addTitle(-1, 0, "title");
        QVERIFY(!m->slip(onTrack("V2")[0].id, 5).isEmpty());                // titles have no source
    }
    void slideMovesTheClipBetweenItsNeighbours()
    {
        m->addMedia("vs", 1, 0);                                            // A 0..120
        m->trimEnd(onTrack("V1")[0].id, 40);                                // A 0..40
        m->addMedia("vs", 1, 40);                                           // B 40..160
        m->trimEnd(onTrack("V1")[1].id, 100);                               // B 40..100 (60 frames)
        m->addMedia("vs", 1, 100);                                          // C 100..220
        const QString b = onTrack("V1")[1].id;
        QCOMPARE(m->slide(b, 20), QString());
        QCOMPARE(onTrack("V1")[0].durationFrames, qint64(60));              // A grew by 20
        QCOMPARE(onTrack("V1")[1].startFrame, qint64(60));                  // B moved by 20, same length
        QCOMPARE(onTrack("V1")[1].durationFrames, qint64(60));
        QCOMPARE(onTrack("V1")[2].startFrame, qint64(120));                 // C's cut moved with it ...
        QCOMPARE(onTrack("V1")[2].sourceInFrame, qint64(20));               // ... and shows later source frames
        QCOMPARE(onTrack("V1")[2].endFrame(), qint64(220));                 // outer edge unchanged
        QVERIFY(!m->slide(onTrack("V1")[0].id, 5).isEmpty());               // first clip has no clip before it
        m->undo();
        QCOMPARE(onTrack("V1")[2].startFrame, qint64(100));
    }
    void toolsRefuseLockedTracks()
    {
        m->addMedia("vs", 1, 0);
        m->addMedia("vs", 1, 120);
        const QString a = onTrack("V1")[0].id;
        m->setTracks({{"V2", "V2", "video"}, {"V1", "V1", "video", false, true, true}, {"A1", "A1", "audio"}, {"A2", "A2", "audio"}});
        QVERIFY(!m->rippleTrimEnd(a, 50).isEmpty());
        QVERIFY(!m->rollEdit(a, 100).isEmpty());
        QVERIFY(!m->slip(a, 5).isEmpty());
        QVERIFY(!m->slide(onTrack("V1")[1].id, 5).isEmpty());
    }

    void modelRolesReportTrackAndOfflineState()
    {
        m->addMedia("vs", 1, 0);
        const QModelIndex idx = m->index(0);
        QCOMPARE(idx.data(TimelineModel::TrackIndexRole).toInt(), 1);
        QCOMPARE(idx.data(TimelineModel::KindRole).toString(), QStringLiteral("video"));
        QCOMPARE(idx.data(TimelineModel::NameRole).toString(), QStringLiteral("silent.mp4"));
        QCOMPARE(idx.data(TimelineModel::OfflineRole).toBool(), false);
        m->setMediaLookup([](const QString&) { return std::optional<MediaLite>(); });
        m->refreshMedia();
        QCOMPARE(m->index(0).data(TimelineModel::OfflineRole).toBool(), true);
    }
};

QTEST_MAIN(TstTimeline)
#include "tst_timeline.moc"
