#include "Transcriber.h"

#include "playback/AudioMixer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace dv::ai {

QString whisperExecutable()
{
    QStringList dirs;
    const QString env = qEnvironmentVariable("DV_WHISPER_DIR");
    if (!env.isEmpty())
        dirs << env;
    const QString app = QCoreApplication::applicationDirPath();
    dirs << app + QStringLiteral("/ai/whisper");
    // development tree: walk up to a folder that has .tools\whisper
    QDir d(app);
    for (int i = 0; i < 6; ++i) {
        dirs << d.filePath(QStringLiteral(".tools/whisper/bin/Release"));
        if (!d.cdUp())
            break;
    }
    for (const QString& dir : std::as_const(dirs)) {
        const QString exe = QDir(dir).filePath(QStringLiteral("whisper-cli.exe"));
        if (QFileInfo::exists(exe))
            return QDir::cleanPath(exe);
    }
    return {};
}

QList<Segment> parseSrt(const QString& text)
{
    QList<Segment> out;
    static const QRegularExpression time(QStringLiteral(R"((\d+):(\d+):(\d+)[,.](\d+)\s*-->\s*(\d+):(\d+):(\d+)[,.](\d+))"));
    const QStringList lines = QString(text).replace(QStringLiteral("\r"), QString()).split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const auto m = time.match(lines.at(i));
        if (!m.hasMatch())
            continue;
        auto sec = [&](int a) {
            return m.captured(a).toDouble() * 3600 + m.captured(a + 1).toDouble() * 60 + m.captured(a + 2).toDouble()
                   + m.captured(a + 3).leftJustified(3, QLatin1Char('0')).left(3).toDouble() / 1000.0;
        };
        QStringList body;
        for (++i; i < lines.size() && !lines.at(i).trimmed().isEmpty(); ++i)
            body << lines.at(i).trimmed();
        const QString t = body.join(QLatin1Char(' ')).trimmed();
        if (!t.isEmpty())
            out.append({sec(1), sec(5), t});
    }
    return out;
}

namespace {
bool writeWav16kMono(const QString& path, const std::vector<int16_t>& pcm)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    const quint32 bytes = quint32(pcm.size() * 2);
    auto u32 = [&](quint32 v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](quint16 v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8);
    u32(16); u16(1); u16(1); u32(16000); u32(32000); u16(2); u16(16);
    f.write("data", 4); u32(bytes);
    f.write(reinterpret_cast<const char*>(pcm.data()), qint64(bytes));
    return f.flush();
}
} // namespace

TranscribeResult transcribeTimeline(const TranscribeRequest& req, const ProgressFn& progress, const std::atomic_bool& cancel)
{
    TranscribeResult result;
    if (!req.snapshot || req.snapshot->durationFrames <= 0) {
        result.error = QStringLiteral("The timeline is empty.");
        return result;
    }
    if (req.whisperExe.isEmpty() || !QFileInfo::exists(req.whisperExe)) {
        result.error = QStringLiteral("whisper.cpp was not found.");
        return result;
    }
    if (!QFileInfo::exists(req.model)) {
        result.error = QStringLiteral("The speech model file was not found.");
        return result;
    }
    QDir().mkpath(req.workDir);
    const QString base = QDir(req.workDir).filePath(QStringLiteral("dv_asr_") + QUuid::createUuid().toString(QUuid::Id128));
    const QString wav = base + QStringLiteral(".wav");
    const QString srt = base + QStringLiteral(".srt");
    struct Cleanup { QString a, b; ~Cleanup() { QFile::remove(a); QFile::remove(b); } } cleanup{wav, srt};

    // 1. mix the timeline and bring it to 16 kHz mono
    const RenderSnapshot& snap = *req.snapshot;
    AudioMixer mixer;
    mixer.setSnapshot(req.snapshot);
    const int64_t total = timecode::frameToSample(snap.durationFrames, snap.fps, snap.sampleRate);
    const double ratio = double(snap.sampleRate) / 16000.0;
    std::vector<int16_t> pcm;
    pcm.reserve(size_t(double(total) / ratio) + 16);
    constexpr int kBlock = 16000;
    std::vector<float> buf(size_t(kBlock) * 2);
    double acc = 0, next = ratio;
    int accN = 0;
    int64_t consumed = 0;
    for (int64_t pos = 0; pos < total; pos += kBlock) {
        if (cancel.load()) {
            result.cancelled = true;
            return result;
        }
        const int n = int(std::min<int64_t>(kBlock, total - pos));
        mixer.render(pos, n, buf.data());
        for (int i = 0; i < n; ++i) {
            acc += 0.5 * (double(buf[size_t(i) * 2]) + double(buf[size_t(i) * 2 + 1]));
            ++accN;
            ++consumed;
            if (double(consumed) >= next) {
                pcm.push_back(int16_t(std::lround(std::clamp(acc / accN, -1.0, 1.0) * 32767.0)));
                acc = 0;
                accN = 0;
                next += ratio;
            }
        }
        if (progress)
            progress(0.1 * double(pos + n) / double(total));
    }
    if (!writeWav16kMono(wav, pcm)) {
        result.error = QStringLiteral("Cannot write the temporary audio file.");
        return result;
    }

    // 2. whisper.cpp
    QProcess proc;
    proc.setProgram(req.whisperExe);
    proc.setWorkingDirectory(QFileInfo(req.whisperExe).absolutePath());
    proc.setArguments({QStringLiteral("-m"), req.model, QStringLiteral("-f"), wav, QStringLiteral("-l"), req.language,
                       QStringLiteral("-osrt"), QStringLiteral("-of"), base, QStringLiteral("-pp"),
                       QStringLiteral("-t"), QString::number(std::max(1, req.threads))});
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start();
    if (!proc.waitForStarted(10000)) {
        result.error = QStringLiteral("whisper.cpp could not be started: %1").arg(proc.errorString());
        return result;
    }
    static const QRegularExpression pct(QStringLiteral(R"(progress\s*=\s*(\d+)\s*%)"));
    QString tail;
    while (proc.state() != QProcess::NotRunning) {
        if (cancel.load()) {
            proc.kill();
            proc.waitForFinished(5000);
            result.cancelled = true;
            return result;
        }
        proc.waitForReadyRead(150);
        const QString chunk = QString::fromUtf8(proc.readAll());
        tail = (tail + chunk).right(2000);
        auto it = pct.globalMatch(chunk);
        while (it.hasNext())
            if (progress)
                progress(0.1 + 0.9 * it.next().captured(1).toDouble() / 100.0);
    }
    tail = (tail + QString::fromUtf8(proc.readAll())).right(2000);
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        result.error = QStringLiteral("whisper.cpp failed (exit code %1): %2").arg(proc.exitCode()).arg(tail.right(300).trimmed());
        return result;
    }
    QFile f(srt);
    if (!f.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("whisper.cpp wrote no result.");
        return result;
    }
    result.segments = parseSrt(QString::fromUtf8(f.readAll()));
    if (progress)
        progress(1.0);
    return result;
}

} // namespace dv::ai
