#pragma once
/**
 * Arc-length parameterisation of an AI line.
 *
 * An AiSpline is a list of points carrying position, speed and curvature, but
 * nothing in it answers "where am I at 340 m into the lap". Everything that
 * reasons in distance along the track needs that: sector splits, AI speed
 * look-ahead, race-position deltas between cars, camera rails, and clamping a
 * car back onto the line after an off.
 *
 * This builds a cumulative-distance table once per line and answers queries
 * against it. Positions are interpolated linearly along the chord between two
 * authored points - not through a spline basis - for the same reason the AI
 * speed samples are: an authored value must never overshoot between knots.
 *
 * The spline is only read, never modified, so a loaded line can be shared.
 */
#include "KsExport.h"
#include "AiFileReader.h"

#include <cstddef>
#include <vector>

namespace ks {
namespace ai {

class KSENGINE_API AiSplineArcLength {
public:
    /** A point sampled by distance along the line. */
    struct Frame {
        AiVec3 position;
        float distance = 0.0f;   // arc length from the first point
        float speed = 0.0f;      // interpolated between the authored speeds
        float curvature = 0.0f;  // 1/radius, interpolated
        std::size_t segment = 0; // index of the segment the sample fell in
    };

    /** Build the distance table. False when the line has fewer than two points
     *  or no length at all (all points coincident); the previous table is
     *  dropped either way, so a failed build never leaves stale data. */
    bool build(const AiSpline& spline);
    void clear();

    bool isValid() const { return m_points.size() >= 2 && m_length > 0.0f; }

    /** Total arc length in metres. For a closed line this includes the closing
     *  segment back to the first point. */
    float length() const { return m_length; }
    bool isClosed() const { return m_closed; }
    std::size_t pointCount() const { return m_points.size(); }

    /** Cumulative distance of authored point i (clamped to the length). */
    float pointDistance(std::size_t i) const;

    /** Sample at a distance. A closed line wraps; an open one clamps. */
    Frame atDistance(float distance) const;

    /** Sample at parametric t in [0,1] (the same wrap/clamp rule applies). */
    Frame atParametric(float t) const;

    /** Distance <-> parametric t conversions (t is linear in arc length). */
    float distanceToParametric(float distance) const;
    float parametricToDistance(float t) const;

    /** Nearest point on the line. Brute force over the segments: an AI line is
     *  a few thousand points, so this is a linear scan and stays exact. */
    Frame closest(const AiVec3& point) const;

    /** `count` frames spaced evenly by arc length. A closed line does not
     *  repeat the first frame at the end. */
    void sampleUniform(int count, std::vector<Frame>& out) const;

private:
    /** Segment containing `distance`, with the local 0..1 parameter in `localT`. */
    std::size_t segmentOf(float distance, float& localT) const;
    Frame frameIn(const AiVec3& a, const AiVec3& b, float localT,
                  std::size_t segment) const;

    std::vector<AiVec3> m_points;
    std::vector<float> m_cumulative; // size == m_points.size()
    std::vector<float> m_speed;
    std::vector<float> m_curvature;
    float m_length = 0.0f;
    bool m_closed = false;
};

} // namespace ai
} // namespace ks
