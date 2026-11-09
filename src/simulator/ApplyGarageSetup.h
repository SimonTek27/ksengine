#pragma once
/**
 * Garage setup -> vehicle physics (GAP_MATRIX P0.3).
 *
 * The garage screen edits SetupData; the physics reads the vehicle's own
 * SetupParams. The two are not the same shape (per-wheel pressures here, an
 * array there; different field names), so the conversion lives in one place.
 *
 * The vehicle's params also carry values the screen has no row for (anti-roll
 * bars, roll centres, compliances, roll steer). The conversion starts from the
 * car's current params and overwrites only what the screen owns, so a garage
 * change never throws away what the car's data files loaded.
 */
#include "SetupGarage.h"
#include "engine/physics/VehicleSimulator.h"

namespace ks {
namespace sim {

inline ks::physics::VehicleSimulator::SetupParams setupParamsFromGarage(
    const SetupData& s, const ks::physics::VehicleSimulator::SetupParams& base) {
    ks::physics::VehicleSimulator::SetupParams p = base;
    // Index order matches the vehicle: FL=0, FR=1, RL=2, RR=3.
    p.tirePsi[0] = s.tirePressureFL;
    p.tirePsi[1] = s.tirePressureFR;
    p.tirePsi[2] = s.tirePressureRL;
    p.tirePsi[3] = s.tirePressureRR;
    p.brakeBias = s.brakeBias;
    p.rideHeightFrontMm = s.rideHeightFront;
    p.rideHeightRearMm = s.rideHeightRear;
    p.springRateFront = s.springRateFront;
    p.springRateRear = s.springRateRear;
    p.frontWingDeg = s.frontWingAngle;
    p.rearWingDeg = s.rearWingAngle;
    p.diffPreloadNm = s.diffPreload;
    p.fuelL = s.fuel;
    p.ballastKg = s.ballast;
    p.tcLevel = s.tcLevel;
    p.absLevel = s.absLevel;
    return p;
}

} // namespace sim
} // namespace ks
