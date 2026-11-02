/**
 * AI line arc-length parameterisation: distances, sampling by distance,
 * wrapping on a closed loop and the nearest-point query.
 *
 * The line here is built in memory on purpose - this covers the arc-length
 * table, not the file readers (ai_line_test is about parsing).
 */
#include "engine/AI/AiSplineArcLength.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                    \
    } while (0)

bool nearValue(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) <= eps;
}

ks::ai::AiSplinePoint point(float x, float y, float z, float speed = 0.0f,
                            float curvature = 0.0f)
{
    ks::ai::AiSplinePoint p;
    p.position.x = x;
    p.position.y = y;
    p.position.z = z;
    p.speed = speed;
    p.curvature = curvature;
    return p;
}

/** Straight line along +Z: 0 m, 100 m, 200 m, speeds 10/20/30 m/s. */
ks::ai::AiSpline straightLine()
{
    ks::ai::AiSpline s;
    s.points.push_back(point(0.f, 0.f, 0.f, 10.f, 0.1f));
    s.points.push_back(point(0.f, 0.f, 100.f, 20.f, 0.2f));
    s.points.push_back(point(0.f, 0.f, 200.f, 30.f, 0.3f));
    return s;
}

/** 100 x 100 square, closed, with the first point repeated at the end - the
 *  layout real fast_lane.ai files use. Perimeter 400 m. */
ks::ai::AiSpline closedSquare()
{
    ks::ai::AiSpline s;
    s.points.push_back(point(0.f, 0.f, 0.f, 10.f));
    s.points.push_back(point(100.f, 0.f, 0.f, 10.f));
    s.points.push_back(point(100.f, 0.f, 100.f, 10.f));
    s.points.push_back(point(0.f, 0.f, 100.f, 10.f));
    s.points.push_back(point(0.f, 0.f, 0.f, 10.f)); // duplicated tail
    s.closed = true;
    return s;
}

} // namespace

