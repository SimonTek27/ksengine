#pragma once
#include "KsExport.h"

#include "PhysicsCoreTypes.h"
#include <string>
#include <vector>
#include <utility>
#include <map>
#include <cmath>

namespace ks {
namespace physics {

class KSENGINE_API EngineModel {
public:
    EngineModel();

    struct TorquePoint {
        float rpm = 0.0f;
        float torque = 0.0f;
        float power = 0.0f;
    };

    struct TurboConfig {
        bool enabled = false;
        float boostPressure = 0.0f;
        float maxBoost = 1.5f;
        float turboLag = 0.5f;
        float wastegateRPM = 7000.0f;
        bool antiLag = false;
        float antiLagThrottle = 0.15f;
        std::vector<std::pair<float, float>> boostCurve;
    };

    struct GearRatio {
        int gear = 0;
        float ratio = 1.0f;
        float maxSpeed = 0.0f;
    };

    struct EngineConfig {
        float peakPower = 200.0f;
        float peakPowerRPM = 6500.0f;
        float peakTorque = 350.0f;
        float peakTorqueRPM = 4000.0f;
        float maxRPM = 7500.0f;
        float idleRPM = 800.0f;
        float revLimiter = 7500.0f;

        std::vector<TorquePoint> torqueCurve;
        std::vector<GearRatio> gearRatios;
        float finalDrive = 3.8f;
        float clutchType = 0.0f;

        float fuelCapacity = 60.0f;
        float fuelConsumption = 0.2f;
        float fuelDensity = 0.75f;

        float coastTorque = 30.0f;
        float coastRPM = 5000.0f;
        float coastNonLinearity = 0.0f;

        TurboConfig turbo;
        float engineInertia = 0.15f;
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

    void update(float dt, float throttle, float load);
    void reset();
    const EngineState& getState() const { return m_state; }
    EngineState& getState() { return m_state; }

    void setConfig(const EngineConfig& config);
    void loadFromIni(const std::string& engineIniPath);
    EngineConfig getConfig() const { return m_config; }

    float calculateTorque(float rpm) const;
    float calculatePower(float rpm) const;
    float calculateTorqueAtRPM(float rpm) const;

    float calculateTurboBoost(float rpm, float throttle) const;
    float calculateTurboTorque(float baseTorque, float boost) const;
    float updateTurboLag(float targetBoost, float dt);
    float thermalDerate() const { return m_state.thermalDerate; }
    float waterTemp() const { return m_state.waterTemp; }
    float oilTemp() const { return m_state.oilTemp; }

    float calculateFuelConsumption(float rpm, float throttle) const;
    float calculateFuelFlow(float power) const;
    float calculateEngineBraking(float rpm) const;

    float calculateWheelRPM(float engineRPM, int gear) const;
    float calculateSpeed(float engineRPM, int gear) const;
    int calculateOptimalGear(float speed, float rpm) const;
    std::vector<float> calculateSpeedsAtRPM(float rpm) const;

    bool isAtRevLimiter(float rpm) const;
    float calculateRevLimiterTorque(float rpm) const;

    static EngineConfig getInline4_2000();
    static EngineConfig getV6_3000();
    static EngineConfig getV8_4000();
    static EngineConfig getV10_5000();
    static EngineConfig getV12_6000();
    static EngineConfig getRotary_1300();
    static EngineConfig getElectric();

    static std::vector<TorquePoint> loadPowerLut(const std::string& lutPath);
    static bool savePowerLut(const std::vector<TorquePoint>& curve, const std::string& lutPath);
    static std::vector<TorquePoint> interpolateCurve(const std::vector<TorquePoint>& points, int targetPoints);
    static bool validateConfig(const EngineConfig& config, std::string* error = nullptr);

    static float rpmToRadPerSec(float rpm) { return rpm * 2.0f * 3.14159f / 60.0f; }
    static float radPerSecToRPM(float radPerSec) { return radPerSec * 60.0f / (2.0f * 3.14159f); }
    static float kWtoHP(float kw) { return kw * 1.341f; }
    static float HPtoKW(float hp) { return hp / 1.341f; }
    static float NmToLBFT(float nm) { return nm * 0.7376f; }

private:
    float interpolateTorqueCurve(float rpm) const;
    float interpolateBoostCurve(float rpm) const;
    void rebuildCurveIfEmpty();
    EngineConfig m_config;
    EngineState m_state;
    float m_currentBoost = 0.0f;
};

class EngineModelManager {
public:
    EngineModelManager();

    EngineModel& model() { return m_model; }
    const EngineModel& model() const { return m_model; }

    void loadFromIni(const std::string& engineIniPath);
    void loadPowerLut(const std::string& lutPath);
    void saveToIni(const std::string& engineIniPath) const;
    void savePowerLut(const std::string& lutPath) const;

    void update(float dt, float throttle, float load);

    float getMaxPower() const;
    float getMaxTorque() const;
    float get0100Time() const;
    float getTopSpeed() const;
    std::vector<float> getTorqueCurve() const;
    std::vector<float> getPowerCurve() const;

    std::map<std::string, std::pair<float, float>> compareEngines(const EngineModelManager& other) const;

private:
    EngineModel m_model;
    float m_weight = 1300.0f;
    float m_dragCoefficient = 0.35f;
    float m_frontalArea = 2.0f;
};

} // namespace physics
} // namespace ks
