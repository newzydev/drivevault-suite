#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

namespace dv::ai {

// One thing the "edit by prompt" feature will do. The planner never touches the project: it only turns a Thai / English sentence into these steps;
// AiController::applyEditPrompt carries them out through the normal undoable timeline operations (one undo step for the whole prompt).
struct EditStep {
    enum Kind { PlaceAll, SplitAt, SplitEvery, AddText, Look, Effect, Transition, Export };
    Kind kind = PlaceAll;
    QString id;             // catalog id: look / effect / transition / text style
    QString text;           // AddText: the words ("" = keep the style's own sample text)
    double seconds = -1;    // SplitAt / AddText: position in seconds (-1 = not given: AddText uses 0)
    double every = 0;       // SplitEvery: interval in seconds
    int clipNumber = 0;     // Look / Effect / Transition target: 0 = every video clip, n = the n-th video clip by start, -1 = the selected clip
    QString direction;      // Transition: "in" | "out"
    QString preset;         // Export: "720p" | "1080p" | "1440p" | "4k" | "" (the export setting)
};

struct EditPlan {
    QList<EditStep> steps;
    QStringList unknown;    // parts of the prompt that were not understood (shown to the user, never guessed)
};

// A look / effect / transition / text style that a prompt can name. Looks are fractions of the way from the default to the end of the range (like the
// tiles of the Filters tab); effects are real parameter values.
struct CatalogEntry {
    QString id;
    QString label;                                // English name (translate with QCoreApplication::translate("EditPlanner", ...))
    QStringList aliases;                          // lower case, Thai or English
    QList<QPair<QString, double>> values;         // parameter key -> value
    QString resetGroup;                           // effects / looks: the group that is reset first ("" = nothing)
};

const QList<CatalogEntry>& lookCatalog();
const QList<CatalogEntry>& effectCatalog();
const QList<CatalogEntry>& transitionCatalog();
const QList<CatalogEntry>& textStyleCatalog();

// Deterministic keyword interpreter - not a language model. Understands several requests in one prompt (separated by commas, "and" / "และ",
// "then" / "แล้ว" ...), e.g. "วางคลิปทั้งหมด แล้วใส่ฟิลเตอร์ขาวดำ ใส่ทรานซิชันสไลด์ ใส่ข้อความ \"สวัสดี\" ที่ 3 วินาที แล้วส่งออก 1080p".
EditPlan parseEditPrompt(const QString& text);

} // namespace dv::ai
