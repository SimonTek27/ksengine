#include "phys_Simulator.h"
#include "PhysicsLogger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace ks {

phys_Simulator* phys_Simulator::s_instance = nullptr;

phys_Simulator* phys_Simulator::instance() {
    if (!s_instance) s_instance = new phys_Simulator();
    return s_instance;
}

phys_Simulator::phys_Simulator() {
    m_aero.setConfigPreset("gt3");
    m_engine.setConfig(physics::EngineModel::getV8_4000());
    {
        std::vector<float> ratios;
        for (const auto& g : m_engine.getConfig().gearRatios) {
            if (g.gear > 0) ratios.push_back(g.ratio);
        }
        if (ratios.empty()) ratios = {3.5f, 2.2f, 1.6f, 1.25f, 1.0f, 0.85f};
        m_gearbox.setRatios(ratios, m_engine.getConfig().finalDrive);
        m_gearbox.setGear(1);
        m_diff.setConfig(physics::DifferentialModel::getLSDClutch());
    }
    {
        auto g = m_suspension.geometryConfig();
        g.wheelBase = static_cast<float>(m_wheelbase);
        m_suspension.setGeometryConfig(g);
    }
    m_weatherSim.setState(m_weather);
    m_weatherSim.start();
    m_state = physics::SimulationState{};
    m_tireCurve.name = "default";
    m_tireCurve.peakSlipAngle = 8.0;
    m_tireCurve.peakSlipRatio = 0.12;
}

void phys_Simulator::startSimulation() {
    if (m_running) return;
    m_running = true;
    PHYSICS_INFO(::ks::physics::LogCategory::GENERAL, "Simulation started");
    if (onSimulationStarted) onSimulationStarted();
}

void phys_Simulator::stopSimulation() {
    if (!m_running) return;
    m_running = false;
    if (onSimulationStopped) onSimulationStopped();
}

void phys_Simulator::reset() {
    m_state = physics::SimulationState{};
    m_throttle = m_brake = m_steering = 0.0;
    m_drsActive = false;
    m_damage.reset();
    m_tires.reset();
    m_engine.reset();
    m_suspension.reset();
    m_diff.reset();
    m_brakeThermal.reset();
    m_hybrid.reset();
    m_chassis.reset(); m_hybrid.setEnabled(m_ersEnabled);
    m_gearbox.setGear(1);
    m_weatherSim.reset(); m_weatherSim.start();
    m_fuelKg = std::min(m_fuelKg, m_fuelCapacityL * 0.75);
    for (int i = 0; i < 4; ++i) {
        m_brakeDiscTemp[i] = 80.0f;
        m_brakePadTemp[i] = 60.0f;
        m_brakeFade[i] = 0.0f;
    }
    if (onSimulationReset) onSimulationReset();
}

