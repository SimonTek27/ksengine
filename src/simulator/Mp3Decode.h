#pragma once
/**
 * mp3 -> PCM for the content that ships as mp3 (showroom ambience and music).
 *
 * The mixer only ever sees interleaved float PCM, so the decode happens once,
 * synchronously, at load time on the thread that opens the showroom. That is
 * deliberate: Media Foundation is not something to call from the render
 * callback, and a showroom track is a few megabytes at most.
 *
 * Windows only, like the rest of the output layer (WASAPI). A failure here is
 * never fatal: the caller reports why the sound is silent and keeps the room.
 */
#include <string>
#include <vector>

namespace ks {
namespace sim {

struct DecodedAudio {
    std::vector<float> samples; // interleaved, -1..1
    int sampleRate = 0;
    int channels = 0;

    bool valid() const { return !samples.empty() && sampleRate > 0 && channels > 0; }
};

/** Decode `path` (anything Media Foundation reads) to float PCM. Returns false
 *  and fills `error` when the file cannot be opened or holds no audio. */
bool decodeMp3ToPcm(const std::string& path, DecodedAudio& out, std::string* error = nullptr);

} // namespace sim
} // namespace ks
