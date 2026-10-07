/**
 * @file ksmmm.cpp
 * @brief CLI for offline constrained Moment Method (Milliken Ch.8).
 *
 * Usage:
 *   ksmmm [--speed 30] [--steer-deg 0] [--beta-max 12] [--steps 41]
 *         [--kappa 0] [--steady] [--out chart.csv]
 *         [--steer-sweep] [--mass 1200] [--wb 2.6] [--front-frac 0.45]
 */

#include "MomentMethodTool.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static float argf(int argc, char** argv, const char* key, float def)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], key) == 0)
            return static_cast<float>(std::atof(argv[i + 1]));
    }
    return def;
}

static const char* args(int argc, char** argv, const char* key, const char* def)
{
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], key) == 0)
            return argv[i + 1];
    }
    return def;
}

static bool has(int argc, char** argv, const char* key)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], key) == 0)
            return true;
    return false;
}

int main(int argc, char** argv)
{
    if (has(argc, argv, "--help") || has(argc, argv, "-h")) {
        std::printf(
            "ksmmm — constrained Moment Method (Milliken Ch.8), offline\n"
            "  --speed M/S       vehicle speed (default 30)\n"
            "  --steer-deg DEG   fixed road-wheel steer for beta sweep\n"
            "  --beta-max DEG    half-range of beta sweep (default 12)\n"
            "  --steps N         sample count (default 41)\n"
            "  --kappa K         imposed rear slip ratio\n"
            "  --steady          iterate yaw rate r≈Ay/V\n"
            "  --steer-sweep     sweep steer at beta=0 instead of beta\n"
            "  --out FILE.csv    write chart CSV\n"
            "  --mass --wb --front-frac --mu --df  vehicle spec\n");
        return 0;
    }

    ks::physics::MomentMethodTool::VehicleSpec spec;
    spec.massKg = argf(argc, argv, "--mass", 1200.f);
    spec.wheelBaseM = argf(argc, argv, "--wb", 2.6f);
    spec.frontWeightFrac = argf(argc, argv, "--front-frac", 0.45f);
    spec.mu = argf(argc, argv, "--mu", 1.0f);
    spec.downforceN = argf(argc, argv, "--df", 0.f);

    ks::physics::MomentMethodTool tool(spec);

    const float speed = argf(argc, argv, "--speed", 30.f);
    const float steerDeg = argf(argc, argv, "--steer-deg", 0.f);
    const float betaMaxDeg = argf(argc, argv, "--beta-max", 12.f);
    const int steps = static_cast<int>(argf(argc, argv, "--steps", 41.f));
    const float kappa = argf(argc, argv, "--kappa", 0.f);
    const bool steady = has(argc, argv, "--steady");
    const bool steerSweep = has(argc, argv, "--steer-sweep");
    const char* outPath = args(argc, argv, "--out", "");

    if (has(argc, argv, "--gg")) {
        ks::physics::MomentMethodTool::GgConfig gcfg;
        gcfg.angleSteps = static_cast<int>(argf(argc, argv, "--gg-steps", 72.f));
        gcfg.brakeBias = argf(argc, argv, "--brake-bias", 0.55f);
        auto env = tool.sweepGgEnvelope(gcfg);
        std::printf("# g-g envelope  mass=%.0f  mu=%.2f  df=%.0f  steps=%d\n",
                    spec.massKg, spec.mu, spec.downforceN, gcfg.angleSteps);
        std::printf("%10s %8s %8s %8s\n", "theta_deg", "ax_g", "ay_g", "g_mag");
        float maxA = 0.f, maxB = 0.f, maxLat = 0.f;
        for (const auto& pt : env) {
            std::printf("%10.1f %8.3f %8.3f %8.3f\n",
                        pt.thetaDeg, pt.axG, pt.ayG, pt.gMag);
            if (pt.axG > maxA) maxA = pt.axG;
            if (pt.axG < maxB) maxB = pt.axG;
            if (pt.ayG > maxLat) maxLat = pt.ayG;
            if (-pt.ayG > maxLat) maxLat = -pt.ayG;
        }
        std::printf("# max accel=+%.2fg  max brake=%.2fg  max |lat|=%.2fg\n",
                    maxA, maxB, maxLat);
        if (outPath && outPath[0]) {
            if (ks::physics::MomentMethodTool::writeGgCsv(outPath, env))
                std::printf("# wrote %s (%zu rows)\n", outPath, env.size());
            else
                std::fprintf(stderr, "failed to write %s\n", outPath);
        }
        return 0;
    }

    if (has(argc, argv, "--pair")) {
        const float speed = argf(argc, argv, "--speed", 40.f);
        const float betaMax = argf(argc, argv, "--beta-max", 8.f) * 0.017453292f;
        const int steps = static_cast<int>(argf(argc, argv, "--steps", 17.f));
        std::printf("# pair analysis V=%.1f\n", speed);
        std::printf("%8s %10s %10s %10s %10s\n", "beta_deg", "FyF", "FyR", "FyF/FyR", "N_Nm");
        for (int i = 0; i < steps; ++i) {
            float t = steps==1?0.f: float(i)/float(steps-1);
            float beta = -betaMax + t * 2.f * betaMax;
            auto s = tool.evaluate(beta, 0.f, speed, 0.f, true, 0.f);
            float ratio = (std::fabs(s.FyR)>1.f) ? s.FyF/s.FyR : 0.f;
            std::printf("%8.2f %10.0f %10.0f %10.3f %10.0f\n",
                beta*57.2958f, s.FyF, s.FyR, ratio, s.Nzm);
        }
        return 0;
    }

    std::vector<ks::physics::MomentMethodTool::Sample> rows;

    if (steerSweep) {
        const float smax = betaMaxDeg * (3.14159265f / 180.f); // reuse as steer range
        rows = tool.sweepSteer(speed, 0.f, -smax, smax, steps, kappa);
        std::printf("# steer sweep  V=%.1f m/s  beta=0  kappa=%.3f  n=%d\n",
                    speed, kappa, steps);
    } else {
        ks::physics::MomentMethodTool::SweepConfig cfg;
        cfg.speedMs = speed;
        cfg.steerRad = steerDeg * (3.14159265f / 180.f);
        cfg.betaMin = -betaMaxDeg * (3.14159265f / 180.f);
        cfg.betaMax = betaMaxDeg * (3.14159265f / 180.f);
        cfg.betaSteps = steps;
        cfg.kappaRear = kappa;
        cfg.steadyYawRate = steady;
        rows = tool.sweepBeta(cfg);
        std::printf("# beta sweep  V=%.1f m/s  steer=%.1f deg  kappa=%.3f  steady=%d  n=%d\n",
                    speed, steerDeg, kappa, steady ? 1 : 0, steps);
    }

    std::printf("%8s %8s %8s %8s %10s %8s %8s\n",
                "beta_deg", "Ay_g", "CN", "N_Nm", "aF_deg", "aR_deg", "Fy_tot");
    float cnAtZeroAy = 0.f;
    float ayAtZeroCn = 0.f;
    bool haveCn0 = false, haveAy0 = false;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& s = rows[i];
        const float bd = s.betaRad * 57.2957795f;
        std::printf("%8.2f %8.3f %8.4f %10.0f %8.2f %8.2f %8.0f\n",
                    bd, s.AyG, s.CN, s.Nzm,
                    s.alphaF * 57.2957795f, s.alphaR * 57.2957795f,
                    s.FyF + s.FyR);
        if (i + 1 < rows.size()) {
            const auto& n = rows[i + 1];
            // zero-cross CN → residual yaw moment at that Ay
            if (!haveCn0 && s.CN * n.CN <= 0.f) {
                const float t = (std::fabs(s.CN) < 1e-9f)
                                    ? 0.f
                                    : s.CN / (s.CN - n.CN);
                ayAtZeroCn = s.AyG + t * (n.AyG - s.AyG);
                haveCn0 = true;
            }
            if (!haveAy0 && s.AyG * n.AyG <= 0.f) {
                const float t = (std::fabs(s.AyG) < 1e-9f)
                                    ? 0.f
                                    : s.AyG / (s.AyG - n.AyG);
                cnAtZeroAy = s.CN + t * (n.CN - s.CN);
                haveAy0 = true;
            }
        }
    }

    if (haveAy0)
        std::printf("# stability: CN @ Ay=0  = %+.4f  (CN>0 → restoring / understeer-ish)\n",
                    cnAtZeroAy);
    if (haveCn0)
        std::printf("# control:   Ay @ CN=0  = %+.3f g\n", ayAtZeroCn);

    if (outPath && outPath[0]) {
        if (ks::physics::MomentMethodTool::writeCsv(outPath, rows))
            std::printf("# wrote %s (%zu rows)\n", outPath, rows.size());
        else
            std::fprintf(stderr, "failed to write %s\n", outPath);
    }
    return 0;
}
