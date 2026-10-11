/**
 * Roadmap 2.7 — pit lane queue (docs/PIT_LANE_QUEUE.md).
 *
 * The queue is what makes a multi-car box exit behave: the first car asking
 * to roll out gets cleared, the others wait in their box until the lane ahead
 * is far enough. SimulationLoop wires it in updateGarageExit() through
 * shouldBlockGarageExit() (GarageExitInput.pathBlocked), so this test pins
 * the two rules that feed that call:
 *
 *  1. releaseGap: a queued car is cleared only once the one released before
 *     it is at least releaseGapM ahead on the pit axis. The header used to
 *     compare the two positions the wrong way round, which left every second
 *     car blocked forever — the reason this file exists.
 *  2. spacing: a car still within minSpacingM of someone ahead on the axis
 *     keeps waiting, even with a clear release gap.
 *
 * Geometry under test: the pit axis runs along +Z from the box row at the
 * origin (heading 0), so "along the pit" is simply the Z coordinate.
 */
#include "simulator/PitLaneQueue.h"

#include <cmath>
#include <cstdio>

static int g_failures = 0;
#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

using ks::sim::PitAxis;
using ks::sim::PitLaneQueue;
using ks::sim::PitLaneQueueConfig;

int main() {
    PitLaneQueue q;
    PitAxis axis; // lane leaves the box row along +Z
    axis.originX = 0.f;
    axis.originZ = 0.f;
    axis.heading = 0.f;
    q.setAxis(axis);
    q.setConfig(PitLaneQueueConfig{}); // releaseGap 12 m, minSpacing 8 m

    // Two cars queue to leave: the player at the box (z = 0) and an AI a
    // couple of metres behind it (z = -2, still on the box row).
    q.setSimTime(0.f);
    q.requestLeave(1, /*raceNumber*/ 7, /*garageIndex*/ 0, /*isPlayer*/ true,
                   0.f, 0.f);
    q.requestLeave(2, /*raceNumber*/ 8, /*garageIndex*/ 1, /*isPlayer*/ false,
                   0.f, -2.f);
    q.updateCar(1, 0.f, 0.f, 0.f);
    q.updateCar(2, 0.f, -2.f, 0.f);
    q.update(1.f / 60.f);

    // Head of the queue rolls out; the second one is held in its box.
    CHECK(q.isCleared(1));
    CHECK(!q.shouldBlockGarageExit(1));
    CHECK(q.isWaiting(2));
    CHECK(q.shouldBlockGarageExit(2));
    CHECK(q.leaveQueuePosition(2) == 1);
    CHECK(q.leaveQueueLength() == 2);

    // The leader gets a full releaseGap down the lane: now the second car is
    // free to go as well (this is the regression the old comparison broke).
    q.updateCar(1, 0.f, 15.f, 20.f);
    q.update(1.f / 60.f);
    CHECK(q.isCleared(1)); // still on its way out
    CHECK(q.isCleared(2));
    CHECK(!q.shouldBlockGarageExit(2));

    // Only part of the gap (8 m < 12 m) is not enough: spacing alone would
    // allow it, the release gap does not.
    q.updateCar(1, 0.f, 6.f, 10.f);
    q.update(1.f / 60.f);
    CHECK(q.isWaiting(2));
    CHECK(q.shouldBlockGarageExit(2));

    // The leader leaves the queue once it is on track: the queue shrinks and
    // the remaining car keeps its place.
    q.markMoving(1);
    q.markDone(1);
    q.updateCar(1, 0.f, 150.f, 30.f);
    q.update(1.f / 60.f);
    CHECK(q.leaveQueueLength() == 1);
    CHECK(q.isCleared(2));
    CHECK(!q.shouldBlockGarageExit(2));

    // A car that never asked to leave is never blocked by the queue.
    CHECK(!q.shouldBlockGarageExit(99));

    if (g_failures == 0) {
        std::printf("pit_queue: OK\n");
        return 0;
    }
    std::printf("pit_queue: %d FAILURES\n", g_failures);
    return 1;
}
