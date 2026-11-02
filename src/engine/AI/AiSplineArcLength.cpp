#include "AiSplineArcLength.h"

#include <algorithm>
#include <cmath>

namespace ks {
namespace ai {
namespace {

/** Two authored points closer than this are the same place: a duplicated
 *  closing point must not create a zero-length segment, and a zero-length
 *  segment would make the local parameter divide by zero. */
constexpr float kEpsilon = 1e-4f;

float distanceBetween(const AiVec3& a, const AiVec3& b)
{
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float dz = b.z - a.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float lerp(float a, float b, float t)
{
    return a + (b - a) * t;
}

} // namespace

void AiSplineArcLength::clear()
{
    m_points.clear();
    m_cumulative.clear();
    m_speed.clear();
    m_curvature.clear();
    m_length = 0.0f;
    m_closed = false;
}

bool AiSplineArcLength::build(const AiSpline& spline)
{
    clear();
    if (spline.points.size() < 2) return false;

    m_points.reserve(spline.points.size());
    m_speed.reserve(spline.points.size());
    m_curvature.reserve(spline.points.size());
    for (const AiSplinePoint& p : spline.points) {
        m_points.push_back(p.position);
        m_speed.push_back(p.speed);
        m_curvature.push_back(p.curvature);
    }

    // A closed line whose file repeated the first point at the end is the
    // common case (fast_lane.ai does it). Drop the duplicate and let the
    // closing segment below represent that edge, so the loop is walked once
    // and no zero-length segment is left in the table.
    if (spline.closed) {
        while (m_points.size() > 2 &&
               distanceBetween(m_points.front(), m_points.back()) <= kEpsilon) {
            m_points.pop_back();
            m_speed.pop_back();
            m_curvature.pop_back();
        }
    }
    m_closed = spline.closed;

    m_cumulative.assign(m_points.size(), 0.0f);
    float total = 0.0f;
    for (std::size_t i = 1; i < m_points.size(); ++i) {
        total += distanceBetween(m_points[i - 1], m_points[i]);
        m_cumulative[i] = total;
    }

    float closingLength = 0.0f;
    if (m_closed) {
        closingLength = distanceBetween(m_points.back(), m_points.front());
        // A loop that ends where it starts already covers the closing edge:
        // treating it as open is right, wrapping would repeat the last segment.
        if (closingLength <= kEpsilon) m_closed = false;
    }
    m_length = total + (m_closed ? closingLength : 0.0f);

    if (!(m_length > kEpsilon)) {
        clear();
        return false;
    }
    return true;
}

float AiSplineArcLength::pointDistance(std::size_t i) const
{
    if (m_cumulative.empty()) return 0.0f;
    if (i >= m_cumulative.size()) return m_length;
    return std::min(m_cumulative[i], m_length);
}

std::size_t AiSplineArcLength::segmentOf(float distance, float& localT) const
{
    localT = 0.0f;
    if (m_points.size() < 2) return 0;

    const std::size_t segments = m_points.size() - 1;
    if (m_closed && distance >= m_cumulative.back()) {
        // Closing segment: last point back to the first.
        const float len = m_length - m_cumulative.back();
        const float d = std::max(0.0f, distance - m_cumulative.back());
        localT = len > kEpsilon ? std::min(1.0f, d / len) : 0.0f;
        return segments;
    }

    // First authored point whose cumulative distance is past `distance`.
    const auto it = std::upper_bound(m_cumulative.begin(), m_cumulative.end(), distance);
    std::size_t i = static_cast<std::size_t>(it - m_cumulative.begin());
    if (i == 0) i = 1;
    if (i > segments) i = segments;
    const std::size_t seg = i - 1;

    const float start = m_cumulative[seg];
    const float end = seg + 1 < m_cumulative.size() ? m_cumulative[seg + 1] : m_length;
    const float len = end - start;
    localT = len > kEpsilon
        ? std::min(1.0f, std::max(0.0f, (distance - start) / len))
        : 0.0f;
    return seg;
}

AiSplineArcLength::Frame AiSplineArcLength::frameIn(const AiVec3& a, const AiVec3& b,
                                                    float localT,
                                                    std::size_t segment) const
{
    Frame f;
    f.position.x = lerp(a.x, b.x, localT);
    f.position.y = lerp(a.y, b.y, localT);
    f.position.z = lerp(a.z, b.z, localT);
    f.segment = segment;

    // Per-point values are interpolated per segment, never through a spline
    // basis, so an authored speed can't overshoot between two knots.
    const std::size_t i0 = std::min(segment, m_points.size() - 1);
    const std::size_t i1 = std::min(i0 + 1, m_points.size() - 1);
    f.speed = lerp(m_speed[i0], m_speed[i1], localT);
    f.curvature = lerp(m_curvature[i0], m_curvature[i1], localT);
    return f;
}

AiSplineArcLength::Frame AiSplineArcLength::atDistance(float distance) const
{
    Frame f;
    if (!isValid()) return f;

    if (m_closed) {
        const float wrapped = std::fmod(distance, m_length);
        distance = wrapped < 0.0f ? wrapped + m_length : wrapped;
    } else {
        distance = std::min(std::max(distance, 0.0f), m_length);
    }

    float localT = 0.0f;
    const std::size_t seg = segmentOf(distance, localT);
    const std::size_t segments = m_points.size() - 1;
    const bool closing = seg >= segments;
    const AiVec3& a = closing ? m_points.back() : m_points[seg];
    const AiVec3& b = closing ? m_points.front() : m_points[seg + 1];

    f = frameIn(a, b, localT, seg);
    f.distance = distance;
    return f;
}

AiSplineArcLength::Frame AiSplineArcLength::atParametric(float t) const
{
    return atDistance(parametricToDistance(t));
}

float AiSplineArcLength::distanceToParametric(float distance) const
{
    if (!(m_length > 0.0f)) return 0.0f;
    if (m_closed) {
        const float wrapped = std::fmod(distance, m_length);
        const float d = wrapped < 0.0f ? wrapped + m_length : wrapped;
        return d / m_length;
    }
    return std::min(std::max(distance, 0.0f), m_length) / m_length;
}

float AiSplineArcLength::parametricToDistance(float t) const
{
    return std::min(std::max(t, 0.0f), 1.0f) * m_length;
}

AiSplineArcLength::Frame AiSplineArcLength::closest(const AiVec3& point) const
{
    Frame best;
    if (!isValid()) return best;

    bool found = false;
    float bestSq = 0.0f;
    float bestDistance = 0.0f;

    const std::size_t segments = m_points.size() - 1;
    const std::size_t total = segments + (m_closed ? 1 : 0);
    for (std::size_t seg = 0; seg < total; ++seg) {
        const bool closing = seg >= segments;
        const AiVec3& a = closing ? m_points.back() : m_points[seg];
        const AiVec3& b = closing ? m_points.front() : m_points[seg + 1];

        const float abx = b.x - a.x;
        const float aby = b.y - a.y;
        const float abz = b.z - a.z;
        const float lenSq = abx * abx + aby * aby + abz * abz;
        if (lenSq <= kEpsilon * kEpsilon) continue;

        const float apx = point.x - a.x;
        const float apy = point.y - a.y;
        const float apz = point.z - a.z;
        float t = (apx * abx + apy * aby + apz * abz) / lenSq;
        t = std::min(1.0f, std::max(0.0f, t));

        const float px = a.x + abx * t;
        const float py = a.y + aby * t;
        const float pz = a.z + abz * t;
        const float dx = point.x - px;
        const float dy = point.y - py;
        const float dz = point.z - pz;
        const float sq = dx * dx + dy * dy + dz * dz;
        if (!found || sq < bestSq) {
            found = true;
            bestSq = sq;
            const float start = closing ? m_cumulative.back() : m_cumulative[seg];
            bestDistance = start + std::sqrt(lenSq) * t;
            best = frameIn(a, b, t, seg);
        }
    }

    if (!found) return Frame();
    best.distance = bestDistance;
    return best;
}

void AiSplineArcLength::sampleUniform(int count, std::vector<Frame>& out) const
{
    out.clear();
    if (!isValid() || count <= 0) return;
    if (count == 1) {
        out.push_back(atDistance(0.0f));
        return;
    }

    // A closed line is a ring: `count` samples cover it without repeating the
    // first one at the end.
    const float step = m_closed
        ? m_length / static_cast<float>(count)
        : m_length / static_cast<float>(count - 1);
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        out.push_back(atDistance(step * static_cast<float>(i)));
    }
}

} // namespace ai
} // namespace ks