int main()
{
    // --- build rejects what has no length -------------------------------
    {
        ks::ai::AiSpline empty;
        ks::ai::AiSplineArcLength arc;
        CHECK(!arc.build(empty));
        CHECK(!arc.isValid());

        ks::ai::AiSpline single;
        single.points.push_back(point(0.f, 0.f, 0.f));
        CHECK(!arc.build(single));
        CHECK(!arc.isValid());
    }

    // --- straight line: length, clamping, interpolation ------------------
    {
        ks::ai::AiSplineArcLength arc;
        CHECK(arc.build(straightLine()));
        CHECK(arc.isValid());
        CHECK(!arc.isClosed());
        CHECK(nearValue(arc.length(), 200.f));
        CHECK(nearValue(arc.pointDistance(0), 0.f));
        CHECK(nearValue(arc.pointDistance(1), 100.f));
        CHECK(nearValue(arc.pointDistance(2), 200.f));

        const auto mid = arc.atDistance(50.f);
        CHECK(nearValue(mid.position.z, 50.f));
        CHECK(nearValue(mid.distance, 50.f));
        CHECK(nearValue(mid.speed, 15.f));    // 10 -> 20 m/s halfway
        CHECK(nearValue(mid.curvature, 0.15f));
        CHECK(mid.segment == 0);

        // An open line clamps instead of wrapping.
        CHECK(nearValue(arc.atDistance(-40.f).position.z, 0.f));
        CHECK(nearValue(arc.atDistance(-40.f).distance, 0.f));
        CHECK(nearValue(arc.atDistance(9999.f).position.z, 200.f));
        CHECK(nearValue(arc.atDistance(9999.f).distance, 200.f));

        CHECK(nearValue(arc.atDistance(150.f).speed, 25.f));
        CHECK(nearValue(arc.atDistance(200.f).speed, 30.f));
    }

    // --- parametric <-> distance ----------------------------------------
    {
        ks::ai::AiSplineArcLength arc;
        CHECK(arc.build(straightLine()));
        CHECK(nearValue(arc.distanceToParametric(50.f), 0.25f));
        CHECK(nearValue(arc.distanceToParametric(-10.f), 0.f));
        CHECK(nearValue(arc.distanceToParametric(500.f), 1.f));
        CHECK(nearValue(arc.parametricToDistance(0.5f), 100.f));
        CHECK(nearValue(arc.parametricToDistance(2.f), 200.f));
        CHECK(nearValue(arc.atParametric(0.25f).position.z, 50.f));
    }

    // --- closed loop wraps ----------------------------------------------
    {
        ks::ai::AiSplineArcLength arc;
        CHECK(arc.build(closedSquare()));
        CHECK(arc.isValid());
        CHECK(arc.isClosed());
        // The duplicated tail point is dropped, not counted as a fifth edge.
        CHECK(arc.pointCount() == 4);
        CHECK(nearValue(arc.length(), 400.f));

        CHECK(nearValue(arc.atDistance(410.f).position.x, 10.f));  // == at 10 m
        CHECK(nearValue(arc.atDistance(410.f).distance, 10.f));
        CHECK(nearValue(arc.atDistance(-10.f).distance, 390.f));
        CHECK(nearValue(arc.distanceToParametric(410.f), 0.025f));

        // Crossing a corner stays on the line.
        const auto corner = arc.atDistance(100.f);
        CHECK(nearValue(corner.position.x, 100.f));
        CHECK(nearValue(corner.position.z, 0.f));

        // The closing edge (last -> first point) is part of the loop.
        const auto closingEdge = arc.atDistance(350.f);
        CHECK(nearValue(closingEdge.position.x, 0.f));
        CHECK(nearValue(closingEdge.position.z, 50.f));
    }

    // --- nearest point ---------------------------------------------------
    {
        ks::ai::AiSplineArcLength arc;
        CHECK(arc.build(straightLine()));
        const auto hit = arc.closest({5.f, 0.f, 150.f});
        CHECK(nearValue(hit.position.z, 150.f));
        CHECK(nearValue(hit.position.x, 0.f));
        CHECK(nearValue(hit.distance, 150.f));
        CHECK(nearValue(hit.speed, 25.f));

        // Before the start / past the end the query clamps to the endpoint.
        CHECK(nearValue(arc.closest({0.f, 0.f, -30.f}).distance, 0.f));
        CHECK(nearValue(arc.closest({0.f, 0.f, 900.f}).distance, 200.f));
    }

    {
        ks::ai::AiSplineArcLength arc;
        CHECK(arc.build(closedSquare()));
        // Near the second edge (x=100 wall), from inside the square.
        const auto hit = arc.closest({90.f, 0.f, 50.f});
        CHECK(nearValue(hit.position.x, 100.f));
        CHECK(nearValue(hit.distance, 150.f));
    }

    // --- uniform sampling ------------------------------------------------
    {
        ks::ai::AiSplineArcLength arc;
        CHECK(arc.build(straightLine()));
        std::vector<ks::ai::AiSplineArcLength::Frame> frames;
        arc.sampleUniform(5, frames);
        CHECK(frames.size() == 5);
        CHECK(nearValue(frames.front().distance, 0.f));
        CHECK(nearValue(frames.back().distance, 200.f));
        CHECK(nearValue(frames[2].position.z, 100.f));
        CHECK(nearValue(frames[1].distance, 50.f));
    }

    {
        ks::ai::AiSplineArcLength arc;
        CHECK(arc.build(closedSquare()));
        std::vector<ks::ai::AiSplineArcLength::Frame> frames;
        // A ring samples with spacing length/count: no repeated first frame.
        arc.sampleUniform(4, frames);
        CHECK(frames.size() == 4);
        CHECK(nearValue(frames[0].distance, 0.f));
        CHECK(nearValue(frames[1].distance, 100.f));
        CHECK(nearValue(frames[3].distance, 300.f));
    }

    // --- a failed rebuild does not leave stale data -----------------------
    {
        ks::ai::AiSplineArcLength arc;
        CHECK(arc.build(straightLine()));
        ks::ai::AiSpline broken;
        broken.points.push_back(point(0.f, 0.f, 0.f));
        CHECK(!arc.build(broken));
        CHECK(!arc.isValid());
        CHECK(nearValue(arc.length(), 0.f));
    }

    if (g_failures == 0) {
        std::printf("ai_spline_arc: %d checks OK\n", g_checks);
        return 0;
    }
    std::printf("ai_spline_arc: %d of %d checks FAILED\n", g_failures, g_checks);
    return 1;
}
