#include "ReplayRecorder.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <system_error>

namespace ks::sim {

static constexpr uint32_t REPLAY_MAGIC = 0x52504C59;
static constexpr uint32_t REPLAY_VERSION = 1;

ReplayRecorder::ReplayRecorder() = default;
ReplayRecorder::~ReplayRecorder()
{
    if (m_recording) stopRecording();
    if (m_playing) stopPlayback();
}

void ReplayRecorder::startRecording()
{
    m_frames.clear();
    m_recording = true;
    m_recordStart = std::chrono::steady_clock::now();
    printf("ReplayRecorder: Recording started\n");
    if (onRecordingStarted) onRecordingStarted();
}

void ReplayRecorder::stopRecording()
{
    m_recording = false;
    int frames = static_cast<int>(m_frames.size());
    printf("ReplayRecorder: Recording stopped - %d frames\n", frames);
    if (onRecordingStopped) onRecordingStopped(frames);
}

void ReplayRecorder::recordFrame(const ks::physics::SimulationState& state,
                                  float throttle, float brake, float steering)
{
    if (!m_recording) return;

    // qualityLevel 1..5 -> ~15, 20, 30, 45, 60 Hz
    static const float kHz[5] = {15.f, 20.f, 30.f, 45.f, 60.f};
    const float minDt = 1.0f / kHz[std::clamp(m_qualityLevel, 1, 5) - 1];
    if (!m_frames.empty()) {
        float lastTime = m_frames.back().time;
        auto now = std::chrono::steady_clock::now();
        float currentTime = std::chrono::duration<float>(now - m_recordStart).count();
        if (currentTime - lastTime < minDt) return;
    }
    // Soft size cap: ~sizeof(ReplayFrame) * N; stop accepting when over budget.
    const std::size_t budget =
        static_cast<std::size_t>(std::max(1, m_maxSizeMb)) * 1024u * 1024u / sizeof(ReplayFrame);
    if (m_frames.size() >= budget) return;

    ReplayFrame frame;
    auto now = std::chrono::steady_clock::now();
    frame.time = std::chrono::duration<float>(now - m_recordStart).count();
    frame.position = vec3(state.position.x, state.position.y, state.position.z);
    frame.rotation = vec3(state.rotation.x, state.rotation.y, state.rotation.z);
    frame.speed = state.speed;
    frame.rpm = state.rpm;
    frame.gear = state.gear;
    frame.throttle = throttle;
    frame.brake = brake;
    frame.steering = steering;

    m_frames.push_back(frame);
}

bool ReplayRecorder::loadReplay(const std::string& filePath)
{
    FILE* f = fopen(filePath.c_str(), "rb");
    if (!f) {
        printf("ReplayRecorder: Cannot open file: %s\n", filePath.c_str());
        return false;
    }

    // The declared frame count is only a claim until the file is long enough to
    // hold the frames: a truncated download or a killed writer used to resize
    // the vector to the header's number and fill it with whatever fread left
    // behind. Size the file first and trust the bytes, not the header.
    constexpr long kHeaderBytes = 12;
    const std::size_t kFrameBytes = sizeof(float) + sizeof(vec3) * 2 + sizeof(float) * 2 +
                                    sizeof(int) + sizeof(float) * 3;
    std::fseek(f, 0, SEEK_END);
    const long fileBytes = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    uint32_t magic, version;
    if (std::fread(&magic, sizeof(magic), 1, f) != 1 || magic != REPLAY_MAGIC) {
        printf("ReplayRecorder: Invalid magic number\n");
        fclose(f);
        return false;
    }

    if (std::fread(&version, sizeof(version), 1, f) != 1 || version != REPLAY_VERSION) {
        printf("ReplayRecorder: Unsupported version: %u\n", version);
        fclose(f);
        return false;
    }

    uint32_t frameCount = 0;
    if (std::fread(&frameCount, sizeof(frameCount), 1, f) != 1) {
        printf("ReplayRecorder: Truncated header: %s\n", filePath.c_str());
        fclose(f);
        return false;
    }
    if (fileBytes < kHeaderBytes ||
        static_cast<std::size_t>(fileBytes - kHeaderBytes) != kFrameBytes * frameCount) {
        printf("ReplayRecorder: Truncated replay: %s declares %u frames, file holds %ld bytes\n",
               filePath.c_str(), frameCount, fileBytes);
        fclose(f);
        return false;
    }
    m_frames.assign(frameCount, ReplayFrame{});

    for (uint32_t i = 0; i < frameCount; ++i) {
        auto& f2 = m_frames[i];
        fread(&f2.time, sizeof(float), 1, f);
        fread(&f2.position, sizeof(vec3), 1, f);
        fread(&f2.rotation, sizeof(vec3), 1, f);
        fread(&f2.speed, sizeof(float), 1, f);
        fread(&f2.rpm, sizeof(float), 1, f);
        fread(&f2.gear, sizeof(int), 1, f);
        fread(&f2.throttle, sizeof(float), 1, f);
        fread(&f2.brake, sizeof(float), 1, f);
        fread(&f2.steering, sizeof(float), 1, f);
    }

    fclose(f);
    m_playing = false;
    m_playbackTime = 0;
    printf("ReplayRecorder: Loaded %u frames from %s\n", frameCount, filePath.c_str());
    return true;
}

bool ReplayRecorder::saveReplay(const std::string& filePath)
{
    // The caller names the file, not the folder. A control-API SAVE into
    // replay/ used to fail on a clean install because only the menu's toggle
    // created that directory first; creating the parent here keeps every
    // writer (menu, control API, tools) working under one rule.
    {
        std::error_code ec;
        const std::filesystem::path parent = std::filesystem::path(filePath).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    }
    FILE* f = fopen(filePath.c_str(), "wb");
    if (!f) {
        printf("ReplayRecorder: Cannot write to file: %s\n", filePath.c_str());
        return false;
    }

    uint32_t magic = REPLAY_MAGIC;
    uint32_t version = REPLAY_VERSION;
    uint32_t frameCount = static_cast<uint32_t>(m_frames.size());

    fwrite(&magic, 4, 1, f);
    fwrite(&version, 4, 1, f);
    fwrite(&frameCount, 4, 1, f);

    for (const auto& fr : m_frames) {
        fwrite(&fr.time, sizeof(float), 1, f);
        fwrite(&fr.position, sizeof(vec3), 1, f);
        fwrite(&fr.rotation, sizeof(vec3), 1, f);
        fwrite(&fr.speed, sizeof(float), 1, f);
        fwrite(&fr.rpm, sizeof(float), 1, f);
        fwrite(&fr.gear, sizeof(int), 1, f);
        fwrite(&fr.throttle, sizeof(float), 1, f);
        fwrite(&fr.brake, sizeof(float), 1, f);
        fwrite(&fr.steering, sizeof(float), 1, f);
    }

    fclose(f);
    printf("ReplayRecorder: Saved %zu frames to %s\n", m_frames.size(), filePath.c_str());
    return true;
}

void ReplayRecorder::startPlayback()
{
    if (m_frames.empty()) {
        printf("ReplayRecorder: No frames to play\n");
        return;
    }
    m_playing = true;
    m_playbackTime = 0;
    printf("ReplayRecorder: Playback started\n");
    if (onPlaybackStarted) onPlaybackStarted();
}

void ReplayRecorder::stopPlayback()
{
    m_playing = false;
    m_playbackTime = 0;
    printf("ReplayRecorder: Playback stopped\n");
    if (onPlaybackStopped) onPlaybackStopped();
}

ReplayFrame ReplayRecorder::getPlaybackFrame() const
{
    ReplayFrame result;
    // Deliberately not gated on m_playing: a paused player still shows the
    // frame under the new playhead after a seek.
    if (m_frames.empty()) return result;

    const float time = m_frames.front().time + m_playbackTime;
    const int idx = indexForTime(time);

    if (idx >= (int)m_frames.size() - 1) {
        return m_frames.back();
    }

    const auto& a = m_frames[idx];
    const auto& b = m_frames[idx + 1];
    float dt = b.time - a.time;
    float t = (dt > 0.0001f) ? (time - a.time) / dt : 0;
    t = std::clamp(t, 0.0f, 1.0f);

    result.time = time;
    result.position = a.position + (b.position - a.position) * t;
    result.rotation = a.rotation + (b.rotation - a.rotation) * t;
    result.speed = a.speed + (b.speed - a.speed) * t;
    result.rpm = a.rpm + (b.rpm - a.rpm) * t;
    result.gear = (t < 0.5f) ? a.gear : b.gear;
    result.throttle = a.throttle + (b.throttle - a.throttle) * t;
    result.brake = a.brake + (b.brake - a.brake) * t;
    result.steering = a.steering + (b.steering - a.steering) * t;

    return result;
}

float ReplayRecorder::duration() const
{
    if (m_frames.size() < 2) return 0;
    return m_frames.back().time - m_frames.front().time;
}

float ReplayRecorder::playbackProgress() const
{
    float dur = duration();
    return (dur > 0) ? (m_playbackTime / dur) : 0;
}

int ReplayRecorder::indexForTime(float time) const
{
    if (m_frames.size() < 2) return 0;
    const auto it = std::upper_bound(m_frames.begin(), m_frames.end(), time,
        [](float t, const ReplayFrame& fr) { return t < fr.time; });
    const int idx = static_cast<int>(it - m_frames.begin()) - 1;
    return std::clamp(idx, 0, static_cast<int>(m_frames.size()) - 1);
}

void ReplayRecorder::updatePlayback(float dt)
{
    if (!m_playing || m_frames.empty()) return;
    if (dt > 0.0f) m_playbackTime += dt * m_playbackSpeed;
    const float dur = duration();
    if (m_playbackTime < dur) return;
    m_playbackTime = dur;
    m_playing = false;
    printf("ReplayRecorder: Playback finished\n");
    if (onPlaybackFinished) onPlaybackFinished();
}

void ReplayRecorder::seekTo(float timeSec)
{
    m_playbackTime = std::clamp(timeSec, 0.0f, duration());
}

void ReplayRecorder::seekProgress(float t01)
{
    seekTo(std::clamp(t01, 0.0f, 1.0f) * duration());
}

bool ReplayRecorder::isFinished() const
{
    if (m_frames.empty()) return true;
    return m_playbackTime >= duration() - 0.0001f;
}

void ReplayRecorder::interpolateFrames(float time, ReplayFrame& out) const
{
    if (m_frames.empty()) return;

    int idx = 0;
    while (idx < (int)m_frames.size() - 1 && m_frames[idx + 1].time <= time) {
        idx++;
    }

    if (idx >= (int)m_frames.size() - 1) {
        out = m_frames.back();
        return;
    }

    const auto& a = m_frames[idx];
    const auto& b = m_frames[idx + 1];
    float dt = b.time - a.time;
    float t = (dt > 0.0001f) ? (time - a.time) / dt : 0;
    t = std::clamp(t, 0.0f, 1.0f);

    out.time = time;
    out.position = a.position + (b.position - a.position) * t;
    out.rotation = a.rotation + (b.rotation - a.rotation) * t;
    out.speed = a.speed + (b.speed - a.speed) * t;
    out.rpm = a.rpm + (b.rpm - a.rpm) * t;
    out.gear = (t < 0.5f) ? a.gear : b.gear;
}

} // namespace ks::sim
