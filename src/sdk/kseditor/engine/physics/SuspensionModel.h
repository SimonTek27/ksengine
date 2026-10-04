#pragma once

#include <QString>
#include <QVector>
#include <QJsonObject>

/**
 * @brief Suspension Model for Assetto Corsa
 *
 * Simulates suspension behavior including springs, dampers, and geometry.
 * Based on:
 * - Girellu Wiki - Physics Pipeline (suspension kinematics)
 * - CSP Cosmic Suspension documentation
 * - AC suspensions.ini format
 *
 * Features:
 * - Spring rate calculation
 * - Damper rates (bump/rebound)
 * - Ride height
 * - Camber gain
 * - Roll center
 * - Anti-roll bars
 * - Bump stops
 */
class SuspensionModel {
public:
    struct SpringConfig {
        float rate = 15000.0f;          // N/m
        float preload = 0.0f;           // meters
        float minLength = 0.3f;         // meters
        float maxLength = 0.5f;         // meters
        float bumpStopRate = 50000.0f;  // N/m
        float bumpStopGap = 0.02f;      // meters
    };

    struct DamperConfig {
        float bumpRate = 2000.0f;       // Ns/m
        float reboundRate = 4000.0f;    // Ns/m
        float fastBumpRate = 1500.0f;   // Ns/m
        float fastReboundRate = 3000.0f;// Ns/m
        float bumpThreshold = 0.05f;    // m/s
        float reboundThreshold = 0.05f; // m/s
        QVector<QPair<float,float>> damperCurve;
        QVector<QPair<float,float>> camberCurve;
        QVector<QPair<float,float>> bumpSteerCurve;
        float antiRollBarStiffness = 15000.0f;
        bool antiRollBarEnabled = true;
    };

    struct GeometryConfig {
        float wheelBase = 2.6f;         // meters
        float trackWidth = 1.5f;        // meters
        float frontTrackWidth = 1.55f;
        float rearTrackWidth = 1.50f;

        // Suspension points (relative to CoG)
        float frontUpperArm[3] = {0.3f, 0.4f, 0.9f};
        float frontLowerArm[3] = {0.3f, -0.1f, 0.9f};
        float frontUpright[3] = {0.3f, 0.0f, 1.0f};
        float rearUpperArm[3] = {0.3f, 0.4f, -0.9f};
        float rearLowerArm[3] = {0.3f, -0.1f, -0.9f};
        float rearUpright[3] = {0.3f, 0.0f, -1.0f};
    };

    struct SuspensionState {
        float compression = 0.0f;       // meters (positive = compressed)
        float velocity = 0.0f;          // m/s
        float force = 0.0f;             // N
        float rideHeight = 0.05f;       // meters
        float camber = -1.0f;           // degrees
        float toe = 0.0f;               // degrees
        float rollCenterHeight = 0.05f; // meters
    };

    struct WheelState {
        float suspensionForce = 0.0f;
        float springForce = 0.0f;
        float damperForce = 0.0f;
        float bumpStopForce = 0.0f;
        float load = 0.0f;              // N (tire load)
        float slipAngle = 0.0f;
        float slipRatio = 0.0f;
    };

    // Core simulation
    void update(float dt, float chassisVerticalVelocity, float load);
    void reset();

    // Configuration
    void setSpringConfig(const SpringConfig& config);
    void setDamperConfig(const DamperConfig& config);
    void setGeometryConfig(const GeometryConfig& config);

    // Force calculation
    float calculateSpringForce(float compression) const;
    float calculateDamperForce(float velocity) const;
    float damperCurveForce(float velocity) const;
    float bumpSteerAngle(float travel) const;
    float camberFromCurve(float travel) const;
    float antiRollBarForce(float leftTravel, float rightTravel) const;
    float calculateBumpStopForce(float compression) const;
    float calculateTotalForce(float compression, float velocity) const;

    // Geometry calculations
    float calculateCamberGain(float compression) const;
    float calculateRollCenterHeight(float compression) const;
    float calculateMotionRatio() const;
    float calculateWheelRate() const;

    // State access
    SuspensionState getState() const { return m_state; }
    float getCompression() const { return m_state.compression; }
    float getRideHeight() const { return m_state.rideHeight; }
    float getCamber() const { return m_state.camber; }

    // Presets
    static SpringConfig getStreetSpring();
    static SpringConfig getSportSpring();
    static SpringConfig getRaceSpring();
    static SpringConfig getSoftSpring();

    static DamperConfig getStreetDamper();
    static DamperConfig getSportDamper();
    static DamperConfig getRaceDamper();

    // Validation
    static bool validateConfig(const SpringConfig& spring, const DamperConfig& damper,
                               QString* error = nullptr);
    void loadFromIni(const QString& iniPath);

private:
    SpringConfig m_spring;
    DamperConfig m_damper;
    GeometryConfig m_geometry;
    SuspensionState m_state;
};

/**
 * @brief Anti-Roll Bar Model
 */
class AntiRollBar {
public:
    struct ARBConfig {
        float stiffness = 25000.0f;     // N/m
        bool enabled = true;
    };

    float calculateForce(float leftCompression, float rightCompression) const;
    void setConfig(const ARBConfig& config) { m_config = config; }
    ARBConfig getConfig() const { return m_config; }

private:
    ARBConfig m_config;
};

/**
 * @brief Suspension Model Manager - High-level interface
 */
class SuspensionModelManager {
public:
    SuspensionModelManager();

    // Access models for each wheel
    SuspensionModel& getWheel(int wheel) { return m_wheels[wheel]; }
    const SuspensionModel& getWheel(int wheel) const { return m_wheels[wheel]; }

    // ARB access
    AntiRollBar& getFrontARB() { return m_frontARB; }
    AntiRollBar& getRearARB() { return m_rearARB; }

    // Configuration
    void loadFromIni(const QString& suspensionIniPath);
    void saveToIni(const QString& suspensionIniPath) const;

    // Simulation
    void update(float dt, float chassisVerticalVelocity, float leftLoad, float rightLoad);

    // Analysis
    float getRideHeightFront() const;
    float getRideHeightRear() const;
    float getCamberFront() const;
    float getCamberRear() const;
    float getWheelRate(int wheel) const;

    // Comparison
    QMap<QString, QPair<float, float>> compareSuspension(const SuspensionModelManager& other) const;

private:
    SuspensionModel m_wheels[4]; // FL, FR, RL, RR
    AntiRollBar m_frontARB;
    AntiRollBar m_rearARB;
};
