#include "AIController.h"
#include <cmath>
#include <limits>
#include <cstdio>
#include <algorithm>

namespace ks::sim {

AIController::AIController() = default;
AIController::~AIController() = default;

bool AIController::loadSpline(const std::string& trackDirectory)
{
    std::string splinePath = trackDirectory + "/ai/fast_lane.ai";
    m_spline = ks::ai::AiFileReader::readSpline(splinePath);

    if (!m_spline.isValid()) {
        // try .txt companion
        m_spline = ks::ai::AiFileReader::readSpline(splinePath + ".txt");
    }

    if (!m_spline.isValid()) {
        std::printf("AIController: Failed to parse spline from: %s\n", splinePath.c_str());
        return false;
    }

    m_cumulativeDistance.resize(m_spline.points.size());
    m_cumulativeDistance[0] = 0.0f;
    for (size_t i = 1; i < m_spline.points.size(); ++i) {
        float dx = m_spline.points[i].position.x - m_spline.points[i - 1].position.x;
        float dy = m_spline.points[i].position.y - m_spline.points[i - 1].position.y;
        float dz = m_spline.points[i].position.z - m_spline.points[i - 1].position.z;
        m_cumulativeDistance[i] = m_cumulativeDistance[i - 1]
                                  + std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    m_splineLoaded = true;
    m_currentIdx = 0;
    m_lapCount = 0;
    m_prevProgress = 0.0f;
    m_traveledSinceLap = 0.0f;
    m_hasProgress = false;

    std::printf("AIController: Loaded spline with %d points\n", (int)m_spline.points.size());
    if (onSplineLoaded) onSplineLoaded(static_cast<int>(m_spline.points.size()));
    return true;
}

void AIController::update(const vec3& carPosition, float carHeading,
                          float speed, int /*gear*/, float dt)
{
    if (!m_splineLoaded || m_spline.points.empty()) {
        m_throttle = 0; m_brake = 1.0f; m_steering = 0;
        return;
    }

    int nearest = findNearestPoint(carPosition);
    m_currentIdx = nearest;
    detectLap(nearest);

    // Speed-scheduled lookahead: a fixed 30 m adds >2 s of phase lag at
    // racing speeds and let the heading loop run away (growing sideslip).
    const float lookaheadDist = std::clamp(8.0f + 1.2f * speed, 12.0f, 40.0f);
    int lookahead = findLookaheadPoint(nearest, lookaheadDist);
    const auto& targetPoint = m_spline.points[static_cast<size_t>(lookahead)];
    vec3 targetPos(targetPoint.position.x, targetPoint.position.y, targetPoint.position.z);

    m_steering = calculateSteering(carPosition, carHeading, targetPos);

    // Curvature feedforward: pure heading-error feedback must first build a
    // cross-track error before it can hold the wheel, which hunted around
    // the line in a ~12 s limit cycle (3.5). The spline tangent tells us
    // the needed yaw direction (+steer yaws towards -heading in this
    // engine); scale by speed^2 since the slip angles that realise the
    // geometric radius grow with v^2.
    float w = tangentHeading(lookahead) - tangentHeading(nearest);
    // Wrap: tangentHeading is an atan2, so the difference jumps by 2*pi
    // when the lookahead straddles the +-pi boundary (w=5.76 flipped the
    // feedforward sign and full-locked the wrong way, hunting the line).
    while (w > 3.14159f) w -= 6.28318f;
    while (w < -3.14159f) w += 6.28318f;
    // Normalised by Ld so the term is signed curvature (w/Ld = 1/R),
    // independent of the lookahead distance itself.
    const float ff = -(w / lookaheadDist) * (speed * speed) * 0.09f;
    m_steering = std::clamp(m_steering + ff, -1.0f, 1.0f);

    // Derivative damping: with only P+FF the cross-track loop hunted in a
    // ~16 s limit cycle (plant lag ~4 s). Double low-pass (err, then dErr)
    // keeps the nearest-index quantisation out of the derivative.
    {
        float dx = targetPos.x - carPosition.x;
        float dz = targetPos.z - carPosition.z;
        float err = std::atan2(dx, dz) - carHeading;
        while (err > 3.14159f) err -= 6.28318f;
        while (err < -3.14159f) err += 6.28318f;
        const float ad = std::clamp(dt / 0.25f, 0.0f, 1.0f);
        m_errLP += (err - m_errLP) * ad;
        if (!m_errInit) { m_errLPPrev = m_errLP; m_errInit = true; }
        const float dErr = (m_errLP - m_errLPPrev) / std::max(dt, 1e-4f);
        m_errLPPrev = m_errLP;
        m_dErrLP += (dErr - m_dErrLP) * std::clamp(dt / 0.30f, 0.0f, 1.0f);
        m_steering = std::clamp(
            m_steering - std::clamp(m_dErrLP * 1.0f, -0.4f, 0.4f),
            -1.0f, 1.0f);
    }

    float targetSpeed = targetPoint.speed * m_speedFactor;
    float curvature = std::abs(targetPoint.curvature);
    if (curvature > 0.01f)
        targetSpeed *= (1.0f - curvature * m_aggression * 0.5f);

    m_throttle = calculateThrottle(speed, targetSpeed, curvature, dt);
    m_brake = calculateBrake(speed, targetSpeed, curvature, dt);
    m_targetGear = calculateGear(speed);
}

// Lap counting for static splines: AiFileReader never fills AiSplinePoint::
// lap (it stays 0 on disk data), so derive crossings from the spline wrap.
// A crossing only counts as a completed lap when the car actually covered
// most of the track since the previous one — a car spawned behind the
// start line must not score a "lap" the moment it drives over it.
void AIController::detectLap(int nearestIdx)
{
    if (!m_spline.closed || m_spline.totalDistance <= 0.0f) return;
    const float total = m_spline.totalDistance;
    const float prog = m_cumulativeDistance[static_cast<size_t>(nearestIdx)];

    if (m_hasProgress) {
        const bool crossedLine = m_prevProgress >= total * 0.5f &&
                                 prog <= total * 0.25f;
        if (crossedLine) {
            if (m_traveledSinceLap >= total * 0.5f) {
                ++m_lapCount;
                if (onLapCompleted) onLapCompleted(m_lapCount);
            }
            m_traveledSinceLap = 0.0f;
        } else if (prog > m_prevProgress) {
            m_traveledSinceLap += prog - m_prevProgress;
        }
    }
    m_prevProgress = prog;
    m_hasProgress = true;
}

int AIController::findNearestPoint(const vec3& pos) const
{
    if (m_spline.points.empty()) return 0;

    int bestIdx = 0;
    float bestDist = std::numeric_limits<float>::max();
    for (size_t i = 0; i < m_spline.points.size(); ++i) {
        float dx = m_spline.points[i].position.x - pos.x;
        float dy = m_spline.points[i].position.y - pos.y;
        float dz = m_spline.points[i].position.z - pos.z;
        float d = dx*dx + dy*dy + dz*dz;
        if (d < bestDist) {
            bestDist = d;
            bestIdx = static_cast<int>(i);
        }
    }
    return bestIdx;
}

int AIController::findLookaheadPoint(int nearestIdx, float dist) const
{
    if (m_spline.points.empty()) return 0;
    float base = m_cumulativeDistance[static_cast<size_t>(nearestIdx)];
    float target = base + dist;
    for (size_t i = static_cast<size_t>(nearestIdx); i < m_spline.points.size(); ++i) {
        if (m_cumulativeDistance[i] >= target)
            return static_cast<int>(i);
    }
    return static_cast<int>(m_spline.points.size()) - 1;
}

float AIController::tangentHeading(int idx) const
{
    const auto& pts = m_spline.points;
    if (pts.empty()) return 0.0f;
    const std::size_t i = static_cast<std::size_t>(idx) % pts.size();
    const std::size_t j = (i + 1) % pts.size();
    return std::atan2(pts[j].position.x - pts[i].position.x,
                      pts[j].position.z - pts[i].position.z);
}

float AIController::calculateSteering(const vec3& carPos, float carHeading,
                                      const vec3& targetPos) const
{
    float dx = targetPos.x - carPos.x;
    float dz = targetPos.z - carPos.z;
    float targetHeading = std::atan2(dx, dz);
    float err = targetHeading - carHeading;
    while (err > 3.14159f) err -= 6.28318f;
    while (err < -3.14159f) err += 6.28318f;
    // Negated: VehicleSimulator's yaw response is net opposite to the
    // steering input (the tire slip chain dominates the kinematic nudge in
    // updatePhysics), so closing the loop on err needs the flipped sign —
    // positive err (target towards +heading) must yield negative steering.
    return -std::clamp(err * 0.6f, -1.0f, 1.0f);
}

float AIController::calculateThrottle(float currentSpeed, float targetSpeed,
                                      float /*curvature*/, float /*dt*/) const
{
    float err = targetSpeed - currentSpeed;
    // Stepped hold (probe-validated): a weak P gain hunted the speed by
    // +/-2 m/s at the grip limit, which re-excited the sideslip cycle.
    // A strong floor holds speed against corner scrub instead.
    if (err > 2.0f) return 1.0f;
    if (err > 0.5f) return 0.45f;
    if (err > -0.5f) return 0.1f;
    return 0.0f;
}

float AIController::calculateBrake(float currentSpeed, float targetSpeed,
                                   float /*curvature*/, float /*dt*/) const
{
    float err = currentSpeed - targetSpeed;
    if (err <= 1.0f) return 0.0f;
    return std::clamp(err * 0.1f, 0.0f, 1.0f);
}

int AIController::calculateGear(float speed) const
{
    // speed m/s rough gear map
    if (speed < 10) return 1;
    if (speed < 20) return 2;
    if (speed < 30) return 3;
    if (speed < 40) return 4;
    if (speed < 50) return 5;
    return 6;
}

} // namespace ks::sim
