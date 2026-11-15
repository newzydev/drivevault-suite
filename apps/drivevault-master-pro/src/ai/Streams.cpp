#include "Streams.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace dm::ai {

QList<SseEvent> SseParser::feed(const QByteArray& bytes)
{
    m_buffer.append(bytes);
    m_buffer.replace("\r\n", "\n");
    QList<SseEvent> out;
    for (;;) {
        const int end = m_buffer.indexOf("\n\n");
        if (end < 0)
            break;
        const QByteArray block = m_buffer.left(end);
        m_buffer.remove(0, end + 2);
        SseEvent event;
        QStringList data;
        for (const QByteArray& line : block.split('\n')) {
            if (line.startsWith(':'))
                continue;                                   // a comment / keep-alive
            if (line.startsWith("event:"))
                event.event = QString::fromUtf8(line.mid(6)).trimmed();
            else if (line.startsWith("data:"))
                data << QString::fromUtf8(line.mid(5)).trimmed();
        }
        if (data.isEmpty() && event.event.isEmpty())
            continue;
        event.data = data.join(QLatin1Char('\n'));
        out << event;
    }
    return out;
}

namespace {

QString errorText(const QJsonObject& root)
{
    const QJsonValue e = root.value(QLatin1String("error"));
    if (e.isObject())
        return e.toObject().value(QLatin1String("message")).toString(QStringLiteral("error"));
    if (e.isString())
        return e.toString();
    return {};
}

} // namespace

StreamPiece readOpenAiEvent(const SseEvent& event)
{
    StreamPiece piece;
    if (event.data == QLatin1String("[DONE]")) {
        piece.done = true;
        return piece;
    }
    const QJsonObject root = QJsonDocument::fromJson(event.data.toUtf8()).object();
    piece.error = errorText(root);
    if (!piece.error.isEmpty())
        return piece;
    const QJsonObject choice = root.value(QLatin1String("choices")).toArray().first().toObject();
    piece.text = choice.value(QLatin1String("delta")).toObject().value(QLatin1String("content")).toString();
    return piece;
}

StreamPiece readAnthropicEvent(const SseEvent& event)
{
    StreamPiece piece;
    const QJsonObject root = QJsonDocument::fromJson(event.data.toUtf8()).object();
    const QString type = event.event.isEmpty() ? root.value(QLatin1String("type")).toString() : event.event;
    if (type == QLatin1String("error")) {
        piece.error = errorText(root);
        if (piece.error.isEmpty())
            piece.error = QStringLiteral("error");
    } else if (type == QLatin1String("content_block_delta")) {
        const QJsonObject delta = root.value(QLatin1String("delta")).toObject();
        if (delta.value(QLatin1String("type")).toString() == QLatin1String("text_delta"))
            piece.text = delta.value(QLatin1String("text")).toString();
    } else if (type == QLatin1String("message_stop")) {
        piece.done = true;
    }
    return piece;
}

} // namespace dm::ai