void phys_Simulator::update(double dt) {
    if (!m_running || dt <= 0.0) return;
    PROFILE_FRAME();
    dt = std::clamp(dt, 1e-4, 0.05);

    // DRS auto
    if (m_drsEnabled && m_drsAuto) {
        const double speedKmh = m_state.speed * 3.6;
        const bool inZone = (m_state.currentLapDistance >= m_drsZoneStart &&
                             m_state.currentLapDistance <= m_drsZoneEnd);
        m_drsActive = inZone && speedKmh >= m_drsSpeedKmh && m_throttle > 0.9;
    }

    // Weather
    m_weatherSim.update(static_cast<float>(dt));
    m_weather = m_weatherSim.state();
    physics::TrackSurface::instance().syncFromWeather(
        m_weather.trackWetness, m_weather.trackTemp);

    updateAero(dt);

    // Engine + gearbox + differential
    {
        float ratio = m_gearbox.currentRatio();
        float loadTq = static_cast<float>(m_state.speed * 15.0);
        if (ratio > 0.1f) loadTq = loadTq / ratio; // reflected load
        m_engine.update(static_cast<float>(dt), static_cast<float>(m_throttle), loadTq);
        m_state.rpm = m_engine.getState().rpm;
        m_state.gear = m_gearbox.gear();
        if (m_fuelEnabled) {
            m_fuelKg = std::max(0.0, m_fuelKg - m_engine.getState().fuelFlow * dt * 0.75);
        }
        if (m_autoShift) {
            m_gearbox.autoShift(m_engine.getState().rpm,
                                m_engine.getConfig().maxRPM,
                                m_engine.getConfig().idleRPM);
            m_state.gear = m_gearbox.gear();
        }
    }

    // Aero re-query for downforce split (also feeds suspension)
    float aeroDfFront = 0.0f, aeroDfRear = 0.0f;
    {
        physics::AeroSimulator::Input in;
        in.speed = m_state.speed;
        in.rideHeightFront = m_suspension.rideHeightFront();
        in.rideHeightRear = m_suspension.rideHeightRear();
        in.airDensity = m_weather.airDensity;
        in.position = m_state.position;
        in.forward = physics::PhysVec3{std::sin(m_state.heading), 0.0f, std::cos(m_state.heading)};
        in.leaderPosition = m_draftLeader;
        auto aout = m_aero.step(in);
        aeroDfFront = aout.forces.frontDownforce;
        aeroDfRear = aout.forces.rearDownforce;
        if (m_damageEnabled) {
            aeroDfFront *= m_damage.downforceMultiplier;
            aeroDfRear *= m_damage.downforceMultiplier;
        }
    }

    // Suspension: weight transfer + spring/damper -> tire normal loads
    m_suspension.update(
        static_cast<float>(dt),
        m_state.acceleration.y,
        m_state.acceleration.z,  // lateral approx
        m_state.acceleration.x,  // long
        static_cast<float>(getEffectiveMass()),
        aeroDfFront, aeroDfRear);
    m_rideHeightFront = m_suspension.rideHeightFront();
    m_rideHeightRear = m_suspension.rideHeightRear();
    std::array<float, 4> loads = m_suspension.normalLoads();

    float engineTq = m_engine.getState().torque;
    if (m_damageEnabled) engineTq *= m_damage.powerMultiplier;
    float axleTorque = m_gearbox.wheelTorqueFromEngine(engineTq);

    // Diff: split axle torque using rear wheel speeds (from tire omega * r)
    float rWheel = 0.33f;
    float wL = static_cast<float>(m_tires.wheelState(2).angularVelocity);
    float wR = static_cast<float>(m_tires.wheelState(3).angularVelocity);
    m_diff.update(static_cast<float>(dt), axleTorque, wL * rWheel, wR * rWheel);
    float hybridTq = m_hybrid.update(static_cast<float>(dt),
                                        static_cast<float>(m_throttle),
                                        static_cast<float>(m_brake),
                                        m_state.speed);
    float driveTorque = m_diff.getLeftTorque() + m_diff.getRightTorque() + hybridTq;
    float brakeTorqueCmd = static_cast<float>(m_brake * getEffectiveMass() * physics::Constants::GRAVITY * 1.2 * 0.33);
    // Per-wheel thermal update + fade
    float brakeTorqueEff = 0.0f;
    for (int i = 0; i < 4; ++i) {
        float tq = brakeTorqueCmd * 0.25f;
        float omega = static_cast<float>(m_tires.wheelState(i).angularVelocity);
        m_brakeThermal.update(static_cast<float>(dt), i, tq, omega, m_state.speed);
        float eff = m_brakeThermal.effectiveTorque(i, tq);
        brakeTorqueEff += eff;
        m_brakeDiscTemp[i] = m_brakeThermal.discTemp(i);
        m_brakeFade[i] = m_brakeThermal.fade(i);
        m_brakePadTemp[i] = m_brakeThermal.state(i).padTemp;
    }

    m_tires.update(static_cast<float>(dt), m_state.speed, 0.0f,
                   static_cast<float>(m_steering) * 0.5f,
                   static_cast<float>(m_throttle), static_cast<float>(m_brake),
                   loads, physics::TrackSurface::instance().getGrip(m_state.position) *
                       static_cast<float>(1.0 - getTrackGripReduction()),
                   driveTorque, brakeTorqueEff);

    // Override longitudinal with tire forces + aero drag
    stepLongitudinal(dt);

    // Tire telemetry
    auto fl = m_tires.wheelState(0);
    if (onTireDataUpdated) {
        onTireDataUpdated(fl.slipAngle, fl.lateralForce, fl.slipRatio, fl.longitudinalForce);
    }
    for (int i = 0; i < 4; ++i) {
        auto w = m_tires.wheelState(i);
        m_state.tyreTemp[i] = w.temperature;
        m_state.tyreWear[i] = w.wear;
        m_state.tyrePressure[i] = w.pressure;
    }

    // Chassis planar
    {
        float fx = m_tires.totalLongitudinalForce();
        float fy = m_tires.totalLateralForce();
        m_chassis.update(static_cast<float>(dt), m_state.speed,
                         static_cast<float>(m_steering) * 0.5f, fx, fy);
        m_state.heading += m_chassis.state().yawRate * static_cast<float>(dt);
        m_state.angularVelocity.y = m_chassis.state().yawRate;
        m_state.acceleration.z = m_chassis.state().lateralAccel;
        m_state.acceleration.x = m_chassis.state().longitudinalAccel;
    }
    {
        auto fl = m_tires.wheelState(0);
        if (std::abs(fl.slipAngle) > 4.0 || std::abs(fl.slipRatio) > 0.08)
            physics::TrackSurface::instance().depositRubber(m_state.position, 0.002f * static_cast<float>(dt));
    }

    syncStateFromInternal();
    if (onStateUpdated) onStateUpdated(m_state);
    PROFILE_END_FRAME();
}

