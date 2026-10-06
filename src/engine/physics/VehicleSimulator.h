#pragma once

#include "PhysicsCoreTypes.h"
#include "EngineModel.h"
#include "KsTireModel.h"
#include "AeroModel.h"
#include "DifferentialModel.h"
#include "SuspensionModel.h"
#include "DamageSystem.h"
#include "BrakeThermalModel.h"
#include "Rf2DamageModel.h"

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <map>

namespace ks {
namespace physics {

struct VehicleFFBSample {
    float slipAngleFL = 0, slipAngleFR = 0;
    float loadFL = 3500, loadFR = 3500;
    float camberFL = -0.03f, camberFR = -0.03f;
    float speedMs = 0;
    float steerAngle = 0;
    float aligningMomentNm = 0;
};

class VehicleSimulator {
public:
    VehicleSimulator();
    ~VehicleSimulator() = default;

    void startSimulation();
    void stopSimulation();
    void reset();
    bool isRunning() const { return m_running; }

    /** Freeze integrate (snap hold / garage park). Inputs still accepted. */
    void setFrozen(bool f) { m_frozen = f; }
    bool isFrozen() const { return m_frozen; }

    void updatePhysics(double dt);

    void setThrottle(double v);
    void setBrake(double v);
    void setSteering(double v);

    SimulationState getState() const { return m_state; }
    SimulationState& state() { return m_state; }
    const VehicleFFBSample& ffbSample() const { return m_ffb; }

    void setMass(double kg);
    void setEnginePower(double kw);
    void setMaxRpm(double rpm);
    void setDragCoeff(double cd);
    void setFrontalArea(double m2);
    void setWheelBase(double m);
    void setTrackWidth(double m);

    /** Apply garage/setup values into runtime physics parameters. */
    struct SetupParams {
        float tirePsi[4] = {2.2f, 2.2f, 2.0f, 2.0f};
        float brakeBias = 0.56f;       // front fraction 0..1
        float rideHeightFrontMm = 30.f;
        float rideHeightRearMm = 35.f;
        float springRateFront = 150.f; // N/mm proxy (wheel rate)
        float springRateRear = 180.f;
        /** Anti-roll bar effective roll stiffness (N·m/rad), Milliken Kφ contribution. */
        float arbFront = 18000.f;
        float arbRear = 12000.f;
        /** Roll center heights (m) — geometric lateral load transfer path. */
        float rollCenterFrontM = 0.05f;
        float rollCenterRearM = 0.08f;
        /** CG height above ground (m) for long/lat load transfer. */
        float cgHeightM = 0.35f;
        /** Static front weight fraction (CG longitudinal position). */
        float frontWeightFrac = 0.45f;
        /** Roll steer (rad road-wheel / rad body roll). +front → understeer with roll. */
        float rollSteerFront = 0.06f;
        float rollSteerRear = -0.04f;
        /** Lateral force compliance → steer (N per rad); higher = stiffer. */
        float latComplianceFront = 9.0e4f;
        float latComplianceRear = 1.2e5f;
        /** Aligning moment compliance (N·m per rad). */
        float alignComplianceFront = 4.0e3f;
        float alignComplianceRear = 5.0e3f;
        float frontWingDeg = 10.f;
        float rearWingDeg = 12.f;
        float diffPreloadNm = 30.f;
        float fuelL = 50.f;
        float ballastKg = 0.f;
        int tcLevel = 0;
        int absLevel = 0;
    };
    void applySetup(const SetupParams& p);
    const SetupParams& setupParams() const { return m_setup; }
    float brakeBias() const { return m_setup.brakeBias; }

    /** Milliken LLTD: front fraction of elastic lateral load transfer (0–1). */
    float lltdFrontFraction() const { return m_lltdFront; }
    float lastAx() const { return m_lastAx; }
    float lastAy() const { return m_lastAy; }
    /** Wheel spin (rad/s) and slip ratio κ. Index: FL=0, FR=1, RL=2, RR=3. */
    float wheelOmega(int i) const {
        return (i >= 0 && i < 4) ? m_wheelOmega[static_cast<size_t>(i)] : 0.f;
    }
    float slipRatio(int i) const {
        return (i >= 0 && i < 4) ? m_slipRatio[static_cast<size_t>(i)] : 0.f;
    }
    /** Milliken/SAE handling metrics (updated each integrate). */
    float sideslipBeta() const { return m_beta; }
    float understeerGradientDegG() const { return m_ugDegG; }
    float staticMargin() const { return m_staticMargin; }
    /** Per-wheel Fx/Fy/Fz (N) and friction-circle usage |F|/(μ Fz). Index FL..RR. */
    float tireFx(int i) const { return (i>=0&&i<4) ? m_tireFx[static_cast<size_t>(i)] : 0.f; }
    float tireFy(int i) const { return (i>=0&&i<4) ? m_tireFy[static_cast<size_t>(i)] : 0.f; }
    float tireFz(int i) const { return (i>=0&&i<4) ? m_tireFz[static_cast<size_t>(i)] : 0.f; }
    float frictionCircleUsage(int i) const {
        return (i>=0&&i<4) ? m_fcUsage[static_cast<size_t>(i)] : 0.f;
    }
    /** Yaw inertia Iz (kg·m²) and relaxation length σ (m) used for transient. */
    float yawInertia() const { return m_yawInertia; }
    float relaxationLength() const { return m_relaxLength; }
    void setYawInertia(float iz) { m_yawInertia = (iz > 50.f) ? iz : 50.f; }
    void setRelaxationLength(float s) { m_relaxLength = (s < 0.15f) ? 0.15f : ((s > 1.5f) ? 1.5f : s); }
    float yawAccel() const { return m_yawAccel; }
    float rollAngle() const { return m_rollAngle; }
    float aquaplaneFactor() const { return m_aquaplane; }
    float effectiveMu() const { return m_effectiveMu; }
    /** Front fraction of aero downforce (0.3–0.7 from wing setup). */
    float aeroBalanceFront() const { return m_aeroBalFront; }
    float rideHeightFrontM() const { return m_rhFront; }
    float rideHeightRearM() const { return m_rhRear; }
    float fltFront() const { return m_fltFront; }
    float fltRear() const { return m_fltRear; }
    DifferentialModel& differential() { return m_diff; }
    const DifferentialModel& differential() const { return m_diff; }
    BrakeThermalModel& brakes() { return m_brakes; }
    const BrakeThermalModel& brakes() const { return m_brakes; }

