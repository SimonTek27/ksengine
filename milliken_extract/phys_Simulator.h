#pragma once

/**
 * @file phys_Simulator.h
 * @brief High-level vehicle physics facade — Qt-free
 *
 * Integrates AeroSimulator and a simplified longitudinal model.
 * VehicleSimulator (VehiclePhysics) can be wired later when that module is Qt-free.
 */

#include "PhysicsCoreTypes.h"
#include "AeroSimulator.h"
#include "DamageSystem.h"
#include "TireSimulator.h"
#include "EngineModel.h"
#include "WeatherPhysics.h"
#include "SuspensionModel.h"
#include "DifferentialModel.h"
#include "BrakeThermalModel.h"
#include "PhysicsProfiler.h"
#include "HybridSystem.h"
#include "TrackSurface.h"
#include "ChassisSimulator.h"

#include <functional>
#include <string>
#include <algorithm>
#include <vector>

namespace ks {

/**
 * Singleton-style simulator used by tools / editor / runtime.
 * Not a QObject: use std::function callbacks instead of signals.
 */
class phys_Simulator {
public:
    static phys_Simulator* instance();

    phys_Simulator(const phys_Simulator&) = delete;
    phys_Simulator& operator=(const phys_Simulator&) = delete;

    void startSimulation();
    void stopSimulation();
    void reset();
    /** Advance physics by dt seconds (call from your tick). */
    void update(double dt);

    void setThrottle(double value);
    void setBrake(double value);
    void setSteering(double value);

    physics::SimulationState getState() const;
    bool isRunning() const { return m_running; }

    void setTireModel(const physics::TireSlipCurve& curve);
    physics::TireSlipCurve tireModel() const { return m_tireCurve; }

    physics::LapTimeEstimate estimateLapTime() const;

    void setMass(double kg);
    void setEnginePower(double kw);
    void setMaxRpm(double rpm);
    void setDragCoeff(double cd);
    void setFrontalArea(double area);
    void setWheelBase(double wb);
    void setTrackWidth(double tw);

    double mass() const { return m_mass; }
    double enginePower() const { return m_enginePowerKw; }
    double maxRpm() const { return m_maxRpm; }

    void setAbsEnabled(bool enabled) { m_absEnabled = enabled; }
    bool absEnabled() const { return m_absEnabled; }
    void setTractionControlEnabled(bool enabled) { m_tcEnabled = enabled; }
    bool tractionControlEnabled() const { return m_tcEnabled; }
    void setAbsThreshold(double slipRatio) { m_absThreshold = slipRatio; }
    void setTcThreshold(double slipRatio) { m_tcThreshold = slipRatio; }
    double absThreshold() const { return m_absThreshold; }
    double tcThreshold() const { return m_tcThreshold; }

    float getBrakeDiscTemp(int wheel) const;
    float getBrakePadTemp(int wheel) const;
    float getBrakeFade(int wheel) const;

    void setErsEnabled(bool enabled) { m_ersEnabled = enabled; m_hybrid.setEnabled(enabled); }
    bool ersEnabled() const { return m_ersEnabled; }
    void setErsMode(int mode) {
        m_ersMode = mode;
        m_hybrid.setMode(static_cast<physics::HybridSystem::Mode>(std::clamp(mode, 0, 3)));
    }
    void activateErsAttackMode() { m_ersMode = 3; m_hybrid.activateAttack(); }

    void setDriveLayout(physics::DriveLayout layout) { m_driveLayout = layout; }
    physics::DriveLayout driveLayout() const { return m_driveLayout; }
    void setCenterDiffPreload(double nm) { m_centerDiffPreload = nm; }
    double centerDiffPreload() const { return m_centerDiffPreload; }
    void setCenterDiffPower(double power) { m_centerDiffPower = power; }
    double centerDiffPower() const { return m_centerDiffPower; }