void phys_Simulator::updateAero(double /*dt*/) {
    physics::AeroSimulator::Input in;
    in.speed = m_state.speed;
    in.rideHeightFront = static_cast<float>(m_rideHeightFront);
    in.rideHeightRear = static_cast<float>(m_rideHeightRear);
    in.airDensity = m_weather.airDensity;
    in.position = m_state.position;
    // Forward from heading (xz plane)
    in.forward = physics::PhysVec3{
        std::sin(m_state.heading),
        0.0f,
        std::cos(m_state.heading)
    };
    in.leaderPosition = m_draftLeader;

    auto out = m_aero.step(in);

    // Apply DRS drag reduction on top of model drag
    float drag = out.forces.drag;
    if (m_drsActive) {
        drag *= static_cast<float>(1.0 - m_drsDragReduction);
    }
    // Damage aero penalty
    if (m_damageEnabled) {
        drag *= m_damage.dragMultiplier;
        out.forces.downforce *= m_damage.downforceMultiplier;
        out.frontLoadN *= m_damage.downforceMultiplier;
        out.rearLoadN *= m_damage.downforceMultiplier;
    }

    // Store last aero loads in unused-ish fields via speed energy path later
    // Longitudinal resistance from aero drag (N) -> acceleration in stepLongitudinal via state
    m_state.acceleration.x = -drag; // stash drag force in accel.x temporarily for step
    // Front/rear vertical load contribution (for tires later)
    (void)out;
}

void phys_Simulator::stepLongitudinal(double dt) {
    const double mass = getEffectiveMass();
    if (mass < 1.0) return;

    // Engine force (very simplified)
    double throttle = std::clamp(m_throttle, 0.0, 1.0);
    double brake = std::clamp(m_brake, 0.0, 1.0);

    if (m_tcEnabled && throttle > 0.5) {
        // soft TC: limit throttle growth (placeholder)
        throttle = std::min(throttle, 0.95);
    }
    if (m_absEnabled && brake > 0.5) {
        brake = std::min(brake, 0.92);
    }

    const double powerW = m_enginePowerKw * 1000.0;
    const double v = std::max(static_cast<double>(m_state.speed), 1.0);
    double driveForce = (powerW * throttle) / v;
    // Cap by friction budget (mu * mass * g * grip)
    const double grip = 1.0 - getTrackGripReduction();
    const double maxFx = 1.5 * mass * physics::Constants::GRAVITY * grip;
    driveForce = std::clamp(driveForce, 0.0, maxFx);

    double brakeForce = brake * maxFx * (m_damageEnabled ? m_damage.brakingMultiplier : 1.0);
    if (m_damageEnabled) {
        driveForce *= m_damage.powerMultiplier;
    }

    // Aero drag force was stored in acceleration.x as negative Newtons
    double aeroDrag = -static_cast<double>(m_state.acceleration.x);
    if (aeroDrag < 0.0) aeroDrag = 0.0;
    // Fallback body Cd if aero returned 0
    if (aeroDrag < 1.0 && v > 1.0) {
        const double q = 0.5 * m_weather.airDensity * v * v;
        aeroDrag = q * m_frontalArea * m_dragCd;
        if (m_drsActive) aeroDrag *= (1.0 - m_drsDragReduction);
    }

    const double rolling = 0.015 * mass * physics::Constants::GRAVITY;
    const double net = driveForce - brakeForce - aeroDrag - rolling;
    const double ax = net / mass;

    m_state.velocity.x += static_cast<float>(ax * dt); // use x as longitudinal in body frame simplified
    // Keep speed scalar aligned
    float spd = std::abs(m_state.velocity.x);
    // integrate along heading
    m_state.position.x += std::sin(m_state.heading) * spd * static_cast<float>(dt);
    m_state.position.z += std::cos(m_state.heading) * spd * static_cast<float>(dt);
    m_state.speed = spd;
    m_state.acceleration = physics::PhysVec3{static_cast<float>(ax), 0.0f, 0.0f};

    m_state.throttle = static_cast<float>(throttle);
    m_state.brake = static_cast<float>(brake);
    m_state.steering = static_cast<float>(m_steering);
    m_state.rpm = static_cast<float>(std::clamp(m_maxRpm * throttle * 0.7 + 800.0, 800.0, m_maxRpm));
    m_state.currentLapDistance += spd * static_cast<float>(dt);
    m_state.lapTime += static_cast<float>(dt);

    if (m_fuelEnabled && throttle > 0.05) {
        // ~0.15 kg/s at full throttle rough
        m_fuelKg = std::max(0.0, m_fuelKg - 0.15 * throttle * dt);
    }
    m_state.fuel = static_cast<float>(m_fuelKg);

    // Brake temps (crude)
    for (int i = 0; i < 4; ++i) {
        m_brakeDiscTemp[i] += static_cast<float>(brake * 40.0 * dt);
        m_brakeDiscTemp[i] -= static_cast<float>(0.5 * dt * (m_brakeDiscTemp[i] - 40.0));
        m_brakeFade[i] = std::clamp((m_brakeDiscTemp[i] - 400.0f) / 400.0f, 0.0f, 1.0f);
    }

    if (onTireDataUpdated) {
        onTireDataUpdated(0.0, 0.0, 0.0, static_cast<float>(driveForce));
    }
}

