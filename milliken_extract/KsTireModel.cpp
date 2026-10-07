#include "KsTireModel.h"

#include <cstdio>
#include <cctype>
#include <fstream>
#include <string>

namespace ks {
namespace physics {
namespace {

/** Fast atan approx (|x| reasonable); relative error ~1e-3 — enough for MF feel. */
inline float fastAtan(float x)
{
    const float ax = std::fabs(x);
    // rational approximation
    const float num = x * (1.68676291f + ax * 0.43784973f);
    const float den = 1.0f + ax * (1.0971495f + ax * 0.15040819f);
    return num / den;
}

inline float fastSin(float x)
{
    // x typically in [-pi/2, pi/2] for MF
    const float x2 = x * x;
    return x * (1.0f - x2 * (0.16666667f - x2 * 0.00833333f));
}

} // namespace

KsTireModel::KsTireModel()
    : m_coefficients(getSlickTireCoefficients()) {}

KsTireModel::KsTireModel(const TireCoefficients& coeffs)
    : m_coefficients(coeffs) {}

float KsTireModel::magicFormula(float x, float B, float C, float D, float E)
{
    const float Bx = B * x;
    const float inner = Bx - E * (Bx - fastAtan(Bx));
    return D * fastSin(C * fastAtan(inner));
}

void KsTireModel::ensureCoeffCache(float Fz, float camberAbs, float mu) const
{
    // Rebuild when operating point drifts (Fz dominates cost of B/D).
    constexpr float kFzTol = 25.f;
    constexpr float kCamTol = 0.01f;
    constexpr float kMuTol = 0.02f;
    if (m_cache.valid
        && std::fabs(Fz - m_cache.Fz) < kFzTol
        && std::fabs(camberAbs - m_cache.camberAbs) < kCamTol
        && std::fabs(mu - m_cache.mu) < kMuTol) {
        return;
    }

    const auto& a = m_coefficients;
    const float Fz_kN = Fz * 0.001f;
    const float loadSens = calculateLoadSensitivity(Fz);

    float Dy = (a.a1 * Fz_kN + a.a2) * Fz_kN;
    Dy = std::fabs(Dy) * mu * loadSens;
    const float DyFloor = 1.15f * mu * loadSens * Fz;
    if (Dy < DyFloor) Dy = DyFloor;
    // sin(2*atan(z)) = 2z/(1+z^2) — exact identity, zero trig
    const float z = Fz_kN / (a.a4 > 1e-4f ? a.a4 : 1.82f);
    const float sin2atan = 2.f * z / (1.f + z * z);
    float BCD_y = a.a3 * sin2atan * (1.0f - a.a5 * camberAbs);
    if (std::fabs(BCD_y) < 8.f * Dy) BCD_y = 8.f * Dy;
    float By = std::clamp(std::fabs(BCD_y) / (Dy + 1e-3f), 0.1f, 40.0f);
    const float Cy = 1.3f;
    const float Ey = a.a6 * Fz_kN + a.a7;

    float Dx = (a.b1 * Fz_kN + a.b2) * Fz_kN;
    Dx = std::fabs(Dx) * mu * loadSens;
    // Floor peak ~ μ Fz (race-tire scale)
    const float DxFloor = 1.05f * mu * loadSens * Fz;
    if (Dx < DxFloor) Dx = DxFloor;
    // exp(-b5*Fz_kN) — cheap series for small arg
    const float expArg = -a.b5 * Fz_kN;
    float expTerm = 1.f + expArg + 0.5f * expArg * expArg;
    if (expArg < -1.f) expTerm = std::exp(expArg);
    float BCD_x = (a.b3 * Fz_kN * Fz_kN + a.b4 * Fz_kN) * expTerm;
    if (std::fabs(BCD_x) < 10.f * Dx) BCD_x = 10.f * Dx;
    float Bx = std::clamp(std::fabs(BCD_x) / (Dx + 1e-3f), 0.1f, 40.0f);
    const float Cx = 1.65f;
    const float Ex = a.b6 * Fz_kN + a.b7;

    m_cache.valid = true;
    m_cache.Fz = Fz;
    m_cache.camberAbs = camberAbs;
    m_cache.mu = mu;
    m_cache.loadSens = loadSens;
    m_cache.Dy = Dy;
    m_cache.By = By;
    m_cache.Cy = Cy;
    m_cache.Ey = Ey;
    m_cache.Dx = Dx;
    m_cache.Bx = Bx;
    m_cache.Cx = Cx;
    m_cache.Ex = Ex;
}

KsTireModel::TireForces KsTireModel::calculateForces(const TireState& state) const
{
    TireForces out;
    const float Fz = std::max(state.normalForce, 1.0f);
    const float mu = std::max(state.frictionCoefficient, 0.1f);
    const float camAbs = std::fabs(state.camberAngle);

    ensureCoeffCache(Fz, camAbs, mu);

    const float alpha = state.slipAngle;
    const float kappa = state.slipRatio;

    float Fy = magicFormula(alpha, m_cache.By, m_cache.Cy, m_cache.Dy, m_cache.Ey);
    Fy += Fz * state.camberAngle * 0.1f;

    float Fx = magicFormula(kappa, m_cache.Bx, m_cache.Cx, m_cache.Dx, m_cache.Ex);

    // Combined slip (friction ellipse) — rsqrt when needed
    const float sx = kappa * (1.f / 0.15f);
    const float sy = alpha * (1.f / 0.12f);
    const float comb2 = sx * sx + sy * sy;
    if (comb2 > 1.0f) {
        const float inv = 1.0f / std::sqrt(comb2);
        Fx *= inv;
        Fy *= inv;
    }

    const float env = calculateTemperatureEffect(state.tireTemp)
                    * calculatePressureEffect(state.tirePressure);
    Fx *= env;
    Fy *= env;

    out.lateralForce = Fy;
    out.longitudinalForce = Fx;
    out.aligningMoment = -Fy * 0.03f;
    out.slipAngleDeg = radToDeg(alpha);
    out.slipRatioPercent = kappa * 100.0f;
    return out;
}

float KsTireModel::calculateLateralForce(const TireState& state) const
{
    return calculateForces(state).lateralForce;
}

float KsTireModel::calculateLongitudinalForce(const TireState& state) const
{
    return calculateForces(state).longitudinalForce;
}

float KsTireModel::calculateAligningMoment(const TireState& state) const
{
    return calculateForces(state).aligningMoment;
}

KsTireModel::TireForces KsTireModel::calculateCombinedSlip(
    float slipAngle, float slipRatio, float normalForce, float camber) const
{
    TireState s;
    s.slipAngle = slipAngle;
    s.slipRatio = slipRatio;
    s.normalForce = normalForce;
    s.camberAngle = camber;
    return calculateForces(s);
}

KsTireModel::TireForces KsTireModel::calculateCombinedSlip(
    float slipAngle, float slipRatio, float normalForce, float camber,
    float frictionMu, float pressurePsi, float tempC) const
{
    TireState s;
    s.slipAngle = slipAngle;
    s.slipRatio = slipRatio;
    s.normalForce = normalForce;
    s.camberAngle = camber;
    s.frictionCoefficient = std::max(0.05f, frictionMu);
    s.tirePressure = pressurePsi;
    s.tireTemp = tempC;
    return calculateForces(s);
}

void KsTireModel::calculateCombinedSlipBatch(
    const float slipAngle[4], const float slipRatio[4],
    const float normalForce[4], const float camber[4],
    float frictionMu, float pressurePsi, float tempC,
    TireForces out[4]) const
{
    for (int i = 0; i < 4; ++i) {
        out[i] = calculateCombinedSlip(slipAngle[i], slipRatio[i], normalForce[i],
                                       camber[i], frictionMu, pressurePsi, tempC);
    }
}

void KsTireModel::calculateCombinedSlipBatch(
    const float slipAngle[4], const float slipRatio[4],
    const float normalForce[4], const float camber[4],
    float frictionMu,
    const float pressurePsi[4], const float tempC[4],
    TireForces out[4]) const
{
    for (int i = 0; i < 4; ++i) {
        out[i] = calculateCombinedSlip(slipAngle[i], slipRatio[i], normalForce[i],
                                       camber[i], frictionMu, pressurePsi[i], tempC[i]);
    }
}

float KsTireModel::calculateLoadSensitivity(float normalForce) const
{
    const float Fz = std::max(normalForce, 1.0f);
    const float Fz0 = std::max(m_coefficients.nominalLoadN, 500.f);
    const float ratio = Fz / Fz0;
    const float expn = std::clamp(m_coefficients.loadSensExp, 0.5f, 1.2f);
    // pow(ratio, exp) ≈ exp(exp*ln(ratio)); for ratio near 1 use binomial
    float s;
    if (std::fabs(ratio - 1.f) < 0.25f) {
        const float d = ratio - 1.f;
        s = 1.f + expn * d + 0.5f * expn * (expn - 1.f) * d * d;
    } else {
        s = std::pow(ratio, expn);
    }
    return std::clamp(s, m_coefficients.loadSensMin, m_coefficients.loadSensMax);
}

float KsTireModel::calculateTemperatureEffect(float temp) const
{
    const float opt = m_coefficients.optTempC > 1.f ? m_coefficients.optTempC : 80.0f;
    const float d = (temp - opt) * (1.f / 30.0f);
    return std::clamp(1.0f - d * d * 0.25f, 0.5f, 1.05f);
}

float KsTireModel::calculatePressureEffect(float pressurePsi) const
{
    const float opt = m_coefficients.optPressurePsi > 1.f ? m_coefficients.optPressurePsi : 26.0f;
    const float d = (pressurePsi - opt) * (1.f / 8.0f);
    return std::clamp(1.0f - d * d * 0.15f, 0.7f, 1.05f);
}

float KsTireModel::calculateWearEffect(float wear) const
{
    return std::clamp(1.0f - wear * 0.35f, 0.4f, 1.0f);
}

float KsTireModel::calculatePeakLateralGrip(float normalForce) const
{
    TireState s;
    s.normalForce = normalForce;
    s.slipAngle = degToRad(8.0f);
    return std::fabs(calculateForces(s).lateralForce);
}

float KsTireModel::calculatePeakLongitudinalGrip(float normalForce) const
{
    TireState s;
    s.normalForce = normalForce;
    s.slipRatio = 0.12f;
    return std::fabs(calculateForces(s).longitudinalForce);
}

std::vector<std::pair<float, float>> KsTireModel::generateLateralCurve(
    float maxSlipAngle, float normalForce, int points) const
{
    std::vector<std::pair<float, float>> curve;
    if (points < 2) return curve;
    curve.reserve(static_cast<size_t>(points));
    for (int i = 0; i < points; ++i) {
        float a = -maxSlipAngle + 2.0f * maxSlipAngle * static_cast<float>(i) / static_cast<float>(points - 1);
        TireState s;
        s.slipAngle = degToRad(a);
        s.normalForce = normalForce;
        curve.emplace_back(a, calculateForces(s).lateralForce);
    }
    return curve;
}

std::vector<std::pair<float, float>> KsTireModel::generateLongitudinalCurve(
    float maxSlipRatio, float normalForce, int points) const
{
    std::vector<std::pair<float, float>> curve;
    if (points < 2) return curve;
    curve.reserve(static_cast<size_t>(points));
    for (int i = 0; i < points; ++i) {
        float k = -maxSlipRatio + 2.0f * maxSlipRatio * static_cast<float>(i) / static_cast<float>(points - 1);
        TireState s;
        s.slipRatio = k;
        s.normalForce = normalForce;
        curve.emplace_back(k, calculateForces(s).longitudinalForce);
    }
    return curve;
}

KsTireModel::TireCoefficients KsTireModel::getStreetTireCoefficients()
{
    TireCoefficients c;
    c.a2 = 900.0f;
    c.b2 = 1000.0f;
    return c;
}

KsTireModel::TireCoefficients KsTireModel::getSlickTireCoefficients()
{
    TireCoefficients c;
    c.a2 = 1100.0f;
    c.b2 = 1200.0f;
    c.a3 = 1200.0f;
    return c;
}

KsTireModel::TireCoefficients KsTireModel::getWetTireCoefficients()
{
    TireCoefficients c;
    c.a2 = 700.0f;
    c.b2 = 750.0f;
    return c;
}

KsTireModel::TireCoefficients KsTireModel::getRallyTireCoefficients()
{
    TireCoefficients c;
    c.a2 = 850.0f;
    c.b2 = 900.0f;
    return c;
}

bool KsTireModel::validateCoefficients(const TireCoefficients& coeffs, std::string* error)
{
    if (coeffs.a2 <= 0.0f || coeffs.b2 <= 0.0f) {
        if (error) *error = "Peak coefficients must be positive";
        return false;
    }
    return true;
}

void KsTireModel::loadFromIni(const std::string& iniPath)
{
    std::ifstream in(iniPath);
    if (!in) {
        std::fprintf(stderr, "KsTireModel: cannot open %s\n", iniPath.c_str());
        return;
    }
    auto& c = m_coefficients;
    std::string line;
    auto trim = [](std::string& s) {
        while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) s.erase(s.begin());
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.pop_back();
    };
    auto upper = [](std::string s) {
        for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
        return s;
    };
    while (std::getline(in, line)) {
        auto sc = line.find(';');
        if (sc != std::string::npos) line = line.substr(0, sc);
        trim(line);
        if (line.empty() || line.front() == '[') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = upper(line.substr(0, eq));
        std::string v = line.substr(eq + 1);
        trim(k);
        trim(v);
        float val = 0.f;
        try {
            val = std::stof(v);
        } catch (...) {
            continue;
        }
        if (k == "A1") c.a1 = val;
        else if (k == "A2") c.a2 = val;
        else if (k == "A3") c.a3 = val;
        else if (k == "A4") c.a4 = val;
        else if (k == "A5") c.a5 = val;
        else if (k == "A6") c.a6 = val;
        else if (k == "A7") c.a7 = val;
        else if (k == "A8") c.a8 = val;
        else if (k == "A9") c.a9 = val;
        else if (k == "A10") c.a10 = val;
        else if (k == "A11") c.a11 = val;
        else if (k == "A12") c.a12 = val;
        else if (k == "A13") c.a13 = val;
        else if (k == "B1") c.b1 = val;
        else if (k == "B2") c.b2 = val;
        else if (k == "B3") c.b3 = val;
        else if (k == "B4") c.b4 = val;
        else if (k == "B5") c.b5 = val;
        else if (k == "B6") c.b6 = val;
        else if (k == "B7") c.b7 = val;
        else if (k == "B8") c.b8 = val;
        else if (k == "C1") c.c1 = val;
        else if (k == "C2") c.c2 = val;
        else if (k == "C3") c.c3 = val;
        else if (k == "C4") c.c4 = val;
        else if (k == "C5") c.c5 = val;
        else if (k == "NOMINAL_LOAD" || k == "FZ0" || k == "LOAD0") c.nominalLoadN = val;
        else if (k == "LOAD_SENSITIVITY" || k == "LOAD_EXP" || k == "LS_EXP") c.loadSensExp = val;
        else if (k == "LOAD_SENS_MIN") c.loadSensMin = val;
        else if (k == "LOAD_SENS_MAX") c.loadSensMax = val;
        else if (k == "PRESSURE_OPT" || k == "OPT_PRESSURE") c.optPressurePsi = val;
        else if (k == "TEMP_OPT" || k == "OPT_TEMP") c.optTempC = val;
    }
    invalidateCache();
}

// ---------------------------------------------------------------------------
// KsTireModelSet
// ---------------------------------------------------------------------------

void KsTireModelSet::setTireCompound(int compound)
{
    m_compound = compound;
    KsTireModel::TireCoefficients c;
    switch (compound) {
    case 1: c = KsTireModel::getStreetTireCoefficients(); break;
    case 2: c = KsTireModel::getWetTireCoefficients(); break;
    case 3: c = KsTireModel::getRallyTireCoefficients(); break;
    default: c = KsTireModel::getSlickTireCoefficients(); break;
    }
    for (auto& m : m_models) m.setCoefficients(c);
}

void KsTireModelSet::setTirePressure(float frontPressure, float rearPressure)
{
    m_frontPressure = frontPressure;
    m_rearPressure = rearPressure;
}

void KsTireModelSet::loadFromIni(const std::string& tireIniPath)
{
    m_models[0].loadFromIni(tireIniPath);
    const auto c = m_models[0].getCoefficients();
    for (int i = 1; i < 4; ++i) m_models[static_cast<size_t>(i)].setCoefficients(c);
}

void KsTireModelSet::saveToIni(const std::string& /*tireIniPath*/) const
{
    // Optional persistence — coeffs live in memory / vehicle INI pipeline.
}

std::vector<float> KsTireModelSet::calculateGripCircle(int wheel, float normalForce) const
{
    std::vector<float> circle;
    circle.reserve(36);
    const auto& m = getModel(wheel);
    for (int i = 0; i < 36; ++i) {
        const float ang = static_cast<float>(i) * 10.f * 0.0174532925f;
        KsTireModel::TireState s;
        s.normalForce = normalForce;
        s.slipAngle = std::sin(ang) * 0.15f;
        s.slipRatio = std::cos(ang) * 0.12f;
        auto f = m.calculateForces(s);
        circle.push_back(std::sqrt(f.lateralForce * f.lateralForce + f.longitudinalForce * f.longitudinalForce));
    }
    return circle;
}

std::vector<float> KsTireModelSet::calculateSlipCurve(int wheel, float normalForce) const
{
    std::vector<float> curve;
    curve.reserve(21);
    const auto& m = getModel(wheel);
    for (int i = 0; i <= 20; ++i) {
        KsTireModel::TireState s;
        s.normalForce = normalForce;
        s.slipAngle = KsTireModel::degToRad(static_cast<float>(i));
        curve.push_back(m.calculateForces(s).lateralForce);
    }
    return curve;
}

float KsTireModelSet::estimateLapTimeImpact(int /*wheel*/, float slipAngleChange) const
{
    return slipAngleChange * 0.02f; // rough heuristic seconds
}

} // namespace physics
} // namespace ks
