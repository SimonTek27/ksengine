#include "VehicleSimulator.h"
#include "TrackSurface.h"
#include "PhysicsProfiler.h"
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
    m_lastAx = 0.f;
    m_lastAy = 0.f;
    m_lltdFront = 0.55f;
    for (int i = 0; i < 4; ++i) {
        m_wheelOmega[i] = 0.f;
        m_slipRatio[i] = 0.f;
    }
    m_beta = 0.f;
    m_ugDegG = 0.f;
    m_staticMargin = 0.f;
    for (int i = 0; i < 4; ++i) {
        m_tireFx[i] = m_tireFy[i] = m_tireFz[i] = m_fcUsage[i] = 0.f;
        m_fyLag[i] = 0.f;
    }
    m_yawAccel = 0.f;
    m_yawDampingNr = 0.f;
    m_rollAngle = 0.f;
    m_aquaplane = 0.f;
    m_effectiveMu = 1.f;
    m_waterDepthMm = 0.f;
    m_fltFront = 0.f;
    m_fltRear = 0.f;
    m_diff.reset();
    m_brakes.reset();
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
    // G12 aero balance: more front wing → more front DF share
    const float bal = 0.38f + 0.012f * (fw - rw);
    m_aeroBalFront = std::clamp(bal, 0.30f, 0.65f);
    m_setup.brakeBias = std::clamp(p.brakeBias, 0.30f, 0.80f);
    m_setup.diffPreloadNm = std::clamp(p.diffPreloadNm, 0.f, 200.f);
    {
        auto dc = m_diff.getConfig();
        dc.preload = m_setup.diffPreloadNm;
        m_diff.setConfig(dc);
    }
    m_setup.springRateFront = std::clamp(p.springRateFront, 40.f, 400.f);
    m_setup.springRateRear = std::clamp(p.springRateRear, 40.f, 400.f);
    m_setup.rideHeightFrontMm = std::clamp(p.rideHeightFrontMm, 10.f, 100.f);
    m_setup.rideHeightRearMm = std::clamp(p.rideHeightRearMm, 10.f, 100.f);
    m_rhFront = m_setup.rideHeightFrontMm * 0.001f;
    m_rhRear = m_setup.rideHeightRearMm * 0.001f;
    m_setup.arbFront = std::clamp(p.arbFront, 0.f, 2.0e5f);
    m_setup.arbRear = std::clamp(p.arbRear, 0.f, 2.0e5f);
    m_setup.rollCenterFrontM = std::clamp(p.rollCenterFrontM, 0.f, 0.25f);
    m_setup.rollCenterRearM = std::clamp(p.rollCenterRearM, 0.f, 0.30f);
    m_setup.cgHeightM = std::clamp(p.cgHeightM, 0.15f, 0.80f);
    m_setup.frontWeightFrac = std::clamp(p.frontWeightFrac, 0.30f, 0.65f);
    m_setup.rollSteerFront = std::clamp(p.rollSteerFront, -0.4f, 0.4f);
    m_setup.rollSteerRear = std::clamp(p.rollSteerRear, -0.4f, 0.4f);
    m_setup.latComplianceFront = std::clamp(p.latComplianceFront, 1.0e4f, 5.0e5f);
    m_setup.latComplianceRear = std::clamp(p.latComplianceRear, 1.0e4f, 5.0e5f);
    m_setup.alignComplianceFront = std::clamp(p.alignComplianceFront, 5.0e2f, 5.0e4f);
    m_setup.alignComplianceRear = std::clamp(p.alignComplianceRear, 5.0e2f, 5.0e4f);
    m_setup.tcLevel = std::clamp(p.tcLevel, 0, 12);
    m_setup.absLevel = std::clamp(p.absLevel, 0, 12);
    auto geom = m_suspension.geometryConfig();
    geom.rollCenterFront = m_setup.rollCenterFrontM;
    geom.rollCenterRear = m_setup.rollCenterRearM;
    geom.wheelBase = static_cast<float>(m_wheelBase);
    geom.frontTrackWidth = static_cast<float>(m_trackWidth);
    geom.rearTrackWidth = static_cast<float>(m_trackWidth);
    m_suspension.setGeometryConfig(geom);
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

    AeroModel::AeroForces af;
    {
        PROFILE_SUBSYSTEM(PhysicsProfiler::Aero);
        PROFILE_SECTION("VehicleSimulator.aero");
        // G16: ride-height sensitive aero (Milliken Ch.3/15 ground effect)
        // Prefer live suspension ride height; fall back to setup mm
        float rhF = m_suspension.rideHeightFront();
        float rhR = m_suspension.rideHeightRear();
        if (rhF < 0.01f) rhF = m_setup.rideHeightFrontMm * 0.001f;
        if (rhR < 0.01f) rhR = m_setup.rideHeightRearMm * 0.001f;
        rhF = std::clamp(rhF, 0.015f, 0.20f);
        rhR = std::clamp(rhR, 0.015f, 0.20f);
        m_rhFront = rhF;
        m_rhRear = rhR;

        AeroModel::AeroState as;
        as.speed = speed;
        as.yawAngle = m_state.heading;
        as.rollAngle = m_rollAngle;
        as.rideHeightFront = rhF;
        as.rideHeightRear = rhR;
        af = m_aero.calculate(as);

        // If model returned split DF, refine aero balance
        if (af.frontDownforce + af.rearDownforce > 50.f) {
            m_aeroBalFront = std::clamp(
                af.frontDownforce / (af.frontDownforce + af.rearDownforce), 0.30f, 0.65f);
        }
    }

    {
        PROFILE_SUBSYSTEM(PhysicsProfiler::DamageModel);
        PROFILE_SECTION("VehicleSimulator.damage");
        m_damage.update(fdt, speed, static_cast<float>(m_rpm));
    }
    const float dmgPower = m_damage.powerMultiplier();
    const float dmgDrag = m_damage.dragMultiplier();
    const float dmgDf = m_damage.downforceMultiplier();
    const float dmgBrake = m_damage.brakingMultiplier();
    const float dmgHand = m_damage.handlingMultiplier();

    float aeroDrag = af.drag * dmgDrag;
    float downforce = af.downforce * dmgDf;

    // --- Wheel loads (Milliken Ch.7 / Ch.18): static + aero + long transfer + LLTD ---
    const float springF = std::max(40.f, m_setup.springRateFront); // N/mm proxy
    const float springR = std::max(40.f, m_setup.springRateRear);
    const float frontFrac = std::clamp(m_setup.frontWeightFrac, 0.30f, 0.65f);
    const float track = std::max(0.8f, static_cast<float>(m_trackWidth));
    const float hCg = std::clamp(m_setup.cgHeightM, 0.15f, 0.80f);
    const float hRcF = std::clamp(m_setup.rollCenterFrontM, 0.f, 0.25f);
    const float hRcR = std::clamp(m_setup.rollCenterRearM, 0.f, 0.30f);
    // Previous-step body accel (m/s²) — breaks algebraic loop with tire forces
    const float Ax = m_lastAx;
    const float Ay = m_lastAy;

    // Static axle loads + aero downforce split (front/rear bias mild)
    const float aeroF = std::clamp(m_aeroBalFront, 0.30f, 0.65f);
    float loadFront = mass * 9.81f * frontFrac + downforce * aeroF;
    float loadRear  = mass * 9.81f * (1.f - frontFrac) + downforce * (1.f - aeroF);

    // Longitudinal load transfer: +Ax (forward accel) → unload front, load rear
    // ΔW = m * Ax * (h / ℓ)   [N]
    const float dLong = mass * Ax * (hCg / std::max(wb, 0.5f));
    loadFront -= dLong;
    loadRear  += dLong;
    loadFront = std::max(200.f, loadFront);
    loadRear  = std::max(200.f, loadRear);

    // Elastic roll stiffness Kφ (N·m/rad): ~ ½ k_wheel t² + ARB
    // springRate is N/mm → *1000 → N/m; two corners ≈ 0.5 * k * t² for axle roll
    const float kWheelF = springF * 1000.f;
    const float kWheelR = springR * 1000.f;
    const float t2 = track * track;
    const float KphiF = 0.5f * kWheelF * t2 + std::max(0.f, m_setup.arbFront);
    const float KphiR = 0.5f * kWheelR * t2 + std::max(0.f, m_setup.arbRear);
    const float KphiSum = std::max(KphiF + KphiR, 1.f);
    m_lltdFront = KphiF / KphiSum; // elastic LLTD front fraction

    // Geometric LLT through roll centers + elastic through (h − h_rc)
    const float hRcAvg = 0.5f * (hRcF + hRcR);
    const float hSprung = std::max(0.05f, hCg - hRcAvg);
    const float invTrack = 1.f / track;
    const float wAxleSum = std::max(loadFront + loadRear, 1.f);
    // Axle geometric share ∝ RC height and axle weight fraction
    const float dGeoF = mass * Ay * hRcF * invTrack * (loadFront / wAxleSum);
    const float dGeoR = mass * Ay * hRcR * invTrack * (loadRear / wAxleSum);
    // Elastic couple reacted by springs/ARB, distributed by LLTD
    const float dElastic = mass * Ay * hSprung * invTrack;
    const float dLatF = dGeoF + dElastic * m_lltdFront;
    const float dLatR = dGeoR + dElastic * (1.f - m_lltdFront);

    // +Ay (accel to vehicle right) → load to left (outside in left turn)
    float loadFL = loadFront * 0.5f + 0.5f * dLatF;
    float loadFR = loadFront * 0.5f - 0.5f * dLatF;
    float loadRL = loadRear  * 0.5f + 0.5f * dLatR;
    float loadRR = loadRear  * 0.5f - 0.5f * dLatR;
    loadFL = std::max(150.f, loadFL);
    loadFR = std::max(150.f, loadFR);
    loadRL = std::max(150.f, loadRL);
    loadRR = std::max(150.f, loadRR);

    // Cold pressure deviation from 2.2 bar → grip scale on normal load proxy
    auto pScale = [](float bar) {
        float d = bar - 2.2f;
        return std::clamp(1.f - 0.08f * d * d, 0.75f, 1.05f);
    };
    loadFL *= pScale(static_cast<float>(m_state.tyrePressure[0]));
    loadFR *= pScale(static_cast<float>(m_state.tyrePressure[1]));
    loadRL *= pScale(static_cast<float>(m_state.tyrePressure[2]));
    loadRR *= pScale(static_cast<float>(m_state.tyrePressure[3]));

    // G13 FLT: (Fz_left − Fz_right) / Fz_axle
    {
        const float fAx = std::max(loadFL + loadFR, 1.f);
        const float rAx = std::max(loadRL + loadRR, 1.f);
        m_fltFront = (loadFL - loadFR) / fAx;
        m_fltRear = (loadRL - loadRR) / rAx;
    }

    // G14: suspension kinematics → camber (Milliken Ch.17 geometry)
    {
        const float aeroF = std::clamp(m_aeroBalFront, 0.30f, 0.65f);
        m_suspension.update(fdt, 0.f, Ay, Ax, mass,
                            downforce * aeroF, downforce * (1.f - aeroF));
    }

    float beta = 0.f;
    if (speed > 1.0f) {
        float vx = m_state.velocity.x * std::sin(m_state.heading) + m_state.velocity.z * std::cos(m_state.heading);
        float vy = m_state.velocity.x * std::cos(m_state.heading) - m_state.velocity.z * std::sin(m_state.heading);
        beta = std::atan2(vy, std::max(0.5f, std::abs(vx)));
    }
    m_beta = beta;
    float yaw = static_cast<float>(m_yawRate);
    // Low-speed SA blend: avoid numerical weave killing longitudinal grip
    const float saScale = std::clamp(speed / 5.0f, 0.15f, 1.0f);
    const float a_cg = wb * (1.f - frontFrac);
    const float b_cg = wb * frontFrac;
    float saFL = (beta + (yaw * a_cg) / std::max(speed, 1.0f) - steerRad) * saScale;
    float saFR = saFL;
    float saRL = (beta - (yaw * b_cg) / std::max(speed, 1.0f)) * saScale;
    float saRR = saRL;

    // --- G7: quasi-static roll + roll steer + compliance steer (Milliken Ch.17/23) ---
    {
        // Roll stiffness from same Kφ used in LLTD
        const float kWheelF = springF * 1000.f;
        const float kWheelR = springR * 1000.f;
        const float t2r = track * track;
        const float KphiTot = 0.5f * kWheelF * t2r + 0.5f * kWheelR * t2r
                            + std::max(0.f, m_setup.arbFront) + std::max(0.f, m_setup.arbRear);
        const float hSpr = std::max(0.05f, hCg - 0.5f * (hRcF + hRcR));
        float phiTarget = (mass * Ay * hSpr) / std::max(KphiTot, 1.f);
        // Ay from previous step (m_lastAy); sign: +Ay → roll negative in SAE-ish
        phiTarget = -phiTarget;
        phiTarget = std::clamp(phiTarget, -0.18f, 0.18f);
        // light lag on roll
        m_rollAngle = m_rollAngle * 0.85f + phiTarget * 0.15f;

        // Roll steer: additional road-wheel angle (adds to α like −δ)
        const float dRollF = m_setup.rollSteerFront * m_rollAngle;
        const float dRollR = m_setup.rollSteerRear * m_rollAngle;
        saFL -= dRollF * saScale;
        saFR -= dRollF * saScale;
        saRL -= dRollR * saScale;
        saRR -= dRollR * saScale;

        // Compliance steer from previous lagged Fy and aligning (Mz ≈ −0.03 Fy proxy if needed)
        const float KcF = std::max(1.e4f, m_setup.latComplianceFront);
        const float KcR = std::max(1.e4f, m_setup.latComplianceRear);
        const float KaF = std::max(5.e2f, m_setup.alignComplianceFront);
        const float KaR = std::max(5.e2f, m_setup.alignComplianceRear);
        // δ_comp = Fy/Kc + Mz/Ka ; Mz tire ≈ −0.03 Fy (matches KsTireModel)
        const float dCompFL = (m_fyLag[0] / KcF) + ((-0.03f * m_fyLag[0]) / KaF);
        const float dCompFR = (m_fyLag[1] / KcF) + ((-0.03f * m_fyLag[1]) / KaF);
        const float dCompRL = (m_fyLag[2] / KcR) + ((-0.03f * m_fyLag[2]) / KaR);
        const float dCompRR = (m_fyLag[3] / KcR) + ((-0.03f * m_fyLag[3]) / KaR);
        saFL -= dCompFL;
        saFR -= dCompFR;
        saRL -= dCompRL;
        saRR -= dCompRR;
    }

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
    // G16: engine braking / coast torque when throttle released
    if (m_throttle < 0.05f && speed > 2.f && m_currentGear > 0) {
        const float coastNm = 25.f + 40.f * rpmN; // grows with rpm
        engineTorque = -coastNm * dmgPower;
    }
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
    float surfaceMu = std::clamp(surf.grip, 0.05f, 2.0f);
    const float tempC = surf.temperature > 1.f ? surf.temperature : 80.f;

    // --- G11 aquaplaning / wet film (Milliken Ch.2 surface + hydroplaning) ---
    // Water depth proxy from wetness (0..1 → 0..4 mm standing water)
    const float wet = std::clamp(surf.wetness, 0.f, 1.f);
    m_waterDepthMm = wet * 4.0f;
    // Mean tire pressure (bar) — higher pressure raises critical speed
    float pBar = 0.25f * (static_cast<float>(m_state.tyrePressure[0])
                        + static_cast<float>(m_state.tyrePressure[1])
                        + static_cast<float>(m_state.tyrePressure[2])
                        + static_cast<float>(m_state.tyrePressure[3]));
    pBar = std::clamp(pBar, 1.2f, 3.5f);
    // Empirical V_crit (m/s): ~ NASA/SAE-style √p / water factor
    // Dry → very high; 3 mm water ~ 15–25 m/s depending on pressure
    const float vCrit = (11.0f + 9.0f * std::sqrt(pBar / 2.2f))
                      / (1.0f + 0.55f * m_waterDepthMm);
    float aqua = 0.f;
    if (wet > 0.04f && speed > 0.5f) {
        // Soft onset from 0.65 Vcrit, full by ~1.15 Vcrit
        const float v0 = 0.65f * vCrit;
        const float v1 = 1.15f * vCrit;
        aqua = std::clamp((speed - v0) / std::max(v1 - v0, 1.f), 0.f, 1.f);
        aqua *= wet; // no aquaplane on dry
    }
    m_aquaplane = aqua;
    // μ collapse + extra wet already partly in surface grip; aqua multiplies further
    const float aquaMuScale = 1.0f - 0.75f * aqua;
    surfaceMu = std::clamp(surfaceMu * aquaMuScale, 0.03f, 2.0f);
    m_effectiveMu = surfaceMu;
    // Dynamic Fz loss (lift): reduce loads especially on front when aquaplaning
    if (aqua > 0.01f) {
        const float liftF = 1.0f - 0.45f * aqua;
        const float liftR = 1.0f - 0.25f * aqua;
        loadFL *= liftF; loadFR *= liftF;
        loadRL *= liftR; loadRR *= liftR;
    }
    // Pressure bar → approx PSI for KsTireModel (1 bar ≈ 14.5 psi)
    auto psi = [&](int i) {
        return static_cast<float>(m_state.tyrePressure[i]) * 14.5f;
    };

    // --- Physical slip ratio κ (Milliken / SAE practical) ---
    // κ = (ωR − Vx) / max(|ωR|, |Vx|, v_min) — stable near rest
    const float R = static_cast<float>(m_wheelRadius);
    const float Iw = std::max(0.5f, m_wheelInertia);
    float vxBody = speed;
    if (speed > 0.25f) {
        vxBody = m_state.velocity.x * std::sin(m_state.heading)
               + m_state.velocity.z * std::cos(m_state.heading);
    }
    const float omegaFree = vxBody / std::max(R, 0.1f);
    for (int i = 0; i < 4; ++i) {
        if (std::fabs(m_wheelOmega[i]) < 0.05f && speed > 0.3f)
            m_wheelOmega[i] = omegaFree;
    }
    float srArr[4];
    for (int i = 0; i < 4; ++i) {
        const float vWh = m_wheelOmega[i] * R;
        // Pacejka/SAE practical: κ = (ωR − Vx) / |Vx| (traction > 0, braking < 0)
        const float denom = std::max(std::fabs(vxBody), 2.0f);
        srArr[i] = std::clamp((vWh - vxBody) / denom, -1.0f, 1.0f);
        m_slipRatio[i] = srArr[i];
    }

    // KsTireModel batch: shared surface mu/temp; Fz-cache warms across wheels
    KsTireModel::TireForces forces[4];
    {
        PROFILE_SUBSYSTEM(PhysicsProfiler::Tires);
        PROFILE_SECTION("VehicleSimulator.tires");
        const float saArr[4] = { saFL, saFR, saRL, saRR };
        const float loadArr[4] = { loadFL, loadFR, loadRL, loadRR };
        const float camArr[4] = {
            m_suspension.corner(0).camberDeg * 0.017453292f,
            m_suspension.corner(1).camberDeg * 0.017453292f,
            m_suspension.corner(2).camberDeg * 0.017453292f,
            m_suspension.corner(3).camberDeg * 0.017453292f
        };
        const float psiArr[4] = { psi(0), psi(1), psi(2), psi(3) };
        float tempArr[4];
        for (int i = 0; i < 4; ++i) {
            float tt = static_cast<float>(m_state.tyreTemp[i]);
            if (tt < 15.f) tt = std::max(tempC, 40.f); // cold start toward track
            tempArr[i] = tt;
        }
        m_tires.calculateCombinedSlipBatch(saArr, srArr, loadArr, camArr,
                                           surfaceMu, psiArr, tempArr, forces);
    }
    const auto& fFL = forces[0];
    const auto& fFR = forces[1];
    const auto& fRL = forces[2];
    const auto& fRR = forces[3];
    const float Fx[4] = {
        fFL.longitudinalForce, fFR.longitudinalForce,
        fRL.longitudinalForce, fRR.longitudinalForce
    };
    const float FyW[4] = {
        fFL.lateralForce, fFR.lateralForce,
        fRL.lateralForce, fRR.lateralForce
    };
    const float FzW[4] = { loadFL, loadFR, loadRL, loadRR };
    const float saW[4] = { saFL, saFR, saRL, saRR };
    // G5: friction-circle usage |F| / (μ Fz)
    for (int i = 0; i < 4; ++i) {
        m_tireFx[i] = Fx[i];
        m_tireFy[i] = FyW[i];
        m_tireFz[i] = FzW[i];
        const float fMax = std::max(surfaceMu * FzW[i], 50.f);
        m_fcUsage[i] = std::sqrt(Fx[i] * Fx[i] + FyW[i] * FyW[i]) / fMax;
        m_state.tireFx[i] = Fx[i];
        m_state.tireFy[i] = FyW[i];
        m_state.tireFz[i] = FzW[i];
        m_state.frictionCircleUsage[i] = m_fcUsage[i];
        m_state.slipAngle[i] = saW[i];
        m_state.slipRatioState[i] = srArr[i];
    }

    // G14 tire wear: accumulate + scale forces
    {
        for (int i = 0; i < 4; ++i) {
            const float w = std::clamp(static_cast<float>(m_state.tyreWear[i]), 0.f, 1.f);
            const float ws = m_tires.calculateWearEffect(w);
            // scale stored forces used downstream
            // (Fx/FyW local const — apply via m_tireFx already set; patch Fx usage via members)
            m_tireFx[i] *= ws;
            m_tireFy[i] *= ws;
            m_state.tireFx[i] = m_tireFx[i];
            m_state.tireFy[i] = m_tireFy[i];
            m_state.tireCamber[i] = m_suspension.corner(i).camberDeg * 0.017453292f;
            // wear rate ~ slip energy / Fz
            const float slipE = (std::fabs(srArr[i]) + std::fabs(saW[i])) * speed * 0.00002f;
            m_state.tyreWear[i] = std::clamp(
                static_cast<float>(m_state.tyreWear[i]) + slipE * fdt, 0.f, 1.f);
        }
    }

    // G17: tire pressure rises with temperature (ideal-gas-ish delta)
    {
        for (int i = 0; i < 4; ++i) {
            const float T = static_cast<float>(m_state.tyreTemp[i]);
            const float Tref = 80.f;
            // cold fill ~ setpoint; hot: +~0.02 bar per 10°C above ref
            float p = static_cast<float>(m_state.tyrePressure[i]);
            if (p < 1.0f) p = 1.8f;
            const float pHot = p + 0.002f * (T - Tref);
            // soft lag toward hot pressure
            p += (pHot - p) * std::clamp(0.15f * fdt, 0.f, 1.f);
            m_state.tyrePressure[i] = std::clamp(p, 1.2f, 3.5f);
        }
    }

    // --- G12 tire thermal (simple energy balance) ---
    {
        const float opt = m_tires.getCoefficients().optTempC;
        const float trackT = tempC;
        for (int i = 0; i < 4; ++i) {
            const float slipHeat = (std::fabs(srArr[i]) * 18.f
                                 + std::fabs(saW[i]) * 12.f) * speed * 0.15f;
            const float cool = 0.35f * (static_cast<float>(m_state.tyreTemp[i]) - trackT);
            float dT = (slipHeat - cool) * fdt;
            m_state.tyreTemp[i] = std::clamp(
                static_cast<float>(m_state.tyreTemp[i]) + dT, 15.f, 140.f);
        }
        (void)opt;
    }

    // Drive (RWD) + brake torque split; TC / ABS + soft peak-κ limiter
    float Tdrive = driveForceTarget * R; // total rear axle drive torque
    const float kappaRear = 0.5f * (srArr[2] + srArr[3]); // signed mean rear
    const float kappaRearAbs = std::fabs(kappaRear);
    // Soft traction limit near MF peak (~0.08–0.12); aggressive past peak
    if (kappaRear > 0.08f && m_throttle > 0.02f) {
        const float cut = std::clamp((kappaRear - 0.08f) / 0.12f, 0.f, 0.98f);
        Tdrive *= (1.f - cut);
    }
    // Clamp ω into peak-κ band when past peak (usable Fx / friction circle)
    if (m_throttle > 0.05f && kappaRear > 0.15f) {
        const float wPeak = omegaFree * 1.10f;
        m_wheelOmega[2] = std::min(m_wheelOmega[2], wPeak);
        m_wheelOmega[3] = std::min(m_wheelOmega[3], wPeak);
    }
    // Soft lock prevention on brakes (keep |κ| near peak braking ~0.12)
    if (m_brake > 0.05f) {
        const float wMin = omegaFree * 0.88f;
        for (int i = 0; i < 4; ++i) {
            if (srArr[i] < -0.18f)
                m_wheelOmega[i] = std::max(m_wheelOmega[i], wMin);
        }
    }
    if (m_setup.tcLevel > 0 && kappaRearAbs > 0.10f && m_throttle > 0.15f) {
        const float cut = std::clamp(0.1f * static_cast<float>(m_setup.tcLevel) * kappaRearAbs, 0.f, 0.9f);
        Tdrive *= (1.f - cut);
    }
    const float biasF = std::clamp(m_setup.brakeBias, 0.3f, 0.8f);
    float TbrakeTot = brakeForceTarget * R;
    if (m_setup.absLevel > 0 && m_brake > 0.25f) {
        float kAbs = 0.25f * (std::fabs(srArr[0]) + std::fabs(srArr[1])
                            + std::fabs(srArr[2]) + std::fabs(srArr[3]));
        if (kAbs > 0.12f) {
            const float cut = std::clamp(0.12f * static_cast<float>(m_setup.absLevel) * kAbs, 0.f, 0.92f);
            TbrakeTot *= (1.f - cut);
        }
    }
    // Soft ABS-like brake relief near lock even at absLevel 0
    if (m_brake > 0.2f) {
        float kFront = 0.5f * (std::fabs(srArr[0]) + std::fabs(srArr[1]));
        if (kFront > 0.18f)
            TbrakeTot *= (1.f - std::clamp((kFront - 0.18f) / 0.4f, 0.f, 0.7f));
    }
    float TbrakeF = 0.5f * TbrakeTot * biasF;
    float TbrakeR = 0.5f * TbrakeTot * (1.f - biasF);
    // G15: brake thermal fade (per-wheel disc/pad)
    {
        const float TbCmd[4] = { TbrakeF, TbrakeF, TbrakeR, TbrakeR };
        for (int i = 0; i < 4; ++i) {
            m_brakes.update(fdt, i, TbCmd[i], m_wheelOmega[i], speed);
            m_state.brakeDiscTemp[i] = m_brakes.discTemp(i);
            m_state.brakeFade[i] = m_brakes.fade(i);
        }
        // Apply fade to commanded brake torque
        const float fFL = 1.f - m_brakes.fade(0);
        const float fFR = 1.f - m_brakes.fade(1);
        const float fRL = 1.f - m_brakes.fade(2);
        const float fRR = 1.f - m_brakes.fade(3);
        TbrakeF = TbrakeF * 0.5f * (fFL + fFR);
        TbrakeR = TbrakeR * 0.5f * (fRL + fRR);
    }
    // G13: LSD / open / locked differential on rear axle
    m_diff.update(fdt, Tdrive, m_wheelOmega[2], m_wheelOmega[3]);
    const float TdrvL = m_diff.getLeftTorque();
    const float TdrvR = m_diff.getRightTorque();
    // T_net = T_drive − T_brake − Fx·R  → I ω̇
    const float TbFL = m_brakes.effectiveTorque(0, 0.5f * TbrakeTot * biasF);
    const float TbFR = m_brakes.effectiveTorque(1, 0.5f * TbrakeTot * biasF);
    const float TbRL = m_brakes.effectiveTorque(2, 0.5f * TbrakeTot * (1.f - biasF));
    const float TbRR = m_brakes.effectiveTorque(3, 0.5f * TbrakeTot * (1.f - biasF));
    const float Tnet[4] = {
        -TbFL - m_tireFx[0] * R,
        -TbFR - m_tireFx[1] * R,
        TdrvL - TbRL - m_tireFx[2] * R,
        TdrvR - TbRR - m_tireFx[3] * R
    };
    const float omegaMax = std::fabs(omegaFree) + 120.f;
    for (int i = 0; i < 4; ++i) {
        m_wheelOmega[i] += (Tnet[i] / Iw) * fdt;
        m_wheelOmega[i] = std::clamp(m_wheelOmega[i], -omegaMax, omegaMax);
        if (speed < 0.25f && m_throttle < 0.02f && m_brake < 0.02f)
            m_wheelOmega[i] *= (1.f - 10.f * fdt);
    }
    const float omegaDriven = 0.5f * (m_wheelOmega[2] + m_wheelOmega[3]);
    m_rpm = std::max(800.0, static_cast<double>(std::fabs(omegaDriven)) * gearRatio * m_finalDrive
                                 * 60.0 / (2.0 * 3.14159265));

    // Deposit rubber when sliding hard (angle or ratio)
    const float slipMag = std::abs(saFL) + std::abs(saFR)
                        + std::abs(srArr[2]) + std::abs(srArr[3]);
    if (slipMag > 0.12f && speed > 5.f) {
        TrackSurface::instance().depositRubber(m_state.position, 0.0002f * fdt * slipMag, 1.5f);
    }

    // G6: tire lateral relaxation length (first-order lag on Fy)
    // σ ≈ 0.3–0.6 m race tires; dFy/dt = (V/σ) (Fy_ss − Fy)
    {
        const float sigma = std::max(0.15f, m_relaxLength);
        const float rate = std::clamp(speed / sigma, 0.f, 80.f); // 1/s
        const float fySS[4] = {
            m_tireFy[0] * dmgHand, m_tireFy[1] * dmgHand,
            m_tireFy[2] * dmgHand, m_tireFy[3] * dmgHand
        };
        for (int i = 0; i < 4; ++i) {
            const float a = std::clamp(rate * fdt, 0.f, 1.f);
            m_fyLag[i] = m_fyLag[i] * (1.f - a) + fySS[i] * a;
        }
    }
    const float FyF = m_fyLag[0] + m_fyLag[1];
    const float FyR = m_fyLag[2] + m_fyLag[3];
    float Fy = FyF + FyR;

    // Longitudinal force from tire model (friction-circle aware), plus aero drag
    float longForce = (m_tireFx[0] + m_tireFx[1] + m_tireFx[2] + m_tireFx[3]) * dmgHand - aeroDrag;
    if (speed < 1.0f && m_throttle > 0.05f)
        longForce = std::max(longForce, driveForceTarget * 0.35f);

    float ax = longForce / mass;
    float ay = Fy / mass;
    // Store for next-step load transfer (Milliken long + LLTD)
    m_lastAx = ax;
    m_lastAy = ay;

    // --- G6 transient yaw: Iz · ṙ = Mz − N_r · r ---
    // a_cg / b_cg already defined above (slip-angle / G7 block)
    // Yaw moment from lagged tire forces + aligning + longitudinal couple
    float mz = FyF * a_cg - FyR * b_cg
             + (fFL.aligningMoment + fFR.aligningMoment
              + fRL.aligningMoment + fRR.aligningMoment)
             + (Fx[0] - Fx[1] + Fx[2] - Fx[3]) * halfTrack * 0.5f;
    // Inertia: auto k² ≈ 0.5 (a²+b²) typical if not set
    float Iz = m_yawInertia;
    if (Iz < 50.f) {
        Iz = mass * 0.5f * (a_cg * a_cg + b_cg * b_cg);
        Iz = std::max(Iz, mass * 0.8f);
    }
    // Physical yaw damping N_r ≈ (C_F a² + C_R b²) / V  (linear tire approx)
    const float epsA = 0.015f;
    float CF = 0.f, CR = 0.f;
    {
        const float aF = 0.5f * (saFL + saFR);
        const float aR = 0.5f * (saRL + saRR);
        CF = std::fabs(FyF) / std::max(std::fabs(aF), epsA);
        CR = std::fabs(FyR) / std::max(std::fabs(aR), epsA);
        CF = std::clamp(CF, 1.e3f, 2.e5f);
        CR = std::clamp(CR, 1.e3f, 2.e5f);
    }
    const float Nr = (CF * a_cg * a_cg + CR * b_cg * b_cg) / std::max(speed, 2.0f);
    m_yawDampingNr = Nr;
    const float yawDot = (mz - Nr * static_cast<float>(m_yawRate)) / std::max(Iz, 50.f);
    m_yawAccel = yawDot;
    m_yawRate += yawDot * fdt;
    // Soft clamp only (no artificial exponential kill)
    m_yawRate = std::clamp(m_yawRate, -3.5, 3.5);

    m_state.heading += static_cast<float>(m_yawRate) * fdt;
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

    // --- G4: Milliken handling metrics (β, UG, static margin) ---
    {
        const float a = wb * (1.f - frontFrac); // CG → front axle
        const float b = wb * frontFrac;         // CG → rear axle
        const float alphaF = 0.5f * (saFL + saFR);
        const float alphaR = 0.5f * (saRL + saRR);
        const float FyF = (fFL.lateralForce + fFR.lateralForce) * dmgHand;
        const float FyR = (fRL.lateralForce + fRR.lateralForce) * dmgHand;
        // Axle cornering stiffness C ≈ Fy/α (N/rad); protect near α→0
        const float epsA = 0.012f; // ~0.7 deg
        float CF = FyF / (std::fabs(alphaF) > epsA ? alphaF : (alphaF >= 0.f ? epsA : -epsA));
        float CR = FyR / (std::fabs(alphaR) > epsA ? alphaR : (alphaR >= 0.f ? epsA : -epsA));
        // Stiffness should be negative in vehicle-dynamics sign (Fy opposes α);
        // use magnitudes for UG formula with Milliken force convention.
        CF = -std::fabs(CF);
        CR = -std::fabs(CR);
        const float CFmag = std::max(std::fabs(CF), 500.f);
        const float CRmag = std::max(std::fabs(CR), 500.f);
        // UG = 57.3 * (W/ℓ) * (a/|CR| − b/|CF|)  [deg/g]  (Milliken Ch.5)
        const float W = mass * 9.81f;
        float ugStiff = 57.3f * (W / std::max(wb, 0.5f)) * (a / CRmag - b / CFmag);
        // Kinematic: UG ≈ (δ − ℓ/R) / (Ay/g) when quasi-steady
        float ug = ugStiff;
        if (speed > 6.f && std::fabs(ay) > 0.8f) {
            const float Rpath = std::max(speed, 1.f)
                / std::max(std::fabs(static_cast<float>(m_yawRate)), 1e-3f);
            const float deltaAck = wb / Rpath;
            const float ugKin = (steerRad - deltaAck) / (ay / 9.81f) * (180.f / 3.14159265f);
            if (std::isfinite(ugKin) && std::fabs(ugKin) < 25.f)
                ug = 0.45f * ugStiff + 0.55f * ugKin;
        }
        ug = std::clamp(ug, -12.f, 12.f);
        // Low-pass for readable telemetry
        m_ugDegG = m_ugDegG * 0.92f + ug * 0.08f;
        // Static margin: (b|CR| − a|CF|) / ((|CF|+|CR|) ℓ)
        float sm = (b * CRmag - a * CFmag)
                 / std::max((CFmag + CRmag) * wb, 1.f);
        sm = std::clamp(sm, -0.5f, 0.5f);
        m_staticMargin = m_staticMargin * 0.92f + sm * 0.08f;

        m_state.sideslipBeta = m_beta;
        m_state.yawRate = static_cast<float>(m_yawRate);
        m_state.yawAccel = m_yawAccel;
        m_state.yawDampingNr = m_yawDampingNr;
        m_state.rollAngle = m_rollAngle;
        m_state.aquaplaneFactor = m_aquaplane;
        m_state.effectiveMu = m_effectiveMu;
        m_state.waterDepthMm = m_waterDepthMm;
        m_state.aeroBalanceFront = m_aeroBalFront;
        m_state.aeroDownforce = downforce;
        m_state.aeroDrag = aeroDrag;
        m_state.rideHeightFrontM = m_rhFront;
        m_state.rideHeightRearM = m_rhRear;
        // G18 quasi-static pitch (rad, nose-down positive under braking)
        {
            const float L = std::max(static_cast<float>(m_wheelBase), 1.f);
            const float h = std::max(m_setup.cgHeightM, 0.2f);
            float pitch = -m_lastAx * (h / L) * 0.15f; // soft geometric
            pitch = std::clamp(pitch, -0.08f, 0.08f);
            m_state.pitchAngle = pitch;
        }
        m_state.fltFront = m_fltFront;
        m_state.fltRear = m_fltRear;
        m_state.diffLockTorque = m_diff.getState().lockingTorque;
        m_state.diffSlip = m_diff.getState().slipRatio;
        m_state.understeerGradientDegG = m_ugDegG;
        m_state.staticMargin = m_staticMargin;
        m_state.lltdFront = m_lltdFront;
    }

    if (m_throttle > 0.05 && m_state.fuel > 0.f)
        m_state.fuel = std::max(0.f, m_state.fuel - static_cast<float>(m_throttle) * 0.00015f * fdt * 60.f);

    m_ffb.slipAngleFL = saFL;
    m_ffb.slipAngleFR = saFR;
    m_ffb.loadFL = loadFL;
    m_ffb.loadFR = loadFR;
    m_ffb.camberFL = m_suspension.corner(0).camberDeg * 0.017453292f;
    m_ffb.camberFR = m_suspension.corner(1).camberDeg * 0.017453292f;
    m_ffb.speedMs = m_state.speed;
    m_ffb.steerAngle = steerRad;
    m_ffb.aligningMomentNm = fFL.aligningMoment + fFR.aligningMoment;
}

void VehicleSimulator::updatePhysics(double dt) {
    if (!m_running || m_frozen) return;
    if (!std::isfinite(dt) || dt <= 0.0 || dt > 0.1) return;
    PROFILE_FRAME();
    {
        PROFILE_SUBSYSTEM(PhysicsProfiler::VehicleDynamics);
        PROFILE_SECTION("VehicleSimulator.updatePhysics");
        integrate(dt);
    }
    PROFILE_END_FRAME();
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
