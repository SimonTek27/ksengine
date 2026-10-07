/**
 * Roadmap 2.1 / P2.8 — wet physics made measurable:
 *  - TrackSurface's wet grip curve (sample() loses up to 45% at full
 *    wetness, monotonic, rubber still adds grip back)
 *  - WeatherSimulator dynamics (WeatherPhysics.cpp): rain soaks the track,
 *    dry air dries it, update() is a no-op before start()
 *  - VehicleSimulator G11 aquaplaning end-to-end: two identical open-loop
 *    runs (as in test_determinism) on dry vs full-wet — μ collapses only
 *    when wet, dry keeps the full surface μ, no aquaplane on dry.
 */
#include "KsTest.h"
#include "engine/physics/TrackSurface.h"
#include "engine/physics/VehicleSimulator.h"
#include "engine/physics/WeatherPhysics.h"

#include <algorithm>
#include <cstdio>

using ks::physics::PhysVec3;
using ks::physics::TrackSurface;
using ks::physics::VehicleSimulator;
using ks::physics::WeatherSimulator;
using ks::physics::WeatherState;

namespace {

struct RunStats {
    float peakAqua = 0.f;
    float peakMu = 0.f;
    float topSpeed = 0.f;
};

// Same open-loop profile as test_determinism (5 s @ 1 kHz), with the
// TrackSurface singleton preconditioned wet or dry.
RunStats runOnWetness(bool wet) {
    TrackSurface& surf = TrackSurface::instance();
    surf.configure();
    surf.setBaseGrip(1.0f);
    surf.setWetness(wet ? 1.0f : 0.0f);

    VehicleSimulator v;
    v.reset();
    v.startSimulation();
    RunStats s;
    const double dt = 0.001;
    for (int i = 0; i < 5000; ++i) {
        const float t = static_cast<float>(i) * static_cast<float>(dt);
        v.setThrottle(t < 4.0f ? 0.9f : 0.3f);
        v.setBrake(0.f);
        v.setSteering(0.f);
        v.updatePhysics(dt);
        const auto st = v.getState();
        s.peakAqua = std::max(s.peakAqua, static_cast<float>(st.aquaplaneFactor));
        s.peakMu = std::max(s.peakMu, static_cast<float>(st.effectiveMu));
        s.topSpeed = std::max(s.topSpeed, static_cast<float>(st.speed));
    }
    return s;
}

} // namespace

int main() {
    const PhysVec3 origin{0.f, 0.f, 0.f};

    // --- Wet grip curve (TrackSurface::sample) ---------------------------
    TrackSurface& surf = TrackSurface::instance();
    surf.configure();
    surf.setBaseGrip(1.0f);

    surf.setWetness(0.0f);
    const float gripDry = surf.sample(origin).grip;
    surf.setWetness(0.5f);
    const float gripHalf = surf.sample(origin).grip;
    surf.setWetness(1.0f);
    const float gripFull = surf.sample(origin).grip;

    KS_CHECK(gripDry > 0.99f && gripDry < 1.01f);        // clean base
    KS_CHECK(gripFull < gripHalf && gripHalf < gripDry); // monotonic
    KS_CHECK(gripFull > gripDry * 0.5f); // ~-45% at full wet: a curve, not a cliff
    KS_CHECK(gripFull < gripDry * 0.6f);

    // Rubber builds grip back up on the same cell. Uses the gameplay radius
    // (1.5 m, as VehicleSimulator/SimulationLoop deposit): cells are 7.8 m
    // wide, so this only works if eachInRadius measures distance to the cell
    // box rather than to the cell center.
    surf.setWetness(0.0f);
    const float gripBefore = surf.sample(origin).grip;
    for (int i = 0; i < 100; ++i) surf.depositRubber(origin, 0.05f, 1.5f);
    const float gripAfter = surf.sample(origin).grip;
    std::printf("rubber probe: before=%.4f after=%.4f rubber=%.4f\n",
                gripBefore, gripAfter, surf.sample(origin).rubber);
    KS_CHECK(gripAfter > gripBefore);

    // --- Weather dynamics (WeatherSimulator) -----------------------------
    WeatherSimulator sim;
    WeatherState st;
    st.rainIntensity = 12.f; // heavy rain, already partly wet
    st.trackWetness = 0.3f;
    sim.setState(st);
    KS_CHECK(!sim.isRunning());
    sim.update(1.0f); // before start(): no-op
    KS_CHECK(sim.state().trackWetness == 0.3f);

    sim.start();
    for (int i = 0; i < 100; ++i) sim.update(1.0f);
    KS_CHECK(sim.state().trackWetness > 0.3f);           // rain accumulates
    KS_CHECK(sim.state().trackWetness <= 1.0f);          // clamped
    KS_CHECK(sim.effects().aquaplaningRisk > 0.0f);

    // Rain stops -> the track dries out.
    WeatherState clear = sim.state();
    clear.rainIntensity = 0.f;
    sim.setState(clear);
    for (int i = 0; i < 600; ++i) sim.update(1.0f);
    KS_CHECK(sim.state().trackWetness < 0.01f);

    // --- G11 aquaplaning end-to-end (VehicleSimulator) -------------------
    const RunStats dry = runOnWetness(false);
    const RunStats wet = runOnWetness(true);
    std::printf("wet_physics: dry(spd=%.1f mu=%.3f aqua=%.2f) wet(spd=%.1f mu=%.3f aqua=%.2f)\n",
                dry.topSpeed, dry.peakMu, dry.peakAqua,
                wet.topSpeed, wet.peakMu, wet.peakAqua);

    KS_CHECK(dry.topSpeed > 5.f);   // the script really gets moving
    KS_CHECK(wet.topSpeed > 5.f);
    KS_CHECK(dry.peakAqua == 0.0f); // never aquaplaning on dry
    KS_CHECK(wet.peakAqua > 0.1f);  // hydroplaning engages at full wet
    KS_CHECK(wet.peakMu < dry.peakMu); // μ collapses only when wet
    KS_CHECK(dry.peakMu > 0.95f);   // dry keeps the full surface μ
    KS_CHECK(wet.peakMu > 0.02f);   // but never divides down to nothing

    return KS_TEST_RESULT("wet_physics_test");
}
