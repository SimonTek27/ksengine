#include "VehicleSimulator.h"
#include "TrackSurface.h"
#include <cstdio>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

namespace ks {
namespace physics {

VehicleSimulator::VehicleSimulator() {
    m_baseMass = m_mass;
    m_state.mass = static_cast<float>(m_mass);
    m_state.position = {0, 0.35f, 0};
    AeroModel::AeroConfig ac;
    ac.frontalArea = static_cast<float>(m_frontalArea);
    ac.dragCoefficient = static_cast<float>(m_cd);
    ac.liftCoefficient = -0.25f;
    m_aero.setConfig(ac);
}

void VehicleSimulator::startSimulation() { m_running = true; }
void VehicleSimulator::stopSimulation() { m_running = false; }

void VehicleSimulator::reset() {
    m_frozen = false;
    m_state = SimulationState{};
    m_state.mass = static_cast<float>(m_mass);
    m_state.position = {0, 0.35f, 0};
    m_throttle = m_brake = m_steering = 0;
    m_currentGear = 1;
    m_rpm = 1000;
    m_yawRate = 0;
    m_ffb = VehicleFFBSample{};
    m_damage.reset();
}

void VehicleSimulator::setThrottle(double v) { m_throttle = std::clamp(v, 0.0, 1.0); }
void VehicleSimulator::setBrake(double v) { m_brake = std::clamp(v, 0.0, 1.0); }
void VehicleSimulator::setSteering(double v) { m_steering = std::clamp(v, -1.0, 1.0); }

void VehicleSimulator::setMass(double kg) {
    m_baseMass = std::max(200.0, kg);
    m_mass = m_baseMass + static_cast<double>(m_setup.ballastKg) + static_cast<double>(m_state.fuel) * 0.74;
    m_state.mass = static_cast<float>(m_mass);
}
void VehicleSimulator::setEnginePower(double kw) { m_enginePowerKw = std::max(10.0, kw); }
void VehicleSimulator::setMaxRpm(double rpm) { m_maxRpm = std::max(3000.0, rpm); }
void VehicleSimulator::setDragCoeff(double cd) {
    m_cd = std::max(0.1, cd);
    auto c = m_aero.getConfig();
    c.dragCoefficient = static_cast<float>(m_cd);
    m_aero.setConfig(c);
}
void VehicleSimulator::setFrontalArea(double m2) {
    m_frontalArea = std::max(0.5, m2);
    auto c = m_aero.getConfig();
    c.frontalArea = static_cast<float>(m_frontalArea);
    m_aero.setConfig(c);
}
void VehicleSimulator::setWheelBase(double m) { m_wheelBase = std::max(1.5, m); }
void VehicleSimulator::setTrackWidth(double m) { m_trackWidth = std::max(1.0, m); }

void VehicleSimulator::applySetup(const SetupParams& p) {
    m_setup = p;
    for (int i = 0; i < 4; ++i)
        m_state.tyrePressure[i] = std::clamp(static_cast<double>(p.tirePsi[i]), 1.2, 3.5);
    m_state.fuel = std::clamp(p.fuelL, 0.f, 150.f);
    m_baseMass = (m_baseMass > 100.0) ? m_baseMass : m_mass;
    m_mass = std::max(200.0, m_baseMass + static_cast<double>(p.ballastKg) + static_cast<double>(p.fuelL) * 0.74);
    m_state.mass = static_cast<float>(m_mass);
    // Wing angles → aero lift/drag proxy
    auto ac = m_aero.getConfig();
    const float fw = std::clamp(p.frontWingDeg, 0.f, 40.f);
    const float rw = std::clamp(p.rearWingDeg, 0.f, 45.f);
    // More wing → more downforce (more negative Cl) and more Cd
    ac.liftCoefficient = -0.15f - 0.012f * (fw + rw);
    ac.dragCoefficient = static_cast<float>(std::max(0.15, m_cd + 0.004 * (fw + rw)));
    m_aero.setConfig(ac);
    m_setup.brakeBias = std::clamp(p.brakeBias, 0.30f, 0.80f);
    m_setup.diffPreloadNm = std::clamp(p.diffPreloadNm, 0.f, 200.f);
    m_setup.springRateFront = std::clamp(p.springRateFront, 40.f, 400.f);
    m_setup.springRateRear = std::clamp(p.springRateRear, 40.f, 400.f);
    m_setup.rideHeightFrontMm = std::clamp(p.rideHeightFrontMm, 10.f, 100.f);
    m_setup.rideHeightRearMm = std::clamp(p.rideHeightRearMm, 10.f, 100.f);
    m_setup.tcLevel = std::clamp(p.tcLevel, 0, 12);
    m_setup.absLevel = std::clamp(p.absLevel, 0, 12);
}



std::map<std::string, std::string> VehicleSimulator::parseIni(const std::string& path) {
    std::map<std::string, std::string> out;
    std::ifstream in(path);
    if (!in) return out;
    std::string line, section;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '/') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        auto trim = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
        };
        trim(key); trim(val);
        auto sc = val.find(';');
        if (sc != std::string::npos) val = val.substr(0, sc);
        trim(val);
        if (!section.empty()) out[section + "/" + key] = val;
        out[key] = val;
    }
    return out;
}

