#pragma once

#include <QString>
#include <QJsonObject>

/**
 * @brief Brake Thermal Model for Assetto Corsa
 *
 * Simulates brake disc and pad thermal behavior.
 * Based on:
 * - F1 brake thermal analysis (github.com/SirPicklee)
 * - Formula SAE Brake Heat Transfer Model (MathWorks)
 * - AC Physics Pipeline documentation
 *
 * Features:
 * - Disc temperature calculation
 * - Pad temperature calculation
 * - Brake fade simulation
 * - Brake wear modeling
 * - Heat transfer between components
 */
class BrakeThermalModel {
public:
    struct BrakeComponent {
        // Geometry
        float discRadius = 0.14f;           // meters (outer radius)
        float discInnerRadius = 0.05f;      // meters (inner radius)
        float discThickness = 0.032f;       // meters
        float padArea = 0.003f;             // m^2 (contact area per pad)
        float padThickness = 0.012f;        // meters

        // Material properties
        float discDensity = 7200.0f;        // kg/m^3 (iron)
        float discSpecificHeat = 460.0f;    // J/(kg*K)
        float discThermalConductivity = 40.0f; // W/(m*K)
        float padDensity = 3500.0f;         // kg/m^3
        float padSpecificHeat = 800.0f;     // J/(kg*K)
        float padThermalConductivity = 5.0f; // W/(m*K)

        // Thermal state
        float discTemp = 200.0f;            // Celsius
        float padTemp = 200.0f;             // Celsius
        float discTempMin = 100.0f;
        float discTempMax = 1100.0f;
        float padTempMin = 100.0f;
        float padTempMax = 900.0f;

        // Wear
        float discWear = 0.0f;              // 0 = new, 1 = worn out
        float padWear = 0.0f;               // 0 = new, 1 = worn out
        float wearRate = 0.0001f;           // wear per Joule

        // Brake bias
        float biasFactor = 0.56f;           // 0-1 (front/rear)
    };

    struct BrakeState {
        float pedalPressure = 0.0f;         // 0-1
        float brakeTorque = 0.0f;           // Nm
        float discAngularVelocity = 0.0f;   // rad/s
        float slipRatio = 0.0f;
        float padWear[2] = {0.0f, 0.0f};   // inner/outer pad
    };

    struct ThermalDynamics {
        float heatGeneration = 0.0f;        // Watts
        float heatDissipation = 0.0f;       // Watts
        float convectiveCooling = 0.0f;     // Watts
        float radiationCooling = 0.0f;      // Watts
        float conductionToHub = 0.0f;       // Watts
        float netHeatFlow = 0.0f;           // Watts
    };

    // Core simulation
    void update(float dt, const BrakeState& state);
    void reset();

    // Configuration
    void setBrakeBias(float bias);
    float getBrakeBalance() const { return m_disc.biasFactor; }
    void setDiscProperties(float radius, float thickness);
    void setPadProperties(float area, float thickness);

    // Temperature calculation
    float calculateDiscTemp() const { return m_disc.discTemp; }
    float calculatePadTemp() const { return m_disc.padTemp; }
    float getDiscTempNormalized() const;
    float getPadTempNormalized() const;

    // Heat generation
    float calculateBrakePower(float brakeTorque, float angularVelocity) const;
    float calculateHeatFlux(float power) const;

    // Heat dissipation
    float calculateConvectiveCooling(float temp, float airflow) const;
    float calculateRadiationCooling(float temp) const;
    float calculateConductionCooling(float temp) const;

    // Wear calculation
    float calculateDiscWear(float heatEnergy) const;
    float calculatePadWear(float heatEnergy) const;
    float getDiscLifeRemaining() const { return 1.0f - m_disc.discWear; }
    float getPadLifeRemaining() const { return 1.0f - m_disc.padWear; }

    // Fade calculation
    float calculateBrakeFade() const;
    float calculateFadeFactor(float temperature) const;

    // Friction coefficient
    float calculateFrictionCoefficient() const;
    float getFrictionAtTemperature(float temp) const;

    // Presets
    static BrakeComponent getIronDisc();
    static BrakeComponent getCarbonDisc();
    static BrakeComponent getCeramicDisc();

    // Validation
    bool validate(QString* error = nullptr) const;

    // Configuration from INI
    void loadFromIni(const QString& brakeIniPath);

    // Utility
    static float celsiusToKelvin(float c) { return c + 273.15f; }
    static float kelvinToCelsius(float k) { return k - 273.15f; }

private:
    BrakeComponent m_disc;
    BrakeState m_state;
    ThermalDynamics m_dynamics;

    float m_dt = 0.0f;
    float m_airflow = 10.0f;
    float m_ambientTemp = 25.0f;
};

/**
 * @brief Brake Model Manager - High-level interface
 */
class BrakeModelManager {
public:
    BrakeModelManager();

    // Access models for each wheel
    BrakeThermalModel& getBrake(int wheel) { return m_brakes[wheel]; }
    const BrakeThermalModel& getBrake(int wheel) const { return m_brakes[wheel]; }

    // Configuration
    void loadFromIni(const QString& brakeIniPath);
    void saveToIni(const QString& brakeIniPath) const;

    // Simulation
    void update(float dt, float pedalPressure, float speed);
    void reset();

    // Analysis
    float getAverageDiscTemp() const;
    float getAveragePadTemp() const;
    float getBrakeFade() const;
    float getBrakeBalance() const;

    // Validation
    bool validate(QString* error = nullptr) const;

private:
    BrakeThermalModel m_brakes[4]; // FL, FR, RL, RR
    float m_speed = 0.0f;
    float m_gearRatio = 3.5f;
    float m_finalDrive = 3.8f;
    float m_wheelRadius = 0.33f;
};
