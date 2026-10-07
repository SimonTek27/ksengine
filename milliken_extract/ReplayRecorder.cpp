#include "ReplayRecorder.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

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

    if (!m_frames.empty()) {
        float lastTime = m_frames.back().time;
        auto now = std::chrono::steady_clock::now();
        float currentTime = std::chrono::duration<float>(now - m_recordStart).count();
        if (currentTime - lastTime < 1.0f / 60.0f) return;
    }

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

    uint32_t magic, version;
    fread(&magic, 4, 1, f);
    if (magic != REPLAY_MAGIC) {
        printf("ReplayRecorder: Invalid magic number\n");
        fclose(f);
        return false;
    }

    fread(&version, 4, 1, f);
    if (version != REPLAY_VERSION) {
        printf("ReplayRecorder: Unsupported version: %u\n", version);
        fclose(f);
        return false;
    }

    uint32_t frameCount;
    fread(&frameCount, 4, 1, f);
    m_frames.resize(frameCount);

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
    printf("ReplayRecorder: Loaded %u frames from %s\n", frameCount, filePath.c_str());
    return true;
}

bool ReplayRecorder::saveReplay(const std::string& filePath)
{
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
    m_playbackIdx = 0;
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
    if (m_frames.empty() || !m_playing) return result;

    float time = m_playbackTime;
    int idx = m_playbackIdx;
    while (idx < (int)m_frames.size() - 1 && m_frames[idx + 1].time <= time) {
        idx++;
    }

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


void ReplayRecorder::seekTo(float timeSec) {
    if (m_frames.empty()) return;
    const float dur = duration();
    m_playbackTime = std::clamp(timeSec, 0.f, dur);
    // binary search index
    int lo = 0, hi = static_cast<int>(m_frames.size()) - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (m_frames[static_cast<size_t>(mid)].time <= m_playbackTime) lo = mid;
        else hi = mid - 1;
    }
    m_playbackIdx = lo;
}

void ReplayRecorder::seekProgress(float t01) {
    seekTo(std::clamp(t01, 0.f, 1.f) * duration());
}

void ReplayRecorder::updatePlayback(float dt) {
    if (!m_playing || m_frames.empty()) return;
    m_playbackTime += dt * m_playbackSpeed;
    const float dur = duration();
    if (m_playbackTime >= dur) {
        m_playbackTime = dur;
        m_playing = false;
        if (onPlaybackFinished) onPlaybackFinished();
        if (onPlaybackStopped) onPlaybackStopped();
        return;
    }
    while (m_playbackIdx + 1 < static_cast<int>(m_frames.size()) &&
           m_frames[static_cast<size_t>(m_playbackIdx + 1)].time <= m_playbackTime)
        ++m_playbackIdx;
}

bool ReplayRecorder::isFinished() const {
    if (m_frames.empty()) return true;
    return m_playbackTime >= duration() - 1e-4f && !m_playing;
}
} // namespace ks::sim