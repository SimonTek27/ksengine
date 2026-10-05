#pragma once

#include "MathTypes.h"
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <cstddef>
#include "engine/AI/AiFileReader.h"

namespace ks::sim {

/** Snapshot of another car, used for overtake / traffic awareness. */
struct AiTrafficCar {
    int id = -1;
    float x = 0, z = 0;
    float heading = 0;
    float speed = 0;
    float alongTrack = 0; // optional cumulative distance
};

class AIController {
public:
    AIController();
    ~AIController();

    bool loadSpline(const std::string& trackDirectory);

    void update(const vec3& carPosition, float carHeading,
                float speed, int gear, float dt);

    /** With traffic awareness: same control loop, but the target speed and a
     *  lateral offset off the racing line account for nearby cars. */
    void update(const vec3& carPosition, float carHeading,
                float speed, int gear, float dt,
                const std::vector<AiTrafficCar>& traffic, int selfId);

    float throttle() const { return m_throttle; }
    float brake() const { return m_brake; }
    float steering() const { return m_steering; }
    int targetGear() const { return m_targetGear; }
    bool isReady() const { return m_splineLoaded; }
    /** Index of the nearest spline point in the last update(). */
    int nearestIndex() const { return m_currentIdx; }
    /** Current lateral offset from the racing line (m, +left / -right). */
    float lateralOffset() const { return m_lateralOffset; }

    /** Start-line crossings counted (0 until the first full lap). */
    int lapCount() const { return m_lapCount; }
    /** Distance along the spline at the current nearest point. */
    float progressDistance() const {
        if (m_cumulativeDistance.empty()) return 0.0f;
        const std::size_t i = static_cast<std::size_t>(m_currentIdx);
        return i < m_cumulativeDistance.size() ? m_cumulativeDistance[i] : 0.0f;
    }
    /** Total spline length (0 until loaded) — race standings scale by it. */
    float splineLength() const { return m_spline.totalDistance; }

    void setLookaheadDistance(float d) { m_lookaheadDist = d; }
    void setSpeedFactor(float f) { m_speedFactor = f; }
    float speedFactor() const { return m_speedFactor; }
    void setAggression(float a) { m_aggression = std::clamp(a, 0.0f, 1.0f); }
    void setOvertakeEnabled(bool on) { m_overtakeEnabled = on; }
    void setSkill(float s) { m_skill = std::clamp(s, 0.2f, 1.0f); }

    std::function<void(int)> onSplineLoaded;
    std::function<void(int)> onLapCompleted;

private:
    int findNearestPoint(const vec3& pos) const;
    int findLookaheadPoint(int nearestIdx, float dist) const;
    float tangentHeading(int idx) const;
    void detectLap(int nearestIdx);
    /** Track tangent at index (xz, normalised). */
    void tangentAt(int idx, float& tx, float& tz) const;
    /** Evaluate traffic: pick an overtaking side + adjust the target speed. */
    void evaluateTraffic(const vec3& carPos, float carHeading, float speed,
                         int nearestIdx, const std::vector<AiTrafficCar>& traffic,
                         int selfId, float& targetSpeedInOut);
    float calculateSteering(const vec3& carPos, float carHeading,
                            const vec3& targetPos) const;
    float calculateThrottle(float currentSpeed, float targetSpeed,
                            float curvature, float dt) const;
    float calculateBrake(float currentSpeed, float targetSpeed,
                         float curvature, float dt) const;
    int calculateGear(float speed) const;

    bool m_splineLoaded = false;
    ks::ai::AiSpline m_spline;
    std::vector<float> m_cumulativeDistance;

    int m_currentIdx = 0;
    int m_lapCount = 0;
    float m_prevProgress = 0.0f;
    float m_traveledSinceLap = 0.0f;
    bool m_hasProgress = false;

    float m_throttle = 0;
    float m_brake = 0;
    float m_steering = 0;
    float m_errLP = 0;
    float m_errLPPrev = 0;
    float m_dErrLP = 0;
    bool m_errInit = false;
    int m_targetGear = 1;

    float m_lookaheadDist = 30.0f;
    float m_speedFactor = 1.0f;
    float m_aggression = 0.5f;
    float m_maxSteerRate = 2.0f;
    float m_skill = 0.75f;
    float m_lateralOffset = 0.f;
    float m_lateralTarget = 0.f;
    bool m_overtakeEnabled = true;
    float m_overtakeSide = 1.f;
};

} // namespace ks::sim