float VehicleSimulator::getf(const std::map<std::string, std::string>& m, const std::string& k, float def) {
    auto it = m.find(k);
    if (it == m.end()) return def;
    try { return std::stof(it->second); } catch (...) { return def; }
}

void VehicleSimulator::loadVehicleParams(const std::string& carPath) {
    auto tryLoad = [&](const char* name, void (VehicleSimulator::*fn)(const std::string&)) {
        fs::path p1 = fs::path(carPath) / "data" / name;
        fs::path p2 = fs::path(carPath) / name;
        if (fs::exists(p1)) (this->*fn)(p1.string());
        else if (fs::exists(p2)) (this->*fn)(p2.string());
    };
    tryLoad("engine.ini", &VehicleSimulator::loadEngineFromIni);
    tryLoad("tyres.ini", &VehicleSimulator::loadTyresFromIni);
    tryLoad("tires.ini", &VehicleSimulator::loadTyresFromIni);
    tryLoad("drivetrain.ini", &VehicleSimulator::loadDrivetrainFromIni);
    tryLoad("aero.ini", &VehicleSimulator::loadAeroFromIni);
    tryLoad("suspension.ini", &VehicleSimulator::loadSuspensionFromIni);
}

void VehicleSimulator::loadEngineFromIni(const std::string& path) {
    auto m = parseIni(path);
    float power = getf(m, "POWER", getf(m, "ENGINE/POWER", static_cast<float>(m_enginePowerKw)));
    float rpm = getf(m, "LIMITER", getf(m, "ENGINE/LIMITER", static_cast<float>(m_maxRpm)));
    if (power > 1.f) m_enginePowerKw = power;
    if (rpm > 1000.f) m_maxRpm = rpm;
}

void VehicleSimulator::loadTyresFromIni(const std::string& path) {
    m_tires.loadFromIni(path); // KsTireModel coeffs + load sensitivity (P0.1)
    auto m = parseIni(path);
    float r = getf(m, "RADIUS", getf(m, "FRONT/RADIUS", static_cast<float>(m_wheelRadius)));
    if (r > 0.1f) m_wheelRadius = r;
}

void VehicleSimulator::loadDrivetrainFromIni(const std::string& path) {
    auto m = parseIni(path);
    float fd = getf(m, "FINAL", getf(m, "DRIVETRAIN/FINAL", static_cast<float>(m_finalDrive)));
    if (fd > 0.5f) m_finalDrive = fd;
}

void VehicleSimulator::loadAeroFromIni(const std::string& path) {
    auto m = parseIni(path);
    float cd = getf(m, "CD", getf(m, "AERO/CD", static_cast<float>(m_cd)));
    float area = getf(m, "AREA", getf(m, "AERO/AREA", static_cast<float>(m_frontalArea)));
    if (cd > 0.05f) setDragCoeff(cd);
    if (area > 0.5f) setFrontalArea(area);
}

