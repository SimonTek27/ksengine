#pragma once
#include "KsExport.h"

#include "PhysicsCoreTypes.h"
#include "EngineModel.h"
#include "KsTireModel.h"
#include "AeroModel.h"
#include "DifferentialModel.h"
#include "SuspensionModel.h"
#include "DamageSystem.h"
#include "BrakeThermalModel.h"
#include "Rf2DamageModel.h"
#include "CarPhysicsJson.h"

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

class KSENGINE_API VehicleSimulator {
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
    /** Rear-axle handbrake demand, 0..1: locks the back wheels on its own, without
     *  the foot brake and outside the bias. */
    void setHandbrake(double v);

    /** Brake fade follows the car's own data/brake_temp_front.lut and
     *  data/brake_temp_rear.lut (temperature -> torque multiplier). Empty curves
     *  leave the thermal model on its config thresholds. */
    /** Thermal config for one axle: 0 = front (wheels 0/1), 1 = rear (2/3). */
    void setBrakeThermalConfig(int axle, const BrakeThermalConfig& c);
    /** brakes.ini [DATA] as the car declared it; 0 = the car said nothing. */
    float brakeMaxTorque() const { return m_brakeMaxTorque; }
    float brakeFrontShare() const { return m_brakeFrontShare; }
    float handbrakeTorque() const { return m_handbrakeTorque; }
    bool brakeBiasAdjustable() const { return m_brakeBiasAdjustable; }
    /** The car's own electronics slip limits (data/electronics.ini through
     *  data.json): where traction control and ABS start cutting. Defaults are the
     *  values this model used before the file was read. */
    void setElectronicsSlipLimits(float tcLimit, float absLimit);
    /** The car's [EDL] block: the brake-based traction aid and the spin it
     *  allows on power and off it. Floats rather than a struct so this header
     *  keeps its own includes. */
    void setEdlSettings(bool declared, float maxSpinPower, float maxSpinCoast,
                        float brakeTorquePower, float brakeTorqueCoast,
                        float deadZonePower, float deadZoneCoast);
    /** [EDL] PRESENT: whether the car has the brake-based aid at all. 0 means the
     *  document's numbers are not a system the car was built with. */
    void setEdlPresence(int present) {
        m_edlPresent = (present == 0 || present == 1) ? present : -1;
    }
    int edlPresent() const { return m_edlPresent; }
    /** Declared numbers and declared hardware both: what a call site needs to
     *  apply the aid. Unknown presence keeps the previous behaviour. */
    bool edlFitted() const { return m_edlDeclared && m_edlPresent != 0; }
    float tractionControlSlipLimit() const { return m_tcSlipLimit; }
    float absSlipLimit() const { return m_absSlipLimit; }
    /** The car's [TRACTION_CONTROL]/[ABS] PRESENT: 1 has it, 0 does not, -1 the
     *  car said nothing. A car that says it has no such hardware gets neither aid
     *  whatever level the setup asks for, which is what AC does with PRESENT = 0. */
    void setElectronicsPresence(int tcPresent, int absPresent);
    /** Reads the car's electronics (data.json: aids, slip limits, EDL) and applies
     *  them. Called by both physics loaders and by the session, which loads the
     *  .ini set file by file and would otherwise never see the manifest. */
    void loadElectronicsForCar(const std::string& carPath);
    /** The car's own answer about its hardware: 1 has it, 0 does not, -1 said
     *  nothing. The setup screen uses this to stop offering a level for a system
     *  the car does not have. */
    int tractionControlPresent() const { return m_tcPresent; }
    int absPresent() const { return m_absPresent; }
    /** A selected level with the car's own hardware check applied. */
    bool tcEnabled() const { return m_setup.tcLevel > 0 && m_tcPresent != 0; }
    bool absEnabled() const { return m_setup.absLevel > 0 && m_absPresent != 0; }

    void setBrakeTempCurves(const std::vector<std::pair<float, float>>& front,
                            const std::vector<std::pair<float, float>>& rear);

    SimulationState getState() const { return m_state; }
    SimulationState& state() { return m_state; }

    /**
     * Tank level (L), clamped to the tank capacity (setFuelTankCapacity).
     * Refuelling in the box needs this: applySetup() would reset the whole
     * setup, and the pit service (SimulationLoop_Pit.cpp) only moves fuel.
     * Mass is recomputed the same way applySetup does, so a full tank still
     * weighs more than an empty one.
     */
    void setFuelL(float liters);

