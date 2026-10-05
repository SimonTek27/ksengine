#pragma once

#include <string>
#include <vector>
#include <utility>

namespace ks {
namespace physics {

class PacejkaTireModel {
public:
    struct TireCoefficients {
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

        float b1 = -21.3f;
        float b2 = 1144.0f;
        float b3 = 49.6f;
        float b4 = 226.0f;
        float b5 = 0.069f;
        float b6 = -0.006f;
        float b7 = 0.056f;
        float b8 = 0.486f;

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

        float nominalLoadN = 4000.f;
        float loadSensExp = 0.9f;
        float loadSensMin = 0.55f;
        float loadSensMax = 1.25f;
        float optPressurePsi = 26.f;
        float optTempC = 80.f;
    };

    struct TireState {
        float slipAngle = 0.0f;
        float slipRatio = 0.0f;
        float normalForce = 0.0f;
        float camberAngle = 0.0f;
        float tirePressure = 26.0f;
        float tireTemp = 80.0f;
        float frictionCoefficient = 1.0f;
    };

    struct TireForces {
        float lateralForce = 0.0f;
        float longitudinalForce = 0.0f;
        float aligningMoment = 0.0f;
        float slipAngleDeg = 0.0f;
        float slipRatioPercent = 0.0f;
    };

    PacejkaTireModel();
    PacejkaTireModel(const TireCoefficients& coeffs);

    static float magicFormula(float x, float B, float C, float D, float E);

    TireForces calculateForces(const TireState& state) const;
    float calculateLateralForce(const TireState& state) const;
    float calculateLongitudinalForce(const TireState& state) const;
    float calculateAligningMoment(const TireState& state) const;

    TireForces calculateCombinedSlip(float slipAngle, float slipRatio,
                                      float normalForce, float camber) const;
    TireForces calculateCombinedSlip(float slipAngle, float slipRatio,
                                      float normalForce, float camber,
                                      float frictionMu, float pressurePsi = 26.f,
                                      float tempC = 80.f) const;

    float calculateLoadSensitivity(float normalForce) const;

    std::vector<std::pair<float, float>> generateLateralCurve(float maxSlipAngle = 15.0f,
                                                       float normalForce = 4000.0f,
                                                       int points = 100) const;
    std::vector<std::pair<float, float>> generateLongitudinalCurve(float maxSlipRatio = 0.3f,
                                                            float normalForce = 4000.0f,
                                                            int points = 100) const;

    float calculatePeakLateralGrip(float normalForce) const;
    float calculatePeakLongitudinalGrip(float normalForce) const;

    float calculateTemperatureEffect(float temp) const;
    float calculatePressureEffect(float pressure) const;
    float calculateWearEffect(float wear) const;

    TireCoefficients getCoefficients() const { return m_coefficients; }
    void setCoefficients(const TireCoefficients& coeffs) { m_coefficients = coeffs; }
    void loadFromIni(const std::string& iniPath);

    static TireCoefficients getStreetTireCoefficients();
    static TireCoefficients getSlickTireCoefficients();
    static TireCoefficients getWetTireCoefficients();
    static TireCoefficients getRallyTireCoefficients();

    static bool validateCoefficients(const TireCoefficients& coeffs, std::string* error = nullptr);

    static float radToDeg(float rad) { return rad * 180.0f / 3.14159265f; }
    static float degToRad(float deg) { return deg * 3.14159265f / 180.0f; }

private:
    TireCoefficients m_coefficients;
};

class TireModelManager {
public:
    TireModelManager();

    PacejkaTireModel& getModel(int wheel) { return m_models[wheel]; }
    const PacejkaTireModel& getModel(int wheel) const { return m_models[wheel]; }

    void setTireCompound(int compound);
    void setTirePressure(float frontPressure, float rearPressure);
    void loadFromIni(const std::string& tireIniPath);
    void saveToIni(const std::string& tireIniPath) const;

    std::vector<float> calculateGripCircle(int wheel, float normalForce) const;
    std::vector<float> calculateSlipCurve(int wheel, float normalForce) const;
    float estimateLapTimeImpact(int wheel, float slipAngleChange) const;

private:
    PacejkaTireModel m_models[4];
    int m_compound = 0;
    float m_frontPressure = 26.0f;
    float m_rearPressure = 24.0f;
};
} // namespace physics
} // namespace ks
