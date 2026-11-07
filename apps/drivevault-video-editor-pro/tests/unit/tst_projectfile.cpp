#include "project/ProjectFile.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest>

using namespace dv;

class TstProjectFile : public QObject {
    Q_OBJECT
private slots:
    void jsonRoundTrip()
    {
        ProjectData d = ProjectFile::makeDefault(QStringLiteral("ทดสอบ Project"), 3840, 2160, {30000, 1001}, 48000);
        d.tracks[0].muted = true;
        d.tracks[1].locked = true;
        d.tracks[2].visible = false;
        d.tracks[0].lane = 0;          // lane heights (collapsed / tall) are saved
        d.tracks[1].lane = 2;

        ProjectData out;
        QString error;
        QVERIFY2(ProjectFile::fromJson(ProjectFile::toJson(d), &out, &error), qPrintable(error));
        QCOMPARE(out.name, d.name);
        QCOMPARE(out.width, 3840);
        QCOMPARE(out.height, 2160);
        QCOMPARE(out.frameRate, (timecode::FrameRate{30000, 1001}));
        QCOMPARE(out.sampleRate, 48000);
        QCOMPARE(out.tracks, d.tracks);
        QCOMPARE(out.tracks.at(0).lane, 0);
        QCOMPARE(out.tracks.at(1).lane, 2);

        // a project file from before the lane heights existed opens with normal lanes
        QJsonObject root = ProjectFile::toJson(d);
        QJsonObject timeline = root.value("timeline").toObject();
        QJsonArray tracks = timeline.value("tracks").toArray();
        QVERIFY(!tracks.isEmpty());
        for (int i = 0; i < tracks.size(); ++i) {
            QJsonObject o = tracks.at(i).toObject();
            QVERIFY(o.contains("lane"));
            o.remove("lane");
            tracks[i] = o;
        }
        timeline["tracks"] = tracks;
        root["timeline"] = timeline;
        ProjectData old;
        QVERIFY2(ProjectFile::fromJson(root, &old, &error), qPrintable(error));
        for (const TrackInfo& tr : old.tracks)
            QCOMPARE(tr.lane, 1);
    }
    void saveLoadAtomicWithThaiPath()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("โฟลเดอร์ ไทย/งานของฉัน.dvproj"));
        const ProjectData d = ProjectFile::makeDefault(QStringLiteral("งานของฉัน"), 1920, 1080, {25, 1}, 44100);
        QString error;
        QVERIFY2(ProjectFile::save(path, d, &error), qPrintable(error));
        ProjectData loaded;
        QVERIFY2(ProjectFile::load(path, &loaded, &error), qPrintable(error));
        QCOMPARE(loaded.name, d.name);
        QCOMPARE(loaded.frameRate.num, qint64(25));

        // Saving again keeps a .bak of the previous version.
        QVERIFY2(ProjectFile::save(path, d, &error), qPrintable(error));
        QVERIFY(QFile::exists(path + ".bak"));
    }
    void rejectsWrongFormat()
    {
        ProjectData out;
        QString error;
        QVERIFY(!ProjectFile::fromJson(QJsonObject{{"format", "something-else"}}, &out, &error));
        QVERIFY(!error.isEmpty());
    }
    void rejectsNewerSchema()
    {
        QJsonObject json = ProjectFile::toJson(ProjectFile::makeDefault("x", 1920, 1080, {30, 1}, 48000));
        json["schemaVersion"] = 999;
        ProjectData out;
        QString error;
        QVERIFY(!ProjectFile::fromJson(json, &out, &error));
        QVERIFY(error.contains("999"));
    }
    void rejectsDamagedSettings()
    {
        QJsonObject json = ProjectFile::toJson(ProjectFile::makeDefault("x", 1920, 1080, {30, 1}, 48000));
        QJsonObject s = json["settings"].toObject();
        s["width"] = -4;
        json["settings"] = s;
        ProjectData out;
        QString error;
        QVERIFY(!ProjectFile::fromJson(json, &out, &error));
    }
    void rejectsInvalidTrack()
    {
        QJsonObject json = ProjectFile::toJson(ProjectFile::makeDefault("x", 1920, 1080, {30, 1}, 48000));
        QJsonObject timeline = json["timeline"].toObject();
        timeline["tracks"] = QJsonArray{QJsonObject{{"id", "T"}, {"kind", "hologram"}}};
        json["timeline"] = timeline;
        ProjectData out;
        QString error;
        QVERIFY(!ProjectFile::fromJson(json, &out, &error));
    }
    void loadReportsMissingAndGarbageFiles()
    {
        QTemporaryDir dir;
        ProjectData out;
        QString error;
        QVERIFY(!ProjectFile::load(dir.filePath("missing.dvproj"), &out, &error));
        QVERIFY(!error.isEmpty());

        QFile f(dir.filePath("garbage.dvproj"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not json at all");
        f.close();
        QVERIFY(!ProjectFile::load(f.fileName(), &out, &error));
    }
    void validation()
    {
        QVERIFY(ProjectFile::validate("ok", 1920, 1080, {30, 1}, 48000).isEmpty());
        QVERIFY(!ProjectFile::validate("", 1920, 1080, {30, 1}, 48000).isEmpty());
        QVERIFY(!ProjectFile::validate("ok", 10, 1080, {30, 1}, 48000).isEmpty());
        QVERIFY(!ProjectFile::validate("ok", 1920, 20000, {30, 1}, 48000).isEmpty());
        QVERIFY(!ProjectFile::validate("ok", 1920, 1080, {0, 0}, 48000).isEmpty());
        QVERIFY(!ProjectFile::validate("ok", 1920, 1080, {30, 1}, 5).isEmpty());
    }
    void mediaAndClipsRoundTripInSchemaV2()
    {
        ProjectData d = ProjectFile::makeDefault("v2", 1920, 1080, {30, 1}, 48000);
        MediaInfo info;
        info.kind = "video";
        info.durationUs = 12345678;
        info.hasVideo = info.hasAudio = true;
        info.width = 1280;
        info.height = 720;
        info.fpsNum = 30000;
        info.fpsDen = 1001;
        info.variableFrameRate = true;
        info.audioRate = 44100;
        info.audioChannels = 2;
        info.videoCodec = "h264";
        info.audioCodec = "aac";
        info.fileSize = 9876543210LL;
        info.modifiedMs = 1700000000123LL;
        d.media.append({"m1", "D:/clips/ไทย.mp4", "../clips/ไทย.mp4", "ไทย.mp4", info});
        d.clips.append({"c1", "m1", "V1", 10, 90, 5, "l1"});
        d.clips.append({"c2", "m1", "A1", 10, 90, 5, "l1"});

        const QJsonObject json = ProjectFile::toJson(d);
        QCOMPARE(json["schemaVersion"].toInt(), 3);
        ProjectData out;
        QString error;
        QVERIFY2(ProjectFile::fromJson(json, &out, &error), qPrintable(error));
        QCOMPARE(out.media.size(), 1);
        QCOMPARE(out.media[0].info.durationUs, int64_t(12345678));
        QCOMPARE(out.media[0].info.fileSize, int64_t(9876543210LL));
        QCOMPARE(out.media[0].info.fpsNum, int64_t(30000));
        QVERIFY(out.media[0].info.variableFrameRate);
        QCOMPARE(out.media[0].relativePath, QStringLiteral("../clips/ไทย.mp4"));
        QCOMPARE(out.clips, d.clips);
    }
    void version1FilesMigrateToAnEmptyTimeline()
    {
        QJsonObject json = ProjectFile::toJson(ProjectFile::makeDefault("old", 1280, 720, {25, 1}, 44100));
        json["schemaVersion"] = 1;
        json.remove("media");
        QJsonObject timeline = json["timeline"].toObject();
        timeline.remove("clips");
        json["timeline"] = timeline;
        ProjectData out;
        QString error;
        QVERIFY2(ProjectFile::fromJson(json, &out, &error), qPrintable(error));
        QVERIFY(out.media.isEmpty());
        QVERIFY(out.clips.isEmpty());
        QCOMPARE(out.tracks.size(), 4);
    }
    void rejectsClipsThatPointNowhere()
    {
        ProjectData d = ProjectFile::makeDefault("bad", 1920, 1080, {30, 1}, 48000);
        d.clips.append({"c1", "missing-media", "V1", 0, 30, 0, ""});
        ProjectData out;
        QString error;
        QVERIFY(!ProjectFile::fromJson(ProjectFile::toJson(d), &out, &error));
        QVERIFY(error.contains("clip"));

        MediaInfo info;
        info.kind = "video";
        info.hasVideo = true;
        info.width = info.height = 64;
        d.media.append({"m", "a.mp4", "", "a.mp4", info});
        d.clips = {{"c1", "m", "NOPE", 0, 30, 0, ""}};
        QVERIFY(!ProjectFile::fromJson(ProjectFile::toJson(d), &out, &error));       // unknown track
        d.clips = {{"c1", "m", "V1", 0, 0, 0, ""}};
        QVERIFY(!ProjectFile::fromJson(ProjectFile::toJson(d), &out, &error));       // zero length
        d.clips = {{"c1", "m", "V1", 0, 30, 0, ""}, {"c1", "m", "V1", 40, 30, 0, ""}};
        QVERIFY(!ProjectFile::fromJson(ProjectFile::toJson(d), &out, &error));       // duplicate id
    }
    void extensionCheck()
    {
        QVERIFY(ProjectFile::hasProjectExtension("a/b/c.dvproj"));
        QVERIFY(ProjectFile::hasProjectExtension("C.DVPROJ"));
        QVERIFY(!ProjectFile::hasProjectExtension("c.json"));
    }
};

QTEST_APPLESS_MAIN(TstProjectFile)
#include "tst_projectfile.moc"
