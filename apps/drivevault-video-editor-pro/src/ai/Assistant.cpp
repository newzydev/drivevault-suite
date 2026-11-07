#include "Assistant.h"

#include <QStringList>

namespace dv::ai {

QString Command::typeName(Type t)
{
    switch (t) {
    case Transcribe: return QStringLiteral("transcribe");
    case DetectSilence: return QStringLiteral("silence");
    case SuggestCuts: return QStringLiteral("cuts");
    case EnhanceAudio: return QStringLiteral("enhance");
    case TrackObject: return QStringLiteral("track");
    case Help: return QStringLiteral("help");
    default: return QStringLiteral("none");
    }
}

namespace {
bool hasAny(const QString& text, std::initializer_list<const char16_t*> words)
{
    for (const char16_t* w : words)
        if (text.contains(QString::fromUtf16(w), Qt::CaseInsensitive))
            return true;
    return false;
}
} // namespace

Command parseCommand(const QString& raw)
{
    const QString text = raw.simplified();
    Command c;
    if (text.isEmpty())
        return c;
    // Order matters: "ไม่มีเสียง" must win over "เสียง..." phrases.
    if (hasAny(text, {u"ไม่มีเสียง", u"ช่วงเงียบ", u"เงียบ", u"silence", u"silent", u"pause", u"dead air"})) {
        c.type = Command::DetectSilence;
    } else if (hasAny(text, {u"จุดตัด", u"เปลี่ยนฉาก", u"ตัดต่ออัตโนมัติ", u"scene", u"cut point", u"suggest cut", u"smart cut", u"where to cut"})) {
        c.type = Command::SuggestCuts;
    } else if (hasAny(text, {u"ซับ", u"ถอดเสียง", u"คำบรรยาย", u"subtitle", u"caption", u"transcri", u"speech to text"})) {
        c.type = Command::Transcribe;
        if (hasAny(text, {u"ไทย", u"thai"}))
            c.language = QStringLiteral("th");
        else if (hasAny(text, {u"อังกฤษ", u"english"}))
            c.language = QStringLiteral("en");
    } else if (hasAny(text, {u"ปรับเสียง", u"เสียงพูด", u"ชัดขึ้น", u"ชัด", u"เสียงรบกวน", u"ลดนอยส์", u"นอยส์",
                              u"enhance", u"denoise", u"noise", u"clean up the audio", u"clearer", u"clarity"})) {
        c.type = Command::EnhanceAudio;
    } else if (hasAny(text, {u"ติดตาม", u"track"})) {
        c.type = Command::TrackObject;
    } else if (hasAny(text, {u"ช่วยเหลือ", u"ทำอะไรได้", u"help", u"what can you do"})) {
        c.type = Command::Help;
    }
    switch (c.type) {
    case Command::Transcribe: c.summary = QStringLiteral("Transcribe the timeline audio into subtitles"); break;
    case Command::DetectSilence: c.summary = QStringLiteral("Find silent passages"); break;
    case Command::SuggestCuts: c.summary = QStringLiteral("Suggest cut points at scene changes and pauses"); break;
    case Command::EnhanceAudio: c.summary = QStringLiteral("Reduce noise and even out the level of the selected clip's audio"); break;
    case Command::TrackObject: c.summary = QStringLiteral("Track a region of the selected clip"); break;
    default: break;
    }
    return c;
}

} // namespace dv::ai