    void setDrsEnabled(bool enabled) { m_drsEnabled = enabled; }
    bool drsEnabled() const { return m_drsEnabled; }
    void setDrsAutoActivate(bool autoActivate) { m_drsAuto = autoActivate; }
    bool drsAutoActivate() const { return m_drsAuto; }
    void setDrsSpeedThreshold(double kmh) { m_drsSpeedKmh = kmh; }
    double drsSpeedThreshold() const { return m_drsSpeedKmh; }
    void setDrsZoneStart(double dist) { m_drsZoneStart = dist; }
    void setDrsZoneEnd(double dist) { m_drsZoneEnd = dist; }
    bool isDrsActive() const { return m_drsActive; }
    double getDrsDragReduction() const { return m_drsDragReduction; }
    void setDrsDragReduction(double factor) { m_drsDragReduction = factor; }

    const physics::DamageState& damageState() const { return m_damage; }
    physics::DamageState& damageState() { return m_damage; }
    void applyCollisionDamage(double impactForce);
    void resetDamage();
    void enableDamageModel(bool enabled) { m_damageEnabled = enabled; }
    bool isDamageModelEnabled() const { return m_damageEnabled; }
    physics::DamageSystem& damageSystem() { return m_damageSystem; }
    const physics::DamageSystem& damageSystem() const { return m_damageSystem; }

    void setWeatherState(const physics::WeatherState& weather);
    const physics::WeatherState& weatherState() const { return m_weather; }
    physics::WeatherState& weatherState() { return m_weather; }
    void setTrackWetness(double wetness) { m_weather.trackWetness = static_cast<float>(wetness); }
    void setRainIntensity(double mmh) { m_weather.rainIntensity = static_cast<float>(mmh); }
    double getAquaplaningRisk() const { return m_weather.aquaplaningRisk(); }
    double getTrackGripReduction() const { return m_weather.gripReduction(); }
    void setAirDensity(double density) { m_weather.airDensity = static_cast<float>(density); }

    void setFuelConsumptionEnabled(bool enabled) { m_fuelEnabled = enabled; }
    bool isFuelConsumptionEnabled() const { return m_fuelEnabled; }
    double getFuelKg() const { return m_fuelKg; }
    void setFuelKg(double kg) { m_fuelKg = kg; }
    double getFuelCapacity() const { return m_fuelCapacityL; }
    void setFuelCapacity(double liters) { m_fuelCapacityL = liters; }
    double getEffectiveMass() const { return m_mass + m_fuelKg; }

    void loadVehicleParams(const std::string& carPath);
    void loadEngineFromIni(const std::string& engineIniPath);
    void loadTyresFromIni(const std::string& tyresIniPath);
    void loadDrivetrainFromIni(const std::string& drivetrainIniPath);
    void loadAeroFromIni(const std::string& aeroIniPath);
    void loadSuspensionFromIni(const std::string& suspensionIniPath);

    physics::WheelState wheelState(int wheel) const;

    physics::ValidationMetrics validateAgainstTelemetry(
        const std::vector<double>& timestamps,
        const std::vector<double>& refSpeed,
        const std::vector<double>& refLateralG,
        const std::vector<double>& refLongG,
        const std::vector<double>& refRPM,
        const std::vector<double>& refThrottle,
        const std::vector<double>& refBrake,
        const std::vector<double>& refSteering) const;

    /** Access integrated aero stack. */
    physics::AeroSimulator& aero() { return m_aero; }
    const physics::AeroSimulator& aero() const { return m_aero; }

    physics::TireSimulator& tires() { return m_tires; }
    const physics::TireSimulator& tires() const { return m_tires; }

    physics::EngineModel& engine() { return m_engine; }
    const physics::EngineModel& engine() const { return m_engine; }
    physics::WeatherSimulator& weatherSim() { return m_weatherSim; }
    const physics::WeatherSimulator& weatherSim() const { return m_weatherSim; }

    physics::SuspensionModel& suspension() { return m_suspension; }
    const physics::SuspensionModel& suspension() const { return m_suspension; }