void phys_Simulator::syncStateFromInternal() {
    m_state.worldPosition = m_state.position;
    m_state.maxSpeed = std::max(m_state.maxSpeed, static_cast<double>(m_state.speed));
}

void phys_Simulator::setGear(int gear) {
    m_gearbox.setGear(gear);
    m_state.gear = m_gearbox.gear();
}

void phys_Simulator::setThrottle(double value) { m_throttle = std::clamp(value, 0.0, 1.0); }
void phys_Simulator::setBrake(double value) { m_brake = std::clamp(value, 0.0, 1.0); }
void phys_Simulator::setSteering(double value) {
    m_steering = std::clamp(value, -1.0, 1.0);
    // yaw rate simplified
    m_state.heading += static_cast<float>(m_steering * m_state.speed * 0.02f);
}

physics::SimulationState phys_Simulator::getState() const { return m_state; }

void phys_Simulator::setTireModel(const physics::TireSlipCurve& curve) { m_tireCurve = curve; }

physics::LapTimeEstimate phys_Simulator::estimateLapTime() const {
    physics::LapTimeEstimate e;
    e.avgSpeed = m_state.speed;
    e.topSpeed = static_cast<float>(m_state.maxSpeed);
    e.confidenceLevel = 0.2f;
    return e;
}

void phys_Simulator::setMass(double kg) {
    m_mass = std::max(200.0, kg);
    auto cfg = m_chassis.config();
    cfg.mass = static_cast<float>(m_mass);
    m_chassis.setConfig(cfg);
}
void phys_Simulator::setEnginePower(double kw) { m_enginePowerKw = std::max(10.0, kw); }
void phys_Simulator::setMaxRpm(double rpm) { m_maxRpm = std::max(1000.0, rpm); }
void phys_Simulator::setDragCoeff(double cd) {
    m_dragCd = cd;
    auto cfg = m_aero.manager().model().getConfig();
    cfg.dragCoefficient = static_cast<float>(cd);
    m_aero.manager().model().setConfig(cfg);
}
void phys_Simulator::setFrontalArea(double area) {
    m_frontalArea = area;
    auto cfg = m_aero.manager().model().getConfig();
    cfg.frontalArea = static_cast<float>(area);
    m_aero.manager().model().setConfig(cfg);
}
void phys_Simulator::setWheelBase(double wb) {
    m_wheelbase = wb;
    auto cfg = m_chassis.config();
    cfg.wheelBase = static_cast<float>(wb);
    cfg.frontAxleDist = cfg.rearAxleDist = static_cast<float>(wb) * 0.5f;
    m_chassis.setConfig(cfg);
    auto g = m_suspension.geometryConfig();
    g.wheelBase = static_cast<float>(wb);
    m_suspension.setGeometryConfig(g);
}
void phys_Simulator::setTrackWidth(double tw) { m_trackWidth = tw; }

