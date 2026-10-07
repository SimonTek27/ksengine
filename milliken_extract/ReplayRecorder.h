#pragma once

#include "MathTypes.h"
#include "engine/physics/PhysicsCoreTypes.h"
#include <chrono>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>

namespace ks::sim {

struct ReplayFrame {
    float time = 0;
    vec3 position;
    vec3 rotation;
    float speed = 0;
    float rpm = 0;
    int gear = 0;
    float throttle = 0;
    float brake = 0;
    float steering = 0;
};

class ReplayRecorder {
public:
    ReplayRecorder();
    ~ReplayRecorder();

    void startRecording();
    void stopRecording();
    void recordFrame(const ks::physics::SimulationState& state,
                     float throttle, float brake, float steering);
    bool isRecording() const { return m_recording; }

    bool loadReplay(const std::string& filePath);
    bool saveReplay(const std::string& filePath);
    void startPlayback();
    void stopPlayback();
    void setPlaybackSpeed(float speed) { m_playbackSpeed = std::clamp(speed, 0.f, 8.f); }
    bool isPlaying() const { return m_playing; }

    /** Sprint 3 / P0.5 — scrub / seek */
    void seekTo(float timeSec);
    void seekProgress(float t01);
    void updatePlayback(float dt);
    bool isFinished() const;

    ReplayFrame getPlaybackFrame() const;
    const std::vector<ReplayFrame>& frames() const { return m_frames; }

    int frameCount() const { return static_cast<int>(m_frames.size()); }
    float duration() const;
    float playbackTime() const { return m_playbackTime; }
    float playbackProgress() const;

    std::function<void()> onRecordingStarted;
    std::function<void(int)> onRecordingStopped;
    std::function<void()> onPlaybackStarted;
    std::function<void()> onPlaybackStopped;
    std::function<void()> onPlaybackFinished;

private:
    void interpolateFrames(float time, ReplayFrame& out) const;

    bool m_recording = false;
    std::chrono::steady_clock::time_point m_recordStart;
    std::vector<ReplayFrame> m_frames;

    bool m_playing = false;
    float m_playbackTime = 0;
    float m_playbackSpeed = 1.0f;
    int m_playbackIdx = 0;
};

} // namespace ks::sim