    void loadVehicleParams(const std::string& carPath);
    void loadEngineFromIni(const std::string& path);
    void loadTyresFromIni(const std::string& path);
    void loadDrivetrainFromIni(const std::string& path);
    void loadAeroFromIni(const std::string& path);
    void loadSuspensionFromIni(const std::string& path);

    int currentGear() const { return m_currentGear; }
    double rpm() const { return m_rpm; }
    const std::vector<double>& gearRatios() const { return m_gearRatios; }
    float finalDrive() const { return static_cast<float>(m_finalDrive); }
    float wheelRadius() const { return static_cast<float>(m_wheelRadius); }

    KsTireModel& tires() { return m_tires; }
    AeroModel& aero() { return m_aero; }

    DamageSystem& damage() { return m_damage; }
    const DamageSystem& damage() const { return m_damage; }
    Rf2DamageParams& damageParams() { return m_rf2Dmg; }
    const Rf2DamageParams& damageParams() const { return m_rf2Dmg; }
    void setDamageEnabled(bool e) {
        auto c = m_damage.config();
        c.enabled = e;
        m_damage.setConfig(c);
    }
    void applyCollisionImpulse(float impulse, const PhysVec3& contactPoint,
                               const PhysVec3& contactNormal) {
        applyRf2Impulse(m_damage, m_rf2Dmg, impulse, contactPoint, contactNormal);
    }
    void applyImpactFromSpeed(float relativeSpeedMs, const PhysVec3& contactPoint,
                              const PhysVec3& contactNormal) {
        const float J = impulseFromImpact(relativeSpeedMs, static_cast<float>(m_mass));
        applyCollisionImpulse(J, contactPoint, contactNormal);
    }

private:
    static std::map<std::string, std::string> parseIni(const std::string& path);
    static float getf(const std::map<std::string, std::string>& m, const std::string& k, float def);
    void integrate(double dt);
    void shiftGears();

    SimulationState m_state;
    VehicleFFBSample m_ffb;
    bool m_running = false;
    bool m_frozen = false;

    double m_throttle = 0, m_brake = 0, m_steering = 0;
    double m_mass = 1200, m_enginePowerKw = 260, m_maxRpm = 8500;
    double m_cd = 0.35, m_frontalArea = 2.2, m_wheelBase = 2.6, m_trackWidth = 1.6;
    double m_wheelRadius = 0.33, m_finalDrive = 3.9;
    std::vector<double> m_gearRatios = {3.5, 2.5, 1.8, 1.4, 1.1, 0.9};
    int m_currentGear = 1;
    double m_rpm = 1000;
    double m_yawRate = 0;
    SetupParams m_setup{};
    double m_baseMass = 1200;
    /** Previous-step body accelerations (m/s²) for load transfer (avoids algebraic loop). */
    float m_lastAx = 0.f;
    float m_lastAy = 0.f;
    float m_lltdFront = 0.55f;
    float m_wheelOmega[4] = {0.f, 0.f, 0.f, 0.f}; // rad/s FL FR RL RR
    float m_slipRatio[4] = {0.f, 0.f, 0.f, 0.f};  // κ last step
    float m_wheelInertia = 1.2f; // kg·m² per wheel (approx race)
    float m_beta = 0.f;           // vehicle sideslip (rad)
    float m_ugDegG = 0.f;         // understeer gradient deg/g
    float m_staticMargin = 0.f;   // static margin (fraction of wheelbase)
    float m_tireFx[4] = {};
    float m_tireFy[4] = {};
    float m_tireFz[4] = {};
    float m_fcUsage[4] = {};
    float m_fyLag[4] = {};          // relaxation-length lagged lateral force
    float m_yawInertia = 0.f;       // kg·m² (0 = auto from mass/wb)
    float m_relaxLength = 0.45f;    // tire relaxation length m
    float m_yawAccel = 0.f;         // rad/s²
    float m_yawDampingNr = 0.f;     // N·m / (rad/s)
    float m_rollAngle = 0.f;        // body roll rad (quasi-static)
    float m_aquaplane = 0.f;        // 0..1 hydroplaning
    float m_effectiveMu = 1.f;
    float m_waterDepthMm = 0.f;
    float m_aeroBalFront = 0.42f;
    float m_rhFront = 0.05f;
    float m_rhRear = 0.07f;
    float m_fltFront = 0.f;
    float m_fltRear = 0.f;

    EngineModel m_engine;
    KsTireModel m_tires;
    AeroModel m_aero;
    DifferentialModel m_diff;
    BrakeThermalModel m_brakes;
    SuspensionModel m_suspension;
    DamageSystem m_damage;
    Rf2DamageParams m_rf2Dmg;
};

} // namespace physics
} // namespace ks
