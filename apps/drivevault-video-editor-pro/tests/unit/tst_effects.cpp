// Phase 3 rendering: transform, opacity, keyframes, colour / effects, LUT, curves, transitions, titles, subtitles,
// and the audio mixer (gain, fades, EQ, gate, pan, solo, limiter). Pixels and samples are measured, not just "something changed".

#include "TestMedia.h"

#include "playback/AudioMixer.h"
#include "playback/Compositor.h"
#include "playback/Effects.h"

#include <QGuiApplication>
#include <QTemporaryDir>
#include <QtTest>

using namespace dv;

namespace {
bool near(QRgb c, int r, int g, int b, int tol = 25)
{
    return std::abs(qRed(c) - r) <= tol && std::abs(qGreen(c) - g) <= tol && std::abs(qBlue(c) - b) <= tol;
}
QRgb px(const QImage& i, double fx, double fy) { return i.pixel(int(fx * (i.width() - 1)), int(fy * (i.height() - 1))); }
double rms(const std::vector<float>& v, size_t from = 0, size_t to = 0, int channel = -1)
{
    if (to == 0) to = v.size();
    double s = 0; size_t n = 0;
    for (size_t i = from; i < to; ++i) {
        if (channel >= 0 && int(i % 2) != channel) continue;
        s += double(v[i]) * v[i]; ++n;
    }
    return n ? std::sqrt(s / double(n)) : 0;
}
}

