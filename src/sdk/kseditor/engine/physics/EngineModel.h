#pragma once

#include <QString>
#include <QVector>
#include <QJsonObject>

/**
 * @brief Engine Model for Assetto Corsa
 *
 * Simulates engine behavior including torque curves, fuel consumption, and turbo.
 * Based on:
 * - antony3d/powerlut - Engine torque/power LUT tool
 * - gro-ove/ac-torque-helper - Engine curve builder
 * - sim-racing-tools - AC conversion calculations
 * - AC engine.ini documentation
 *
 * Features:
 * - Torque/power curve interpolation
 * - Turbo boost simulation
 * - Fuel consumption modeling
 * - Engine braking
 * - Rev limiter
 * - Gear ratios
 */
class EngineModel {
public:
    struct TorquePoint {
        float rpm = 0.0f;
        float torque = 0.0f;        // Nm
        float power = 0.0f;         // kW (calculated)
    };

    struct TurboConfig {
        bool enabled = false;
        float boostPressure = 0.0f;     // bar
        float maxBoost = 1.5f;          // bar
        float turboLag = 0.5f;          // seconds
        float wastegateRPM = 7000.0f;
        bool antiLag = false;
        float antiLagThrottle = 0.15f;
        QVector<QPair<float, float>> boostCurve; // RPM -> boost
    };

    struct GearRatio {
        int gear = 0;
        float ratio = 1.0f;
        float maxSpeed = 0.0f;          // km/h at max RPM
    };

    struct EngineConfig {
        // Basic specs
        float peakPower = 200.0f;       // kW
        float peakPowerRPM = 6500.0f;
        float peakTorque = 350.0f;      // Nm
        float peakTorqueRPM = 4000.0f;
        float maxRPM = 7500.0f;
        float idleRPM = 800.0f;
        float revLimiter = 7500.0f;

        // Torque curve (RPM -> Torque LUT)
        QVector<TorquePoint> torqueCurve;

        // Transmission
        QVector<GearRatio> gearRatios;
        float finalDrive = 3.8f;
        float clutchType = 0.0f;        // 0=standard, 1=slipper

        // Fuel
        float fuelCapacity = 60.0f;     // liters
        float fuelConsumption = 0.2f;   // liters per kWh
        float fuelDensity = 0.75f;      // kg/liter

        // Engine braking
        float coastTorque = 30.0f;      // Nm at coastRPM
        float coastRPM = 5000.0f;
        float coastNonLinearity = 0.0f;

        // Turbo
        TurboConfig turbo;

        // Inertia
        float engineInertia = 0.15f;    // kg*m^2
    };

    struct EngineState {
        float rpm = 800.0f;
        float throttle = 0.0f;
        float load = 0.0f;
        float torque = 0.0f;
        float power = 0.0f;
        float fuel = 60.0f;
        float fuelFlow = 0.0f;
        float temperature = 80.0f;
        float waterTemp = 90.0f;
        float oilTemp = 95.0f;
        float boost = 0.0f;
        float thermalDerate = 1.0f;
        bool isRevLimiter = false;
        bool isOffThrottle = false;
    };

    // Core simulation
    void update(float dt, float throttle, float load);
    void reset();

    // Configuration
    void setConfig(const EngineConfig& config);
    void loadFromIni(const QString& engineIniPath);
    EngineConfig getConfig() const { return m_config; }

    // Torque calculation
    float calculateTorque(float rpm) const;
    float calculatePower(float rpm) const;
    float calculateTorqueAtRPM(float rpm) const;

    // Turbo
    float calculateTurboBoost(float rpm, float throttle) const;
    float calculateTurboTorque(float baseTorque, float boost) const;
    float updateTurboLag(float targetBoost, float dt);
    float thermalDerate() const { return m_state.thermalDerate; }
    float waterTemp() const { return m_state.waterTemp; }
    float oilTemp() const { return m_state.oilTemp; }

    // Fuel consumption
    float calculateFuelConsumption(float rpm, float throttle) const;
    float calculateFuelFlow(float power) const;

    // Engine braking
    float calculateEngineBraking(float rpm) const;

    // Gear calculations
    float calculateWheelRPM(float engineRPM, int gear) const;
    float calculateSpeed(float engineRPM, int gear) const;
    int calculateOptimalGear(float speed, float rpm) const;
    QVector<float> calculateSpeedsAtRPM(float rpm) const;

    // Rev limiter
    bool isAtRevLimiter(float rpm) const;
    float calculateRevLimiterTorque(float rpm) const;

    // Presets
    static EngineConfig getInline4_2000();
    static EngineConfig getV6_3000();
    static EngineConfig getV8_4000();
    static EngineConfig getV10_5000();
    static EngineConfig getV12_6000();
    static EngineConfig getRotary_1300();
    static EngineConfig getElectric();

    // LUT operations
    static QVector<TorquePoint> loadPowerLut(const QString& lutPath);
    static bool savePowerLut(const QVector<TorquePoint>& curve, const QString& lutPath);
    static QVector<TorquePoint> interpolateCurve(const QVector<TorquePoint>& points, int targetPoints);

    // Validation
    static bool validateConfig(const EngineConfig& config, QString* error = nullptr);

    // Utility
    static float rpmToRadPerSec(float rpm) { return rpm * 2.0f * 3.14159f / 60.0f; }
    static float radPerSecToRPM(float radPerSec) { return radPerSec * 60.0f / (2.0f * 3.14159f); }
    static float kWtoHP(float kw) { return kw * 1.341f; }
    static float HPtoKW(float hp) { return hp / 1.341f; }
    static float NmToLBFT(float nm) { return nm * 0.7376f; }

private:
    EngineConfig m_config;
    EngineState m_state;

    float interpolateTorqueCurve(float rpm) const;
    float interpolateBoostCurve(float rpm) const;
};

/**
 * @brief Engine Model Manager - High-level interface
 */
class EngineModelManager {
public:
    EngineModelManager();

    // Access
    EngineModel& model() { return m_model; }
    const EngineModel& model() const { return m_model; }

    // Configuration
    void loadFromIni(const QString& engineIniPath);
    void loadPowerLut(const QString& lutPath);
    void saveToIni(const QString& engineIniPath) const;
    void savePowerLut(const QString& lutPath) const;

    // Simulation
    void update(float dt, float throttle, float load);

    // Analysis
    float getMaxPower() const;
    float getMaxTorque() const;
    float get0100Time() const;
    float getTopSpeed() const;
    QVector<float> getTorqueCurve() const;
    QVector<float> getPowerCurve() const;

    // Comparison
    QMap<QString, QPair<float, float>> compareEngines(const EngineModelManager& other) const;

private:
    EngineModel m_model;
    float m_weight = 1300.0f;
    float m_dragCoefficient = 0.35f;
    float m_frontalArea = 2.0f;
};
