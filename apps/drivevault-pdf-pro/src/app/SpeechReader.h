#pragma once

#include <QObject>
#include <QString>

class QTimer;

namespace dp {

// Reads text aloud with the speech engine of Windows (SAPI) and the voice that is set in Windows. Thai needs a Thai voice installed in Windows
// (Settings > Time & language > Speech); without it the default voice may not read Thai. QML singleton "Speech".
class SpeechReader : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT FINAL)
    Q_PROPERTY(bool speaking READ speaking NOTIFY speakingChanged FINAL)
public:
    explicit SpeechReader(QObject* parent = nullptr);
    ~SpeechReader() override;
    bool available() const { return m_voice != nullptr; }
    bool speaking() const { return m_speaking; }
    Q_INVOKABLE bool speak(const QString& text);       // starts reading (replaces what is being read); false when there is no voice or no text
    Q_INVOKABLE void stop();
signals:
    void speakingChanged();
private:
    void poll();
    void* m_voice = nullptr;                           // ISpVoice*
    QTimer* m_timer = nullptr;
    bool m_speaking = false;
};

} // namespace dp
