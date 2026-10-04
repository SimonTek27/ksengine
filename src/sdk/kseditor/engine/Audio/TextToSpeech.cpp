#include "TextToSpeech.h"

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sapi.h>
#include <sphelper.h>
#include <objbase.h>
#include <qdebug.h>
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "sapi.lib")

#ifndef SPF_PURGEBEFORE
#define SPF_PURGEBEFORE 2
#endif
#endif

namespace ks {
namespace audio {

TextToSpeech::TextToSpeech(QObject* parent)
    : QObject(parent)
    , m_voice(nullptr)
    , m_isSpeaking(false)
    , m_volume(100)
    , m_rate(0)
{
    initSapi();
}

TextToSpeech::~TextToSpeech()
{
    shutdownSapi();
}

bool TextToSpeech::initSapi()
{
#ifdef Q_OS_WIN
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        qWarning("TextToSpeech: CoInitializeEx failed (0x%08lx)", hr);
        return false;
    }

    hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice, (void**)&m_voice);
    if (FAILED(hr) || !m_voice) {
        qWarning("TextToSpeech: CoCreateInstance SpVoice failed (0x%08lx)", hr);
        return false;
    }

    ISpObjectToken* cpToken = nullptr;
    IEnumSpObjectTokens* cpEnum = nullptr;
    hr = SpEnumTokens(SPCAT_VOICES, nullptr, nullptr, &cpEnum);
    if (SUCCEEDED(hr) && cpEnum) {
        ULONG count = 0;
        cpEnum->GetCount(&count);
        for (ULONG i = 0; i < count; ++i) {
            hr = cpEnum->Next(1, &cpToken, nullptr);
            if (SUCCEEDED(hr) && cpToken) {
                WCHAR* desc = nullptr;
                cpToken->GetStringValue(L"", &desc);
                if (desc) {
                    m_voiceNames.append(QString::fromWCharArray(desc));
                    CoTaskMemFree(desc);
                }
                cpToken->Release();
                cpToken = nullptr;
            }
        }
        cpEnum->Release();
    }

    ISpObjectToken* defaultToken = nullptr;
    hr = m_voice->GetVoice(&defaultToken);
    if (SUCCEEDED(hr) && defaultToken) {
        WCHAR* desc = nullptr;
        defaultToken->GetStringValue(L"", &desc);
        if (desc) {
            m_currentVoiceName = QString::fromWCharArray(desc);
            CoTaskMemFree(desc);
        }
        defaultToken->Release();
    }

    m_voice->SetVolume((USHORT)m_volume);
    m_voice->SetRate(m_rate);
    return true;
#else
    return false;
#endif
}

void TextToSpeech::shutdownSapi()
{
#ifdef Q_OS_WIN
    stop();
    if (m_voice) {
        m_voice->Release();
        m_voice = nullptr;
    }
#endif
}

bool TextToSpeech::isSpeaking() const
{
    return m_isSpeaking;
}

QStringList TextToSpeech::availableVoices() const
{
    return m_voiceNames;
}

QString TextToSpeech::currentVoice() const
{
    return m_currentVoiceName;
}

int TextToSpeech::volume() const
{
    return m_volume;
}

int TextToSpeech::rate() const
{
    return m_rate;
}

void TextToSpeech::speak(const QString& text)
{
#ifdef Q_OS_WIN
    if (!m_voice || text.isEmpty()) return;
    stop();
    std::wstring wtext = text.toStdWString();
    HRESULT hr = m_voice->Speak(wtext.c_str(), SPF_ASYNC | SPF_PURGEBEFORE, nullptr);
    if (SUCCEEDED(hr)) {
        m_isSpeaking = true;
        emit started();
    }
#endif
}

void TextToSpeech::speakAsync(const QString& text)
{
    speak(text);
}

void TextToSpeech::stop()
{
#ifdef Q_OS_WIN
    if (m_voice) {
        m_voice->Speak(nullptr, SPF_PURGEBEFORE, nullptr);
        m_voice->SetNotifyWin32Event();
    }
#endif
    if (m_isSpeaking) {
        m_isSpeaking = false;
        emit finished();
    }
}

void TextToSpeech::pause()
{
#ifdef Q_OS_WIN
    if (m_voice) {
        m_voice->Pause();
        emit paused();
    }
#endif
}

void TextToSpeech::resume()
{
#ifdef Q_OS_WIN
    if (m_voice) {
        m_voice->Resume();
        emit resumed();
    }
#endif
}

void TextToSpeech::setVoice(const QString& name)
{
#ifdef Q_OS_WIN
    if (!m_voice || name.isEmpty()) return;
    IEnumSpObjectTokens* cpEnum = nullptr;
    HRESULT hr = SpEnumTokens(SPCAT_VOICES, nullptr, nullptr, &cpEnum);
    if (FAILED(hr) || !cpEnum) return;
    ISpObjectToken* cpToken = nullptr;
    ULONG fetched = 0;
    while (SUCCEEDED(cpEnum->Next(1, &cpToken, &fetched)) && fetched > 0) {
        WCHAR* desc = nullptr;
        cpToken->GetStringValue(L"", &desc);
        if (desc) {
            QString voiceName = QString::fromWCharArray(desc);
            CoTaskMemFree(desc);
            if (voiceName == name) {
                m_voice->SetVoice(cpToken);
                m_currentVoiceName = name;
                cpToken->Release();
                break;
            }
        }
        cpToken->Release();
        cpToken = nullptr;
    }
    cpEnum->Release();
#endif
}

void TextToSpeech::setVolume(int percent)
{
    m_volume = qBound(0, percent, 100);
#ifdef Q_OS_WIN
    if (m_voice) m_voice->SetVolume((USHORT)m_volume);
#endif
}

void TextToSpeech::setRate(int rate)
{
    m_rate = qBound(-10, rate, 10);
#ifdef Q_OS_WIN
    if (m_voice) m_voice->SetRate(m_rate);
#endif
}

bool TextToSpeech::saveToWav(const QString& text, const QString& filePath)
{
#ifdef Q_OS_WIN
    if (!m_voice || text.isEmpty() || filePath.isEmpty()) return false;
    ISpStream* cpStream = nullptr;
    ISpStreamFormat* cpOldFormat = nullptr;
    HRESULT hr = m_voice->GetOutputStream(&cpOldFormat);
    if (FAILED(hr)) return false;
    std::wstring wpath = filePath.toStdWString();
    hr = SPBindToFile(wpath.c_str(), SPFM_CREATE_ALWAYS, &cpStream);
    if (FAILED(hr)) { cpOldFormat->Release(); return false; }
    hr = m_voice->SetOutput(cpStream, TRUE);
    if (FAILED(hr)) { cpStream->Release(); cpOldFormat->Release(); return false; }
    std::wstring wtext = text.toStdWString();
    hr = m_voice->Speak(wtext.c_str(), SPF_DEFAULT, nullptr);
    m_voice->SetOutput(cpOldFormat, FALSE);
    cpStream->Close();
    cpStream->Release();
    cpOldFormat->Release();
    return SUCCEEDED(hr);
#else
    return false;
#endif
}

} // namespace audio
} // namespace ks
