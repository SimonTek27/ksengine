#include "TireNormalizeTool.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

namespace ks {
namespace physics {

TireNormalizeTool::TireNormalizeTool()
    : m_tires(KsTireModel::getSlickTireCoefficients()) {}

TireNormalizeTool::TireNormalizeTool(const KsTireModel::TireCoefficients& c)
    : m_tires(c) {}

void TireNormalizeTool::setCoefficients(const KsTireModel::TireCoefficients& c)
{
    m_tires.setCoefficients(c);
}

bool TireNormalizeTool::loadIni(const std::string& path)
{
    m_tires.loadFromIni(path);
    return true;
}

void TireNormalizeTool::usePreset(const std::string& name)
{
    std::string n = name;
    for (char& ch : n)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (n == "street")
        m_tires.setCoefficients(KsTireModel::getStreetTireCoefficients());
    else if (n == "wet")
        m_tires.setCoefficients(KsTireModel::getWetTireCoefficients());
    else if (n == "rally")
        m_tires.setCoefficients(KsTireModel::getRallyTireCoefficients());
    else
        m_tires.setCoefficients(KsTireModel::getSlickTireCoefficients());
}

TireNormalizeTool::Metrics TireNormalizeTool::evaluate(float fz0, float mu) const
{
    Metrics m;
    auto c = m_tires.getCoefficients();
    if (fz0 < 0.f)
        fz0 = c.nominalLoadN > 100.f ? c.nominalLoadN : 4000.f;
    m.fz0 = fz0;

    std::string err;
    m.valid = KsTireModel::validateCoefficients(c, &err);
    m.message = err;

    // Sweep pure lateral
    float peakFy = 0.f, aPeak = 0.f;
    for (int i = 0; i <= 80; ++i) {
        const float a = (static_cast<float>(i) / 80.f) * 0.35f; // 0..20°
        auto f = m_tires.calculateCombinedSlip(a, 0.f, fz0, 0.f, mu, c.optPressurePsi, c.optTempC);
        if (std::fabs(f.lateralForce) > peakFy) {
            peakFy = std::fabs(f.lateralForce);
            aPeak = a;
        }
    }
    m.peakFy = peakFy;
    m.muY = peakFy / std::max(fz0, 1.f);
    m.alphaPeakDeg = aPeak * 57.2957795f;

    // Cornering stiffness near 0
    {
        const float a = 0.02f; // ~1.15°
        auto f = m_tires.calculateCombinedSlip(a, 0.f, fz0, 0.f, mu, c.optPressurePsi, c.optTempC);
        m.corneringStiff = std::fabs(f.lateralForce) / a;
    }

    // Pure longitudinal
    float peakFx = 0.f, kPeak = 0.f;
    for (int i = 0; i <= 80; ++i) {
        const float k = (static_cast<float>(i) / 80.f) * 0.35f;
        auto f = m_tires.calculateCombinedSlip(0.f, k, fz0, 0.f, mu, c.optPressurePsi, c.optTempC);
        if (std::fabs(f.longitudinalForce) > peakFx) {
            peakFx = std::fabs(f.longitudinalForce);
            kPeak = k;
        }
    }
    m.peakFx = peakFx;
    m.muX = peakFx / std::max(fz0, 1.f);
    m.kappaPeak = kPeak;

    {
        const float k = 0.02f;
        auto f = m_tires.calculateCombinedSlip(0.f, k, fz0, 0.f, mu, c.optPressurePsi, c.optTempC);
        m.longStiff = std::fabs(f.longitudinalForce) / k;
    }

    // Load sensitivity check at 1.5 Fz0
    {
        const float fz2 = 1.5f * fz0;
        float peak2 = 0.f;
        for (int i = 0; i <= 40; ++i) {
            const float a = (static_cast<float>(i) / 40.f) * 0.30f;
            auto f = m_tires.calculateCombinedSlip(a, 0.f, fz2, 0.f, mu, c.optPressurePsi, c.optTempC);
            peak2 = std::max(peak2, std::fabs(f.lateralForce));
        }
        const float linear = 1.5f * peakFy;
        m.loadSensRatio = peak2 / std::max(linear, 1.f); // <1 = load sensitive (expected)
    }

    if (m.muY < 0.5f || m.muY > 2.5f)
        m.message += (m.message.empty() ? "" : "; ") + std::string("muY out of typical 0.5–2.5");
    if (m.muX < 0.5f || m.muX > 2.5f)
        m.message += (m.message.empty() ? "" : "; ") + std::string("muX out of typical 0.5–2.5");

    return m;
}

TireNormalizeTool::Metrics TireNormalizeTool::normalizeToMu(float targetMu, float fz0)
{
    auto c = m_tires.getCoefficients();
    if (fz0 < 0.f)
        fz0 = c.nominalLoadN > 100.f ? c.nominalLoadN : 4000.f;

    Metrics before = evaluate(fz0, 1.f);
    const float target = std::clamp(targetMu, 0.5f, 2.2f);

    // Scale a2/b2 (peak-ish coefficients) proportionally toward target μ
    if (before.muY > 0.05f) {
        const float sy = target / before.muY;
        c.a2 *= sy;
        c.a3 *= std::sqrt(sy); // stiffness-ish
    }
    if (before.muX > 0.05f) {
        const float sx = target / before.muX;
        c.b2 *= sx;
        c.b4 *= std::sqrt(sx);
    }
    c.nominalLoadN = fz0;
    m_tires.setCoefficients(c);

    Metrics after = evaluate(fz0, 1.f);
    after.message = "normalized to target mu=" + std::to_string(target)
                  + " (was muY=" + std::to_string(before.muY)
                  + " muX=" + std::to_string(before.muX) + ")";
    return after;
}

std::vector<TireNormalizeTool::CurvePoint> TireNormalizeTool::lateralCurve(
    float fz, float maxAlphaDeg, int points) const
{
    std::vector<CurvePoint> out;
    const int n = std::max(5, points);
    out.reserve(static_cast<size_t>(n));
    const float aMax = maxAlphaDeg * (3.14159265f / 180.f);
    auto c = m_tires.getCoefficients();
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n - 1);
        const float a = -aMax + t * 2.f * aMax;
        auto f = m_tires.calculateCombinedSlip(a, 0.f, fz, 0.f, 1.f, c.optPressurePsi, c.optTempC);
        out.push_back({a, f.lateralForce});
    }
    return out;
}

