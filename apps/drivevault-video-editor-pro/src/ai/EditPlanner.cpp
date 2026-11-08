#include "EditPlanner.h"

#include <QRegularExpression>

#include <algorithm>

namespace dv::ai {

namespace {
using V = QList<QPair<QString, double>>;

CatalogEntry entry(const char* id, const char* label, QStringList aliases, V values = {}, const char* resetGroup = "")
{
    return {QString::fromLatin1(id), QString::fromLatin1(label), aliases, values, QString::fromLatin1(resetGroup)};
}
QStringList L(std::initializer_list<const char16_t*> items)
{
    QStringList list;
    for (const char16_t* s : items)
        list << QString::fromUtf16(s);
    return list;
}
} // namespace

// ---- catalogs (the numbers are the same as the tiles of the tool panel) ---------------------------------------------------------------

const QList<CatalogEntry>& lookCatalog()
{
    // fractions of the way from the default to the end of the range (see ToolPanel.qml: applyParams / mix)
    static const QList<CatalogEntry> list = {
        entry("vivid", QT_TRANSLATE_NOOP("EditPlanner", "Vivid"), L({u"vivid", u"สดใส", u"สีสด"}), {{"saturation", 0.45}, {"contrast", 0.2}}, "color"),
        entry("warm", QT_TRANSLATE_NOOP("EditPlanner", "Warm"), L({u"warm", u"โทนอุ่น", u"อุ่น"}), {{"temperature", 0.45}, {"saturation", 0.1}}, "color"),
        entry("cool", QT_TRANSLATE_NOOP("EditPlanner", "Cool"), L({u"cool", u"โทนเย็น", u"เย็น"}), {{"temperature", -0.45}, {"tint", -0.1}}, "color"),
        entry("mono", QT_TRANSLATE_NOOP("EditPlanner", "Black & white"), L({u"mono", u"black and white", u"b&w", u"ขาวดำ"}), {{"saturation", -1}, {"contrast", 0.15}}, "color"),
        entry("faded", QT_TRANSLATE_NOOP("EditPlanner", "Faded"), L({u"faded", u"จางๆ", u"ซีดจาง"}), {{"contrast", -0.3}, {"brightness", 0.12}, {"saturation", -0.25}}, "color"),
        entry("cinema", QT_TRANSLATE_NOOP("EditPlanner", "Cinematic"), L({u"cinematic", u"cinema", u"ซีนีมา", u"โทนหนัง", u"ภาพยนตร์"}), {{"contrast", 0.3}, {"saturation", -0.15}, {"temperature", -0.2}}, "color"),
        entry("bright", QT_TRANSLATE_NOOP("EditPlanner", "Bright"), L({u"bright", u"สว่าง"}), {{"brightness", 0.2}, {"contrast", 0.05}}, "color"),
        entry("dark", QT_TRANSLATE_NOOP("EditPlanner", "Dark"), L({u"dark", u"มืด"}), {{"brightness", -0.2}, {"contrast", 0.1}}, "color"),
        entry("film", QT_TRANSLATE_NOOP("EditPlanner", "Film"), L({u"film", u"ฟิล์ม"}), {{"contrast", 0.2}, {"saturation", -0.15}, {"temperature", 0.2}, {"brightness", 0.05}}, "color"),
        entry("vintage", QT_TRANSLATE_NOOP("EditPlanner", "Vintage"), L({u"vintage", u"วินเทจ", u"ย้อนยุค"}), {{"saturation", -0.35}, {"temperature", 0.4}, {"contrast", -0.1}, {"tint", 0.1}}, "color"),
        entry("sepia", QT_TRANSLATE_NOOP("EditPlanner", "Sepia"), L({u"sepia", u"ซีเปีย"}), {{"saturation", -0.7}, {"temperature", 0.6}, {"contrast", 0.05}}, "color"),
        entry("retro", QT_TRANSLATE_NOOP("EditPlanner", "Retro"), L({u"retro", u"เรโทร"}), {{"saturation", 0.2}, {"temperature", 0.3}, {"contrast", -0.2}, {"tint", 0.15}}, "color"),
        entry("noir", QT_TRANSLATE_NOOP("EditPlanner", "Noir"), L({u"noir", u"นัวร์"}), {{"saturation", -1}, {"contrast", 0.5}, {"brightness", -0.1}}, "color"),
        entry("bleach", QT_TRANSLATE_NOOP("EditPlanner", "Bleach"), L({u"bleach", u"ฟอกสี"}), {{"saturation", -0.5}, {"contrast", 0.4}}, "color"),
        entry("sunset", QT_TRANSLATE_NOOP("EditPlanner", "Sunset"), L({u"sunset", u"พระอาทิตย์ตก", u"ซันเซ็ต"}), {{"temperature", 0.6}, {"saturation", 0.3}, {"brightness", 0.05}}, "color"),
        entry("golden", QT_TRANSLATE_NOOP("EditPlanner", "Golden hour"), L({u"golden hour", u"golden", u"แสงทอง"}), {{"temperature", 0.5}, {"saturation", 0.15}, {"contrast", 0.1}, {"brightness", 0.1}}, "color"),
        entry("ocean", QT_TRANSLATE_NOOP("EditPlanner", "Ocean"), L({u"ocean", u"มหาสมุทร", u"ทะเล"}), {{"temperature", -0.6}, {"tint", -0.15}, {"saturation", 0.15}}, "color"),
        entry("forest", QT_TRANSLATE_NOOP("EditPlanner", "Forest"), L({u"forest", u"ป่า"}), {{"tint", -0.35}, {"saturation", 0.25}, {"temperature", -0.1}}, "color"),
        entry("neon", QT_TRANSLATE_NOOP("EditPlanner", "Neon night"), L({u"neon night", u"neon", u"ราตรีนีออน", u"นีออน"}), {{"saturation", 0.7}, {"contrast", 0.3}, {"tint", 0.4}, {"temperature", -0.3}}, "color"),
        entry("pastel", QT_TRANSLATE_NOOP("EditPlanner", "Pastel"), L({u"pastel", u"พาสเทล"}), {{"saturation", -0.3}, {"brightness", 0.2}, {"contrast", -0.25}}, "color"),
        entry("moody", QT_TRANSLATE_NOOP("EditPlanner", "Moody"), L({u"moody", u"หม่น"}), {{"brightness", -0.15}, {"contrast", 0.3}, {"saturation", -0.2}, {"temperature", -0.15}}, "color"),
        entry("tealOrange", QT_TRANSLATE_NOOP("EditPlanner", "Teal and orange"), L({u"tealorange", u"teal", u"ฟ้าส้ม"}), {{"temperature", 0.25}, {"tint", -0.2}, {"contrast", 0.25}, {"saturation", 0.2}}, "color"),
        entry("hdr", QT_TRANSLATE_NOOP("EditPlanner", "Punchy"), L({u"punchy", u"hdr", u"จัดจ้าน"}), {{"contrast", 0.4}, {"saturation", 0.4}}, "color"),
        entry("none", QT_TRANSLATE_NOOP("EditPlanner", "Original"), L({u"original", u"no filter", u"ต้นฉบับ", u"ไม่มีฟิลเตอร์", u"ลบฟิลเตอร์"}), {}, "color"),
    };
    return list;
}

const QList<CatalogEntry>& effectCatalog()
{
    // real parameter values; the group is reset first ("" for zoom / tilt, which live in the transform group)
    static const QList<CatalogEntry> list = {
        entry("blurLight", QT_TRANSLATE_NOOP("EditPlanner", "Light blur"), L({u"light blur", u"เบลอเบา", u"เบลอนิดหน่อย"}), {{"blur", 6}}, "effects"),
        entry("blurStrong", QT_TRANSLATE_NOOP("EditPlanner", "Strong blur"), L({u"strong blur", u"heavy blur", u"เบลอแรง", u"เบลอมาก"}), {{"blur", 40}}, "effects"),
        entry("blur", QT_TRANSLATE_NOOP("EditPlanner", "Soft blur"), L({u"blur", u"เบลอ"}), {{"blur", 17.5}}, "effects"),
        entry("sharpenStrong", QT_TRANSLATE_NOOP("EditPlanner", "Extra sharp"), L({u"extra sharp", u"คมพิเศษ"}), {{"sharpen", 2.7}}, "effects"),
        entry("sharpen", QT_TRANSLATE_NOOP("EditPlanner", "Sharpen"), L({u"sharpen", u"sharp", u"คมชัด", u"คม"}), {{"sharpen", 1.5}}, "effects"),
        entry("vignetteStrong", QT_TRANSLATE_NOOP("EditPlanner", "Dark edges"), L({u"dark edges", u"ขอบมืด"}), {{"vignette", 1}}, "effects"),
        entry("vignette", QT_TRANSLATE_NOOP("EditPlanner", "Vignette"), L({u"vignette", u"วิกเนตต์", u"วิกเน็ต"}), {{"vignette", 0.5}}, "effects"),
        entry("dream", QT_TRANSLATE_NOOP("EditPlanner", "Dream"), L({u"dream", u"ฝันหวาน"}), {{"blur", 10}, {"vignette", 0.35}}, "effects"),
        entry("softPortrait", QT_TRANSLATE_NOOP("EditPlanner", "Soft portrait"), L({u"soft portrait", u"พอร์ตเทรต"}), {{"blur", 5}, {"vignette", 0.25}}, "effects"),
        entry("focus", QT_TRANSLATE_NOOP("EditPlanner", "Focus"), L({u"focus", u"โฟกัส"}), {{"vignette", 0.7}, {"sharpen", 1.5}}, "effects"),
        entry("crisp", QT_TRANSLATE_NOOP("EditPlanner", "Crisp"), L({u"crisp", u"คมกริบ"}), {{"sharpen", 2.4}, {"vignette", 0.2}}, "effects"),
        entry("letterboxWide", QT_TRANSLATE_NOOP("EditPlanner", "Wide bars"), L({u"wide bars", u"แถบกว้าง"}), {{"cropT", 0.18}, {"cropB", 0.18}}, "effects"),
        entry("cinematicBars", QT_TRANSLATE_NOOP("EditPlanner", "Cinema bars"), L({u"cinema bars", u"แถบหนัง"}), {{"cropT", 0.12}, {"cropB", 0.12}, {"vignette", 0.4}}, "effects"),
        entry("letterbox", QT_TRANSLATE_NOOP("EditPlanner", "Letterbox"), L({u"letterbox", u"เลตเตอร์บ็อกซ์", u"แถบดำ"}), {{"cropT", 0.12}, {"cropB", 0.12}}, "effects"),
        entry("pillarbox", QT_TRANSLATE_NOOP("EditPlanner", "Side bars"), L({u"side bars", u"pillarbox", u"แถบข้าง"}), {{"cropL", 0.1}, {"cropR", 0.1}}, "effects"),
        entry("zoomBig", QT_TRANSLATE_NOOP("EditPlanner", "Zoom 160%"), L({u"zoom 160", u"ซูม 160", u"ซูมใหญ่"}), {{"scale", 1.6}, {"rotation", 0}}, ""),
        entry("zoomIn", QT_TRANSLATE_NOOP("EditPlanner", "Zoom 125%"), L({u"zoom 125", u"ซูม 125", u"zoom", u"ซูม"}), {{"scale", 1.25}, {"rotation", 0}}, ""),
        entry("tiltLeft", QT_TRANSLATE_NOOP("EditPlanner", "Tilt left"), L({u"tilt left", u"เอียงซ้าย"}), {{"scale", 1.12}, {"rotation", -5}}, ""),
        entry("tilt", QT_TRANSLATE_NOOP("EditPlanner", "Tilt right"), L({u"tilt", u"เอียง"}), {{"scale", 1.12}, {"rotation", 5}}, ""),
        entry("normalSize", QT_TRANSLATE_NOOP("EditPlanner", "Normal size"), L({u"normal size", u"ขนาดปกติ"}), {{"scale", 1}, {"rotation", 0}}, ""),
        entry("reset", QT_TRANSLATE_NOOP("EditPlanner", "Remove effects"), L({u"remove effects", u"no effect", u"ลบเอฟเฟกต์", u"ไม่มีเอฟเฟกต์", u"ล้างเอฟเฟกต์"}), {}, "effects"),
    };
    return list;
}

const QList<CatalogEntry>& transitionCatalog()
{
    static const QList<CatalogEntry> list = {
        entry("dissolve", QT_TRANSLATE_NOOP("EditPlanner", "Cross dissolve"), L({u"cross dissolve", u"dissolve", u"ละลาย", u"ค่อยๆ จาง"})),
        entry("dip", QT_TRANSLATE_NOOP("EditPlanner", "Dip to black"), L({u"dip to black", u"dip", u"จางผ่านสีดำ"})),
        entry("fade", QT_TRANSLATE_NOOP("EditPlanner", "Fade from black"), L({u"fade", u"เฟด", u"จางเข้า"})),
        entry("flash", QT_TRANSLATE_NOOP("EditPlanner", "Flash"), L({u"flash", u"แฟลช"})),
        entry("wipeLeft", QT_TRANSLATE_NOOP("EditPlanner", "Wipe left"), L({u"wipe left", u"เช็ดไปซ้าย"})),
        entry("wipeDown", QT_TRANSLATE_NOOP("EditPlanner", "Wipe down"), L({u"wipe down", u"เช็ดลง"})),
        entry("wipeUp", QT_TRANSLATE_NOOP("EditPlanner", "Wipe up"), L({u"wipe up", u"เช็ดขึ้น"})),
        entry("wipe", QT_TRANSLATE_NOOP("EditPlanner", "Wipe right"), L({u"wipe right", u"wipe", u"เช็ดไปขวา", u"เช็ด"})),
        entry("slideLeft", QT_TRANSLATE_NOOP("EditPlanner", "Slide from left"), L({u"slide from left", u"สไลด์จากซ้าย"})),
        entry("slideUp", QT_TRANSLATE_NOOP("EditPlanner", "Slide from bottom"), L({u"slide from bottom", u"สไลด์จากล่าง"})),
        entry("slideDown", QT_TRANSLATE_NOOP("EditPlanner", "Slide from top"), L({u"slide from top", u"สไลด์จากบน"})),
        entry("slide", QT_TRANSLATE_NOOP("EditPlanner", "Slide from right"), L({u"slide from right", u"slide", u"สไลด์จากขวา", u"สไลด์"})),
        entry("zoomOut", QT_TRANSLATE_NOOP("EditPlanner", "Zoom out"), L({u"zoom out", u"ซูมออก"})),
        entry("zoom", QT_TRANSLATE_NOOP("EditPlanner", "Zoom in"), L({u"zoom in", u"zoom", u"ซูมเข้า", u"ซูม"})),
        entry("spin", QT_TRANSLATE_NOOP("EditPlanner", "Spin"), L({u"spin", u"หมุน"})),
        entry("none", QT_TRANSLATE_NOOP("EditPlanner", "None"), L({u"no transition", u"ไม่มีทรานซิชัน", u"ลบทรานซิชัน"})),
    };
    return list;
}

const QList<CatalogEntry>& textStyleCatalog()
{
    static const QList<CatalogEntry> list = {
        entry("lowerThird", QT_TRANSLATE_NOOP("EditPlanner", "Lower third"), L({u"lower third", u"แถบชื่อ"})),
        entry("yellowSub", QT_TRANSLATE_NOOP("EditPlanner", "Subtitle"), L({u"subtitle", u"ซับไตเติล", u"ซับ"})),
        entry("caption", QT_TRANSLATE_NOOP("EditPlanner", "Caption"), L({u"caption", u"คำบรรยาย", u"แคปชัน"})),
        entry("cornerTag", QT_TRANSLATE_NOOP("EditPlanner", "Tag"), L({u"tag", u"ป้าย"})),
        entry("bigNumber", QT_TRANSLATE_NOOP("EditPlanner", "01"), L({u"big number", u"ตัวเลขใหญ่"})),
        entry("bigBold", QT_TRANSLATE_NOOP("EditPlanner", "BIG TEXT"), L({u"big text", u"big bold", u"ตัวใหญ่"})),
        entry("outline", QT_TRANSLATE_NOOP("EditPlanner", "Outline"), L({u"outline", u"เส้นขอบ"})),
        entry("impact", QT_TRANSLATE_NOOP("EditPlanner", "IMPACT"), L({u"impact", u"อิมแพกต์"})),
        entry("redAlert", QT_TRANSLATE_NOOP("EditPlanner", "ALERT"), L({u"alert", u"เตือนภัย"})),
        entry("neonBlue", QT_TRANSLATE_NOOP("EditPlanner", "Neon"), L({u"neon blue", u"นีออนฟ้า"})),
        entry("neonPink", QT_TRANSLATE_NOOP("EditPlanner", "Neon"), L({u"neon pink", u"neon", u"นีออนชมพู", u"นีออน"})),
        entry("gold", QT_TRANSLATE_NOOP("EditPlanner", "Gold"), L({u"gold", u"สีทอง", u"ทอง"})),
        entry("elegant", QT_TRANSLATE_NOOP("EditPlanner", "Elegant"), L({u"elegant", u"หรูหรา"})),
        entry("script", QT_TRANSLATE_NOOP("EditPlanner", "Handwriting"), L({u"handwriting", u"script", u"ลายมือ"})),
        entry("typewriter", QT_TRANSLATE_NOOP("EditPlanner", "Typewriter"), L({u"typewriter", u"พิมพ์ดีด"})),
        entry("title", QT_TRANSLATE_NOOP("EditPlanner", "Title"), L({u"title", u"ชื่อเรื่อง", u"หัวเรื่อง"})),
    };
    return list;
}

// ---- parsing ----------------------------------------------------------------------------------------------------------------------------

namespace {

bool isAscii(const QString& s)
{
    for (QChar c : s)
        if (c.unicode() > 127)
            return false;
    return true;
}

// Position of `alias` in the (lower case) clause, or -1. ASCII aliases must stand alone (so "dip" is not found in "ship").
int findAlias(const QString& clause, const QString& alias)
{
    int from = 0;
    while (true) {
        const int at = clause.indexOf(alias, from);
        if (at < 0)
            return -1;
        if (!isAscii(alias))
            return at;
        const bool leftOk = at == 0 || !clause.at(at - 1).isLetter() || clause.at(at - 1).unicode() > 127;
        const int end = at + alias.size();
        const bool rightOk = end >= clause.size() || !clause.at(end).isLetter() || clause.at(end).unicode() > 127;
        if (leftOk && rightOk)
            return at;
        from = at + 1;
    }
}

struct Hit {
    const CatalogEntry* entry = nullptr;
    int alias = 0;      // length of the alias that matched
    int at = -1;
};
Hit bestMatch(const QString& clause, const QList<CatalogEntry>& list)
{
    Hit best;
    for (const CatalogEntry& e : list)
        for (const QString& a : e.aliases) {
            const int at = findAlias(clause, a);
            if (at >= 0 && int(a.size()) > best.alias) {
                best.entry = &e;
                best.alias = int(a.size());
                best.at = at;
            }
        }
    return best;
}

bool hasAny(const QString& s, std::initializer_list<const char16_t*> words)
{
    for (const char16_t* w : words)
        if (findAlias(s, QString::fromUtf16(w)) >= 0)
            return true;
    return false;
}

double unitSeconds(const QString& unit)
{
    const QString u = unit.toLower();
    if (u.startsWith(QLatin1String("นาที")) || u.startsWith(QLatin1String("min")) || u == QLatin1String("m"))
        return 60.0;
    return 1.0;
}

// Times in the clause, in seconds. A bare number counts only with `bareIsSeconds` or after "at" / "ที่" / "ตอน".
QList<double> parseTimes(const QString& clause, bool bareIsSeconds)
{
    static const QRegularExpression clock(QStringLiteral(R"((\d+):(\d{1,2})(?::(\d{1,2}))?)"));
    static const QRegularExpression number(QStringLiteral(
        R"((\d+(?:\.\d+)?)\s*(นาที|minutes?|mins?|วินาที|วิ|seconds?|secs?|sec|s(?![a-z])|m(?![a-z]))?)"));
    QList<double> times;
    QString rest = clause;
    for (auto it = clock.globalMatch(clause); it.hasNext();) {
        const auto m = it.next();
        const double a = m.captured(1).toDouble(), b = m.captured(2).toDouble();
        times << (m.captured(3).isEmpty() ? a * 60 + b : (a * 60 + b) * 60 + m.captured(3).toDouble());
        rest.replace(m.capturedStart(), m.capturedLength(), QString(m.capturedLength(), QLatin1Char(' ')));
    }
    qsizetype prevEnd = -10;
    bool prevMinutes = false;
    for (auto it = number.globalMatch(rest); it.hasNext();) {
        const auto m = it.next();
        const bool hasUnit = !m.captured(2).isEmpty();
        const QString before = rest.left(m.capturedStart()).trimmed().toLower();
        const bool marked = before.endsWith(QLatin1String("ที่")) || before.endsWith(QLatin1String("at")) || before.endsWith(QLatin1String("ตอน"))
                            || before.endsWith(QLatin1String("@")) || before.endsWith(QLatin1String("|")) || before.endsWith(QLatin1String("วินาทีที่"));
        if (!hasUnit && !bareIsSeconds && !marked)
            continue;
        const double unit = hasUnit ? unitSeconds(m.captured(2)) : 1.0;
        const double value = m.captured(1).toDouble() * unit;
        if (prevMinutes && hasUnit && unit == 1.0 && m.capturedStart() - prevEnd <= 3 && !times.isEmpty())
            times.last() += value;               // "1 นาที 30 วินาที"
        else
            times << value;
        prevEnd = m.capturedEnd();
        prevMinutes = hasUnit && unit == 60.0;
    }
    return times;
}

// the clip a look / effect / transition applies to: 0 every video clip, n the n-th clip, -1 the selected clip
int targetClip(const QString& clause)
{
    static const QRegularExpression nth(QStringLiteral(R"((?:คลิปที่|คลิป|clip(?:\s*(?:number|no\.?|#))?)\s*(\d+))"), QRegularExpression::CaseInsensitiveOption);
    const auto m = nth.match(clause);
    if (m.hasMatch() && m.captured(1).toInt() > 0)
        return m.captured(1).toInt();
    if (hasAny(clause, {u"ที่เลือก", u"selected", u"this clip", u"คลิปนี้"}))
        return -1;
    return 0;
}

QString exportPreset(const QString& clause)
{
    if (clause.contains(QLatin1String("4k")) || clause.contains(QLatin1String("2160")))
        return QStringLiteral("4k");
    if (clause.contains(QLatin1String("1440")) || clause.contains(QLatin1String("2k")))
        return QStringLiteral("1440p");
    if (clause.contains(QLatin1String("1080")) || clause.contains(QLatin1String("full hd")) || clause.contains(QLatin1String("fhd")))
        return QStringLiteral("1080p");
    if (clause.contains(QLatin1String("720")))
        return QStringLiteral("720p");
    return {};
}

QString stripLeading(QString s, const QStringList& words)
{
    bool again = true;
    while (again) {
        again = false;
        s = s.trimmed();
        for (const QString& w : words)
            if (s.startsWith(w, Qt::CaseInsensitive) && (!isAscii(w) || w.endsWith(QLatin1Char(' ')) || s.size() == w.size() || !s.at(w.size()).isLetter())) {
                s = s.mid(w.size());
                again = true;
                break;
            }
    }
    return s.trimmed();
}

} // namespace

EditPlan parseEditPrompt(const QString& raw)
{
    EditPlan plan;
    QString text = raw;

    // quoted words are kept aside so that nothing inside them is read as a command
    QStringList quoted;
    static const QRegularExpression quotes(QStringLiteral(u"[\"“「『‘]([^\"“”「」『』‘’]+)[\"”」』’]"));
    for (auto m = quotes.match(text); m.hasMatch(); m = quotes.match(text)) {
        quoted << m.captured(1).trimmed();
        text.replace(m.capturedStart(), m.capturedLength(), QString(QChar(1)) + QString::number(quoted.size() - 1) + QString(QChar(1)));
    }
    text.replace(QLatin1String("black & white"), QLatin1String("mono"), Qt::CaseInsensitive);
    text.replace(QLatin1String("black and white"), QLatin1String("mono"), Qt::CaseInsensitive);
    text.replace(QLatin1String("teal and orange"), QLatin1String("tealorange"), Qt::CaseInsensitive);
    text.replace(QLatin1String("teal & orange"), QLatin1String("tealorange"), Qt::CaseInsensitive);

    // "ตัดที่ 5, 10 และ 15 วินาที": the separators between numbers stay inside one clause
    static const QRegularExpression numberList(QStringLiteral(R"((\d)\s*(?:,|และ|and)\s*(?=\d))"));
    text.replace(numberList, QStringLiteral("\\1|"));

    static const QRegularExpression splitter(QStringLiteral(
        R"([\n;,，、]+|\.(?=\s|$)|!+|\s+(?:and then|and|then|after that|also|next)\s+|(?:แล้วก็|แล้ว|จากนั้น|หลังจากนั้น|ต่อจากนั้น|และ|พร้อมทั้ง)|\s+(?=(?:ใส่|เพิ่ม|วาง|ตัด|แบ่ง|แยก|ส่งออก|สร้าง|เรียง|ปรับ|add|place|put|split|cut|export|apply|insert|create)(?![a-z])))"),
                                              QRegularExpression::CaseInsensitiveOption);
    const QStringList clauses = text.split(splitter, Qt::SkipEmptyParts);

    auto restore = [&](QString s) {
        static const QRegularExpression token(QStringLiteral("\x01(\\d+)\x01"));
        for (auto m = token.match(s); m.hasMatch(); m = token.match(s))
            s.replace(m.capturedStart(), m.capturedLength(), quoted.value(m.captured(1).toInt()));
        return s;
    };

    for (const QString& rawClause : clauses) {
        const QString original = rawClause.trimmed();          // keeps the case (unquoted text goes on the screen as typed)
        const QString clause = original.toLower();
        if (clause.isEmpty())
            continue;
        const QString shown = restore(clause);
        EditStep step;

        // ---- export ----
        if (hasAny(clause, {u"ส่งออก", u"export", u"render", u"เรนเดอร์", u"บันทึกเป็นวิดีโอ"})) {
            step.kind = EditStep::Export;
            step.preset = exportPreset(clause);
            plan.steps << step;
            continue;
        }

        // ---- place every clip ----
        const bool placeVerb = hasAny(clause, {u"วาง", u"ใส่", u"ต่อ", u"เรียง", u"จัด", u"นำ", u"ประกอบ", u"place", u"add", u"put", u"arrange", u"assemble", u"lay", u"build"});
        const bool allWord = hasAny(clause, {u"ทั้งหมด", u"ทุกคลิป", u"ทุกไฟล์", u"ตามลำดับ", u"all", u"in order", u"everything", u"every clip", u"every file"});
        const bool mediaWord = hasAny(clause, {u"คลิป", u"สื่อ", u"ไฟล์", u"วิดีโอ", u"ภาพ", u"clip", u"clips", u"media", u"file", u"files", u"footage", u"video", u"videos", u"timeline", u"ไทม์ไลน์"});
        if ((placeVerb && allWord && mediaWord) || hasAny(clause, {u"สร้างไทม์ไลน์", u"build the timeline", u"rough cut", u"assemble"})) {
            step.kind = EditStep::PlaceAll;
            plan.steps << step;
            continue;
        }

        // ---- split ----
        const bool splitVerb = hasAny(clause, {u"ตัด", u"แบ่ง", u"แยก", u"split", u"cut", u"slice"});
        static const QRegularExpression every(QStringLiteral(R"((?:ทุกๆ|ทุก|every)\s*(\d+(?:\.\d+)?)\s*(นาที|minutes?|mins?|min|วินาที|วิ|seconds?|secs?|sec|s(?![a-z]))?)"));
        if (splitVerb && !clause.contains(QChar(1))) {
            const auto em = every.match(clause);
            if (em.hasMatch()) {
                step.kind = EditStep::SplitEvery;
                step.every = em.captured(1).toDouble() * (em.captured(2).isEmpty() ? 1.0 : unitSeconds(em.captured(2)));
                if (step.every >= 0.1) {
                    plan.steps << step;
                    continue;
                }
            }
            const QList<double> times = parseTimes(clause, true);
            if (!times.isEmpty()) {
                for (double t : times) {
                    EditStep s;
                    s.kind = EditStep::SplitAt;
                    s.seconds = t;
                    plan.steps << s;
                }
                continue;
            }
        }

        // ---- text ----
        const bool textWord = hasAny(clause, {u"ข้อความ", u"ตัวหนังสือ", u"ไตเติล", u"หัวเรื่อง", u"ชื่อเรื่อง", u"แคปชัน", u"คำบรรยาย", u"แถบชื่อ",
                                              u"text", u"title", u"caption", u"lower third", u"subtitle"});
        if (textWord || (clause.contains(QChar(1)) && placeVerb)) {
            step.kind = EditStep::AddText;
            const Hit style = bestMatch(clause, textStyleCatalog());
            step.id = style.entry ? style.entry->id : QStringLiteral("title");
            const QList<double> times = parseTimes(clause, false);
            step.seconds = times.isEmpty() ? -1 : times.first();
            if (clause.contains(QChar(1))) {
                static const QRegularExpression token(QStringLiteral("\x01(\\d+)\x01"));
                step.text = quoted.value(token.match(clause).captured(1).toInt());
            } else {
                QString content = original;
                static const QRegularExpression timePhrase(QStringLiteral(
                    R"((?:ที่|ตอน|at|@)?\s*\d+(?::\d+)*(?:\.\d+)?\s*(?:นาที|minutes?|mins?|min|วินาที|วิ|seconds?|secs?|sec|s)\b?)"),
                    QRegularExpression::CaseInsensitiveOption);
                content.remove(timePhrase);
                const QStringList noise = {QStringLiteral("ใส่"), QStringLiteral("เพิ่ม"), QStringLiteral("สร้าง"), QStringLiteral("เขียน"), QStringLiteral("แสดง"),
                                                 QStringLiteral("พิมพ์"), QStringLiteral("add"), QStringLiteral("insert"), QStringLiteral("put"), QStringLiteral("create"),
                                                 QStringLiteral("write"), QStringLiteral("show"), QStringLiteral("type"), QStringLiteral("a "), QStringLiteral("the "),
                                                 QStringLiteral("ข้อความ"), QStringLiteral("ตัวหนังสือ"), QStringLiteral("ไตเติล"), QStringLiteral("หัวเรื่อง"),
                                                 QStringLiteral("ชื่อเรื่อง"), QStringLiteral("แคปชัน"), QStringLiteral("text"), QStringLiteral("title"), QStringLiteral("caption"),
                                                 QStringLiteral("ว่า"), QStringLiteral("saying"), QStringLiteral("says"), QStringLiteral("that says"), QStringLiteral("with"),
                                                 QStringLiteral("ที่"), QStringLiteral("ตอน"), QStringLiteral("at")};
                for (bool again = true; again;) {          // verbs, the word "text" and the style name come in any order before the words
                    again = false;
                    const QString before = content;
                    content = stripLeading(content, noise);
                    if (style.entry)
                        for (const QString& a : style.entry->aliases)
                            if (content.startsWith(a, Qt::CaseInsensitive)) {
                                content = content.mid(a.size()).trimmed();
                                break;
                            }
                    again = content != before;
                }
                step.text = content;
            }
            plan.steps << step;
            continue;
        }

        // ---- transition / look / effect (a keyword names the kind; otherwise the name decides) ----
        const bool transitionWord = hasAny(clause, {u"ทรานซิชัน", u"transition", u"transitions"});
        const bool lookWord = hasAny(clause, {u"ฟิลเตอร์", u"filter", u"โทนสี", u"โทน", u"look", u"color grade", u"สีภาพ"});
        const bool effectWord = hasAny(clause, {u"เอฟเฟกต์", u"เอฟเฟค", u"เอฟเฟ็กต์", u"effect", u"effects"});
        Hit hit;
        enum { None, Look, Effect, Transition } kind = None;
        if (transitionWord) {
            hit = bestMatch(clause, transitionCatalog());
            kind = Transition;
        } else if (lookWord) {
            hit = bestMatch(clause, lookCatalog());
            kind = Look;
        } else if (effectWord) {
            hit = bestMatch(clause, effectCatalog());
            kind = Effect;
        } else {
            const Hit l = bestMatch(clause, lookCatalog());
            const Hit e = bestMatch(clause, effectCatalog());
            const Hit t = bestMatch(clause, transitionCatalog());
            hit = l;
            kind = l.entry ? Look : None;
            if (e.entry && e.alias > hit.alias) { hit = e; kind = Effect; }
            if (t.entry && t.alias > hit.alias) { hit = t; kind = Transition; }
        }
        if (kind == Transition && !hit.entry) {          // "ใส่ทรานซิชัน" alone: the plain cross dissolve
            step.kind = EditStep::Transition;
            step.id = QStringLiteral("dissolve");
            step.direction = QStringLiteral("in");
            step.clipNumber = targetClip(clause);
            plan.steps << step;
            continue;
        }
        if (kind != None && hit.entry) {
            step.id = hit.entry->id;
            step.clipNumber = targetClip(clause);
            if (kind == Transition) {
                step.kind = EditStep::Transition;
                step.direction = hasAny(clause, {u"ท้ายคลิป", u"ตอนจบ", u"ขาออก", u"end of", u"at the end", u"out"}) ? QStringLiteral("out") : QStringLiteral("in");
                static const QStringList outTypes = {"fade", "dip", "wipe", "slide", "zoom", "spin", "flash", "none"};
                if (step.direction == QLatin1String("out") && !outTypes.contains(step.id)) {
                    plan.unknown << shown;
                    continue;
                }
            } else {
                step.kind = kind == Look ? EditStep::Look : EditStep::Effect;
            }
            plan.steps << step;
            continue;
        }
        plan.unknown << shown;
    }
    return plan;
}

} // namespace dv::ai