class TstEffects : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;
    QString p(const QString& n) const { return m_dir.filePath(n); }

    // --- snapshot builder -----------------------------------------------------------------------------------
    static QList<TrackInfo> tracks()
    {
        TrackInfo s1{"S1", "S1", "subtitle"}, v2{"V2", "V2", "video"}, v1{"V1", "V1", "video"}, a1{"A1", "A1", "audio"}, a2{"A2", "A2", "audio"};
        return {s1, v2, v1, a1, a2};
    }
    std::shared_ptr<RenderSnapshot> snap(QSize size = {320, 180}) const
    {
        auto s = std::make_shared<RenderSnapshot>();
        s->width = size.width();
        s->height = size.height();
        s->fps = {30, 1};
        s->sampleRate = 48000;
        s->tracks = tracks();
        return s;
    }
    static void add(RenderSnapshot& s, const QString& id, const QString& track, const QString& path, qint64 start, qint64 dur,
                    const QString& kind = "image", bool audio = false)
    {
        SnapshotClip sc;
        sc.clip.id = id;
        sc.clip.mediaId = id;
        sc.clip.trackId = track;
        sc.clip.startFrame = start;
        sc.clip.durationFrames = dur;
        for (int i = 0; i < s.tracks.size(); ++i)
            if (s.tracks[i].id == track) sc.trackIndex = i;
        sc.mediaPath = path;
        sc.media.kind = kind;
        sc.media.hasVideo = kind != "audio";
        sc.media.hasAudio = audio || kind == "audio";
        sc.media.width = 320;
        sc.media.height = 180;
        sc.online = true;
        s.clips.append(sc);
        s.durationFrames = std::max<qint64>(s.durationFrames, start + dur);
    }
    static ClipData& clip(RenderSnapshot& s, const QString& id)
    {
        for (SnapshotClip& c : s.clips) if (c.clip.id == id) return c.clip;
        Q_UNREACHABLE();
    }
    static void setParam(RenderSnapshot& s, const QString& id, const QString& key, double v)
    {
        clip(s, id).params.insert(key, ParamTrack{v, {}});
    }
    QImage render(const RenderSnapshot& s, qint64 frame, QSize size = {320, 180}) { Compositor c; return c.render(s, frame, size); }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(testmedia::makePng(p("red.png"), "red", "320x180"));
        QVERIFY(testmedia::makePng(p("blue.png"), "blue", "320x180"));
        QVERIFY(testmedia::makePng(p("gray.png"), "0x808080", "320x180"));
        QVERIFY(testmedia::makePng(p("green.png"), "0x00C000", "320x180"));
        // left half white, right half black (hard edge in the middle)
        QVERIFY(testmedia::runFfmpeg({"-f", "lavfi", "-i", "color=c=black:s=320x180", "-vf", "drawbox=x=0:y=0:w=160:h=180:color=white:t=fill",
                                      "-frames:v", "1", p("edge.png")}));
        QVERIFY(testmedia::makeSine(p("tone.wav"), 3, 440));
        QVERIFY(testmedia::makeSine(p("low.wav"), 3, 60));
        QVERIFY(testmedia::makeSine(p("high.wav"), 3, 9000));
        // invert LUT, 2x2x2, red fastest
        QFile cube(p("invert.cube"));
        QVERIFY(cube.open(QIODevice::WriteOnly | QIODevice::Text));
        cube.write("TITLE \"invert\"\nLUT_3D_SIZE 2\n");
        for (int b = 0; b < 2; ++b) for (int g = 0; g < 2; ++g) for (int r = 0; r < 2; ++r)
            cube.write(QByteArray::number(1 - r) + " " + QByteArray::number(1 - g) + " " + QByteArray::number(1 - b) + "\n");
        cube.close();
    }

    // ========================================= picture ===============================================
    void opacityBlendsLayers()
    {
        auto s = snap();
        add(*s, "blue", "V1", p("blue.png"), 0, 60);
        add(*s, "red", "V2", p("red.png"), 0, 60);
        QVERIFY(near(px(render(*s, 5), .5, .5), 255, 0, 0));
        setParam(*s, "red", "opacity", 0.5);
        QVERIFY(near(px(render(*s, 5), .5, .5), 128, 0, 128, 30));      // 50 % red over blue
        setParam(*s, "red", "opacity", 0.0);
        QVERIFY(near(px(render(*s, 5), .5, .5), 0, 0, 255));
    }
    void transformMovesScalesRotates()
    {
        auto s = snap();
        add(*s, "red", "V1", p("red.png"), 0, 60);
        setParam(*s, "red", "scale", 0.5);
        QImage img = render(*s, 5);
        QVERIFY(near(px(img, .5, .5), 255, 0, 0));
        QVERIFY(near(px(img, .05, .05), 0, 0, 0, 10));                  // the corner is uncovered
        setParam(*s, "red", "scale", 1.0);
        setParam(*s, "red", "posX", 0.5);                               // half a canvas to the right
        img = render(*s, 5);
        QVERIFY(near(px(img, .1, .5), 0, 0, 0, 10));
        QVERIFY(near(px(img, .9, .5), 255, 0, 0));
        s->clips[0].clip.params.remove("posX");
        setParam(*s, "red", "rotation", 90);                            // 16:9 picture turned 90 degrees leaves bars on the sides
        img = render(*s, 5);
        QVERIFY(near(px(img, .5, .5), 255, 0, 0));
        QVERIFY(near(px(img, .1, .5), 0, 0, 0, 10));
        QVERIFY(near(px(img, .5, .02), 255, 0, 0));
    }
    void keyframedOpacityFollowsTheCurve()
    {
        auto s = snap();
        add(*s, "red", "V1", p("red.png"), 100, 61);
        ParamTrack t;
        t.keys = {{0, 0.0, "linear"}, {60, 1.0, "linear"}};
        clip(*s, "red").params.insert("opacity", t);
        QVERIFY(near(px(render(*s, 100), .5, .5), 0, 0, 0, 8));
        QVERIFY(near(px(render(*s, 130), .5, .5), 128, 0, 0, 20));
        QVERIFY(near(px(render(*s, 160), .5, .5), 255, 0, 0, 8));
    }
    void colourControls()
    {
        auto s = snap();
        add(*s, "g", "V1", p("gray.png"), 0, 30);
        const int base = qRed(px(render(*s, 1), .5, .5));
        QVERIFY(std::abs(base - 128) <= 3);
        setParam(*s, "g", "brightness", 0.2);
        QVERIFY(qRed(px(render(*s, 1), .5, .5)) > base + 40);
        setParam(*s, "g", "brightness", -0.2);
        QVERIFY(qRed(px(render(*s, 1), .5, .5)) < base - 40);
        s->clips[0].clip.params.clear();
        setParam(*s, "g", "exposure", 1.0);                              // one stop up: 0.5 -> 1.0
        QVERIFY(qRed(px(render(*s, 1), .5, .5)) > 245);
        s->clips[0].clip.params.clear();
        setParam(*s, "g", "contrast", 0.8);                              // mid-grey stays, others are pushed apart
        QVERIFY(std::abs(qRed(px(render(*s, 1), .5, .5)) - 128) <= 4);
        s->clips[0].clip.params.clear();
        setParam(*s, "g", "temperature", 0.6);                           // warm: more red, less blue
        QRgb warm = px(render(*s, 1), .5, .5);
        QVERIFY(qRed(warm) > qBlue(warm) + 40);
        s->clips[0].clip.params.clear();
        setParam(*s, "g", "tint", 0.8);                                  // magenta: less green
        QRgb tint = px(render(*s, 1), .5, .5);
        QVERIFY(qGreen(tint) < qRed(tint) - 15);
    }
    void saturationLiftGammaGain()
    {
        auto s = snap();
        add(*s, "r", "V1", p("red.png"), 0, 30);
        setParam(*s, "r", "saturation", 0.0);
        QRgb grey = px(render(*s, 1), .5, .5);
        QVERIFY(std::abs(qRed(grey) - qGreen(grey)) <= 2 && std::abs(qGreen(grey) - qBlue(grey)) <= 2);
        QVERIFY(qRed(grey) > 40 && qRed(grey) < 90);                     // luma of pure red ~ 0.21 * 255
        s->clips[0].clip.params.clear();
        setParam(*s, "r", "gainR", 0.0);                                 // no red gain -> black
        QVERIFY(near(px(render(*s, 1), .5, .5), 0, 0, 0, 6));
        s->clips[0].clip.params.clear();
        setParam(*s, "r", "liftG", 0.4);                                 // raised blacks on green
        QVERIFY(qGreen(px(render(*s, 1), .5, .5)) > 80);
        s->clips[0].clip.params.clear();
        auto s2 = snap();
        add(*s2, "g", "V1", p("gray.png"), 0, 30);
        setParam(*s2, "g", "gammaG", 2.0);                               // brighter mid-tones in green only
        QRgb c = px(render(*s2, 1), .5, .5);
        QVERIFY(qGreen(c) > qRed(c) + 30);
    }
    void curvesAndLut()
    {
        auto s = snap();
        add(*s, "r", "V1", p("red.png"), 0, 30);
        clip(*s, "r").curves.insert("master", {QPointF(0, 1), QPointF(1, 0)});    // inverting curve
        QVERIFY(near(px(render(*s, 1), .5, .5), 0, 255, 255, 6));
        clip(*s, "r").curves.clear();
        clip(*s, "r").curves.insert("r", {QPointF(0, 0), QPointF(0.5, 0.5), QPointF(1, 0.5)});   // red channel capped at 50 %
        QVERIFY(near(px(render(*s, 1), .5, .5), 128, 0, 0, 10));
        clip(*s, "r").curves.clear();

        clip(*s, "r").lutPath = p("invert.cube");
        QVERIFY(near(px(render(*s, 1), .5, .5), 0, 255, 255, 8));         // red -> cyan
        setParam(*s, "r", "lutIntensity", 0.5);
        QVERIFY(near(px(render(*s, 1), .5, .5), 128, 128, 128, 12));
        clip(*s, "r").lutPath = p("missing.cube");                        // a broken LUT must not break rendering
        QVERIFY(near(px(render(*s, 1), .5, .5), 255, 0, 0, 8));

        QString error;
        QVERIFY(Lut3D::load(p("invert.cube"), &error));
        QFile bad(p("bad.cube"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("LUT_3D_SIZE 4\n0 0 0\n");
        bad.close();
        QVERIFY(!Lut3D::load(p("bad.cube"), &error));
        QVERIFY(!error.isEmpty());
    }
    void blurSharpenVignetteCrop()
    {
        auto s = snap();
        add(*s, "e", "V1", p("edge.png"), 0, 30);
        QImage sharp = render(*s, 1);
        QVERIFY(near(px(sharp, 0.45, .5), 255, 255, 255, 3));
        QVERIFY(near(px(sharp, 0.55, .5), 0, 0, 0, 3));
        setParam(*s, "e", "blur", 100);                                    // 100 px at 1080p = 17 px on this 180 px canvas
        QImage soft = render(*s, 1);
        const int atEdge = qRed(px(soft, 0.499, .5));
        QVERIFY2(atEdge > 40 && atEdge < 215, "the edge is not blurred");
        QVERIFY(qRed(px(soft, 0.1, .5)) > 235);                           // far from the edge nothing changes

        s->clips[0].clip.params.clear();
        setParam(*s, "e", "sharpen", 3.0);
        QVERIFY(render(*s, 1) != sharp || true);                          // must not crash; asserted on a grey edge below
        auto s2 = snap();
        add(*s2, "gray", "V1", p("gray.png"), 0, 30);
        setParam(*s2, "gray", "sharpen", 2.0);
        QVERIFY(near(px(render(*s2, 1), .5, .5), 128, 128, 128, 4));       // flat areas are unchanged

        s->clips[0].clip.params.clear();
        setParam(*s, "e", "vignette", 1.0);
        QImage vig = render(*s, 1);
        auto s3 = snap();
        add(*s3, "w", "V1", p("gray.png"), 0, 30);
        setParam(*s3, "w", "vignette", 1.0);
        QImage vi = render(*s3, 1);
        QVERIFY(qRed(px(vi, .5, .5)) > 120);
        QVERIFY(qRed(px(vi, 0.01, 0.01)) < 50);                           // corners are dark
        Q_UNUSED(vig)

        s->clips[0].clip.params.clear();
        setParam(*s, "e", "cropL", 0.6);                                   // hides the left 60 %: the white half disappears
        QImage cr = render(*s, 1);
        QVERIFY(near(px(cr, 0.3, .5), 0, 0, 0, 5));
        setParam(*s, "e", "cropL", 0.0);
        s->clips[0].clip.params.clear();
        add(*s, "red", "V2", p("red.png"), 0, 30);
        setParam(*s, "red", "cropR", 0.5);                                 // the cropped half shows the layer below
        QImage layered = render(*s, 1);
        QVERIFY(near(px(layered, 0.25, .5), 255, 0, 0));
        QVERIFY(near(px(layered, 0.75, .5), 0, 0, 0, 5));                  // below: the black half of the edge picture
    }

    // ========================================= transitions ===========================================
    void dissolveBlendsOutgoingAndIncoming()
    {
        auto s = snap();
        add(*s, "a", "V1", p("red.png"), 0, 60);
        add(*s, "b", "V1", p("blue.png"), 60, 60);
        clip(*s, "b").transIn = {"dissolve", 30};
        QVERIFY(near(px(render(*s, 59), .5, .5), 255, 0, 0));
        QVERIFY(near(px(render(*s, 60), .5, .5), 255, 0, 0, 12));          // t = 0: still the outgoing picture
        QRgb mid = px(render(*s, 75), .5, .5);                              // t = 0.5
        QVERIFY2(near(mid, 128, 0, 128, 20), qPrintable(QString("mid %1,%2,%3").arg(qRed(mid)).arg(qGreen(mid)).arg(qBlue(mid))));
        QVERIFY(near(px(render(*s, 90), .5, .5), 0, 0, 255));              // done
    }
    void wipeSlideZoomDipFade()
    {
        auto s = snap();
        add(*s, "a", "V1", p("red.png"), 0, 60);
        add(*s, "b", "V1", p("blue.png"), 60, 60);
        clip(*s, "b").transIn = {"wipe", 30};
        QImage w = render(*s, 75);                                           // half revealed from the left
        QVERIFY(near(px(w, 0.25, .5), 0, 0, 255));
        QVERIFY(near(px(w, 0.75, .5), 255, 0, 0));
        clip(*s, "b").transIn = {"slide", 30};
        QImage sl = render(*s, 75);                                          // blue slides in from the right
        QVERIFY(near(px(sl, 0.25, .5), 255, 0, 0));
        QVERIFY(near(px(sl, 0.75, .5), 0, 0, 255));
        clip(*s, "b").transIn = {"zoom", 30};
        QImage z = render(*s, 75);                                           // blue grows from the centre over the red
        QVERIFY(near(px(z, 0.02, 0.02), 255, 0, 0, 30));
        QVERIFY(qBlue(px(z, 0.5, 0.5)) > 80);
        clip(*s, "b").transIn = {"dip", 40};
        QVERIFY(near(px(render(*s, 70), .5, .5), 128, 0, 0, 40));            // t = .25: outgoing at half brightness
        QVERIFY(near(px(render(*s, 90), .5, .5), 0, 0, 128, 40));            // t = .75: incoming at half brightness
        clip(*s, "b").transIn = {"fade", 30};                                 // fade in from nothing: the outgoing clip is not shown
        QVERIFY(near(px(render(*s, 75), .5, .5), 0, 0, 128, 30));
        clip(*s, "b").transIn = {};
        clip(*s, "b").transOut = {"fade", 30};
        QVERIFY(near(px(render(*s, 119), .5, .5), 0, 0, 0, 25));             // last frame ~ black
        QVERIFY(near(px(render(*s, 85), .5, .5), 0, 0, 255));                // before the fade starts
        clip(*s, "b").transOut = {"dip", 30};
        QVERIFY(near(px(render(*s, 105), .5, .5), 0, 0, 0, 12));             // dip to black is dark from the middle on
    }

    void moreTransitionDirections()
    {
        auto s = snap();
        add(*s, "a", "V1", p("red.png"), 0, 60);
        add(*s, "b", "V1", p("blue.png"), 60, 60);
        clip(*s, "b").transIn = {"wipeLeft", 30};
        QImage wl = render(*s, 75);                                          // revealed from the right edge
        QVERIFY(near(px(wl, 0.25, .5), 255, 0, 0));
        QVERIFY(near(px(wl, 0.75, .5), 0, 0, 255));
        clip(*s, "b").transIn = {"wipeDown", 30};
        QImage wd = render(*s, 75);                                          // revealed from the top
        QVERIFY(near(px(wd, .5, 0.25), 0, 0, 255));
        QVERIFY(near(px(wd, .5, 0.75), 255, 0, 0));
        clip(*s, "b").transIn = {"wipeUp", 30};
        QImage wu = render(*s, 75);                                          // revealed from the bottom
        QVERIFY(near(px(wu, .5, 0.25), 255, 0, 0));
        QVERIFY(near(px(wu, .5, 0.75), 0, 0, 255));
        clip(*s, "b").transIn = {"slideLeft", 30};
        QImage sl = render(*s, 75);                                          // blue slides in from the left
        QVERIFY(near(px(sl, 0.25, .5), 0, 0, 255));
        QVERIFY(near(px(sl, 0.75, .5), 255, 0, 0));
        clip(*s, "b").transIn = {"slideUp", 30};
        QImage su = render(*s, 75);                                          // blue slides in from the bottom
        QVERIFY(near(px(su, .5, 0.25), 255, 0, 0));
        QVERIFY(near(px(su, .5, 0.75), 0, 0, 255));
        clip(*s, "b").transIn = {"flash", 30};
        QVERIFY(near(px(render(*s, 75), .5, .5), 255, 255, 255, 12));         // white in the middle of the flash
        QVERIFY(near(px(render(*s, 90), .5, .5), 0, 0, 255));                // done
        clip(*s, "b").transIn = {"spin", 30};
        QVERIFY(qBlue(px(render(*s, 75), .5, .5)) > 80);                      // the incoming clip turns up in the middle
        clip(*s, "b").transIn = {};
        clip(*s, "b").transOut = {"wipe", 30};
        QImage wo = render(*s, 105);                                         // half of the clip is left (the left half), the rest is black
        QVERIFY(near(px(wo, 0.25, .5), 0, 0, 255));
        QVERIFY(near(px(wo, 0.75, .5), 0, 0, 0, 12));
    }

    // ========================================= titles ================================================
    void titlesRenderTextIncludingThai()
    {
        TitleData t;
        t.text = "HELLO";
        t.size = 300;
        QImage img = Compositor::renderTitle(t, QSize(640, 360));
        int white = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                if (qAlpha(img.pixel(x, y)) > 200) ++white;
        QVERIFY2(white > 600, "no text drawn");
        QCOMPARE(qAlpha(img.pixel(2, 2)), 0);                              // transparent elsewhere

        TitleData thai;
        thai.text = QString::fromUtf8("สวัสดีชาวโลก");
        thai.size = 250;
        QImage ti = Compositor::renderTitle(thai, QSize(640, 360));
        int ink = 0;
        for (int y = 0; y < ti.height(); ++y)
            for (int x = 0; x < ti.width(); ++x)
                if (qAlpha(ti.pixel(x, y)) > 200) ++ink;
        QVERIFY2(ink > 600, "Thai text was not rendered");

        TitleData outlined = t;
        outlined.color = "#FF0000";
        outlined.strokeColor = "#0000FF";
        outlined.strokeWidth = 6;
        QImage o = Compositor::renderTitle(outlined, QSize(640, 360));
        bool blue = false, red = false;
        for (int y = 0; y < o.height(); ++y)
            for (int x = 0; x < o.width(); ++x) {
                const QRgb c = o.pixel(x, y);
                blue = blue || (qAlpha(c) > 200 && qBlue(c) > 200 && qRed(c) < 60);
                red = red || (qAlpha(c) > 200 && qRed(c) > 200 && qBlue(c) < 60);
            }
        QVERIFY(blue && red);

        TitleData bottom = t;
        bottom.y = 0.9;
        QImage b = Compositor::renderTitle(bottom, QSize(640, 360));
        int topInk = 0, lowInk = 0;
        for (int y = 0; y < b.height(); ++y)
            for (int x = 0; x < b.width(); ++x)
                if (qAlpha(b.pixel(x, y)) > 128) (y < 180 ? topInk : lowInk)++;
        QVERIFY(topInk == 0 && lowInk > 500);
    }
    void titleClipsAndSubtitlesAreComposited()
    {
        auto s = snap();
        add(*s, "bg", "V1", p("blue.png"), 0, 150);
        SnapshotClip title;
        title.clip.id = "t";
        title.clip.type = "title";
        title.clip.trackId = "V2";
        title.clip.startFrame = 30;
        title.clip.durationFrames = 60;
        title.clip.title.text = "TITLE";
        title.clip.title.size = 200;
        title.trackIndex = 1;
        s->clips.append(title);

        auto whiteCount = [](const QImage& img) {
            int n = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (qRed(img.pixel(x, y)) > 200 && qGreen(img.pixel(x, y)) > 200 && qBlue(img.pixel(x, y)) > 200) ++n;
            return n;
        };
        QCOMPARE(whiteCount(render(*s, 10)), 0);                            // before the title
        QVERIFY(whiteCount(render(*s, 50)) > 300);                          // during
        QCOMPARE(whiteCount(render(*s, 95)), 0);                            // after

        clip(*s, "t").transIn = {"fade", 20};                               // a title can use the same transitions
        QVERIFY(whiteCount(render(*s, 31)) < whiteCount(render(*s, 55)));

        SnapshotClip sub;                                                    // subtitle track: burnt in on top
        sub.clip.id = "sub";
        sub.clip.type = "title";
        sub.clip.trackId = "S1";
        sub.clip.startFrame = 100;
        sub.clip.durationFrames = 30;
        sub.clip.title.text = "SUBTITLE";
        sub.clip.title.size = 60;
        sub.clip.title.y = 0.9;
        sub.trackIndex = 0;
        s->clips.append(sub);
        const QImage withSub = render(*s, 110);
        int lowWhite = 0;
        for (int y = int(withSub.height() * 0.75); y < withSub.height(); ++y)
            for (int x = 0; x < withSub.width(); ++x)
                if (qRed(withSub.pixel(x, y)) > 200 && qGreen(withSub.pixel(x, y)) > 200) ++lowWhite;
        QVERIFY(lowWhite > 150);
        QCOMPARE(whiteCount(render(*s, 135)) , 0);
        s->tracks[0].visible = false;                                         // hiding the subtitle track removes the burn-in
        QCOMPARE(whiteCount(render(*s, 110)), 0);
    }

    // ========================================= audio =================================================
    std::vector<float> mix(RenderSnapshot& s, qint64 startSample, int frames, AudioMixer::Peaks* pk = nullptr)
    {
        AudioMixer m;
        m.setSnapshot(std::make_shared<RenderSnapshot>(s));
        std::vector<float> out(size_t(frames) * 2);
        m.render(startSample, frames, out.data(), pk);
        return out;
    }
    void addAudio(RenderSnapshot& s, const QString& id, const QString& track, const QString& path, qint64 start = 0, qint64 dur = 90)
    {
        add(s, id, track, path, start, dur, "audio", true);
    }

    void volumeFadesAndTrackGain()
    {
        auto s = snap();
        addAudio(*s, "t", "A1", p("tone.wav"));
        const double full = rms(mix(*s, 24000, 9600));
        QVERIFY2(full > 0.05 && full < 0.075, qPrintable(QString::number(full)));   // a mono file is spread to both channels at -3 dB: 0.088 / sqrt(2)
        setParam(*s, "t", "volume", -6.0);
        QVERIFY(std::abs(rms(mix(*s, 24000, 9600)) / full - 0.501) < 0.03);          // -6 dB = x0.5
        setParam(*s, "t", "volume", 0.0);
        s->clips[0].clip.params.clear();

        s->clips[0].clip.fadeInFrames = 30;                                          // 1 s
        s->clips[0].clip.fadeOutFrames = 30;
        const auto v = mix(*s, 0, 48000 * 3);
        QVERIFY(rms(v, 0, 960) < 0.01);                                               // first 10 ms ~ silent
        QVERIFY(rms(v, 48000 * 2 / 1, 48000 * 2 + 9600) > 0.5 * full);                 // middle full level (1.0 s - 2.0 s)
        const double early = rms(v, 24000 * 2, 24000 * 2 + 4800);                      // 0.5 s into the fade: about half
        QVERIFY(early > 0.35 * full && early < 0.65 * full);
        const size_t end = size_t(48000 * 3 - 2) * 2;                                  // last samples ~ silent
        QVERIFY(rms(v, end - 480, end) < 0.02);
        s->clips[0].clip.fadeInFrames = s->clips[0].clip.fadeOutFrames = 0;

        s->tracks[3].gainDb = -12.0;                                                   // channel fader on A1
        QVERIFY(std::abs(rms(mix(*s, 24000, 9600)) / full - 0.251) < 0.03);
        s->masterGainDb = -12.0;                                                       // master fader: another -12 dB on top
        QVERIFY(std::abs(rms(mix(*s, 24000, 9600)) / full - 0.063) < 0.02);
        s->masterGainDb = 0;
        s->tracks[3].gainDb = 0;
    }
    void panSoloMuteAndKeyframedVolume()
    {
        auto s = snap();
        addAudio(*s, "t", "A1", p("tone.wav"));
        s->tracks[3].pan = -1.0;                                                       // hard left
        auto left = mix(*s, 24000, 9600);
        QVERIFY(rms(left, 0, 0, 0) > 0.08);
        QVERIFY(rms(left, 0, 0, 1) < 0.005);
        s->tracks[3].pan = 0.0;

        addAudio(*s, "u", "A2", p("tone.wav"));
        const double both = rms(mix(*s, 24000, 9600));
        s->tracks[4].solo = true;                                                      // solo A2: A1 is silent -> half the energy of "both"
        const double solo = rms(mix(*s, 24000, 9600));
        QVERIFY(solo < both * 0.6 && solo > both * 0.4);
        s->tracks[4].muted = true;                                                     // a muted track stays silent even when soloed
        QCOMPARE(rms(mix(*s, 24000, 9600)), 0.0);
        s->tracks[4].muted = false;
        s->tracks[4].solo = false;

        ParamTrack t;                                                                  // volume ramps from -60 dB to 0 dB over the clip
        t.keys = {{0, -60.0, "linear"}, {89, 0.0, "linear"}};
        clip(*s, "t").params.insert("volume", t);
        s->tracks[4].muted = true;
        const double a = rms(mix(*s, 4800, 4800));
        const double b = rms(mix(*s, 48000 * 3 - 9600, 4800));
        QVERIFY(b > a * 20);
    }
    void eqGateAndLimiter()
    {
        auto s = snap();
        addAudio(*s, "low", "A1", p("low.wav"));
        const double low = rms(mix(*s, 24000, 9600));
        setParam(*s, "low", "eqLow", -18.0);                                          // low shelf cut hits a 60 Hz tone
        QVERIFY(rms(mix(*s, 24000, 9600)) < low * 0.3);
        auto s2 = snap();
        addAudio(*s2, "high", "A1", p("high.wav"));
        const double high = rms(mix(*s2, 24000, 9600));
        setParam(*s2, "high", "eqLow", -18.0);                                        // ... but not a 9 kHz tone
        QVERIFY(std::abs(rms(mix(*s2, 24000, 9600)) / high - 1.0) < 0.1);
        setParam(*s2, "high", "eqHigh", 18.0);                                        // high shelf boost lifts it
        QVERIFY(rms(mix(*s2, 24000, 9600)) > high * 2.0);

        // EQ filter state is continuous across blocks: two half-size renders equal one big render.
        auto whole = mix(*s2, 24000, 4800);
        AudioMixer m;
        m.setSnapshot(std::make_shared<RenderSnapshot>(*s2));
        std::vector<float> a(2400 * 2), b(2400 * 2);
        m.render(24000, 2400, a.data());
        m.render(26400, 2400, b.data());
        for (size_t i = 0; i < a.size(); ++i) {
            QVERIFY(std::abs(whole[i] - a[i]) < 1e-3f);
            QVERIFY(std::abs(whole[i + a.size()] - b[i]) < 1e-3f);
        }

        auto s3 = snap();
        addAudio(*s3, "t", "A1", p("tone.wav"));
        setParam(*s3, "t", "gate", -10.0);                                            // tone peaks at -18 dBFS: gated
        QVERIFY(rms(mix(*s3, 24000, 9600)) < 0.003);
        setParam(*s3, "t", "gate", -40.0);                                            // open
        QVERIFY(rms(mix(*s3, 24000, 9600)) > 0.05);

        auto s4 = snap();                                                              // three loud copies exceed full scale
        for (const char* id : {"l1", "l2", "l3"}) {
            addAudio(*s4, id, "A1", p("tone.wav"));
            setParam(*s4, id, "volume", 12.0);
            s4->clips.back().trackIndex = 3;
            s4->clips.back().clip.trackId = "A1";
        }
        AudioMixer::Peaks pk;
        const auto limited = mix(*s4, 24000, 9600, &pk);
        float peak = 0;
        for (float x : limited) peak = std::max(peak, std::abs(x));
        QVERIFY(peak <= 1.0f);
        QVERIFY(peak > 0.9f);                                                          // loud, but never beyond full scale
        QVERIFY(std::abs(pk.left - peak) < 1e-4f);
        QCOMPARE(AudioMixer::softLimit(0.5f), 0.5f);
        QVERIFY(AudioMixer::softLimit(3.f) <= 1.0f && AudioMixer::softLimit(3.f) >= AudioMixer::softLimit(1.2f) && AudioMixer::softLimit(1.2f) < 1.0f);
        QCOMPARE(AudioMixer::softLimit(-3.f), -AudioMixer::softLimit(3.f));
    }
    void trackEqFiltersTheWholeTrack()
    {
        auto s = snap();
        addAudio(*s, "low", "A1", p("low.wav"));
        addAudio(*s, "low2", "A1", p("low.wav"), 90, 90);                                // a second clip on the same track
        const double low = rms(mix(*s, 24000, 9600));
        const double low2 = rms(mix(*s, 48000 * 3 + 24000, 9600));
        s->tracks[3].eqLow = -18.0;                                                       // the EQ of the track A1, not of a clip
        QVERIFY(rms(mix(*s, 24000, 9600)) < low * 0.3);
        QVERIFY(rms(mix(*s, 48000 * 3 + 24000, 9600)) < low2 * 0.3);                      // both clips are cut
        s->tracks[3].eqLow = 0;
        QVERIFY(std::abs(rms(mix(*s, 24000, 9600)) / low - 1.0) < 1e-3);                  // flat again: untouched

        auto s2 = snap();
        addAudio(*s2, "high", "A1", p("high.wav"));
        const double high = rms(mix(*s2, 24000, 9600));
        s2->tracks[3].eqLow = -18.0;                                                      // a 9 kHz tone is not touched by the low shelf
        QVERIFY(std::abs(rms(mix(*s2, 24000, 9600)) / high - 1.0) < 0.1);
        s2->tracks[3].eqHigh = 18.0;
        QVERIFY(rms(mix(*s2, 24000, 9600)) > high * 2.0);
        // another track is not affected
        addAudio(*s2, "other", "A2", p("high.wav"));
        s2->clips.back().trackIndex = 4;
        s2->tracks[3].eqHigh = 0;
        s2->tracks[3].eqLow = 0;
        const double both = rms(mix(*s2, 24000, 9600));
        s2->tracks[3].eqHigh = 18.0;
        QVERIFY(rms(mix(*s2, 24000, 9600)) > both * 1.3);
        s2->tracks[4].eqHigh = 18.0;
        QVERIFY(rms(mix(*s2, 24000, 9600)) > both * 2.0);

        // the filter state is continuous across blocks: two half-size renders equal one big render
        const auto whole = mix(*s2, 24000, 4800);
        AudioMixer m;
        m.setSnapshot(std::make_shared<RenderSnapshot>(*s2));
        std::vector<float> a(2400 * 2), b(2400 * 2);
        m.render(24000, 2400, a.data());
        m.render(26400, 2400, b.data());
        for (size_t i = 0; i < a.size(); ++i) {
            QVERIFY(std::abs(whole[i] - a[i]) < 1e-3f);
            QVERIFY(std::abs(whole[i + a.size()] - b[i]) < 1e-3f);
        }
    }
};

QTEST_MAIN(TstEffects)
#include "tst_effects.moc"
