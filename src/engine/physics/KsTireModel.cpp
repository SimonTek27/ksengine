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
    // Milliken Ch. 2/14: the peak force grows more slowly than the load, so the
    // grip coefficient falls as load rises. calculateLoadSensitivity returns
    // (Fz/Fz0)^exp with exp < 1; applying it straight to the force made the
    // coefficient RISE with load (measured 0.64@2kN, 1.11@4kN, 1.31@8kN), so a
    // loaded tyre was rewarded and load transfer cost nothing — the opposite of
    // the balance lever LLTD is supposed to be. Scaling by (Fz/Fz0)^(exp-1)
    // instead leaves the peak ~ Fz^exp and the coefficient monotone decreasing,
    // on both axes.
    const float loadRatio = Fz / std::max(a.nominalLoadN, 500.f);
    const float loadExp = std::clamp(a.loadSensExp, 0.5f, 1.2f);
    const float loadSensCoef = std::pow(loadRatio, loadExp - 1.f);

    float Dy = (a.a1 * Fz_kN + a.a2) * Fz_kN;
    Dy = std::fabs(Dy) * mu * loadSensCoef;
    // sin(2*atan(z)) = 2z/(1+z^2) — exact identity, zero trig
    const float z = Fz_kN / (a.a4 > 1e-4f ? a.a4 : 1.82f);
    const float sin2atan = 2.f * z / (1.f + z * z);
    float BCD_y = a.a3 * sin2atan * (1.0f - a.a5 * camberAbs);
    // NOTE: no stiffness floor here. The extract forced BCD_y >= 8·Dy
    // (By >= 8 -> MF peak at alpha ≈ 19 deg), which put the tyre on the
    // past-peak declining branch in any slide: lateral grip collapses as
    // sideslip grows, the car cannot catch a slide and spins off
    // (ai_race_test, roadmap 3.5). HEAD's tuned config keeps By ≈ 0.4 —
    // the curve still RISES at large alpha (mushy but stable, no cliff),
    // which is the working point the roadmap documents ("current lateral
    // stiffness is below the real one ... physics gap to close with
    // relaxation length").
    // Milliken Ch. 2: a racing tyre gives its best lateral force at 5-8 deg of
    // slip angle. The raw coefficients give By ~0.25 — a curve that never peaks
    // inside any slip angle a car reaches (14 deg was still only 0.3 g), which is
    // why LLTD, roll steer and compliance could not be felt. The floor is what
    // puts the cornering stiffness where a tyre's is; the earlier extract used 8
    // (peak ~19 deg) and the AI spun, so the value comes from the angle a tyre
    // peaks at, not from a stiffness guess.
    constexpr float kCy = 1.3f;
    constexpr float kLateralPeakDeg = 9.f;
    const float byFloor = std::tan(3.14159265f / (2.f * kCy))
                        / degToRad(kLateralPeakDeg);
    if (std::fabs(BCD_y) < byFloor * Dy) BCD_y = byFloor * Dy;
    float By = std::clamp(std::fabs(BCD_y) / (Dy + 1e-3f), 0.1f, 40.0f);
    const float Cy = 1.3f;
    const float Ey = a.a6 * Fz_kN + a.a7;

    float Dx = (a.b1 * Fz_kN + a.b2) * Fz_kN;
    Dx = std::fabs(Dx) * mu * loadSensCoef;
    // Floor at the friction circle (race-tire scale), carried through the same
    // load sensitivity so the floor does not undo it at high load.
    const float DxFloor = 1.05f * mu * loadSensCoef * Fz;
    if (Dx < DxFloor) Dx = DxFloor;
    // exp(-b5*Fz_kN) — cheap series for small arg
    const float expArg = -a.b5 * Fz_kN;
    float expTerm = 1.f + expArg + 0.5f * expArg * expArg;
    if (expArg < -1.f) expTerm = std::exp(expArg);
    float BCD_x = (a.b3 * Fz_kN * Fz_kN + a.b4 * Fz_kN) * expTerm;
    // The Bx floor is what places the longitudinal peak. Solved from the formula
    // (longitudinalPeakSlip), Bx = 10 puts the maximum at 0.141 slip, inside
    // Milliken's 0.10-0.15 band (p. 37). Bx = 8 put it at 0.185, outside the band,
    // and 13.7 at 0.10 but with a knee so early that the 99% shoulder fell to
    // 0.062. 10 is also as stiff as the 0.5 ms wheel solver wants. Pinned by
    // car_electronics_test [tire-peak].
    constexpr float kBxFloor = 10.f;
    if (std::fabs(BCD_x) < kBxFloor * Dx) BCD_x = kBxFloor * Dx;
    float Bx = std::clamp(std::fabs(BCD_x) / (Dx + 1e-3f), 0.1f, 40.0f);
    const float Cx = 1.65f;
    const float Ex = a.b6 * Fz_kN + a.b7;

    // Slip where this curve actually gives its maximum force. Measured on the
    // curve (magicFormula), not solved analytically: the fast trig approximations
    // move the peak several points away from the analytic answer, and a control
    // that used the analytic one would aim past the optimum it is trying to hold.
    // Cached with the coefficients, so the search costs nothing per step.
    {
        float bestF = 0.f, bestK = 0.10f;
        for (int i = 1; i <= 40; ++i) {
            const float k = 0.01f * static_cast<float>(i);
            const float f = std::fabs(magicFormula(k, Bx, Cx, Dx, Ex));
            if (f > bestF) { bestF = f; bestK = k; }
        }
        float lo = std::max(0.001f, bestK - 0.01f), hi = bestK + 0.01f;
        for (int i = 0; i < 24; ++i) {
            const float m1 = lo + (hi - lo) / 3.f;
            const float m2 = hi - (hi - lo) / 3.f;
            if (std::fabs(magicFormula(m1, Bx, Cx, Dx, Ex))
                < std::fabs(magicFormula(m2, Bx, Cx, Dx, Ex))) lo = m1;
            else hi = m2;
        }
        m_cache.peakKappa = 0.5f * (lo + hi);
    }

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

    // Combined slip: friction-circle normalization on the FORCES, not on the
    // raw slips. The old proxy (sx = kappa/0.15, sy = alpha/0.12) divided the
    // lateral force by alpha/0.12 beyond 6.9 deg of slip, which capped every
    // tyre at ~140 N instead of the ~3 kN peak of its own MF curve — the car
    // could not build lateral force to corner at all (ai_race_test, roadmap
    // 3.5). Normalising by the peak forces keeps pure-axis behaviour at the
    // full MF curve and still limits the combined case to the friction
    // circle. (Same fix as HEAD PacejkaTireModel::calculateForces.)
    const float nx = Fx / (m_cache.Dx + 1e-3f);
    const float ny = Fy / (m_cache.Dy + 1e-3f);
    const float comb = std::sqrt(nx * nx + ny * ny);
    if (comb > 1.0f) {
        Fx /= comb;
        Fy /= comb;
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
    // Cold race slicks keep ~65% of peak grip (not 50%): sim cars launch on
    // ambient-temp tires and must still be drivable before the thermal model
    // brings them into the window.
    return std::clamp(1.0f - d * d * 0.25f, 0.65f, 1.05f);
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

float KsTireModel::longitudinalPeakSlip(float normalForce, float frictionMu) const
{
    const float Fz = std::max(normalForce, 1.0f);
    ensureCoeffCache(Fz, 0.f, std::max(frictionMu, 0.05f));
    return std::clamp(m_cache.peakKappa, 0.01f, 0.6f);
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
    TireCoefficients c = m_coefficients; // parse into a copy: commit only if valid
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
        else if (k == "C6") c.c6 = val;
        else if (k == "C7") c.c7 = val;
        else if (k == "C8") c.c8 = val;
        else if (k == "C9") c.c9 = val;
        else if (k == "C10") c.c10 = val;
        else if (k == "C11") c.c11 = val;
        else if (k == "C12") c.c12 = val;
        else if (k == "C13") c.c13 = val;
        else if (k == "NOMINAL_LOAD" || k == "FZ0" || k == "LOAD0") c.nominalLoadN = val;
        else if (k == "LOAD_SENSITIVITY" || k == "LOAD_EXP" || k == "LS_EXP") c.loadSensExp = val;
        else if (k == "LOAD_SENS_MIN") c.loadSensMin = val;
        else if (k == "LOAD_SENS_MAX") c.loadSensMax = val;
        else if (k == "PRESSURE_OPT" || k == "OPT_PRESSURE") c.optPressurePsi = val;
        else if (k == "TEMP_OPT" || k == "OPT_TEMP") c.optTempC = val;
    }
    // Validate before committing: an invalid file must leave the current
    // coefficients untouched (parity with the historical PacejkaTireModel
    // loader — see tests/ksengine/test_physics_ini.cpp).
    std::string err;
    if (validateCoefficients(c, &err)) {
        m_coefficients = c;
        invalidateCache();
    } else {
        std::fprintf(stderr, "KsTireModel: %s rejected: %s\n", iniPath.c_str(), err.c_str());
    }
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