    physics::DifferentialModel& differential() { return m_diff; }
    const physics::DifferentialModel& differential() const { return m_diff; }
    physics::GearboxModel& gearbox() { return m_gearbox; }
    const physics::GearboxModel& gearbox() const { return m_gearbox; }
    void setGear(int gear);
    int currentGear() const { return m_gearbox.gear(); }
    void setAutoShift(bool enabled) { m_autoShift = enabled; }
    bool autoShift() const { return m_autoShift; }

    physics::BrakeThermalModel& brakeThermal() { return m_brakeThermal; }
    const physics::BrakeThermalModel& brakeThermal() const { return m_brakeThermal; }

    physics::HybridSystem& hybrid() { return m_hybrid; }
    const physics::HybridSystem& hybrid() const { return m_hybrid; }

    physics::ChassisSimulator& chassis() { return m_chassis; }
    const physics::ChassisSimulator& chassis() const { return m_chassis; }
    physics::TrackSurface& trackSurface() { return physics::TrackSurface::instance(); }

    /** Optional leader position for draft (nullptr = none). */
    void setDraftLeader(const physics::PhysVec3* leaderWorldPos) { m_draftLeader = leaderWorldPos; }

    // Callbacks (replace Qt signals)
    std::function<void(const physics::SimulationState&)> onStateUpdated;
    std::function<void()> onSimulationStarted;
    std::function<void()> onSimulationReset;
    std::function<void()> onSimulationStopped;
    std::function<void(double slipAngle, double lateralForce, double slipRatio, double longitudinalForce)> onTireDataUpdated;

private:
    phys_Simulator();
    ~phys_Simulator() = default;

    static phys_Simulator* s_instance;

    void stepLongitudinal(double dt);
    void updateAero(double dt);
    void syncStateFromInternal();

    bool m_running = false;
    double m_throttle = 0.0;
    double m_brake = 0.0;
    double m_steering = 0.0;

    double m_mass = 1300.0;
    double m_enginePowerKw = 300.0;
    double m_maxRpm = 7500.0;
    double m_dragCd = 0.35;
    double m_frontalArea = 2.0;
    double m_wheelbase = 2.7;
    double m_trackWidth = 1.6;

    bool m_absEnabled = true;
    bool m_tcEnabled = true;
    double m_absThreshold = 0.15;
    double m_tcThreshold = 0.12;

    bool m_ersEnabled = false;
    int m_ersMode = 0;

    physics::DriveLayout m_driveLayout = physics::DriveLayout::RWD;
    double m_centerDiffPreload = 0.0;
    double m_centerDiffPower = 0.5;

    bool m_drsEnabled = false;
    bool m_drsAuto = false;
    bool m_drsActive = false;
    double m_drsSpeedKmh = 100.0;
    double m_drsZoneStart = 0.0;
    double m_drsZoneEnd = 0.0;
    double m_drsDragReduction = 0.15;

    physics::DamageState m_damage; // lightweight mirror for API
    physics::DamageSystem m_damageSystem;
    bool m_damageEnabled = true;
    physics::WeatherState m_weather;
    physics::TireSlipCurve m_tireCurve;

    bool m_fuelEnabled = true;
    double m_fuelKg = 50.0;
    double m_fuelCapacityL = 100.0;

    physics::SimulationState m_state;
    physics::AeroSimulator m_aero;
    physics::TireSimulator m_tires;
    physics::EngineModel m_engine;
    physics::WeatherSimulator m_weatherSim;
    physics::SuspensionModel m_suspension;
    physics::DifferentialModel m_diff;
    physics::GearboxModel m_gearbox;
    physics::BrakeThermalModel m_brakeThermal;
    physics::HybridSystem m_hybrid;
    physics::ChassisSimulator m_chassis;
    const physics::PhysVec3* m_draftLeader = nullptr;

    float m_brakeDiscTemp[4] = {80, 80, 80, 80};
    float m_brakePadTemp[4] = {60, 60, 60, 60};
    float m_brakeFade[4] = {0, 0, 0, 0};

    double m_rideHeightFront = 0.05;
    double m_rideHeightRear = 0.07;
    bool m_autoShift = true;
};

} // namespace ks
