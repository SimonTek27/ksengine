#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

namespace ks {
namespace physics {

struct AIVec3 {
    float x = 0, y = 0, z = 0;
    AIVec3() = default;
    AIVec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    AIVec3 operator-(AIVec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    AIVec3 operator+(AIVec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    AIVec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    static float dot(AIVec3 a, AIVec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
};

struct AITarget {
    AIVec3 pos;
    float speed = 20.0f;
    float curvature = 0.0f;
};

struct AIInput {
    AIVec3 pos;
    AIVec3 vel;
    float heading = 0;
    float speed = 0;
};

struct AIOutput {
    float throttle = 0;
    float brake = 0;
    float steer = 0;
};

/**
 * Simple path-following AI — Qt-free.
 */
class AIDriver {
public:
    void setPath(const std::vector<AITarget>& path) {
        m_path = path;
        m_idx = 0;
    }

    void setObstacles(const std::vector<AIVec3>& obs) { m_obstacles = obs; }

    AIOutput update(const AIInput& in) {
        AIOutput out;
        if (m_path.empty()) return out;

        m_idx = nearestIdx(in.pos);
        int look = std::min(m_idx + 2, static_cast<int>(m_path.size()) - 1);
        const AITarget& tp = m_path[static_cast<size_t>(look)];

        AIVec3 toT = tp.pos - in.pos;
        float dist = toT.length();
        float targetHeading = std::atan2(toT.x, toT.z);
        float headingErr = targetHeading - in.heading;
        while (headingErr > 3.14159f) headingErr -= 6.28318f;
        while (headingErr < -3.14159f) headingErr += 6.28318f;
        out.steer = std::clamp(headingErr * 1.5f, -1.0f, 1.0f);

        float desired = tp.speed;
        // obstacle slowdown
        AIVec3 fwd(std::sin(in.heading), 0, std::cos(in.heading));
        for (const auto& o : m_obstacles) {
            AIVec3 d = o - in.pos;
            float along = AIVec3::dot(d, fwd);
            if (along > 0 && along < 15.0f) {
                AIVec3 lat = d - fwd * along;
                if (lat.length() < 3.0f)
                    desired = std::min(desired, 8.0f);
            }
        }

        float speedErr = desired - in.speed;
        if (speedErr > 0.5f) {
            out.throttle = std::clamp(speedErr * 0.15f, 0.0f, 1.0f);
            out.brake = 0;
        } else if (speedErr < -0.5f) {
            out.brake = std::clamp(-speedErr * 0.2f, 0.0f, 1.0f);
            out.throttle = 0;
        }
        (void)dist;
        return out;
    }

private:
    int nearestIdx(const AIVec3& p) const {
        int best = 0;
        float bestD = 1e9f;
        for (size_t i = 0; i < m_path.size(); ++i) {
            float d = (m_path[i].pos - p).length();
            if (d < bestD) {
                bestD = d;
                best = static_cast<int>(i);
            }
        }
        return best;
    }

    std::vector<AITarget> m_path;
    std::vector<AIVec3> m_obstacles;
    int m_idx = 0;
};

} // namespace physics
} // namespace ks
