#pragma once

/**
 * Engine, drivetrain, and fuel management simulation — Qt-free.
 */

#include "PhysicsCoreTypes.h"
#include <vector>
#include <memory>
#include <string>
#include <cstdint>

namespace ks {
namespace physics {

struct FuelManagementModel;

struct EngineConfig {
    double maxPowerKw = 350.0;
    double maxRpm = 7500.0;
    double idleRpm = 800.0;
    double peakTorqueRpm = 4000.0;
    double peakTorqueNm = 400.0;
    double revLimit = 8000.0;
    double engineBrakingFactor = 0.1;
};

struct DrivetrainConfig {
    DriveLayout driveLayout = DriveLayout::RWD;
    std::vector<double> gearRatios = {3.5, 2.5, 1.8, 1.4, 1.1, 0.9};
    double finalDriveRatio = 3.8;
    double wheelRadius = 0.33;
    double clutchSlipFactor = 0.05;
};

struct FuelConfig {
    bool consumptionEnabled = true;
    double capacityLiters = 80.0;
    double fuelDensityKgPerLiter = 0.75;
    double consumptionFactor = 0.2;
};

struct EngineState {
    double rpm = 800.0;
    double torque = 0.0;
    double power = 0.0;
    double throttle = 0.0;
    bool isRunning = false;
    double temperature = 80.0;
};

struct DrivetrainState {
    int currentGear = 1;
    double wheelSpeed = 0.0;
    double engineRpm = 0.0;
    double axleTorque = 0.0;
    double clutchEngagement = 1.0;
};

struct FuelState {
    double fuelLiters = 80.0;
    double consumptionRateLph = 0.0;
    double remainingRangeKm = 0.0;
};

class EngineSimulator {
public:
    EngineSimulator() = default;
    ~EngineSimulator() = default;

    void setEngineConfig(const EngineConfig& c) { m_engineCfg = c; }
    void setDrivetrainConfig(const DrivetrainConfig& c) { m_driveCfg = c; }
    void setFuelConfig(const FuelConfig& c) { m_fuelCfg = c; }

    const EngineConfig& engineConfig() const { return m_engineCfg; }
    const DrivetrainConfig& drivetrainConfig() const { return m_driveCfg; }
    const FuelConfig& fuelConfig() const { return m_fuelCfg; }

    const EngineState& engineState() const { return m_engine; }
    const DrivetrainState& drivetrainState() const { return m_drive; }
    const FuelState& fuelState() const { return m_fuel; }

    void setThrottle(double t) { m_engine.throttle = t; }
    void setRunning(bool r) { m_engine.isRunning = r; }
    void shiftUp();
    void shiftDown();
    void setGear(int g);

    void update(double dt, double vehicleSpeedMs);

    double outputTorque() const { return m_drive.axleTorque; }
    double currentRpm() const { return m_engine.rpm; }

private:
    double torqueAtRpm(double rpm, double throttle) const;
    void updateFuel(double dt, double powerKw);

    EngineConfig m_engineCfg;
    DrivetrainConfig m_driveCfg;
    FuelConfig m_fuelCfg;
    EngineState m_engine;
    DrivetrainState m_drive;
    FuelState m_fuel;
};

} // namespace physics
} // namespace ks
