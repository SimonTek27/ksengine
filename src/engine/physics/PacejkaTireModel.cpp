#include "PacejkaTireModel.h"
#include "../../Config/IniFile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ks {
namespace physics {

namespace {

// Applies A1..A13 / B1..B8 / C1..C13 entries of an INI group onto c.
// Missing keys leave the current value untouched.
void applyCoeffGroup(const ks::config::IniFile& ini, const std::string& group,
                     PacejkaTireModel::TireCoefficients& c) {
    float* a[13] = {&c.a1, &c.a2, &c.a3, &c.a4, &c.a5, &c.a6, &c.a7,
                    &c.a8, &c.a9, &c.a10, &c.a11, &c.a12, &c.a13};
    float* b[8] = {&c.b1, &c.b2, &c.b3, &c.b4, &c.b5, &c.b6, &c.b7, &c.b8};
    float* cc[13] = {&c.c1, &c.c2, &c.c3, &c.c4, &c.c5, &c.c6, &c.c7,
                     &c.c8, &c.c9, &c.c10, &c.c11, &c.c12, &c.c13};
    char key[8];
    for (int i = 0; i < 13; ++i) {
        std::snprintf(key, sizeof(key), "A%d", i + 1);
        if (ini.contains(group, key)) *a[i] = static_cast<float>(ini.getDouble(group, key, *a[i]));
    }
    for (int i = 0; i < 8; ++i) {
        std::snprintf(key, sizeof(key), "B%d", i + 1);
        if (ini.contains(group, key)) *b[i] = static_cast<float>(ini.getDouble(group, key, *b[i]));
    }
    for (int i = 0; i < 13; ++i) {
        std::snprintf(key, sizeof(key), "C%d", i + 1);
        if (ini.contains(group, key)) *cc[i] = static_cast<float>(ini.getDouble(group, key, *cc[i]));
    }
}

void saveCoeffGroup(ks::config::IniFile& ini, const std::string& group,
                    const PacejkaTireModel::TireCoefficients& c) {
    const float* a[13] = {&c.a1, &c.a2, &c.a3, &c.a4, &c.a5, &c.a6, &c.a7,
                          &c.a8, &c.a9, &c.a10, &c.a11, &c.a12, &c.a13};
    const float* b[8] = {&c.b1, &c.b2, &c.b3, &c.b4, &c.b5, &c.b6, &c.b7, &c.b8};
    const float* cc[13] = {&c.c1, &c.c2, &c.c3, &c.c4, &c.c5, &c.c6, &c.c7,
                           &c.c8, &c.c9, &c.c10, &c.c11, &c.c12, &c.c13};
    char key[8];
    for (int i = 0; i < 13; ++i) {
        std::snprintf(key, sizeof(key), "A%d", i + 1);
        ini.setNumber(group, key, *a[i]);
    }
    for (int i = 0; i < 8; ++i) {
        std::snprintf(key, sizeof(key), "B%d", i + 1);
        ini.setNumber(group, key, *b[i]);
    }
    for (int i = 0; i < 13; ++i) {
        std::snprintf(key, sizeof(key), "C%d", i + 1);
        ini.setNumber(group, key, *cc[i]);
    }
}

// Flat keys first, then optional legacy wrapper groups.
void applyFlatAndFallbacks(const ks::config::IniFile& ini,
                           PacejkaTireModel::TireCoefficients& c) {
    applyCoeffGroup(ini, "", c);
    if (ini.contains("PACEJKA", "A1") || ini.contains("PACEJKA", "B2"))
        applyCoeffGroup(ini, "PACEJKA", c);
    if (ini.contains("TYRE", "A1") || ini.contains("TYRE", "B2"))
        applyCoeffGroup(ini, "TYRE", c);
}

} // namespace

PacejkaTireModel::PacejkaTireModel()
    : m_coefficients(getSlickTireCoefficients()) {}

PacejkaTireModel::PacejkaTireModel(const TireCoefficients& coeffs)
    : m_coefficients(coeffs) {}

float PacejkaTireModel::magicFormula(float x, float B, float C, float D, float E) {
    const float Bx = B * x;
    return D * std::sin(C * std::atan(Bx - E * (Bx - std::atan(Bx))));
}

PacejkaTireModel::TireForces PacejkaTireModel::calculateForces(const TireState& state) const {
    TireForces out;
    const float Fz = std::max(state.normalForce, 1.0f);
    const float mu = std::max(state.frictionCoefficient, 0.1f);
    const auto& a = m_coefficients;

    const float alpha = state.slipAngle;
    const float Fz_kN = Fz / 1000.0f;
    float D_y = (a.a1 * Fz_kN + a.a2) * Fz_kN;
    D_y = std::abs(D_y) * mu;
    float BCD_y = a.a3 * std::sin(std::atan(Fz_kN / a.a4) * 2.0f) * (1.0f - a.a5 * std::abs(state.camberAngle));
    float B_y = std::clamp(std::abs(BCD_y) / (D_y + 1e-3f), 0.1f, 40.0f);
    float C_y = 1.3f;
    float E_y = a.a6 * Fz_kN + a.a7;
    float Fy = magicFormula(alpha, B_y, C_y, D_y, E_y);
    Fy += Fz * state.camberAngle * 0.1f;

    const float kappa = state.slipRatio;
    float D_x = (a.b1 * Fz_kN + a.b2) * Fz_kN;
    D_x = std::abs(D_x) * mu;
    float BCD_x = (a.b3 * Fz_kN * Fz_kN + a.b4 * Fz_kN) * std::exp(-a.b5 * Fz_kN);
    float B_x = std::clamp(std::abs(BCD_x) / (D_x + 1e-3f), 0.1f, 40.0f);
    float C_x = 1.65f;
    float E_x = a.b6 * Fz_kN + a.b7;
    float Fx = magicFormula(kappa, B_x, C_x, D_x, E_x);

    // Combined slip: friction-circle normalization on the FORCES, not on
    // the raw slips. The previous proxy (sx = kappa/0.15, sy = alpha/0.12)
    // divided the lateral force by alpha/0.12 beyond 6.9 deg of slip, which
    // capped every tyre at ~slope*0.12 = ~140 N instead of the ~3 kN peak of
    // its own MF curve — the car could not build lateral force to corner at
    // all (AI racing, roadmap 3.5). Normalising by the peak forces keeps
    // pure-axis behaviour at the full MF curve and still limits the
    // combined case to the friction circle.
    const float nx = Fx / (D_x + 1e-3f);
    const float ny = Fy / (D_y + 1e-3f);
    const float comb = std::sqrt(nx * nx + ny * ny);
    if (comb > 1.0f) {
        Fx /= comb;
        Fy /= comb;
    }

    Fx *= calculateTemperatureEffect(state.tireTemp) * calculatePressureEffect(state.tirePressure);
    Fy *= calculateTemperatureEffect(state.tireTemp) * calculatePressureEffect(state.tirePressure);

    out.lateralForce = Fy;
    out.longitudinalForce = Fx;
    out.aligningMoment = -Fy * 0.03f;
    out.slipAngleDeg = radToDeg(alpha);
    out.slipRatioPercent = kappa * 100.0f;
    return out;
}

PacejkaTireModel::TireForces PacejkaTireModel::calculateCombinedSlip(
    float slipAngle, float slipRatio, float normalForce, float camber) const {
    TireState s;
    s.slipAngle = slipAngle;
    s.slipRatio = slipRatio;
    s.normalForce = normalForce;
    s.camberAngle = camber;
    return calculateForces(s);
}

float PacejkaTireModel::calculateLateralForce(const TireState& state) const {
    return calculateForces(state).lateralForce;
}
float PacejkaTireModel::calculateLongitudinalForce(const TireState& state) const {
    return calculateForces(state).longitudinalForce;
}
float PacejkaTireModel::calculateAligningMoment(const TireState& state) const {
    return calculateForces(state).aligningMoment;
}
float PacejkaTireModel::calculateLoadSensitivity(float normalForce) const {
    return std::clamp(normalForce / 4000.0f, 0.5f, 1.5f);
}

float PacejkaTireModel::calculateTemperatureEffect(float temp) const {
    const float opt = 80.0f;
    const float d = (temp - opt) / 30.0f;
    return std::clamp(1.0f - d * d * 0.25f, 0.5f, 1.05f);
}

float PacejkaTireModel::calculatePressureEffect(float pressurePsi) const {
    const float opt = 26.0f;
    const float d = (pressurePsi - opt) / 8.0f;
    return std::clamp(1.0f - d * d * 0.15f, 0.7f, 1.05f);
}

float PacejkaTireModel::calculateWearEffect(float wear) const {
    return std::clamp(1.0f - wear * 0.35f, 0.4f, 1.0f);
}

float PacejkaTireModel::calculatePeakLateralGrip(float normalForce) const {
    TireState s;
    s.normalForce = normalForce;
    s.slipAngle = degToRad(8.0f);
    return std::abs(calculateForces(s).lateralForce);
}

float PacejkaTireModel::calculatePeakLongitudinalGrip(float normalForce) const {
    TireState s;
    s.normalForce = normalForce;
    s.slipRatio = 0.12f;
    return std::abs(calculateForces(s).longitudinalForce);
}

std::vector<std::pair<float, float>> PacejkaTireModel::generateLateralCurve(
    float maxSlipAngle, float normalForce, int points) const {
    std::vector<std::pair<float, float>> curve;
    curve.reserve(points);
    for (int i = 0; i < points; ++i) {
        float a = -maxSlipAngle + 2.0f * maxSlipAngle * (float)i / (points - 1);
        TireState s;
        s.slipAngle = degToRad(a);
        s.normalForce = normalForce;
        curve.emplace_back(a, calculateForces(s).lateralForce);
    }
    return curve;
}

std::vector<std::pair<float, float>> PacejkaTireModel::generateLongitudinalCurve(
    float maxSlipRatio, float normalForce, int points) const {
    std::vector<std::pair<float, float>> curve;
    curve.reserve(points);
    for (int i = 0; i < points; ++i) {
        float k = -maxSlipRatio + 2.0f * maxSlipRatio * (float)i / (points - 1);
        TireState s;
        s.slipRatio = k;
        s.normalForce = normalForce;
        curve.emplace_back(k, calculateForces(s).longitudinalForce);
    }
    return curve;
}

PacejkaTireModel::TireCoefficients PacejkaTireModel::getStreetTireCoefficients() {
    PacejkaTireModel::TireCoefficients c;
    c.a2 = 900.0f; c.b2 = 1000.0f;
    return c;
}

PacejkaTireModel::TireCoefficients PacejkaTireModel::getSlickTireCoefficients() {
    PacejkaTireModel::TireCoefficients c;
    c.a2 = 1100.0f; c.b2 = 1200.0f;
    c.a3 = 1200.0f;
    return c;
}

PacejkaTireModel::TireCoefficients PacejkaTireModel::getWetTireCoefficients() {
    PacejkaTireModel::TireCoefficients c;
    c.a2 = 700.0f; c.b2 = 750.0f;
    return c;
}

PacejkaTireModel::TireCoefficients PacejkaTireModel::getRallyTireCoefficients() {
    PacejkaTireModel::TireCoefficients c;
    c.a2 = 850.0f; c.b2 = 900.0f;
    return c;
}

bool PacejkaTireModel::validateCoefficients(const TireCoefficients& coeffs, std::string* error) {
    if (coeffs.a2 <= 0.0f || coeffs.b2 <= 0.0f) {
        if (error) *error = "Peak coefficients must be positive";
        return false;
    }
    return true;
}

void PacejkaTireModel::loadFromIni(const std::string& iniPath) {
    ks::config::IniFile ini;
    if (!ini.load(iniPath)) return;
    TireCoefficients c = m_coefficients;
    applyFlatAndFallbacks(ini, c);
    if (validateCoefficients(c, nullptr)) m_coefficients = c;
}

TireModelManager::TireModelManager() {
    auto slick = PacejkaTireModel::getSlickTireCoefficients();
    for (int i = 0; i < 4; ++i)
        m_models[i].setCoefficients(slick);
}

void TireModelManager::setTireCompound(int compound) {
    m_compound = compound;
    PacejkaTireModel::TireCoefficients c;
    if (compound == 1) c = PacejkaTireModel::getWetTireCoefficients();
    else if (compound == 2) c = PacejkaTireModel::getStreetTireCoefficients();
    else if (compound == 3) c = PacejkaTireModel::getRallyTireCoefficients();
    else c = PacejkaTireModel::getSlickTireCoefficients();
    for (int i = 0; i < 4; ++i) m_models[i].setCoefficients(c);
}

void TireModelManager::setTirePressure(float frontPressure, float rearPressure) {
    m_frontPressure = frontPressure;
    m_rearPressure = rearPressure;
}

void TireModelManager::loadFromIni(const std::string& path) {
    ks::config::IniFile ini;
    if (!ini.load(path)) return;

    const char* wheelGroups[4] = {"FL", "FR", "RL", "RR"};
    for (int w = 0; w < 4; ++w) {
        PacejkaTireModel::TireCoefficients c = m_models[w].getCoefficients();
        applyFlatAndFallbacks(ini, c);
        // Axle group, then wheel-specific override.
        applyCoeffGroup(ini, (w < 2) ? "FRONT" : "REAR", c);
        if (ini.contains(wheelGroups[w], "A1") || ini.contains(wheelGroups[w], "B2"))
            applyCoeffGroup(ini, wheelGroups[w], c);
        if (PacejkaTireModel::validateCoefficients(c, nullptr))
            m_models[w].setCoefficients(c);
    }
    m_frontPressure = static_cast<float>(ini.getDouble("FRONT", "PRESSURE", m_frontPressure));
    m_rearPressure = static_cast<float>(ini.getDouble("REAR", "PRESSURE", m_rearPressure));
}

void TireModelManager::saveToIni(const std::string& path) const {
    ks::config::IniFile ini;
    const char* wheelGroups[4] = {"FL", "FR", "RL", "RR"};
    for (int w = 0; w < 4; ++w)
        saveCoeffGroup(ini, wheelGroups[w], m_models[w].getCoefficients());
    ini.setNumber("FRONT", "PRESSURE", m_frontPressure);
    ini.setNumber("REAR", "PRESSURE", m_rearPressure);
    ini.save(path);
}

std::vector<float> TireModelManager::calculateGripCircle(int wheel, float normalForce) const {
    std::vector<float> r;
    r.push_back(m_models[wheel].calculatePeakLongitudinalGrip(normalForce));
    r.push_back(m_models[wheel].calculatePeakLateralGrip(normalForce));
    return r;
}

std::vector<float> TireModelManager::calculateSlipCurve(int wheel, float normalForce) const {
    std::vector<float> r;
    auto curve = m_models[wheel].generateLateralCurve(12.0f, normalForce, 25);
    for (auto& p : curve) r.push_back(p.second);
    return r;
}

float TireModelManager::estimateLapTimeImpact(int /*wheel*/, float slipAngleChange) const {
    return slipAngleChange * 0.01f;
}

} // namespace physics
} // namespace ks
