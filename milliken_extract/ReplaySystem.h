#pragma once
/** Replay recording/playback — Qt-free. */
#include <string>
#include <vector>
#include <cstdint>
#include <fstream>

namespace ks {
namespace physics {

struct ReplayFrame {
    double time = 0;
    float x = 0, y = 0, z = 0;
    float yaw = 0, pitch = 0, roll = 0;
    float speed = 0;
    float throttle = 0, brake = 0, steer = 0;
    int gear = 0;
};

class ReplaySystem {
public:
    void clear() { m_frames.clear(); m_playing = false; }
    void recordFrame(const ReplayFrame& f) { if (m_recording) m_frames.push_back(f); }
    void setRecording(bool r) { m_recording = r; }
    bool isRecording() const { return m_recording; }

    bool save(const std::string& path) const {
        std::ofstream out(path, std::ios::binary);
        if (!out) return false;
        uint32_t n = static_cast<uint32_t>(m_frames.size());
        out.write(reinterpret_cast<const char*>(&n), sizeof(n));
        if (!m_frames.empty())
            out.write(reinterpret_cast<const char*>(m_frames.data()),
                      static_cast<std::streamsize>(m_frames.size() * sizeof(ReplayFrame)));
        return true;
    }

    bool load(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        uint32_t n = 0;
        in.read(reinterpret_cast<char*>(&n), sizeof(n));
        m_frames.resize(n);
        if (n)
            in.read(reinterpret_cast<char*>(m_frames.data()),
                    static_cast<std::streamsize>(n * sizeof(ReplayFrame)));
        return true;
    }

    void play() { m_playing = true; m_playIndex = 0; }
    void stop() { m_playing = false; }
    bool isPlaying() const { return m_playing; }

    bool nextFrame(ReplayFrame& out) {
        if (!m_playing || m_playIndex >= m_frames.size()) {
            m_playing = false;
            return false;
        }
        out = m_frames[m_playIndex++];
        return true;
    }

    size_t frameCount() const { return m_frames.size(); }

private:
    std::vector<ReplayFrame> m_frames;
    bool m_recording = false;
    bool m_playing = false;
    size_t m_playIndex = 0;
};

} // namespace physics
} // namespace ks
