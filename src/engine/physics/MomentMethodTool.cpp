#include "MomentMethodTool.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>

namespace ks {
namespace physics {

MomentMethodTool::MomentMethodTool()
    : m_tires(KsTireModel::getSlickTireCoefficients()) {}

MomentMethodTool::MomentMethodTool(const VehicleSpec& spec)
    : m_spec(spec)
    , m_tires(KsTireModel::getSlickTireCoefficients()) {}

MomentMethodTool::Sample MomentMethodTool::evaluate(
    float betaRad, float steerRad, float speedMs,
    float kappaRear, bool loadTransfer, float yawRate) const
{
    Sample s;
    s.betaRad = betaRad;
    s.steerRad = steerRad;
    s.speedMs = speedMs;
    s.kappa = kappaRear;

    const float L = std::max(m_spec.wheelBaseM, 1.0f);
    const float a = L * (1.f - m_spec.frontWeightFrac);
    const float b = L * m_spec.frontWeightFrac;
    const float V = std::max(speedMs, 1.0f);
    const float mass = std::max(m_spec.massKg, 200.f);
    const float W = mass * 9.81f;

    // Bicycle slip angles (Milliken / SAE)
    s.alphaF = betaRad + (a * yawRate) / V - steerRad;
    s.alphaR = betaRad - (b * yawRate) / V;

    // Static + aero axle loads
    float loadF = W * m_spec.frontWeightFrac + m_spec.downforceN * m_spec.frontAeroFrac;
    float loadR = W * (1.f - m_spec.frontWeightFrac)
                + m_spec.downforceN * (1.f - m_spec.frontAeroFrac);

    // Approximate long. transfer from imposed rear thrust (κ → Fx later); use 0 for pure lat.
    // Lateral transfer: total ΔW = m Ay h / t — needs Ay; first pass without, optional 2nd pass.
    float FzFL = 0.5f * loadF, FzFR = 0.5f * loadF;
    float FzRL = 0.5f * loadR, FzRR = 0.5f * loadR;

    auto tire = [&](float alpha, float kappa, float Fz) {
        return m_tires.calculateCombinedSlip(alpha, kappa, Fz, -0.03f,
                                             m_spec.mu, m_spec.tirePressurePsi,
                                             m_spec.tireTempC);
    };

    auto forces = tire(s.alphaF, 0.f, FzFL);
    float FyF = forces.lateralForce;
    float FxF = forces.longitudinalForce;
    forces = tire(s.alphaF, 0.f, FzFR);
    FyF += forces.lateralForce;
    FxF += forces.longitudinalForce;

    forces = tire(s.alphaR, kappaRear, FzRL);
    float FyR = forces.lateralForce;
    float FxR = forces.longitudinalForce;
    forces = tire(s.alphaR, kappaRear, FzRR);
    FyR += forces.lateralForce;
    FxR += forces.longitudinalForce;

    float Ay = (FyF + FyR) / mass; // m/s²

    if (loadTransfer) {
        const float track = std::max(m_spec.trackM, 0.8f);
        const float h = std::max(m_spec.cgHeightM, 0.15f);
        // Longitudinal from total Fx (rear-biased)
        const float dLong = mass * ((FxF + FxR) / mass) * (h / L);
        loadF = std::max(200.f, loadF - dLong);
        loadR = std::max(200.f, loadR + dLong);
        // Lateral LLT simple 50/50 elastic for constrained chart
        const float dLat = mass * Ay * h / track;
        FzFL = std::max(150.f, 0.5f * loadF + 0.25f * dLat);
        FzFR = std::max(150.f, 0.5f * loadF - 0.25f * dLat);
        FzRL = std::max(150.f, 0.5f * loadR + 0.25f * dLat);
        FzRR = std::max(150.f, 0.5f * loadR - 0.25f * dLat);

        forces = tire(s.alphaF, 0.f, FzFL);
        FyF = forces.lateralForce; FxF = forces.longitudinalForce;
        forces = tire(s.alphaF, 0.f, FzFR);
        FyF += forces.lateralForce; FxF += forces.longitudinalForce;
        forces = tire(s.alphaR, kappaRear, FzRL);
        FyR = forces.lateralForce; FxR = forces.longitudinalForce;
        forces = tire(s.alphaR, kappaRear, FzRR);
        FyR += forces.lateralForce; FxR += forces.longitudinalForce;
        Ay = (FyF + FyR) / mass;
    }

    s.FyF = FyF;
    s.FyR = FyR;
    s.FxF = FxF;
    s.FxR = FxR;
    s.Nzm = a * FyF - b * FyR; // + small Fx couple ignored in bicycle MMM
    s.AyG = Ay / 9.81f;
    s.CN = s.Nzm / std::max(W * L, 1.f);
    return s;
}

std::vector<MomentMethodTool::Sample> MomentMethodTool::sweepBeta(
    const SweepConfig& cfg) const
{
    std::vector<Sample> out;
    const int n = std::max(2, cfg.betaSteps);
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float t = (n == 1) ? 0.f : static_cast<float>(i) / static_cast<float>(n - 1);
        const float beta = cfg.betaMin + t * (cfg.betaMax - cfg.betaMin);
        float r = 0.f;
        Sample s = evaluate(beta, cfg.steerRad, cfg.speedMs, cfg.kappaRear,
                            cfg.includeLoadTransfer, r);
        if (cfg.steadyYawRate) {
            for (int k = 0; k < cfg.steadyIters; ++k) {
                // r ≈ Ay / V for path curvature consistent with lateral accel
                r = (s.AyG * 9.81f) / std::max(cfg.speedMs, 1.f);
                s = evaluate(beta, cfg.steerRad, cfg.speedMs, cfg.kappaRear,
                             cfg.includeLoadTransfer, r);
            }
        }
        out.push_back(s);
    }
    return out;
}

std::vector<MomentMethodTool::Sample> MomentMethodTool::sweepSteer(
    float speedMs, float betaRad, float steerMin, float steerMax, int steps,
    float kappaRear) const
{
    std::vector<Sample> out;
    const int n = std::max(2, steps);
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n - 1);
        const float delta = steerMin + t * (steerMax - steerMin);
        out.push_back(evaluate(betaRad, delta, speedMs, kappaRear, true, 0.f));
    }
    return out;
}

