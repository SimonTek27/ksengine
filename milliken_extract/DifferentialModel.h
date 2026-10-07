#pragma once

/**
 * @file DifferentialModel.h
 * @brief Open / LSD / locked differential + simple gearbox — Qt-free
 */

#include <cmath>
#include <string>
#include <vector>

namespace ks {
namespace physics {

class DifferentialModel {
public:
    enum class DiffType {
        Open,
        LSD_Cls,
        LSD_Viscous,
        LSD_Geared,
        Locked,
        Active
    };

    struct DiffConfig {
        DiffType type = DiffType::LSD_Cls;
        float preload = 20.0f;
        float coastPower = 0.3f;
        float drivePower = 0.5f;
        float maxLock = 400.0f;
        float slipThreshold = 0.05f;
    };

    struct DiffState {
        float leftTorque = 0.0f;
        float rightTorque = 0.0f;
        float lockingTorque = 0.0f;
        float slipRatio = 0.0f;
        float temperature = 80.0f;
        bool isLocking = false;
    };

    DifferentialModel() { m_config = getLSDClutch(); }

    void update(float dt, float inputTorque, float leftSpeed, float rightSpeed);
    void reset();

    void setConfig(const DiffConfig& config) { m_config = config; }
    DiffConfig getConfig() const { return m_config; }
    DiffState getState() const { return m_state; }

    float getLeftTorque() const { return m_state.leftTorque; }
    float getRightTorque() const { return m_state.rightTorque; }

    float calculateSlipRatio(float leftSpeed, float rightSpeed) const;
    float calculateLockingTorque(float slipRatio, bool isDrive) const;

    void loadFromIni(const std::string& iniPath);

    static DiffConfig getOpenDiff();
    static DiffConfig getLSDClutch();
    static DiffConfig getLSDViscous();
    static DiffConfig getLSDTorsen();
    static DiffConfig getLockedDiff();
    static DiffConfig getActiveDiff();
    static std::string getDiffTypeName(DiffType type);

private:
    DiffConfig m_config;
    DiffState m_state;
};

class GearboxModel {
public:
    void setRatios(const std::vector<float>& ratios, float finalDrive);
    void setGear(int gear);
    int gear() const { return m_gear; }
    float finalDrive() const { return m_finalDrive; }
    float currentRatio() const;

    void autoShift(float rpm, float maxRpm, float idleRpm);

    float wheelTorqueFromEngine(float engineTorque) const {
        return engineTorque * currentRatio();
    }

private:
    std::vector<float> m_ratios{3.5f, 2.2f, 1.6f, 1.25f, 1.0f, 0.85f};
    float m_finalDrive = 3.8f;
    int m_gear = 1;
};

} // namespace physics
} // namespace ks