float phys_Simulator::getBrakeDiscTemp(int wheel) const {
    return (wheel >= 0 && wheel < 4) ? m_brakeDiscTemp[wheel] : 0.0f;
}
float phys_Simulator::getBrakePadTemp(int wheel) const {
    return (wheel >= 0 && wheel < 4) ? m_brakePadTemp[wheel] : 0.0f;
}
float phys_Simulator::getBrakeFade(int wheel) const {
    return (wheel >= 0 && wheel < 4) ? m_brakeFade[wheel] : 0.0f;
}

void phys_Simulator::applyCollisionDamage(double impactForce) {
    if (!m_damageEnabled) return;
    m_damage.applyImpact(static_cast<float>(impactForce));
    // Zone impact from the front (simplified contact)
    physics::CollisionEvent ev;
    ev.contactPoint = m_state.position;
    ev.contactNormal = physics::PhysVec3{0, 0, 1};
    ev.impactEnergy = static_cast<float>(impactForce);
    m_damageSystem.processCollision(ev);
    // Mirror multipliers into DamageState for aero/longitudinal
    m_damage.powerMultiplier = m_damageSystem.powerMultiplier();
    m_damage.handlingMultiplier = m_damageSystem.handlingMultiplier();
    m_damage.brakingMultiplier = m_damageSystem.brakingMultiplier();
    m_damage.downforceMultiplier = m_damageSystem.downforceMultiplier();
    m_damage.dragMultiplier = m_damageSystem.dragMultiplier();
    m_damage.bodyDamage = m_damageSystem.overallDamage();
}
void phys_Simulator::resetDamage() {
    m_damage.reset();
    m_damageSystem.reset();
}

void phys_Simulator::setWeatherState(const physics::WeatherState& weather) {
    m_weather = weather;
    m_weatherSim.setState(weather);
}

void phys_Simulator::loadVehicleParams(const std::string& carPath) {
    loadAeroFromIni(carPath);
}
void phys_Simulator::loadEngineFromIni(const std::string& path) { m_engine.loadFromIni(path); m_maxRpm = m_engine.getConfig().maxRPM; m_enginePowerKw = m_engine.getConfig().peakPower; }
void phys_Simulator::loadTyresFromIni(const std::string& /*path*/) {}
void phys_Simulator::loadDrivetrainFromIni(const std::string& path) {
    m_diff.loadFromIni(path);
    // Optional: parse gear ratios from same file later
}
void phys_Simulator::loadAeroFromIni(const std::string& aeroIniPath) {
    // Accept either directory or full path to aero.ini
    if (aeroIniPath.size() >= 8 && aeroIniPath.substr(aeroIniPath.size() - 8) == "aero.ini") {
        auto cfg = physics::AeroModel::loadFromIni(aeroIniPath);
        m_aero.manager().model().setConfig(cfg);
        m_dragCd = cfg.dragCoefficient;
        m_frontalArea = cfg.frontalArea;
    } else {
        m_aero.loadFromCarPath(aeroIniPath);
    }
}
void phys_Simulator::loadSuspensionFromIni(const std::string& path) { m_suspension.loadFromIni(path); }

physics::WheelState phys_Simulator::wheelState(int wheel) const {
    physics::WheelState w;
    auto t = m_tires.wheelState(wheel);
    w.slipAngle = static_cast<float>(t.slipAngle);
    w.slipRatio = static_cast<float>(t.slipRatio);
    w.lateralForce = static_cast<float>(t.lateralForce);
    w.longitudinalForce = static_cast<float>(t.longitudinalForce);
    w.normalLoad = static_cast<float>(t.normalLoad);
    w.temperature = static_cast<float>(t.temperature);
    w.wear = static_cast<float>(t.wear);
    w.pressure = static_cast<float>(t.pressure);
    return w;
}

physics::ValidationMetrics phys_Simulator::validateAgainstTelemetry(
    const std::vector<double>& timestamps,
    const std::vector<double>& refSpeed,
    const std::vector<double>& /*refLateralG*/,
    const std::vector<double>& /*refLongG*/,
    const std::vector<double>& /*refRPM*/,
    const std::vector<double>& /*refThrottle*/,
    const std::vector<double>& /*refBrake*/,
    const std::vector<double>& /*refSteering*/) const {
    physics::ValidationMetrics m;
    m.nSamples = static_cast<int>(std::min(timestamps.size(), refSpeed.size()));
    return m;
}

} // namespace ks
