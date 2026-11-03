#include "Mp3Decode.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <cstdint>
#include <cstdio>

namespace ks {
namespace sim {
namespace {

std::wstring widen(const std::string& text) {
    if (text.empty()) return std::wstring();
    const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (needed <= 1) return std::wstring();
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    if (MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wide[0], needed) == 0)
        return std::wstring();
    wide.resize(static_cast<size_t>(needed - 1));
    return wide;
}

template <typename T>
void release(T*& pointer) {
    if (pointer != nullptr) {
        pointer->Release();
        pointer = nullptr;
    }
}

} // namespace

bool decodeMp3ToPcm(const std::string& path, DecodedAudio& out, std::string* error) {
    out.samples.clear();
    out.sampleRate = 0;
    out.channels = 0;

    // The showroom opens on the main thread, which already initialised COM for
    // WASAPI; RPC_E_CHANGED_MODE means somebody else owns the apartment, which
    // is fine as long as we do not uninitialise it on the way out.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comOwned = SUCCEEDED(com);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE) {
        if (error) *error = "CoInitializeEx failed";
        return false;
    }
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET))) {
        if (error) *error = "Media Foundation is not available";
        if (comOwned) CoUninitialize();
        return false;
    }

    bool ok = false;
    IMFSourceReader* reader = nullptr;
    IMFMediaType* wanted = nullptr;
    do {
        const std::wstring wide = widen(path);
        if (wide.empty()) {
            if (error) *error = "empty path";
            break;
        }
        if (FAILED(MFCreateSourceReaderFromURL(wide.c_str(), nullptr, &reader))) {
            if (error) *error = "Media Foundation cannot open the file";
            break;
        }
        // Ask for the simplest thing we can mix: plain PCM in the file's own
        // rate and channel count, no resampling games.
        if (FAILED(MFCreateMediaType(&wanted))) break;
        wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        wanted->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        if (FAILED(reader->SetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, wanted))) {
            if (error) *error = "this stream cannot be turned into PCM";
            break;
        }

        IMFMediaType* current = nullptr;
        if (FAILED(reader->GetCurrentMediaType(
                static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &current)))
            break;
        UINT32 rate = 0, channels = 0, bits = 0;
        current->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
        current->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
        current->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bits);
        release(current);
        if (rate == 0 || channels == 0 || (bits != 16 && bits != 32)) {
            if (error) *error = "unexpected PCM layout";
            break;
        }
        out.sampleRate = static_cast<int>(rate);
        out.channels = static_cast<int>(channels);

        bool endOfStream = false;
        while (!endOfStream) {
            DWORD flags = 0;
            LONGLONG stamp = 0;
            IMFSample* sample = nullptr;
            if (FAILED(reader->ReadSample(
                    static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags,
                    &stamp, &sample))) {
                if (error) *error = "ReadSample failed";
                break;
            }
            if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) endOfStream = true;
            if (sample == nullptr) continue;

            IMFMediaBuffer* buffer = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
                BYTE* data = nullptr;
                DWORD capacity = 0, length = 0;
                if (SUCCEEDED(buffer->Lock(&data, &capacity, &length))) {
                    if (bits == 16) {
                        const auto* pcm = reinterpret_cast<const std::int16_t*>(data);
                        const size_t count = length / sizeof(std::int16_t);
                        out.samples.reserve(out.samples.size() + count);
                        for (size_t i = 0; i < count; ++i)
                            out.samples.push_back(static_cast<float>(pcm[i]) / 32768.0f);
                    } else {
                        const auto* pcm = reinterpret_cast<const float*>(data);
                        const size_t count = length / sizeof(float);
                        out.samples.insert(out.samples.end(), pcm, pcm + count);
                    }
                    buffer->Unlock();
                }
                release(buffer);
            }
            release(sample);
        }

        ok = out.valid();
        if (!ok && error) *error = "the stream decoded to no samples";
    } while (false);

    release(wanted);
    release(reader);
    MFShutdown();
    if (comOwned) CoUninitialize();
    return ok;
}

} // namespace sim
} // namespace ks