bool MomentMethodTool::writeCsv(const std::string& path,
                                const std::vector<Sample>& rows)
{
    std::ofstream os(path);
    if (!os)
        return false;
    os << "beta_deg,steer_deg,speed_ms,Ay_g,CN,N_Nm,alphaF_deg,alphaR_deg,"
          "FyF,FyR,FxF,FxR,kappa\n";
    constexpr float r2d = 57.2957795f;
    for (const auto& s : rows) {
        os << s.betaRad * r2d << ',' << s.steerRad * r2d << ',' << s.speedMs << ','
           << s.AyG << ',' << s.CN << ',' << s.Nzm << ','
           << s.alphaF * r2d << ',' << s.alphaR * r2d << ','
           << s.FyF << ',' << s.FyR << ',' << s.FxF << ',' << s.FxR << ','
           << s.kappa << '\n';
    }
    return static_cast<bool>(os);
}


bool MomentMethodTool::isGgFeasible(float axMs2, float ayMs2, const GgConfig& cfg,
                                    float* outFzTotal) const
{
    const float L = std::max(m_spec.wheelBaseM, 1.0f);
    const float mass = std::max(m_spec.massKg, 200.f);
    const float W = mass * 9.81f;
    const float track = std::max(m_spec.trackM, 0.8f);
    const float h = std::max(m_spec.cgHeightM, 0.15f);
    const float mu = std::max(0.05f, m_spec.mu * cfg.muScale);

    float loadF = W * m_spec.frontWeightFrac + m_spec.downforceN * m_spec.frontAeroFrac;
    float loadR = W * (1.f - m_spec.frontWeightFrac)
                + m_spec.downforceN * (1.f - m_spec.frontAeroFrac);

    // Longitudinal load transfer (+Ax accel → rear)
    const float dLong = mass * axMs2 * (h / L);
    loadF = std::max(150.f, loadF - dLong);
    loadR = std::max(150.f, loadR + dLong);

    // Lateral load transfer (+Ay → load to left)
    const float dLatF = mass * ayMs2 * (h / track) * (loadF / std::max(loadF + loadR, 1.f));
    const float dLatR = mass * ayMs2 * (h / track) * (loadR / std::max(loadF + loadR, 1.f));
    float Fz[4] = {
        std::max(100.f, 0.5f * loadF + 0.5f * dLatF),
        std::max(100.f, 0.5f * loadF - 0.5f * dLatF),
        std::max(100.f, 0.5f * loadR + 0.5f * dLatR),
        std::max(100.f, 0.5f * loadR - 0.5f * dLatR)
    };
    if (outFzTotal) {
        *outFzTotal = Fz[0] + Fz[1] + Fz[2] + Fz[3];
    }

    const float FxReq = mass * axMs2;
    const float FyReq = mass * ayMs2;

    // Allocate longitudinal: accel on driven axle, brake by bias
    float Fx[4] = {0, 0, 0, 0};
    if (FxReq >= 0.f) {
        if (cfg.rwd) {
            Fx[2] = 0.5f * FxReq;
            Fx[3] = 0.5f * FxReq;
        } else {
            for (int i = 0; i < 4; ++i) Fx[i] = 0.25f * FxReq;
        }
    } else {
        const float bb = std::clamp(cfg.brakeBias, 0.3f, 0.8f);
        Fx[0] = Fx[1] = 0.5f * FxReq * bb;
        Fx[2] = Fx[3] = 0.5f * FxReq * (1.f - bb);
    }

    // Lateral proportional to normal load
    const float FzSum = std::max(Fz[0] + Fz[1] + Fz[2] + Fz[3], 1.f);
    float Fy[4];
    for (int i = 0; i < 4; ++i)
        Fy[i] = FyReq * (Fz[i] / FzSum);

    // Friction circle check per tire
    for (int i = 0; i < 4; ++i) {
        const float fMax = mu * Fz[i];
        const float f = std::sqrt(Fx[i] * Fx[i] + Fy[i] * Fy[i]);
        if (f > fMax * 1.001f)
            return false;
    }
    return true;
}