void VehicleSimulator::loadSuspensionFromIni(const std::string& path) {
    auto m = parseIni(path);
    float wb = getf(m, "WHEELBASE", getf(m, "SUSPENSION/WHEELBASE", static_cast<float>(m_wheelBase)));
    float tw = getf(m, "TRACK", getf(m, "SUSPENSION/TRACK", static_cast<float>(m_trackWidth)));
    if (wb > 1.5f) m_wheelBase = wb;
    if (tw > 1.0f) m_trackWidth = tw;
}

void VehicleSimulator::shiftGears() {
    if (m_rpm > m_maxRpm * 0.95 && m_currentGear < static_cast<int>(m_gearRatios.size()))
        ++m_currentGear;
    else if (m_rpm < 2000 && m_currentGear > 1)
        --m_currentGear;
}

void VehicleSimulator::integrate(double dt) {
    const float fdt = static_cast<float>(std::clamp(dt, 1e-4, 0.05));
    float speed = m_state.speed;
    const float mass = static_cast<float>(m_mass);
    const float wb = static_cast<float>(m_wheelBase);
    const float halfTrack = static_cast<float>(m_trackWidth) * 0.5f;
    const float steerRad = static_cast<float>(m_steering) * 0.45f;

    AeroModel::AeroState as;
    as.speed = speed;
    as.yawAngle = m_state.heading;
    auto af = m_aero.calculate(as);

    m_damage.update(fdt, speed, static_cast<float>(m_rpm));
    const float dmgPower = m_damage.powerMultiplier();
    const float dmgDrag = m_damage.dragMultiplier();
    const float dmgDf = m_damage.downforceMultiplier();
    const float dmgBrake = m_damage.brakingMultiplier();
    const float dmgHand = m_damage.handlingMultiplier();

    float aeroDrag = af.drag * dmgDrag;
    float downforce = af.downforce * dmgDf;

    const float springF = std::max(40.f, m_setup.springRateFront);
    const float springR = std::max(40.f, m_setup.springRateRear);
    const float springSum = springF + springR;
    const float frontFrac = springF / springSum;
    float loadFront = mass * 9.81f * frontFrac + downforce * 0.4f;
    float loadRear  = mass * 9.81f * (1.f - frontFrac) + downforce * 0.6f;
    loadFront = std::max(500.f, loadFront);
    loadRear  = std::max(500.f, loadRear);
    float loadFL = loadFront * 0.5f, loadFR = loadFront * 0.5f;
    float loadRL = loadRear * 0.5f,  loadRR = loadRear * 0.5f;
    // Cold pressure deviation from 2.2 bar → grip scale
    auto pScale = [](float psi) {
        float d = psi - 2.2f;
        return std::clamp(1.f - 0.08f * d * d, 0.75f, 1.05f);
    };
    loadFL *= pScale(static_cast<float>(m_state.tyrePressure[0]));
    loadFR *= pScale(static_cast<float>(m_state.tyrePressure[1]));
    loadRL *= pScale(static_cast<float>(m_state.tyrePressure[2]));
    loadRR *= pScale(static_cast<float>(m_state.tyrePressure[3]));

    float beta = 0.f;
    if (speed > 1.0f) {
        float vx = m_state.velocity.x * std::sin(m_state.heading) + m_state.velocity.z * std::cos(m_state.heading);
        float vy = m_state.velocity.x * std::cos(m_state.heading) - m_state.velocity.z * std::sin(m_state.heading);
        beta = std::atan2(vy, std::max(0.5f, std::abs(vx)));
    }
    float yaw = static_cast<float>(m_yawRate);
    float saFL = beta + (yaw * wb * 0.45f) / std::max(speed, 1.0f) - steerRad;
    float saFR = saFL;
    float saRL = beta - (yaw * wb * 0.55f) / std::max(speed, 1.0f);
    float saRR = saRL;

    const auto& sFL = m_damage.suspensionDamage(0);
    const auto& sFR = m_damage.suspensionDamage(1);
    saFL += sFL.toeDeviation * 0.01745f;
    saFR += sFR.toeDeviation * 0.01745f;

    const size_t gi = static_cast<size_t>(std::max(0, m_currentGear - 1) % static_cast<int>(m_gearRatios.size()));
    float gearRatio = static_cast<float>(m_gearRatios[gi]);
    float wheelOmega = speed / static_cast<float>(m_wheelRadius);
    m_rpm = std::max(800.0, wheelOmega * gearRatio * m_finalDrive * 60.0 / (2.0 * 3.14159265));
    if (!m_damage.isEngineFailed())
        shiftGears();
    const size_t gi2 = static_cast<size_t>(std::max(0, m_currentGear - 1) % static_cast<int>(m_gearRatios.size()));
    gearRatio = static_cast<float>(m_gearRatios[gi2]);

    float peakTorque = static_cast<float>(m_enginePowerKw * 1000.0 / (m_maxRpm * 2.0 * 3.14159265 / 60.0));
    float rpmN = static_cast<float>(std::min(m_rpm / m_maxRpm, 1.0));
    float engineTorque = peakTorque * std::sin(rpmN * 3.14159265f) * static_cast<float>(m_throttle) * dmgPower;
    if (m_damage.isTransmissionFailed())
        engineTorque *= 0.3f;
    float driveForceTarget = (engineTorque * gearRatio * static_cast<float>(m_finalDrive))
                             / static_cast<float>(m_wheelRadius);
    if (m_setup.tcLevel > 0 && speed > 3.f && m_throttle > 0.3)
        driveForceTarget *= (1.f - 0.03f * static_cast<float>(m_setup.tcLevel));
    float brakeForceTarget = static_cast<float>(m_brake) * mass * 12.0f * dmgBrake;
    // Soft ABS: reduce brake force if high slip at high ABS level
    if (m_setup.absLevel > 0 && speed > 5.f && m_brake > 0.4)
        brakeForceTarget *= (1.f - 0.04f * static_cast<float>(m_setup.absLevel));
    // TC: reduce drive when slip high
    // (applied after driveForceTarget)

    // Surface grip at vehicle position (P0.2)
    const auto surf = TrackSurface::instance().sample(m_state.position);
    const float surfaceMu = std::clamp(surf.grip, 0.05f, 2.0f);
    const float tempC = surf.temperature > 1.f ? surf.temperature : 80.f;
    // Pressure bar → approx PSI for KsTireModel (1 bar ≈ 14.5 psi)
    auto psi = [&](int i) {
        return static_cast<float>(m_state.tyrePressure[i]) * 14.5f;
    };

    auto tireForce = [&](float sa, float load, int wheel) {
        return m_tires.calculateCombinedSlip(sa, 0.f, load, -0.03f,
            surfaceMu, psi(wheel), tempC);
    };

    auto fFL = tireForce(saFL, loadFL, 0);
    auto fFR = tireForce(saFR, loadFR, 1);
    auto fRL = tireForce(saRL, loadRL, 2);
    auto fRR = tireForce(saRR, loadRR, 3);

    // Deposit rubber when sliding hard
    const float slipMag = std::abs(saFL) + std::abs(saFR);
    if (slipMag > 0.12f && speed > 5.f) {
        TrackSurface::instance().depositRubber(m_state.position, 0.0002f * fdt * slipMag, 1.5f);
    }

    float Fy = (fFL.lateralForce + fFR.lateralForce + fRL.lateralForce + fRR.lateralForce) * dmgHand;

    float longForce = 0.f;
    {
        float cmd = driveForceTarget - brakeForceTarget - aeroDrag;
        float gripLong = (std::abs(fFL.longitudinalForce) + std::abs(fFR.longitudinalForce)
                       + std::abs(fRL.longitudinalForce) + std::abs(fRR.longitudinalForce));
        if (gripLong < 1.f) gripLong = mass * 12.f;
        longForce = std::clamp(cmd, -gripLong - 1.f, gripLong + 1.f);
        if (speed < 1.0f && m_throttle > 0.05)
            longForce = std::max(longForce, driveForceTarget * 0.5f);
    }

    float ax = longForce / mass;
    float ay = Fy / mass;

    float mz = (fFL.lateralForce + fFR.lateralForce) * (wb * 0.45f)
             - (fRL.lateralForce + fRR.lateralForce) * (wb * 0.55f)
             + (fFL.longitudinalForce - fFR.longitudinalForce) * halfTrack * 0.1f;
    float Iz = mass * (wb * wb + halfTrack * halfTrack * 4.f) * 0.15f;
    m_yawRate += (mz / std::max(Iz, 1.f)) * fdt;
    m_yawRate *= (1.0f - 2.0f * fdt);

    m_state.heading += static_cast<float>(m_yawRate) * fdt + steerRad * 0.02f * fdt * std::max(speed, 0.f);
    m_state.rotation.y = m_state.heading;

    float c = std::cos(m_state.heading);
    float s = std::sin(m_state.heading);
    float worldAx = ax * s - ay * c;
    float worldAz = ax * c + ay * s;

    m_state.velocity.x += worldAx * fdt;
    m_state.velocity.z += worldAz * fdt;
    m_state.velocity.y = 0;
    m_state.position.x += m_state.velocity.x * fdt;
    m_state.position.z += m_state.velocity.z * fdt;
    m_state.position.y = 0.35f;

    const auto cg = m_damage.cgShift();
    m_state.position.x += cg.x * 0.01f;
    m_state.position.z += cg.z * 0.01f;

    m_state.speed = std::sqrt(m_state.velocity.x * m_state.velocity.x +
                              m_state.velocity.z * m_state.velocity.z);
    m_state.acceleration = {worldAx, 0, worldAz};
    m_state.angularVelocity.y = static_cast<float>(m_yawRate);
    m_state.throttle = static_cast<float>(m_throttle);
    m_state.brake = static_cast<float>(m_brake);
    m_state.steering = static_cast<float>(m_steering);
    m_state.gear = m_currentGear;
    m_state.rpm = static_cast<float>(m_rpm);
    m_state.mass = mass;
    if (m_throttle > 0.05 && m_state.fuel > 0.f)
        m_state.fuel = std::max(0.f, m_state.fuel - static_cast<float>(m_throttle) * 0.00015f * fdt * 60.f);

    m_ffb.slipAngleFL = saFL;
    m_ffb.slipAngleFR = saFR;
    m_ffb.loadFL = loadFL;
    m_ffb.loadFR = loadFR;
    m_ffb.camberFL = m_ffb.camberFR = -0.03f;
    m_ffb.speedMs = m_state.speed;
    m_ffb.steerAngle = steerRad;
    m_ffb.aligningMomentNm = fFL.aligningMoment + fFR.aligningMoment;
}

void VehicleSimulator::updatePhysics(double dt) {
    if (!m_running || m_frozen) return;
    if (!std::isfinite(dt) || dt <= 0.0 || dt > 0.1) return;
    integrate(dt);
    if (!std::isfinite(m_state.speed) || !std::isfinite(m_state.position.x) ||
        !std::isfinite(m_state.position.z) || !std::isfinite(m_state.heading)) {
        m_state.velocity = {};
        m_state.angularVelocity = {};
        m_state.acceleration = {};
        m_state.speed = 0.f;
        if (!std::isfinite(m_state.position.x)) m_state.position.x = 0.f;
        if (!std::isfinite(m_state.position.y)) m_state.position.y = 0.35f;
        if (!std::isfinite(m_state.position.z)) m_state.position.z = 0.f;
        if (!std::isfinite(m_state.heading)) m_state.heading = 0.f;
        m_yawRate = 0;
        m_rpm = 1000;
    }
}

} // namespace physics
} // namespace ks
