#pragma once

#include <QString>
#include <QJsonObject>
#include <QVector>

/**
 * @brief Pacejka Tire Model for Assetto Corsa
 *
 * Implementation of the Pacejka Magic Formula tire model.
 * Based on:
 * - JyNing04/Pacejka-tire-model (github.com)
 * - Stanford University tire model documentation
 * - AC Physics Pipeline documentation
 *
 * The Magic Formula: F = D * sin(C * arctan(B*x - E*(B*x - arctan(B*x))))
 *
 * Where:
 * - B = Stiffness factor
 * - C = Shape factor
 * - D = Peak value
 * - E = Curvature factor
 * - x = Slip angle or slip ratio
 */
class PacejkaTireModel {
public:
    struct TireCoefficients {
        // Lateral force coefficients
        float a1 = -22.1f;
        float a2 = 1011.0f;
        float a3 = 1078.0f;
        float a4 = 1.82f;
        float a5 = 0.208f;
        float a6 = 0.0f;
        float a7 = -0.354f;
        float a8 = 0.707f;
        float a9 = 0.028f;
        float a10 = 0.0f;
        float a11 = 14.8f;
        float a12 = 0.022f;
        float a13 = 0.0f;

        // Longitudinal force coefficients
        float b1 = -21.3f;
        float b2 = 1144.0f;
        float b3 = 49.6f;
        float b4 = 226.0f;
        float b5 = 0.069f;
        float b6 = -0.006f;
        float b7 = 0.056f;
        float b8 = 0.486f;

        // Aligning moment coefficients
        float c1 = -2.72f;
        float c2 = -2.28f;
        float c3 = -1.86f;
        float c4 = -2.73f;
        float c5 = 0.110f;
        float c6 = -0.070f;
        float c7 = 0.643f;
        float c8 = -4.04f;
        float c9 = 0.015f;
        float c10 = -0.066f;
        float c11 = 0.945f;
        float c12 = 0.030f;
        float c13 = 0.070f;
    };

    struct TireState {
        float slipAngle = 0.0f;        // radians
        float slipRatio = 0.0f;        // 0-1
        float normalForce = 0.0f;      // Newtons
        float camberAngle = 0.0f;      // radians
        float tirePressure = 26.0f;    // PSI
        float tireTemp = 80.0f;        // Celsius
        float frictionCoefficient = 1.0f;
    };

    struct TireForces {
        float lateralForce = 0.0f;     // Newtons
        float longitudinalForce = 0.0f;// Newtons
        float aligningMoment = 0.0f;   // Nm
        float slipAngleDeg = 0.0f;     // degrees
        float slipRatioPercent = 0.0f; // percentage
    };

    // Constructor
    PacejkaTireModel();
    PacejkaTireModel(const TireCoefficients& coeffs);

    // Core Magic Formula
    static float magicFormula(float x, float B, float C, float D, float E);

    // Force calculation
    TireForces calculateForces(const TireState& state) const;
    float calculateLateralForce(const TireState& state) const;
    float calculateLongitudinalForce(const TireState& state) const;
    float calculateAligningMoment(const TireState& state) const;

    // Combined slip
    TireForces calculateCombinedSlip(float slipAngle, float slipRatio,
                                      float normalForce, float camber) const;

    // Load sensitivity
    float calculateLoadSensitivity(float normalForce) const;

    // Tire curve generation
    QVector<QPair<float, float>> generateLateralCurve(float maxSlipAngle = 15.0f,
                                                       float normalForce = 4000.0f,
                                                       int points = 100) const;
    QVector<QPair<float, float>> generateLongitudinalCurve(float maxSlipRatio = 0.3f,
                                                            float normalForce = 4000.0f,
                                                            int points = 100) const;

    // Peak grip calculation
    float calculatePeakLateralGrip(float normalForce) const;
    float calculatePeakLongitudinalGrip(float normalForce) const;

    // Temperature effects
    float calculateTemperatureEffect(float temp) const;
    float calculatePressureEffect(float pressure) const;

    // Wear effects
    float calculateWearEffect(float wear) const;

    // Coefficient access
    TireCoefficients getCoefficients() const { return m_coefficients; }
    void setCoefficients(const TireCoefficients& coeffs) { m_coefficients = coeffs; }

    // Preset coefficients
    static TireCoefficients getStreetTireCoefficients();
    static TireCoefficients getSlickTireCoefficients();
    static TireCoefficients getWetTireCoefficients();
    static TireCoefficients getRallyTireCoefficients();

    // Validation
    static bool validateCoefficients(const TireCoefficients& coeffs, QString* error = nullptr);

    // Utility
    static float radToDeg(float rad) { return rad * 180.0f / 3.14159265f; }
    static float degToRad(float deg) { return deg * 3.14159265f / 180.0f; }

private:
    TireCoefficients m_coefficients;
};

/**
 * @brief Tire Model Manager - High-level interface
 */
class TireModelManager {
public:
    TireModelManager();

    // Model access
    PacejkaTireModel& getModel(int wheel) { return m_models[wheel]; }
    const PacejkaTireModel& getModel(int wheel) const { return m_models[wheel]; }

    // Configuration
    void setTireCompound(int compound);
    void setTirePressure(float frontPressure, float rearPressure);
    void loadFromIni(const QString& tireIniPath);
    void saveToIni(const QString& tireIniPath) const;

    // Analysis
    QVector<float> calculateGripCircle(int wheel, float normalForce) const;
    QVector<float> calculateSlipCurve(int wheel, float normalForce) const;
    float estimateLapTimeImpact(int wheel, float slipAngleChange) const;

private:
    PacejkaTireModel m_models[4]; // FL, FR, RL, RR
    int m_compound = 0;
    float m_frontPressure = 26.0f;
    float m_rearPressure = 24.0f;
};