    /** Roadmap 2.7 follow-up — real tank size: the pit refuel stops at this
     *  value instead of a hard-coded one. Clamped to [5, 300] L; shrinking it
     *  below the current level trims the fuel (and the mass) right away.
     *  Default 150 L — the clamp applySetup()/setFuelL() always used. */
    void setFuelTankCapacity(float liters);
    float fuelTankCapacityL() const { return m_fuelTankL; }

    /** Roadmap 2.7 follow-up — fitted tyres (PitLaneRepair job "TYRES"):
     *  wear back to zero, which is all the tyre model needs to give full
     *  forces again. Temperatures/pressures are left alone: like on a real
     *  out-lap they come back up within a stint. */
    void resetTyreWear();
    const VehicleFFBSample& ffbSample() const { return m_ffb; }

    void setMass(double kg);
    void setEnginePower(double kw);
    void setMaxRpm(double rpm);
    void setDragCoeff(double cd);
    void setFrontalArea(double m2);
    void setWheelBase(double m);
    void setTrackWidth(double m);
    /** Roadmap "physics.json": the unified file carries the tyre radius, the
     *  final drive and the gear set directly, without going through an .ini. */
    void setWheelRadius(double m);
    void setFinalDrive(double ratio);
    void setGearRatios(const std::vector<double>& ratios);
    double wheelBase() const { return m_wheelBase; }
    double trackWidth() const { return m_trackWidth; }

    // Roadmap 2.9: read back the tuned values so upgrade application can
    // capture the car's baseline AFTER its own data inis loaded and recompute
    // from absolutes on every re-selection (never stacking on modified state).
    double mass() const { return m_mass; }
    /** Dry mass (no ballast, no fuel) — what data/physics.json stores as
     *  massKg; mass() is this plus whatever is on board right now. */
    double baseMass() const { return m_baseMass; }
    double enginePowerKw() const { return m_enginePowerKw; }
    double maxRpm() const { return m_maxRpm; }
    double dragCoeff() const { return m_cd; }
    double frontalArea() const { return m_frontalArea; }

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
    /** Legacy per-file .ini set (engine/tyres/drivetrain/aero/suspension).
     *  loadVehicleParams() prefers the unified data/physics.json and only
     *  calls this when there is none; tools that migrate a folder call it
     *  directly to read the old format. */
    void loadVehicleParamsFromIni(const std::string& carPath);
    void loadEngineFromIni(const std::string& path);
    void loadTyresFromIni(const std::string& path);
    void loadDrivetrainFromIni(const std::string& path);
    void loadAeroFromIni(const std::string& path);
    void loadSuspensionFromIni(const std::string& path);
    /** models brakes.ini [DATA]: MAX_TORQUE, FRONT_SHARE, HANDBRAKE_TORQUE and
     *  COCKPIT_ADJUSTABLE. A car that omits one leaves the engine's estimate. */
    void loadBrakesFromIni(const std::string& path);
    /** AC has the real tank in [FUEL] MAX_FUEL (litres): data/data.json in
     *  converted content (read by CarPhysicsJson::loadAcFuelCapacity before
     *  the .ini set), data/car.ini in stock folders, and data/powertrain.ini
     *  in the mods that keep it there. Always before engine.ini, so the
     *  ksengine-side FUEL_CAPACITY override still wins. */
    void loadFuelCapacityFromIni(const std::string& path);

    /** What the engine is doing: the state a start sequence goes through. */
    enum class EngineRunState { Off, Cranking, Running, Stalled };

    /**
     * Apply the car's declared start sequence (data.json "start"). A car that
     * declares none keeps the old behaviour: ignition on, engine running. A car
     * that declares one spawns with the ignition off and is started on request.
     */
    void setEngineStartConfig(const CarStartConfig& c);
    const CarStartConfig& engineStartConfig() const { return m_start; }

    EngineRunState engineRunState() const { return m_runState; }
    bool isEngineRunning() const { return m_runState == EngineRunState::Running; }
    bool ignitionOn() const { return m_ignitionOn; }
    void setIgnition(bool on);
    /** Starter button: cranks when it can, asks a running engine to stop. */
    bool pressStarter(double speedMs);

