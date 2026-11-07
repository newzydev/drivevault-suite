#pragma once

#include <QString>
#include <QVariantMap>

namespace dv::ai {

// A request the assistant understood. Only these typed commands exist - the assistant never runs shell commands
// or touches files; every command is carried out by the AI services and edits go through the undoable timeline.
struct Command {
    enum Type { None, Transcribe, DetectSilence, SuggestCuts, EnhanceAudio, TrackObject, Help };
    Type type = None;
    QString language;          // Transcribe: "th" | "en" | "" (use the setting)
    QString summary;           // what will happen, shown before it runs (not translated here)
    bool valid() const { return type != None; }
    static QString typeName(Type t);
};

// Keyword based command interpreter for Thai and English requests. It is a deterministic rule set, not a language model:
// anything it does not recognise returns Type None.
Command parseCommand(const QString& text);

} // namespace dv::ai
