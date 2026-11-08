/**
 * Parity 4 — race flags: yellow/SC reduce AI speed + AC SM `flag` channel.
 *
 * Qt-free: compiles the simulator TUs directly (track_loader_test pattern).
 */
#include "RaceSessionManager.h"
#include "GarageExit.h"
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

    // --- roadmap 2.7 follow-up: the race pit window ---
    // A race keeps the lane closed until the green (configure() leaves the
    // session inactive) and reopens it for green, yellow and safety car.
    RaceSessionManager pw;
    pw.configure(RaceConfig{});
    CHECK(!pw.pitLaneOpen());
    pw.startSession();
    CHECK(pw.pitLaneOpen());
    pw.startCountdown(0.5f);
    CHECK(!pw.pitLaneOpen());
    for (int i = 0; i < 50 && pw.isCountingDown(); ++i) pw.update(st, 0.05f);
    CHECK(pw.pitLaneOpen());
    pw.setFlag(RaceFlag::Yellow);
    CHECK(pw.pitLaneOpen());
    pw.setFlag(RaceFlag::SafetyCar);
    CHECK(pw.pitLaneOpen());
    pw.endSession();
    CHECK(!pw.pitLaneOpen());

    // The director can close an otherwise open lane, and configure() clears the
    // order instead of carrying it into the next session.
    RaceSessionManager dir;
    dir.configure(RaceConfig{});
    dir.startSession();
    CHECK(dir.pitLaneOpen());
    dir.closePitLane();
    CHECK(!dir.pitLaneOpen());
    CHECK(dir.pitLaneClosedByDirector());
    dir.configure(RaceConfig{});
    CHECK(!dir.pitLaneClosedByDirector());
    CHECK(!dir.pitLaneOpen()); // new session: green not shown yet

    // Practice owns no window: the lane stays open before, during and after it.
    RaceConfig practiceCfg;
    practiceCfg.sessionType = RaceConfig::SessionType::Practice;
    RaceSessionManager practice;
    practice.configure(practiceCfg);
    CHECK(practice.pitLaneOpen());
    practice.startSession();
    CHECK(practice.pitLaneOpen());
    practice.setFlag(RaceFlag::Yellow);
    CHECK(practice.pitLaneOpen());
    practice.endSession();
    CHECK(practice.pitLaneOpen());

    // --- the HUD line the window produces (GarageExitController::statusLabel) ---
    // Held in the box with the lane closed: the reason is visible before the
    // driver asks to leave. Rolling down an open lane keeps its own label.
    CHECK(GarageExitController::statusLabel(GarageExitPhase::InGarage, false, "IN GARAGE") ==
          "PIT CLOSED");
    CHECK(GarageExitController::statusLabel(GarageExitPhase::InGarage, true, "IN GARAGE") ==
          "IN GARAGE");
    CHECK(GarageExitController::statusLabel(GarageExitPhase::PitLane, false, "PIT LANE") ==
          "PIT LANE");
    CHECK(GarageExitController::statusLabel(GarageExitPhase::OnTrack, false, "ON TRACK") ==
          "ON TRACK");

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