    /** The car's declared clutch (drivetrain.ini [CLUTCH]/[AUTOCLUTCH]). */
    void setClutchConfig(const CarClutchConfig& c) { m_clutch = c; }
    const CarClutchConfig& clutchConfig() const { return m_clutch; }
    /** Pedal 0 = released (clutch closed), 1 = pressed (clutch open). */
    void setClutchPedal(double v) { m_clutchPedal = std::clamp(v, 0.0, 1.0); }
    double clutchPedal() const { return m_clutchPedal; }
    /** How much torque the clutch can pass, 0..1: the load on the engine. */
    double clutchEngagement(double speedMs) const;
    bool antistallActive() const { return m_antistallActive; }

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

    // Gate 2.12 - the NaN recovery path in updatePhysics() used to reset the
    // state silently; a1 h stability run has to be able to prove "zero
    // recoveries". The counter is logged ("[nan] ...") and readable here.
    unsigned long long nanRecoveries() const { return m_nanRecoveries; }

private:
    static std::map<std::string, std::string> parseIni(const std::string& path);
    static float getf(const std::map<std::string, std::string>& m, const std::string& k, float def);
    void integrate(double dt);
    void shiftGears();
    void stepEngineStart(double speedMs, double dt);
    double engineIdleFloor() const;
    bool engineDrivesWheels() const;
    /** Where the running engine settles: idle when the clutch is open, the
     *  drivetrain when it is closed, the antistall's hold when that is on. */
    double engineRpmTarget(double wheelRpm, double speedMs) const;
    void approachEngineRpm(double target, double dt);

    SimulationState m_state;
    unsigned long long m_nanRecoveries = 0;
    VehicleFFBSample m_ffb;
    bool m_running = false;
    bool m_frozen = false;

    double m_throttle = 0, m_brake = 0, m_steering = 0;
    double m_handbrake = 0.0; // rear-axle handbrake demand, 0..1
    /** Damage cg shift already folded into m_state.position. Only the change
     *  moves the model: applying the whole shift every step turned it into a
     *  constant drift (0.01 * cg per step). */
    PhysVec3 m_appliedCgShift{};
    double m_mass = 1200, m_enginePowerKw = 260, m_maxRpm = 8500;
    double m_cd = 0.35, m_frontalArea = 2.2, m_wheelBase = 2.6, m_trackWidth = 1.6;
    double m_wheelRadius = 0.33, m_finalDrive = 3.9;
    std::vector<double> m_gearRatios = {3.5, 2.5, 1.8, 1.4, 1.1, 0.9};
    int m_currentGear = 1;
    double m_rpm = 1000;
    CarStartConfig m_start;
    EngineRunState m_runState = EngineRunState::Running;
    bool m_ignitionOn = true;
    /** False while cranking or stopped: the wheels do not set the revs then. */
    bool m_rpmFromWheels = true;
    CarClutchConfig m_clutch;
    double m_clutchPedal = 0.0;
    bool m_antistallActive = false;
    double m_crankTimer = 0.0;
    double m_antistallTimer = 0.0;
    double m_yawRate = 0;
    SetupParams m_setup{};
    double m_baseMass = 1200;
    /** Tank capacity in litres — pit refuel target (roadmap 2.7 follow-up). */
    float m_fuelTankL = 150.f;
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
    // brakes.ini [DATA]: 0 means "the car did not say", so the mass-based
    // estimate stays in charge instead of a zero the setup would inherit.
    float m_brakeMaxTorque = 0.f;
    float m_brakeFrontShare = 0.f;
    float m_handbrakeTorque = 0.f;
    // |slip ratio| at which the aids may cut, as the car declares it.
    float m_tcSlipLimit = 0.10f;
    float m_absSlipLimit = 0.12f;
    // PRESENT: 1 = the car has the system, 0 = it does not, -1 = it said nothing.
    int m_tcPresent = -1;
    int m_absPresent = -1;
    // [EDL] as the car's document declares it (the conversion always writes it).
    bool m_edlDeclared = false;
    int m_edlPresent = -1;
    float m_edlMaxSpinPower = 0.55f;
    float m_edlMaxSpinCoast = 0.40f;
    float m_edlBrakeTorquePower = 140.f;
    float m_edlBrakeTorqueCoast = 420.f;
    float m_edlDeadZonePower = 0.12f;
    float m_edlDeadZoneCoast = 0.12f;
    bool m_brakeBiasAdjustable = false;
    SuspensionModel m_suspension;
    DamageSystem m_damage;
    Rf2DamageParams m_rf2Dmg;
};

} // namespace physics
} // namespace ks
