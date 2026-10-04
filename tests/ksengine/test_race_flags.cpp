/**
 * Parity 4 — race flags: yellow/SC reduce AI speed + AC SM `flag` channel.
 *
 * Qt-free: compiles the simulator TUs directly (track_loader_test pattern).
 */
#include "RaceSessionManager.h"
#include "MultiCarManager.h"
#include "adapters/assetto_corsa/AcSharedMemory.h"

#include <cmath>
#include <cstdio>

static int g_failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

using namespace ks::sim;

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main()
{
    // --- RaceSessionManager: flag lifecycle + implied AI speed factor ---
    RaceSessionManager rsm;
    RaceConfig cfg;
    cfg.trackLength = 500.0f;
    rsm.configure(cfg);
    CHECK(rsm.flag() == RaceFlag::None);

    rsm.startSession();
    CHECK(rsm.flag() == RaceFlag::Green);
    CHECK(near(rsm.aiSpeedFactor(), 1.0f));

    rsm.startCountdown(2.0f);
    CHECK(rsm.flag() == RaceFlag::None);

    ks::physics::SimulationState st{};
    for (int i = 0; i < 100 && rsm.isCountingDown(); ++i)
        rsm.update(st, 0.05f);
    CHECK(!rsm.isCountingDown());
    CHECK(rsm.flag() == RaceFlag::Green);
    CHECK(near(rsm.aiSpeedFactor(), 1.0f));

    rsm.setFlag(RaceFlag::Yellow);
    CHECK(rsm.flag() == RaceFlag::Yellow);
    CHECK(near(rsm.aiSpeedFactor(), 0.6f));

    rsm.setFlag(RaceFlag::SafetyCar);
    CHECK(near(rsm.aiSpeedFactor(), 0.5f));

    rsm.setFlag(RaceFlag::Green);
    CHECK(near(rsm.aiSpeedFactor(), 1.0f));

    rsm.endSession();
    CHECK(rsm.flag() == RaceFlag::Checkered);
    CHECK(near(rsm.aiSpeedFactor(), 1.0f));

    // --- AC shared-memory flag channel mapping (AC_FLAG_TYPE) ---
    CHECK(ks::ac::ksRaceFlagToAcFlag(0) == 0); // None      -> AC_NO_FLAG
    CHECK(ks::ac::ksRaceFlagToAcFlag(1) == 0); // Green     -> AC_NO_FLAG
    CHECK(ks::ac::ksRaceFlagToAcFlag(2) == 2); // Yellow    -> AC_YELLOW_FLAG
    CHECK(ks::ac::ksRaceFlagToAcFlag(3) == 1); // Blue      -> AC_BLUE_FLAG
    CHECK(ks::ac::ksRaceFlagToAcFlag(4) == 4); // White     -> AC_WHITE_FLAG
    CHECK(ks::ac::ksRaceFlagToAcFlag(5) == 3); // Black     -> AC_BLACK_FLAG
    CHECK(ks::ac::ksRaceFlagToAcFlag(6) == 5); // Checkered -> AC_CHECKERED_FLAG
    CHECK(ks::ac::ksRaceFlagToAcFlag(7) == 6); // Meatball  -> AC_PENALTY_FLAG
    CHECK(ks::ac::ksRaceFlagToAcFlag(8) == 2); // SafetyCar -> AC_YELLOW_FLAG

    // --- MultiCarManager: factor reaches existing and future AI cars ---
    MultiCarManager mc;
    mc.setAISpeedFactor(0.5f);

    int id1 = mc.addCar("test_car", "AI1", vec3(0.0f, 0.0f, 0.0f), false);
    CarEntry* e1 = mc.getCar(id1);
    CHECK(e1 != nullptr && e1->ai != nullptr);
    if (e1 && e1->ai) CHECK(near(e1->ai->speedFactor(), 0.5f));

    int id2 = mc.addCar("test_car2", "AI2", vec3(0.0f, 10.0f, 0.0f), false);
    CarEntry* e2 = mc.getCar(id2);
    CHECK(e2 != nullptr && e2->ai != nullptr);
    if (e2 && e2->ai) CHECK(near(e2->ai->speedFactor(), 0.5f));

    mc.setAISpeedFactor(0.6f); // yellow
    CHECK(e1 && e1->ai && near(e1->ai->speedFactor(), 0.6f));
    CHECK(e2 && e2->ai && near(e2->ai->speedFactor(), 0.6f));

    mc.setAISpeedFactor(1.0f); // green
    CHECK(e1 && e1->ai && near(e1->ai->speedFactor(), 1.0f));

    if (g_failures == 0) {
        std::printf("race_flags: OK\n");
        return 0;
    }
    std::printf("race_flags: %d FAILURES\n", g_failures);
    return 1;
}
