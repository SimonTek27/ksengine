#pragma once
/**
 * AIController — racing line follow + basic overtake (Sprint 6 / P0.6).
 */
#include "MathTypes.h"
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <cmath>
#include "engine/AI/AiFileReader.h"

namespace ks::sim {

/** Snapshot of another car for overtake / traffic. */
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

    /** With traffic awareness (preferred). */
    void update(const vec3& carPosition, float carHeading,
                float speed, int gear, float dt,
                const std::vector<AiTrafficCar>& traffic, int selfId);

    float throttle() const { return m_throttle; }
    float brake() const { return m_brake; }
    float steering() const { return m_steering; }
    int targetGear() const { return m_targetGear; }
    bool isReady() const { return m_splineLoaded; }
    int nearestIndex() const { return m_currentIdx; }
    float lateralOffset() const { return m_lateralOffset; }

    void setLookaheadDistance(float d) { m_lookaheadDist = std::clamp(d, 5.f, 120.f); }
    void setSpeedFactor(float f) { m_speedFactor = std::clamp(f, 0.3f, 1.5f); }
    void setAggression(float a) { m_aggression = std::clamp(a, 0.0f, 1.0f); }
    void setOvertakeEnabled(bool on) { m_overtakeEnabled = on; }
    void setSkill(float s) { m_skill = std::clamp(s, 0.2f, 1.0f); }

    std::function<void(int)> onSplineLoaded;
    std::function<void(int)> onLapCompleted;

private:
    int findNearestPoint(const vec3& pos) const;
    int findLookaheadPoint(int nearestIdx, float dist) const;
    float calculateSteering(const vec3& carPos, float carHeading,
                            const vec3& targetPos) const;
    float calculateThrottle(float currentSpeed, float targetSpeed,
                            float curvature, float dt) const;
    float calculateBrake(float currentSpeed, float targetSpeed,
                         float curvature, float dt) const;
    int calculateGear(float speed) const;
    void rebuildCumulative();
    vec3 pointAt(int idx) const;
    /** Track tangent at index (xz). */
    void tangentAt(int idx, float& tx, float& tz) const;
    /** Evaluate traffic: adjust lateral offset + target speed. */
    void evaluateTraffic(const vec3& carPos, float carHeading, float speed,
                         int nearestIdx, const std::vector<AiTrafficCar>& traffic,
                         int selfId, float& targetSpeedInOut);

    bool m_splineLoaded = false;
    ks::ai::AiSpline m_spline;
    std::vector<float> m_cumulativeDistance;

    int m_currentIdx = 0;
    int m_prevLap = 0;

    float m_throttle = 0;
    float m_brake = 0;
    float m_steering = 0;
    int m_targetGear = 1;

    float m_lookaheadDist = 30.0f;
    float m_speedFactor = 1.0f;
    float m_aggression = 0.5f;
    float m_skill = 0.75f;
    float m_maxSteerRate = 2.0f;
    float m_lateralOffset = 0.f;   // meters left/right of line
    float m_lateralTarget = 0.f;
    bool m_overtakeEnabled = true;
    float m_overtakeSide = 1.f;    // +1 left, -1 right
};

} // namespace ks::sim
