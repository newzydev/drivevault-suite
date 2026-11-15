#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace dm::ai {

struct SseEvent {
    QString event;       // the "event:" line ("" when there is none)
    QString data;        // the "data:" lines joined with \n
};

// Server-sent events ("event: x\ndata: {...}\n\n"): feed the bytes as they arrive, get the complete events. Pure, tested without a network.
class SseParser {
public:
    QList<SseEvent> feed(const QByteArray& bytes);

private:
    QByteArray m_buffer;
};

// What one event of a streaming reply means: some text, the end of the answer, or an error.
struct StreamPiece {
    QString text;
    bool done = false;
    QString error;
};

StreamPiece readOpenAiEvent(const SseEvent& event);       // OpenAI style: choices[0].delta.content, "[DONE]", {"error": {...}}
StreamPiece readAnthropicEvent(const SseEvent& event);    // Anthropic: content_block_delta / text_delta, message_stop, error

} // namespace dm::ai
