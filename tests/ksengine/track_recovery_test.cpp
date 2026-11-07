// Track recovery — a car that stops out on track with a dead engine does not
// stay there: the marshals reach it and tow it to its box. The rule must not
// touch a car that is moving, has its engine running, or is already in the pit
// lane, because in all three cases there is nothing to recover.

#include "KsTest.h"

#include "TrackRecovery.h"

#include <cmath>

namespace sim = ks::sim;

namespace {

sim::StrandedCar deadOutOnTrack() {
    sim::StrandedCar car;
    car.speedMs = 0.f;
    car.engineRunning = false;
    car.inPitLane = false;
    return car;
}

constexpr float kDt = 0.01f;

/// Steps the rule until it reports the tow, or gives up after `limitSec`.
float runUntilTowed(sim::TrackRecovery& rule, const sim::StrandedCar& car, float limitSec,
                    int* tows = nullptr) {
    int count = 0;
    float t = 0.f;
    for (; t < limitSec; t += kDt) {
        if (rule.update(kDt, car)) {
            ++count;
            if (tows) *tows = count;
            return t + kDt;
        }
    }
    if (tows) *tows = count;
    return -1.f;
}

} // namespace

int main() {
    using sim::RecoveryState;
    using sim::StrandedCar;
    using sim::TrackRecovery;

    // 1. Moving: nothing to do, and no time is banked.
    {
        TrackRecovery rule;
        StrandedCar car = deadOutOnTrack();
        car.speedMs = 20.f;
        int tows = 0;
        runUntilTowed(rule, car, 60.f, &tows);
        KS_CHECK(tows == 0);
        KS_CHECK(rule.strandedSec() == 0.f);
        KS_CHECK(rule.state() == RecoveryState::Clear);
        KS_CHECK(rule.secondsToTow() < 0.f);
    }

    // 2. Stopped but the engine runs: the driver stopped on purpose.
    {
        TrackRecovery rule;
        StrandedCar car = deadOutOnTrack();
        car.engineRunning = true;
        int tows = 0;
        runUntilTowed(rule, car, 60.f, &tows);
        KS_CHECK(tows == 0);
        KS_CHECK(rule.strandedSec() == 0.f);
    }

    // 3. A dead engine in the pit lane (or its own box): that is a pit stop.
    {
        TrackRecovery rule;
        StrandedCar car = deadOutOnTrack();
        car.inPitLane = true;
        int tows = 0;
        runUntilTowed(rule, car, 60.f, &tows);
        KS_CHECK(tows == 0);
        KS_CHECK(rule.strandedSec() == 0.f);
    }

    // 4. Dead engine out on track: armed after stopSec, towed once at
    //    stopSec + marshalSec, never twice for the same stop.
    {
        TrackRecovery rule;
        const float armedAt = rule.config().stopSec;
        const float towAtCfg = rule.config().stopSec + rule.config().marshalSec;
        StrandedCar car = deadOutOnTrack();

        float armedSeen = -1.f;
        float t = 0.f;
        float towedAt = -1.f;
        int tows = 0;
        for (; t < towAtCfg + 5.f; t += kDt) {
            const bool wasArmed = rule.armed();
            const bool towed = rule.update(kDt, car);
            if (!wasArmed && rule.armed() && armedSeen < 0.f) armedSeen = t + kDt;
            if (towed) {
                ++tows;
                if (towedAt < 0.f) towedAt = t + kDt;
                break;
            }
        }
        KS_CHECK(tows == 1);
        KS_CHECK(armedSeen > 0.f);
        KS_CHECK(std::fabs(armedSeen - armedAt) <= 0.03f);
        KS_CHECK(towedAt > 0.f);
        KS_CHECK(std::fabs(towedAt - towAtCfg) <= 0.03f);
        KS_CHECK(rule.state() == RecoveryState::Towed);

        // The caller has dropped it in the box: the next step with the car in
        // the lane clears the state, and no second tow arrives.
        car.inPitLane = true;
        int extraTows = 0;
        runUntilTowed(rule, car, 30.f, &extraTows);
        KS_CHECK(extraTows == 0);
        KS_CHECK(rule.state() == RecoveryState::Clear);
    }

    // 5. The marshals do not bank a partial stop: getting going again clears it.
    {
        TrackRecovery rule;
        StrandedCar car = deadOutOnTrack();
        for (int i = 0; i < 1000; ++i) rule.update(kDt, car); // 10 s stranded
        KS_CHECK(rule.armed());
        StrandedCar rolling = car;
        rolling.speedMs = 12.f;
        rule.update(kDt, rolling);
        KS_CHECK(rule.strandedSec() == 0.f);
        KS_CHECK(!rule.armed());

        // The clock starts again from zero, so the tow is a full wait away.
        // Stops half a second short on purpose: the threshold itself is float
        // accumulated, and this check is about the clock restarting.
        int tows = 0;
        const float towedAt =
            runUntilTowed(rule, car, rule.config().stopSec + rule.config().marshalSec - 0.5f,
                          &tows);
        KS_CHECK(tows == 0); // limit reached exactly at the threshold
        KS_CHECK(towedAt < 0.f);
    }

    // 6. secondsToTow counts down once armed and is -1 before that.
    {
        TrackRecovery rule;
        StrandedCar car = deadOutOnTrack();
        rule.update(kDt, car);
        KS_CHECK(rule.secondsToTow() < 0.f);
        for (int i = 0; i < 300; ++i) rule.update(kDt, car); // past stopSec
        KS_CHECK(rule.armed());
        const float left = rule.secondsToTow();
        KS_CHECK(left > 0.f && left <= rule.config().marshalSec + 0.01f);
        rule.update(kDt, car);
        KS_CHECK(rule.secondsToTow() < left);
    }

    return ks::test::finish("track_recovery_test");
}
