#pragma once
#include "KsExport.h"
/**
 * KsTireModel — ksengine Magic Formula tire (project-branded successor to PacejkaTireModel).
 *
 * Core curve: F = D * sin(C * arctan(B*x - E*(B*x - arctan(B*x))))
 * Hot path: Fz/camber/mu coefficient cache + combined-slip ellipse.
 *
 * Migration: #include "PacejkaTireModel.h" still works (deprecated alias).
 */
#include <string>
#include <vector>
#include <utility>
#include <array>
#include <cmath>
#include <algorithm>

namespace ks {
namespace physics {

class KSENGINE_API KsTireModel {
public:
    struct TireCoefficients {
        float a1 = -22.1f, a2 = 1011.0f, a3 = 1078.0f, a4 = 1.82f, a5 = 0.208f;
        float a6 = 0.0f, a7 = -0.354f, a8 = 0.707f, a9 = 0.028f, a10 = 0.0f;
        float a11 = 14.8f, a12 = 0.022f, a13 = 0.0f;

        float b1 = -21.3f, b2 = 1144.0f, b3 = 49.6f, b4 = 226.0f, b5 = 0.069f;
        float b6 = -0.006f, b7 = 0.056f, b8 = 0.486f;

        float c1 = -2.72f, c2 = -2.28f, c3 = -1.86f, c4 = -2.73f, c5 = 0.110f;
        float c6 = -0.070f, c7 = 0.643f, c8 = -4.04f, c9 = 0.015f, c10 = -0.066f;
        float c11 = 0.945f, c12 = 0.030f, c13 = 0.070f;

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

    KsTireModel();
    explicit KsTireModel(const TireCoefficients& coeffs);

    /** Classic Magic Formula (kept public for tools / curve editor). */
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

    /** Evaluate 4 wheels in one call (shared surface mu/temp); writes forces[4]. */
    void calculateCombinedSlipBatch(const float slipAngle[4], const float slipRatio[4],
                                    const float normalForce[4], const float camber[4],
                                    float frictionMu, float pressurePsi, float tempC,
                                    TireForces out[4]) const;
    /** Per-wheel pressure (psi) and temperature (°C). */
    void calculateCombinedSlipBatch(const float slipAngle[4], const float slipRatio[4],
                                    const float normalForce[4], const float camber[4],
                                    float frictionMu,
                                    const float pressurePsi[4], const float tempC[4],
                                    TireForces out[4]) const;

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
    void setCoefficients(const TireCoefficients& coeffs) {
        m_coefficients = coeffs;
        invalidateCache();
    }
    void loadFromIni(const std::string& iniPath);

    static TireCoefficients getStreetTireCoefficients();
    static TireCoefficients getSlickTireCoefficients();
    static TireCoefficients getWetTireCoefficients();
    static TireCoefficients getRallyTireCoefficients();
    static bool validateCoefficients(const TireCoefficients& coeffs, std::string* error = nullptr);

    static float radToDeg(float rad) { return rad * 57.2957795f; }
    static float degToRad(float deg) { return deg * 0.0174532925f; }

    void invalidateCache() const { m_cache.valid = false; }

private:
    struct CoeffCache {
        bool valid = false;
        float Fz = -1.f;
        float camberAbs = -1.f;
        float mu = -1.f;
        float loadSens = 1.f;
        float Dy = 0, By = 10.f, Cy = 1.3f, Ey = 0;
        float Dx = 0, Bx = 10.f, Cx = 1.65f, Ex = 0;
    };

    void ensureCoeffCache(float Fz, float camberAbs, float mu) const;

    TireCoefficients m_coefficients;
    mutable CoeffCache m_cache;
};

/** Four-wheel set sharing one compound (runtime helper). */
class KsTireModelSet {
public:
    KsTireModelSet() = default;

    KsTireModel& getModel(int wheel) { return m_models[static_cast<size_t>(wheel & 3)]; }
    const KsTireModel& getModel(int wheel) const { return m_models[static_cast<size_t>(wheel & 3)]; }

    void setTireCompound(int compound);
    void setTirePressure(float frontPressure, float rearPressure);
    void loadFromIni(const std::string& tireIniPath);
    void saveToIni(const std::string& tireIniPath) const;

    std::vector<float> calculateGripCircle(int wheel, float normalForce) const;
    std::vector<float> calculateSlipCurve(int wheel, float normalForce) const;
    float estimateLapTimeImpact(int wheel, float slipAngleChange) const;

private:
    std::array<KsTireModel, 4> m_models{};
    int m_compound = 0;
    float m_frontPressure = 26.0f;
    float m_rearPressure = 24.0f;
};

} // namespace physics
} // namespace ks
