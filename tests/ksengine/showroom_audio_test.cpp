/**
 * Showroom audio, measured instead of listened to.
 *
 * The showroom plays two tracks at once — room tone and menu music — with the
 * volumes coming from showroom.json (0.6 and 0.5 in the shipped hall). Three
 * things about that path can only be checked in numbers:
 *
 *  - the two tracks must not sum past full scale. 0.6 + 0.5 is 1.1, and the
 *    mixer clipped exactly that, which is why the pair sounded wrong rather
 *    than loud;
 *  - the loop seam must not click. A file that was not authored to loop does
 *    not meet itself, and the step between its last and first sample is a
 *    click at every wrap — and one at start, which is the noise the showroom
 *    opened on;
 *  - a track whose header names no rate must not freeze into a steady tone.
 *
 * The test writes two small wavs (48 kHz stereo and 44.1 kHz mono, so the
 * mixer resamples one of them), loads them through the same entry point the
 * loop uses, renders offline and measures the result. No output device is
 * opened: initialize(false) keeps this runnable on a machine with no sound.
 */
#include "KsTest.h"
#include "simulator/ShowroomConfig.h"
#include "simulator/SimulatorAudio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace {

constexpr int kRate = 44100;

void writeWav(const std::filesystem::path& path, int rate, int channels,
              float frequency, float seconds) {
    const int frames = static_cast<int>(static_cast<float>(rate) * seconds);
    std::vector<std::int16_t> pcm(static_cast<std::size_t>(frames) * channels);
    for (int i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(rate);
        const auto v = static_cast<std::int16_t>(0.5f * 32767.f *
                                                 std::sin(6.2831853f * frequency * t));
        for (int c = 0; c < channels; ++c)
            pcm[static_cast<std::size_t>(i) * channels + c] = v;
    }
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(pcm.size() * 2);
    std::FILE* f = std::fopen(path.string().c_str(), "wb");
    KS_CHECK(f != nullptr);
    if (f == nullptr) return;
    const auto u32 = [&](std::uint32_t v) { std::fwrite(&v, 4, 1, f); };
    const auto u16 = [&](std::uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    u32(16);
    u16(1); // PCM
    u16(static_cast<std::uint16_t>(channels));
    u32(static_cast<std::uint32_t>(rate));
    u32(static_cast<std::uint32_t>(rate * channels * 2));
    u16(static_cast<std::uint16_t>(channels * 2));
    u16(16);
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);
    std::fwrite(pcm.data(), 2, pcm.size(), f);
    std::fclose(f);
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "ksim_showroom_audio_test";
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path ambiencePath = dir / "ambience.wav";
    const fs::path musicPath = dir / "music_00.wav";
    writeWav(ambiencePath, 48000, 2, 220.f, 0.30f); // stereo, needs a resample
    writeWav(musicPath, 44100, 1, 330.f, 0.20f);    // mono, needs an upmix

    ks::sim::SimulatorAudio audio;
    KS_CHECK(audio.initialize(false)); // mixer and synth, no output device

    ks::sim::ShowroomAudioTrack ambience;
    ambience.baseName = "ambience";
    ambience.path = ambiencePath.string();
    ambience.format = ks::sim::ShowroomAudioFormat::Wav;
    ambience.volume = 0.6f;
    ambience.loop = true;

    ks::sim::ShowroomAudioTrack music;
    music.baseName = "music_00";
    music.path = musicPath.string();
    music.format = ks::sim::ShowroomAudioFormat::Wav;
    music.volume = 0.5f;
    music.loop = true;

    KS_CHECK(audio.loadShowroomAudio(ambience, music));

    // One and a half seconds, rendered in blocks: the mixer never fills more
    // than its own channel buffer, so a single huge call would leave the tail
    // silent and the loop untested.
    const int total = kRate * 3 / 2;
    std::vector<float> out(static_cast<std::size_t>(total) * 2, 0.f);
    for (int done = 0; done < total; done += 256) {
        const int block = std::min(256, total - done);
        audio.renderOffline(out.data() + static_cast<std::size_t>(done) * 2, block, 2, kRate);
    }

    float peak = 0.f;
    double energy = 0.0;
    for (float v : out) {
        peak = std::max(peak, std::fabs(v));
        energy += static_cast<double>(v) * static_cast<double>(v);
    }
    const double rms = std::sqrt(energy / static_cast<double>(out.size()));

    // 1. Something was mixed, and it did not clip: two tracks at 0.6 and 0.5
    //    are 1.1 summed, which used to run the output into the ceiling.
    KS_CHECK(peak > 0.01f);
    KS_CHECK(peak <= 1.0f);
    KS_CHECK(rms > 0.002);

    // 2. The first sample is silent: the loop ramps in instead of starting on
    //    whatever value the file happens to hold.
    KS_CHECK(std::fabs(out[0]) < 0.01f);

    // 3. No step anywhere in the render, loop seam included: a click is a jump
    //    far above one sample of a 330 Hz sine at this rate.
    float biggestStep = 0.f;
    for (std::size_t i = 1; i < out.size(); ++i)
        biggestStep = std::max(biggestStep, std::fabs(out[i] - out[i - 1]));
    KS_CHECK(biggestStep < 0.10f);

    // 4. Still playing at the end: both tracks loop, so the tail cannot be
    //    silence that a one-shot would have left behind.
    float tail = 0.f;
    for (std::size_t i = out.size() - static_cast<std::size_t>(kRate) / 5 * 2; i < out.size(); ++i)
        tail = std::max(tail, std::fabs(out[i]));
    KS_CHECK(tail > 0.01f);

    // 5. A track the player stops stays stopped. The car channels are muted
    //    first, the way the showroom mutes them: the engine synth runs whether
    //    or not a car is there, and it is the reason a parked hall is silent
    //    by volume and not by absence.
    audio.setEngineVolume(0.f);
    audio.setEnvironmentVolume(0.f);
    audio.stopShowroomAudio();
    std::vector<float> silent(2048, 1.f); // pre-filled: the mixer must clear it
    audio.renderOffline(silent.data(), 1024, 2, kRate);
    float residual = 0.f;
    for (std::size_t i = 0; i < 2048; ++i) residual = std::max(residual, std::fabs(silent[i]));
    KS_CHECK(residual < 0.01f);

    std::error_code ignored;
    fs::remove_all(dir, ignored);

    std::printf("showroom audio: %.3f peak, %.4f rms, %.3f max step, loops and stops OK\n",
                static_cast<double>(peak), rms, static_cast<double>(biggestStep));
    return KS_TEST_RESULT("showroom_audio_test");
}
