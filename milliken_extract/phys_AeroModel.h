#pragma once

#include <QString>
#include <QMap>
#include <QVector>
#include <QJsonObject>

/**
 * @brief Aerodynamic Model for Assetto Corsa
 *
 * Simulates aerodynamic forces on cars.
 * Based on AC aero.ini documentation and community tools:
 * - aero.ini format documentation
 * - GTPlanet aero discussion
 * - AC Physics Pipeline documentation
 *
 * Features:
 * - Wing downforce calculation
 * - Drag calculation
 * - Ground effect
 * - Aero balance
 * - Ride height sensitivity
 */
class AeroModel {
public:
    struct Wing {
        QString name;
        float chord = 1.0f;              // meters
        float span = 1.5f;               // meters
        float angle = 0.0f;              // degrees
        float position[3] = {0, 0, 0};   // x, y, z relative to CoG

        // LUT data (Angle of Attack -> Coefficient)
        QVector<QPair<float, float>> aoaClLut;  // AoA -> CL
        QVector<QPair<float, float>> aoaCdLut;  // AoA -> CD

        // Height-dependent coefficients
        QVector<QPair<float, float>> heightClLut; // Height -> CL
        QVector<QPair<float, float>> heightCdLut; // Height -> CD

        // Gains
        float clGain = 1.0f;
        float cdGain = 1.0f;

        // Calculated values
        float area() const { return chord * span; }
        float cl = 0.0f;
        float cd = 0.0f;
    };

    struct AeroConfig {
        QVector<Wing> wings;
        float frontalArea = 2.0f;        // m^2
        float dragCoefficient = 0.35f;
        float liftCoefficient = -0.1f;   // negative = downforce
        float groundEffectFactor = 1.0f;
        float rideHeightSensitivity = 1.0f;
    };

    struct AeroState {
        float speed = 0.0f;              // m/s
        float rideHeightFront = 0.05f;   // meters
        float rideHeightRear = 0.07f;    // meters
        float yawAngle = 0.0f;           // radians (sideslip)
        float rollAngle = 0.0f;          // radians
        float pitchAngle = 0.0f;         // radians
        float airDensity = 1.225f;       // kg/m^3
    };

    struct AeroForces {
        float downforce = 0.0f;          // Newtons
        float drag = 0.0f;               // Newtons
        float lateralForce = 0.0f;       // Newtons
        float frontDownforce = 0.0f;     // Newtons
        float rearDownforce = 0.0f;      // Newtons
        float aeroBalance = 0.5f;        // 0-1 (front/rear)
        float ldRatio = 0.0f;            // Lift-to-drag ratio
    };

    // Core calculation
    AeroForces calculate(const AeroState& state) const;
    float calculateWingForce(const Wing& wing, const AeroState& state) const;

    // Coefficient lookup
    float interpolateCl(const Wing& wing, float aoa) const;
    float interpolateCd(const Wing& wing, float aoa) const;
    float interpolateHeightCl(const Wing& wing, float height) const;
    float interpolateHeightCd(const Wing& wing, float height) const;

    // Configuration
    void setConfig(const AeroConfig& config) { m_config = config; }
    AeroConfig getConfig() const { return m_config; }
    void loadFromIniFile(const QString& iniPath) { m_config = loadFromIni(iniPath); }
    void addWing(const Wing& wing);
    void removeWing(int index);
    void clearWings();
    int wingCount() const { return m_config.wings.size(); }
    Wing& wing(int index) { return m_config.wings[index]; }
    const Wing& wing(int index) const { return m_config.wings[index]; }

    // Presets
    static AeroConfig getSedanConfig();
    static AeroConfig getGT3Config();
    static AeroConfig getFormulaConfig();
    static AeroConfig getRoadCarConfig();

    // INI operations
    static AeroConfig loadFromIni(const QString& iniPath);
    static bool saveToIni(const AeroConfig& config, const QString& iniPath);

    // Validation
    static bool validateConfig(const AeroConfig& config, QString* error = nullptr);

    // Utility
    static float calculateDynamicPressure(float speed, float airDensity);
    static float calculateReynoldsNumber(float speed, float chord, float viscosity = 1.5e-5f);

private:
    AeroConfig m_config;

    float interpolateLut(const QVector<QPair<float, float>>& lut, float x) const;
};

/**
 * @brief Aero Model Manager - High-level interface
 */
class AeroModelManager {
public:
    AeroModelManager();

    // Access
    AeroModel& model() { return m_model; }
    const AeroModel& model() const { return m_model; }

    // Configuration
    void loadFromIni(const QString& carPath);
    void saveToIni(const QString& carPath) const;

    // Simulation
    AeroModel::AeroForces calculateForces(float speed, float rideHeightFront, float rideHeightRear) const;

    // Analysis
    float calculateTopSpeed(float enginePower, float weight) const;
    float calculateCorneringForce(float speed, float cornerRadius) const;
    float calculateBrakeDistance(float speed, float friction) const;

    // Comparison
    QMap<QString, QPair<float, float>> compareAero(const AeroModelManager& other) const;

private:
    AeroModel m_model;
    float m_weight = 1300.0f;           // kg
    float m_wheelbase = 2.6f;           // meters
    float m_trackWidth = 1.5f;          // meters
};
