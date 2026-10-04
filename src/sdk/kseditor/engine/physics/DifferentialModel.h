#pragma once

#include <QString>
#include <QJsonObject>

/**
 * @brief Differential Model for Assetto Corsa
 *
 * Simulates differential behavior (open, limited-slip, locked).
 * Based on AC physics documentation and community tools.
 *
 * Features:
 * - Open differential
 * - Limited-slip differential (LSD)
 * - Locked differential
 * - Active differential
 * - Preload, coast, drive settings
 */
class DifferentialModel {
public:
    enum class DiffType {
        Open,
        LSD_Cls,       // Clutch-type LSD
        LSD_Viscous,   // Viscous LSD
        LSD_Geared,    // Geared LSD (e.g., Torsen)
        Locked,
        Active
    };

    struct DiffConfig {
        DiffType type = DiffType::LSD_Cls;
        float preload = 20.0f;           // Nm
        float coastPower = 0.3f;         // 0-1 (unlocking on coast)
        float drivePower = 0.5f;         // 0-1 (locking on drive)
        float coastBrake = 0.3f;         // 0-1
        float driveBrake = 0.4f;         // 0-1
        float maxLock = 100.0f;          // Nm (maximum locking torque)
        float rampAngle = 30.0f;         // degrees (ramp angle for clutch type)
        float slipThreshold = 5.0f;      // % (slip ratio threshold)
    };

    struct DiffState {
        float leftTorque = 0.0f;         // Nm
        float rightTorque = 0.0f;        // Nm
        float lockingTorque = 0.0f;      // Nm
        float slipRatio = 0.0f;          // %
        float temperature = 80.0f;       // Celsius
        bool isLocking = false;
    };

    // Core simulation
    void update(float dt, float inputTorque, float leftSpeed, float rightSpeed);
    void reset();

    // Configuration
    void setConfig(const DiffConfig& config);
    DiffConfig getConfig() const { return m_config; }

    // State access
    DiffState getState() const { return m_state; }
    float getLeftTorque() const { return m_state.leftTorque; }
    float getRightTorque() const { return m_state.rightTorque; }
    float getLockingTorque() const { return m_state.lockingTorque; }
    float getSlipRatio() const { return m_state.slipRatio; }

    // Torque distribution
    float calculateLeftTorque(float inputTorque, float slipRatio) const;
    float calculateRightTorque(float inputTorque, float slipRatio) const;
    float calculateLockingTorque(float slipRatio, bool isDrive) const;

    // Slip calculation
    float calculateSlipRatio(float leftSpeed, float rightSpeed) const;
    float calculateSpeedDifference(float leftSpeed, float rightSpeed) const;

    // Temperature effects
    float calculateTemperatureEffect() const;
    void updateTemperature(float dt, float lockingTorque);

    // Presets
    static DiffConfig getOpenDiff();
    static DiffConfig getLSDClutch();
    static DiffConfig getLSDViscous();
    static DiffConfig getLSDTorsen();
    static DiffConfig getLockedDiff();
    static DiffConfig getActiveDiff();

    // Validation
    static bool validateConfig(const DiffConfig& config, QString* error = nullptr);
    void loadFromIni(const QString& iniPath);

    // Utility
    static QString getDiffTypeName(DiffType type);

private:
    DiffConfig m_config;
    DiffState m_state;

    float calculateClutchLsdTorque(float slipRatio) const;
    float calculateViscousLsdTorque(float slipRatio) const;
    float calculateGearedLsdTorque(float slipRatio) const;
};

/**
 * @brief Differential Model Manager - High-level interface
 */
class DifferentialModelManager {
public:
    DifferentialModelManager();

    // Access
    DifferentialModel& model() { return m_model; }
    const DifferentialModel& model() const { return m_model; }

    // Configuration
    void loadFromIni(const QString& drivetrainIniPath);
    void saveToIni(const QString& drivetrainIniPath) const;

    // Simulation
    void update(float dt, float engineTorque, float leftWheelSpeed, float rightWheelSpeed);

    // Analysis
    float getLockingPercentage() const;
    float getTemperature() const;
    bool isLocking() const;

    // Comparison
    QMap<QString, QPair<float, float>> compareDiffs(const DifferentialModelManager& other) const;

private:
    DifferentialModel m_model;
};
