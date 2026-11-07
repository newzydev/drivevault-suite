#pragma once

// Generates small, deterministic media files for the tests using the ffmpeg.exe of the local
// FFmpeg build (DV_TEST_FFMPEG is set by CMake).

#include <QDir>
#include <QProcess>
#include <QString>
#include <QStringList>

namespace testmedia {

inline bool runFfmpeg(const QStringList& args, QString* log = nullptr)
{
    QProcess p;
    p.start(QStringLiteral(DV_TEST_FFMPEG), QStringList{"-hide_banner", "-loglevel", "error", "-y"} + args);
    if (!p.waitForFinished(60000) || p.exitCode() != 0) {
        if (log)
            *log = QString::fromLocal8Bit(p.readAllStandardError());
        return false;
    }
    return true;
}

// 2 s clip: first second `colorA`, second second `colorB`, 30 fps; optional 440 Hz sine audio.
inline bool makeTwoColorVideo(const QString& file, const QString& colorA = "red", const QString& colorB = "blue",
                              bool audio = true, const QString& size = "320x240", int fps = 30, int secondsEach = 1)
{
    const QString d = QString::number(secondsEach);
    QStringList args{"-f", "lavfi", "-i", QString("color=c=%1:s=%2:r=%3:d=%4").arg(colorA, size).arg(fps).arg(d),
                     "-f", "lavfi", "-i", QString("color=c=%1:s=%2:r=%3:d=%4").arg(colorB, size).arg(fps).arg(d)};
    if (audio)
        args << "-f" << "lavfi" << "-i" << QString("sine=frequency=440:sample_rate=48000:duration=%1").arg(secondsEach * 2);
    args << "-filter_complex" << "[0:v][1:v]concat=n=2:v=1:a=0[v]" << "-map" << "[v]";
    if (audio)
        args << "-map" << "2:a" << "-c:a" << "aac" << "-b:a" << "128k";
    args << "-c:v" << "mpeg4" << "-q:v" << "2" << "-pix_fmt" << "yuv420p" << "-g" << "12" << file;
    return runFfmpeg(args);
}

inline bool makeSine(const QString& file, int seconds = 2, int freq = 440, int rate = 48000)
{
    return runFfmpeg({"-f", "lavfi", "-i", QString("sine=frequency=%1:sample_rate=%2:duration=%3").arg(freq).arg(rate).arg(seconds),
                      file});
}

inline bool makePng(const QString& file, const QString& color = "green", const QString& size = "200x100")
{
    return runFfmpeg({"-f", "lavfi", "-i", QString("color=c=%1:s=%2").arg(color, size), "-frames:v", "1", file});
}

} // namespace testmedia