std::vector<TireNormalizeTool::CurvePoint> TireNormalizeTool::longitudinalCurve(
    float fz, float maxKappa, int points) const
{
    std::vector<CurvePoint> out;
    const int n = std::max(5, points);
    out.reserve(static_cast<size_t>(n));
    auto c = m_tires.getCoefficients();
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n - 1);
        const float k = -maxKappa + t * 2.f * maxKappa;
        auto f = m_tires.calculateCombinedSlip(0.f, k, fz, 0.f, 1.f, c.optPressurePsi, c.optTempC);
        out.push_back({k, f.longitudinalForce});
    }
    return out;
}

bool TireNormalizeTool::writeCurveCsv(const std::string& path,
                                      const std::vector<CurvePoint>& lat,
                                      const std::vector<CurvePoint>& lng)
{
    std::ofstream os(path);
    if (!os)
        return false;
    os << "alpha_rad,Fy,kappa,Fx\n";
    const size_t n = std::max(lat.size(), lng.size());
    for (size_t i = 0; i < n; ++i) {
        if (i < lat.size())
            os << lat[i].x << ',' << lat[i].force;
        else
            os << ',';
        os << ',';
        if (i < lng.size())
            os << lng[i].x << ',' << lng[i].force;
        else
            os << ',';
        os << '\n';
    }
    return static_cast<bool>(os);
}

bool TireNormalizeTool::writeReport(const std::string& path, const Metrics& m,
                                    const KsTireModel::TireCoefficients& c)
{
    std::ofstream os(path);
    if (!os)
        return false;
    os << "ksengine tire normalization report (Milliken Ch.14)\n";
    os << "valid=" << (m.valid ? 1 : 0) << "\n";
    os << "message=" << m.message << "\n";
    os << "Fz0=" << m.fz0 << "\n";
    os << "peakFy=" << m.peakFy << " muY=" << m.muY << " alphaPeakDeg=" << m.alphaPeakDeg << "\n";
    os << "peakFx=" << m.peakFx << " muX=" << m.muX << " kappaPeak=" << m.kappaPeak << "\n";
    os << "Ca_N_per_rad=" << m.corneringStiff << " Cs_N_per_kappa=" << m.longStiff << "\n";
    os << "loadSensRatio=" << m.loadSensRatio << " (<1 expected)\n";
    os << "a2=" << c.a2 << " b2=" << c.b2 << " nominalLoadN=" << c.nominalLoadN << "\n";
    return static_cast<bool>(os);
}

} // namespace physics
} // namespace ks
