#pragma once

#include "PhysicsCoreTypes.h"
#include "EngineModel.h"
#include "PacejkaTireModel.h"
#include "AeroModel.h"
#include "DifferentialModel.h"
#include "SuspensionModel.h"
#include "DamageSystem.h"
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

    void loadVehicleParams(const std::string& carPath);
    void loadEngineFromIni(const std::string& path);
    void loadTyresFromIni(const std::string& path);
    void loadDrivetrainFromIni(const std::string& path);
    void loadAeroFromIni(const std::string& path);
    void loadSuspensionFromIni(const std::string& path);

    int currentGear() const { return m_currentGear; }
    const std::vector<double>& gearRatios() const { return m_gearRatios; }
    float finalDrive() const { return static_cast<float>(m_finalDrive); }
    float wheelRadius() const { return static_cast<float>(m_wheelRadius); }
    double rpm() const { return m_rpm; }

    PacejkaTireModel& tires() { return m_tires; }
    AeroModel& aero() { return m_aero; }

    // --- rF2-style damage ---
    DamageSystem& damage() { return m_damage; }
    const DamageSystem& damage() const { return m_damage; }
    Rf2DamageParams& damageParams() { return m_rf2Dmg; }
    const Rf2DamageParams& damageParams() const { return m_rf2Dmg; }
    void setDamageEnabled(bool e) {
        auto c = m_damage.config();
        c.enabled = e;
        m_damage.setConfig(c);
    }
    /** Apply collision impulse (rF2-like thresholds → mechanical systems). */
    void applyCollisionImpulse(float impulse, const PhysVec3& contactPoint,
                               const PhysVec3& contactNormal) {
        applyRf2Impulse(m_damage, m_rf2Dmg, impulse, contactPoint, contactNormal);
    }
    /** Wall/car impact helper from relative speed (m/s). */
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

    double m_throttle = 0, m_brake = 0, m_steering = 0;
    double m_mass = 1200, m_enginePowerKw = 260, m_maxRpm = 8500;
    double m_cd = 0.35, m_frontalArea = 2.2, m_wheelBase = 2.6, m_trackWidth = 1.6;
    double m_wheelRadius = 0.33, m_finalDrive = 3.9;
    std::vector<double> m_gearRatios = {3.5, 2.5, 1.8, 1.4, 1.1, 0.9};
    int m_currentGear = 1;
    double m_rpm = 1000;
    double m_yawRate = 0;

    EngineModel m_engine;
    PacejkaTireModel m_tires;
    AeroModel m_aero;
    DifferentialModel m_diff;
    SuspensionModel m_suspension;
    DamageSystem m_damage;
    Rf2DamageParams m_rf2Dmg;
};

} // namespace physics
} // namespace ks
