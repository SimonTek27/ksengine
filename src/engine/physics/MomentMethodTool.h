#pragma once
#include "KsExport.h"

/**
 * @file MomentMethodTool.h
 * @brief Offline constrained Moment Method (Milliken Ch.8) — NOT in sim hot path.
 *
 * Sweeps vehicle sideslip β (and optional steer δ) at fixed speed, computes
 * normalized lateral acceleration A_Y and yaw moment coefficient C_N from
 * bicycle kinematics + KsTireModel forces.
 *
 *   α_F = β + (a r)/V − δ ,  α_R = β − (b r)/V
 *   For pure constrained MMM with free curvature often set r = V / R path or r=0
 *   for "stability" diagrams; we use r = 0 (straight-path constrained) by default
 *   and optional steady-turn r = Ay-consistent iteration.
 *
 * Outputs: β, δ, A_Y (g), C_N (nondim), N (N·m), FyF/R, αF/R.
 *
 * G9: g-g diagram envelope (Milliken Ch.9) via friction-circle + load transfer.
 */

#include "KsTireModel.h"

#include <string>
#include <vector>

namespace ks {
namespace physics {

class KSENGINE_API MomentMethodTool {
public:
    struct VehicleSpec {
        float massKg = 1200.f;
        float wheelBaseM = 2.6f;
        float frontWeightFrac = 0.45f; // → b = frac * L, a = (1-frac)*L
        float trackM = 1.6f;
        float cgHeightM = 0.35f;
        float mu = 1.0f;
        float tirePressurePsi = 26.f;
        float tireTempC = 80.f;
        float downforceN = 0.f; // total aero DF at this operating speed
        float frontAeroFrac = 0.42f;
    };

    struct Sample {
        float betaRad = 0.f;
        float steerRad = 0.f;
        float speedMs = 0.f;
        float alphaF = 0.f;
        float alphaR = 0.f;
        float FyF = 0.f;
        float FyR = 0.f;
        float FxF = 0.f;
        float FxR = 0.f;
        float Nzm = 0.f;     // yaw moment N·m
        float AyG = 0.f;     // lateral accel in g
        float CN = 0.f;      // N / (W * L) nondimensional
        float kappa = 0.f;   // imposed long. slip (driven axle proxy)
    };

    struct SweepConfig {
        float speedMs = 30.f;
        float steerRad = 0.f;
        float betaMin = -0.20f; // rad ~ ±11.5°
        float betaMax = 0.20f;
        int betaSteps = 41;
        float kappaRear = 0.f; // imposed rear slip ratio (thrust/brake)
        bool includeLoadTransfer = true;
        bool steadyYawRate = false; // if true, r ≈ Ay/V iteration
        int steadyIters = 3;
    };

    MomentMethodTool();
    explicit MomentMethodTool(const VehicleSpec& spec);

    void setSpec(const VehicleSpec& s) { m_spec = s; }
    const VehicleSpec& spec() const { return m_spec; }
    KsTireModel& tires() { return m_tires; }
    const KsTireModel& tires() const { return m_tires; }

    /** Single constrained evaluation at (β, δ, V). */
    Sample evaluate(float betaRad, float steerRad, float speedMs,
                    float kappaRear = 0.f, bool loadTransfer = true,
                    float yawRate = 0.f) const;

    /** Sweep β at fixed δ, V → Milliken C_N–A_Y stability chart data. */
    std::vector<Sample> sweepBeta(const SweepConfig& cfg) const;

    /** Sweep δ at fixed β=0 (or given), V → control moment chart. */
    std::vector<Sample> sweepSteer(float speedMs, float betaRad,
                                   float steerMin, float steerMax, int steps,
                                   float kappaRear = 0.f) const;

    /** Write CSV: beta_deg,steer_deg,Ay_g,CN,N_Nm,alphaF_deg,alphaR_deg,FyF,FyR */
    static bool writeCsv(const std::string& path, const std::vector<Sample>& rows);

    // ---- G9: g-g diagram (Milliken Ch.9) ----
    struct GgPoint {
        float axG = 0.f;   ///< longitudinal accel in g (+ accel)
        float ayG = 0.f;   ///< lateral accel in g
        float gMag = 0.f;  ///< sqrt(ax²+ay²)
        float thetaDeg = 0.f;
        float fzTotal = 0.f;
        bool feasible = true;
    };

    struct GgConfig {
        int angleSteps = 72;       ///< full 360°
        float gMaxSearch = 3.5f;   ///< upper binary-search bound (g)
        int binaryIters = 18;
        float brakeBias = 0.55f;   ///< front brake force fraction
        bool rwd = true;           ///< drive on rear axle
        float muScale = 1.0f;      ///< extra grip scale
    };

    /** Max |g| in direction theta (0=pure +Ax, 90°=pure +Ay). */
    GgPoint maxGInDirection(float thetaRad, const GgConfig& cfg) const;

    /** Full envelope around origin. */
    std::vector<GgPoint> sweepGgEnvelope(const GgConfig& cfg) const;

    static bool writeGgCsv(const std::string& path, const std::vector<GgPoint>& rows);

private:
    bool isGgFeasible(float axMs2, float ayMs2, const GgConfig& cfg,
                      float* outFzTotal = nullptr) const;
    VehicleSpec m_spec{};
    KsTireModel m_tires;
};

} // namespace physics
} // namespace ks