MomentMethodTool::GgPoint MomentMethodTool::maxGInDirection(
    float thetaRad, const GgConfig& cfg) const
{
    GgPoint p;
    p.thetaDeg = thetaRad * 57.2957795f;
    const float nx = std::cos(thetaRad);
    const float ny = std::sin(thetaRad);

    float lo = 0.f, hi = std::max(0.1f, cfg.gMaxSearch);
    // Expand hi if still feasible
    for (int e = 0; e < 4; ++e) {
        const float ax = hi * nx * 9.81f;
        const float ay = hi * ny * 9.81f;
        if (!isGgFeasible(ax, ay, cfg))
            break;
        hi *= 1.35f;
    }
    float fz = 0.f;
    for (int i = 0; i < cfg.binaryIters; ++i) {
        const float mid = 0.5f * (lo + hi);
        const float ax = mid * nx * 9.81f;
        const float ay = mid * ny * 9.81f;
        if (isGgFeasible(ax, ay, cfg, &fz))
            lo = mid;
        else
            hi = mid;
    }
    p.gMag = lo;
    p.axG = lo * nx;
    p.ayG = lo * ny;
    p.fzTotal = fz;
    p.feasible = lo > 1e-4f;
    return p;
}

std::vector<MomentMethodTool::GgPoint> MomentMethodTool::sweepGgEnvelope(
    const GgConfig& cfg) const
{
    std::vector<GgPoint> out;
    const int n = std::max(8, cfg.angleSteps);
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float th = (2.f * 3.14159265f) * static_cast<float>(i) / static_cast<float>(n);
        out.push_back(maxGInDirection(th, cfg));
    }
    return out;
}

bool MomentMethodTool::writeGgCsv(const std::string& path,
                                  const std::vector<GgPoint>& rows)
{
    std::ofstream os(path);
    if (!os)
        return false;
    os << "theta_deg,ax_g,ay_g,g_mag,fz_total\n";
    for (const auto& p : rows) {
        os << p.thetaDeg << ',' << p.axG << ',' << p.ayG << ','
           << p.gMag << ',' << p.fzTotal << '\n';
    }
    return static_cast<bool>(os);
}

} // namespace physics
} // namespace ks
