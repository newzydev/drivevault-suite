#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "SpeechReader.h"

#include <QTimer>

#include <windows.h>
#include <sapi.h>

namespace dp {

SpeechReader::SpeechReader(QObject* parent)
    : QObject(parent)
{
    // Qt starts COM on the GUI thread (a single threaded apartment), which is what SAPI needs.
    ISpVoice* voice = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, reinterpret_cast<void**>(&voice))))
        m_voice = voice;
    m_timer = new QTimer(this);
    m_timer->setInterval(250);
    connect(m_timer, &QTimer::timeout, this, [this] { poll(); });
}

SpeechReader::~SpeechReader()
{
    if (auto* v = static_cast<ISpVoice*>(m_voice)) {
        v->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
        v->Release();
    }
}

bool SpeechReader::speak(const QString& text)
{
    auto* v = static_cast<ISpVoice*>(m_voice);
    const QString clean = text.simplified();
    if (!v || clean.isEmpty())
        return false;
    const std::wstring w = clean.toStdWString();
    if (FAILED(v->Speak(w.c_str(), SPF_ASYNC | SPF_PURGEBEFORESPEAK | SPF_IS_NOT_XML, nullptr)))
        return false;
    if (!m_speaking) {
        m_speaking = true;
        emit speakingChanged();
    }
    m_timer->start();
    return true;
}

void SpeechReader::stop()
{
    if (auto* v = static_cast<ISpVoice*>(m_voice))
        v->Speak(nullptr, SPF_PURGEBEFORESPEAK, nullptr);
    poll();
}

void SpeechReader::poll()
{
    bool running = false;
    if (auto* v = static_cast<ISpVoice*>(m_voice)) {
        SPVOICESTATUS status{};
        if (SUCCEEDED(v->GetStatus(&status, nullptr)))
            running = status.dwRunningState == SPRS_IS_SPEAKING;
    }
    if (running != m_speaking) {
        m_speaking = running;
        emit speakingChanged();
    }
    if (!running)
        m_timer->stop();
}

} // namespace dp
