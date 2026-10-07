#include "AIController.h"
#include <cstdio>
#include <limits>
#include <filesystem>
#include <cmath>

namespace fs = std::filesystem;

namespace ks::sim {

AIController::AIController() = default;
AIController::~AIController() = default;

bool AIController::loadSpline(const std::string& trackDirectory) {
    m_splineLoaded = false;
    m_spline = {};
    m_cumulativeDistance.clear();
    m_currentIdx = 0;
    m_prevLap = 0;

    const char* candidates[] = {
        "/ai/fast_lane.ai", "/ai/fast_lane.txt",
        "/data/ai/fast_lane.ai", "/fast_lane.ai", "/ai/ideal_line.ai"
    };
    std::string path;
    for (auto c : candidates) {
        fs::path p = fs::path(trackDirectory + c);
        if (fs::exists(p)) { path = p.string(); break; }
    }
    if (path.empty()) {
        std::fprintf(stderr, "AIController: no AI spline in %s\n", trackDirectory.c_str());
        return false;
    }
    m_spline = ks::ai::AiFileReader::readSpline(path);
    if (!m_spline.isValid()) {
        std::fprintf(stderr, "AIController: invalid spline %s\n", path.c_str());
        return false;
    }
    rebuildCumulative();
    m_splineLoaded = true;
    std::fprintf(stderr, "AIController: loaded %zu points from %s (%.0fm)\n",
                 m_spline.points.size(), path.c_str(), m_spline.totalDistance);
    if (onSplineLoaded) onSplineLoaded(static_cast<int>(m_spline.points.size()));
    return true;
}

void AIController::rebuildCumulative() {
    m_cumulativeDistance.assign(m_spline.points.size(), 0.f);
    float acc = 0.f;
    for (size_t i = 0; i < m_spline.points.size(); ++i) {
        m_cumulativeDistance[i] = acc;
        if (i + 1 < m_spline.points.size()) {
            const auto& a = m_spline.points[i].position;
            const auto& b = m_spline.points[i + 1].position;
            float dx = b.x - a.x, dy = b.y - a.y, dz = b.z - a.z;
            acc += std::sqrt(dx * dx + dy * dy + dz * dz);
        }
    }
    m_spline.totalDistance = acc;
}

vec3 AIController::pointAt(int idx) const {
    const auto& p = m_spline.points[static_cast<size_t>(idx)].position;
    return vec3(p.x, p.y, p.z);
}

void AIController::tangentAt(int idx, float& tx, float& tz) const {
    const int n = static_cast<int>(m_spline.points.size());
    if (n < 2) { tx = 0; tz = 1; return; }
    int i0 = std::clamp(idx, 0, n - 1);
    int i1 = std::min(i0 + 1, n - 1);
    const auto& a = m_spline.points[static_cast<size_t>(i0)].position;
    const auto& b = m_spline.points[static_cast<size_t>(i1)].position;
    tx = b.x - a.x;
    tz = b.z - a.z;
    float len = std::sqrt(tx * tx + tz * tz);
    if (len > 1e-4f) { tx /= len; tz /= len; }
    else { tx = 0; tz = 1; }
}

void AIController::update(const vec3& carPosition, float carHeading,
                          float speed, int gear, float dt) {
    static const std::vector<AiTrafficCar> empty;
    update(carPosition, carHeading, speed, gear, dt, empty, -1);
}

void AIController::update(const vec3& carPosition, float carHeading,
                          float speed, int gear, float dt,
                          const std::vector<AiTrafficCar>& traffic, int selfId) {
    (void)gear;
    if (!m_splineLoaded || m_spline.points.empty()) {
        m_throttle = 0; m_brake = 1.f; m_steering = 0;
        return;
    }

    int nearest = findNearestPoint(carPosition);
    m_currentIdx = nearest;

    // Lap heuristic: wrap near start
    if (nearest < 5 && m_prevLap >= static_cast<int>(m_spline.points.size()) / 2) {
        m_prevLap = 0;
        if (onLapCompleted) onLapCompleted(1);
    } else {
        m_prevLap = nearest;
    }

    float lookDist = m_lookaheadDist * (0.7f + 0.3f * m_skill);
    // More lookahead at high speed
    lookDist += std::clamp(speed * 0.4f, 0.f, 40.f);
    int lookahead = findLookaheadPoint(nearest, lookDist);

    float targetSpeed = m_spline.points[static_cast<size_t>(lookahead)].speed;
    if (targetSpeed < 1.f) {
        // Derive from curvature if speed field empty
        float curv = std::abs(m_spline.points[static_cast<size_t>(lookahead)].curvature);
        targetSpeed = std::clamp(55.f / (1.f + curv * 40.f), 8.f, 70.f);
    }
    targetSpeed *= m_speedFactor * (0.85f + 0.15f * m_skill);

    float curvature = std::abs(m_spline.points[static_cast<size_t>(lookahead)].curvature);
    if (curvature > 0.01f)
        targetSpeed *= (1.0f - curvature * m_aggression * 0.5f);

    evaluateTraffic(carPosition, carHeading, speed, nearest, traffic, selfId, targetSpeed);

    // Smooth lateral offset toward target
    const float latRate = 2.5f * dt;
    if (m_lateralOffset < m_lateralTarget)
        m_lateralOffset = std::min(m_lateralTarget, m_lateralOffset + latRate);
    else
        m_lateralOffset = std::max(m_lateralTarget, m_lateralOffset - latRate);

    // Target point with lateral offset
    vec3 targetPos = pointAt(lookahead);
    float tx, tz;
    tangentAt(lookahead, tx, tz);
    // left normal = (-tz, tx)
    targetPos.x += -tz * m_lateralOffset;
    targetPos.z +=  tx * m_lateralOffset;

    float desiredSteer = calculateSteering(carPosition, carHeading, targetPos);
    // Rate limit steering
    float maxDelta = m_maxSteerRate * dt * (0.5f + 0.5f * m_skill);
    m_steering = std::clamp(m_steering + std::clamp(desiredSteer - m_steering, -maxDelta, maxDelta),
                            -1.f, 1.f);

    m_throttle = calculateThrottle(speed, targetSpeed, curvature, dt);
    m_brake = calculateBrake(speed, targetSpeed, curvature, dt);
    m_targetGear = calculateGear(speed);
}

void AIController::evaluateTraffic(const vec3& carPos, float carHeading, float speed,
                                   int nearestIdx, const std::vector<AiTrafficCar>& traffic,
                                   int selfId, float& targetSpeedInOut) {
    if (!m_overtakeEnabled || traffic.empty()) {
        m_lateralTarget = 0.f;
        return;
    }

    float tx, tz;
    tangentAt(nearestIdx, tx, tz);
    const float nx = -tz, nz = tx; // left normal

    float closestAhead = 1e9f;
    float sideBias = 0.f;
    bool blocked = false;

    for (const auto& o : traffic) {
        if (o.id == selfId) continue;
        float dx = o.x - carPos.x;
        float dz = o.z - carPos.z;
        float along = dx * tx + dz * tz;
        float lat = dx * nx + dz * nz;
        // Ahead within corridor
        if (along > 2.f && along < 45.f && std::abs(lat) < 6.f) {
            if (along < closestAhead) {
                closestAhead = along;
                sideBias = lat;
            }
            // Relative speed
            float rel = speed - o.speed;
            if (along < 25.f && rel > -5.f) {
                blocked = true;
            }
            // Match speed if too close and slower car ahead
            if (along < 12.f && o.speed < speed) {
                targetSpeedInOut = std::min(targetSpeedInOut, o.speed * 0.95f);
            }
        }
    }

    if (blocked && closestAhead < 35.f) {
        // Overtake: pick side opposite to their lateral bias, or default left
        if (std::abs(sideBias) > 0.5f)
            m_overtakeSide = (sideBias > 0.f) ? -1.f : 1.f;
        // Offset up to 3.5m based on aggression
        float mag = 2.0f + 1.5f * m_aggression;
        m_lateralTarget = m_overtakeSide * mag;
        // Slight speed bump if aggressive and path clear-ish
        if (m_aggression > 0.4f && closestAhead > 8.f)
            targetSpeedInOut *= 1.0f + 0.08f * m_aggression;
    } else {
        // Return to line
        m_lateralTarget = 0.f;
    }
}

int AIController::findNearestPoint(const vec3& pos) const {
    if (m_spline.points.empty()) return 0;
    // Search window around current for performance
    const int n = static_cast<int>(m_spline.points.size());
    int start = std::max(0, m_currentIdx - 30);
    int end = std::min(n, m_currentIdx + 80);
    int bestIdx = m_currentIdx;
    float bestDist = 1e30f;
    auto consider = [&](int i) {
        float dx = m_spline.points[static_cast<size_t>(i)].position.x - pos.x;
        float dz = m_spline.points[static_cast<size_t>(i)].position.z - pos.z;
        float d = dx * dx + dz * dz;
        if (d < bestDist) { bestDist = d; bestIdx = i; }
    };
    for (int i = start; i < end; ++i) consider(i);
    // Periodic full scan if drifted far
    if (bestDist > 40.f * 40.f) {
        for (int i = 0; i < n; i += 3) consider(i);
    }
    return bestIdx;
}

int AIController::findLookaheadPoint(int nearestIdx, float dist) const {
    if (m_spline.points.empty()) return 0;
    float base = m_cumulativeDistance[static_cast<size_t>(nearestIdx)];
    float target = base + dist;
    const size_t n = m_spline.points.size();
    for (size_t i = static_cast<size_t>(nearestIdx); i < n; ++i) {
        if (m_cumulativeDistance[i] >= target)
            return static_cast<int>(i);
    }
    // Wrap closed
    if (m_spline.closed || n > 10) {
        float wrapTarget = target - m_spline.totalDistance;
        if (wrapTarget > 0.f) {
            for (size_t i = 0; i < n; ++i)
                if (m_cumulativeDistance[i] >= wrapTarget)
                    return static_cast<int>(i);
        }
    }
    return static_cast<int>(n) - 1;
}

float AIController::calculateSteering(const vec3& carPos, float carHeading,
                                      const vec3& targetPos) const {
    float dx = targetPos.x - carPos.x;
    float dz = targetPos.z - carPos.z;
    float targetHeading = std::atan2(dx, dz);
    float err = targetHeading - carHeading;
    while (err > 3.14159f) err -= 6.28318f;
    while (err < -3.14159f) err += 6.28318f;
    return std::clamp(err * 1.35f, -1.0f, 1.0f);
}

float AIController::calculateThrottle(float currentSpeed, float targetSpeed,
                                      float curvature, float dt) const {
    (void)dt; (void)curvature;
    float err = targetSpeed - currentSpeed;
    if (err > 2.f) return std::clamp(0.4f + err * 0.04f, 0.f, 1.f);
    if (err > 0.f) return 0.35f;
    return 0.f;
}

float AIController::calculateBrake(float currentSpeed, float targetSpeed,
                                   float curvature, float dt) const {
    (void)dt;
    float err = currentSpeed - targetSpeed;
    float b = 0.f;
    if (err > 3.f) b = std::clamp(err * 0.05f, 0.f, 1.f);
    if (curvature > 0.05f && currentSpeed > targetSpeed * 0.9f)
        b = std::max(b, std::clamp(curvature * 2.f, 0.f, 0.6f));
    return b;
}

int AIController::calculateGear(float speed) const {
    if (speed < 8.f) return 1;
    if (speed < 16.f) return 2;
    if (speed < 25.f) return 3;
    if (speed < 35.f) return 4;
    if (speed < 45.f) return 5;
    return 6;
}

} // namespace ks::sim
