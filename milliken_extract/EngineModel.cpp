#include "EngineModel.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace ks {
namespace physics {

static float lerp(float a, float b, float t) { return a + (b - a) * t; }

EngineModel::EngineModel() {
    m_config = getV8_4000();
    rebuildCurveIfEmpty();
    m_state.rpm = m_config.idleRPM;
}

void EngineModel::rebuildCurveIfEmpty() {
    if (!m_config.torqueCurve.empty()) return;
    // Build simple curve from peak torque/power
    m_config.torqueCurve.clear();
    for (int rpm = 500; rpm <= static_cast<int>(m_config.maxRPM) + 500; rpm += 250) {
        TorquePoint p;
        p.rpm = static_cast<float>(rpm);
        float t = rpm / m_config.peakTorqueRPM;
        // asymmetric peak
        float shape = std::exp(-std::pow((rpm - m_config.peakTorqueRPM) / (m_config.maxRPM * 0.35f), 2.0f));
        p.torque = m_config.peakTorque * shape;
        if (rpm < m_config.idleRPM) p.torque *= 0.3f;
        p.power = p.torque * rpm * 2.0f * 3.14159265f / 60.0f / 1000.0f; // kW
        m_config.torqueCurve.push_back(p);
    }
    if (m_config.gearRatios.empty()) {
        const float ratios[] = {0, 3.5f, 2.2f, 1.6f, 1.25f, 1.0f, 0.85f};
        for (int g = 1; g <= 6; ++g) {
            GearRatio gr;
            gr.gear = g;
            gr.ratio = ratios[g];
            m_config.gearRatios.push_back(gr);
        }
    }
}

void EngineModel::setConfig(const EngineConfig& config) {
    m_config = config;
    rebuildCurveIfEmpty();
}

void EngineModel::reset() {
    m_state = EngineState{};
    m_state.rpm = m_config.idleRPM;
    m_currentBoost = 0.0f;
}

float EngineModel::interpolateTorqueCurve(float rpm) const {
    const auto& c = m_config.torqueCurve;
    if (c.empty()) return 0.0f;
    if (rpm <= c.front().rpm) return c.front().torque;
    if (rpm >= c.back().rpm) return c.back().torque;
    for (size_t i = 1; i < c.size(); ++i) {
        if (rpm <= c[i].rpm) {
            float t = (rpm - c[i - 1].rpm) / (c[i].rpm - c[i - 1].rpm + 1e-6f);
            return lerp(c[i - 1].torque, c[i].torque, t);
        }
    }
    return c.back().torque;
}

float EngineModel::interpolateBoostCurve(float rpm) const {
    const auto& c = m_config.turbo.boostCurve;
    if (c.empty()) return m_config.turbo.maxBoost * std::clamp(rpm / m_config.turbo.wastegateRPM, 0.0f, 1.0f);
    if (rpm <= c.front().first) return c.front().second;
    if (rpm >= c.back().first) return c.back().second;
    for (size_t i = 1; i < c.size(); ++i) {
        if (rpm <= c[i].first) {
            float t = (rpm - c[i - 1].first) / (c[i].first - c[i - 1].first + 1e-6f);
            return lerp(c[i - 1].second, c[i].second, t);
        }
    }
    return c.back().second;
}

float EngineModel::calculateTorque(float rpm) const { return interpolateTorqueCurve(rpm); }
float EngineModel::calculateTorqueAtRPM(float rpm) const { return calculateTorque(rpm); }
float EngineModel::calculatePower(float rpm) const {
    return calculateTorque(rpm) * rpm * 2.0f * 3.14159265f / 60.0f / 1000.0f;
}

float EngineModel::calculateTurboBoost(float rpm, float throttle) const {
    if (!m_config.turbo.enabled) return 0.0f;
    float target = interpolateBoostCurve(rpm) * std::clamp(throttle, 0.0f, 1.0f);
    return std::min(target, m_config.turbo.maxBoost);
}

float EngineModel::calculateTurboTorque(float baseTorque, float boost) const {
    // ~10% torque per 0.1 bar rough
    return baseTorque * (1.0f + boost * 0.8f);
}

float EngineModel::updateTurboLag(float targetBoost, float dt) {
    if (!m_config.turbo.enabled) {
        m_currentBoost = 0.0f;
        return 0.0f;
    }
    float lag = std::max(m_config.turbo.turboLag, 0.05f);
    float alpha = dt / (lag + dt);
    m_currentBoost += (targetBoost - m_currentBoost) * alpha;
    return m_currentBoost;
}

float EngineModel::calculateFuelConsumption(float rpm, float throttle) const {
    float power = calculatePower(rpm) * std::clamp(throttle, 0.0f, 1.0f);
    return calculateFuelFlow(power);
}

float EngineModel::calculateFuelFlow(float powerKw) const {
    // liters/s
    return std::max(0.0f, powerKw) * m_config.fuelConsumption / 3600.0f;
}

float EngineModel::calculateEngineBraking(float rpm) const {
    float t = m_config.coastTorque * (rpm / std::max(m_config.coastRPM, 1.0f));
    return std::min(t, m_config.coastTorque * 1.5f);
}

float EngineModel::calculateWheelRPM(float engineRPM, int gear) const {
    if (gear <= 0 || gear > static_cast<int>(m_config.gearRatios.size())) return 0.0f;
    float r = m_config.gearRatios[gear - 1].ratio * m_config.finalDrive;
    if (r < 1e-3f) return 0.0f;
    return engineRPM / r;
}

float EngineModel::calculateSpeed(float engineRPM, int gear) const {
    // assume 0.33m radius
    float wheelRpm = calculateWheelRPM(engineRPM, gear);
    float rps = wheelRpm / 60.0f;
    return rps * 2.0f * 3.14159265f * 0.33f;
}

int EngineModel::calculateOptimalGear(float speed, float /*rpm*/) const {
    int best = 1;
    float bestDiff = 1e9f;
    for (const auto& g : m_config.gearRatios) {
        if (g.gear <= 0) continue;
        float s = calculateSpeed(m_config.peakPowerRPM, g.gear);
        float d = std::abs(s - speed);
        if (d < bestDiff) { bestDiff = d; best = g.gear; }
    }
    return best;
}

std::vector<float> EngineModel::calculateSpeedsAtRPM(float rpm) const {
    std::vector<float> out;
    for (const auto& g : m_config.gearRatios) {
        if (g.gear > 0) out.push_back(calculateSpeed(rpm, g.gear));
    }
    return out;
}

bool EngineModel::isAtRevLimiter(float rpm) const {
    return rpm >= m_config.revLimiter;
}

float EngineModel::calculateRevLimiterTorque(float rpm) const {
    if (rpm < m_config.revLimiter) return 1.0f;
    float over = rpm - m_config.revLimiter;
    return std::clamp(1.0f - over / 200.0f, 0.0f, 1.0f);
}

void EngineModel::update(float dt, float throttle, float load) {
    dt = std::clamp(dt, 1e-4f, 0.05f);
    throttle = std::clamp(throttle, 0.0f, 1.0f);
    m_state.throttle = throttle;

    float targetBoost = calculateTurboBoost(m_state.rpm, throttle);
    float boost = updateTurboLag(targetBoost, dt);
    m_state.boost = boost;

    float baseTq = calculateTorque(m_state.rpm);
    float tq = calculateTurboTorque(baseTq, boost) * throttle;
    if (throttle < 0.05f) {
        tq = -calculateEngineBraking(m_state.rpm);
    }
    tq *= calculateRevLimiterTorque(m_state.rpm);
    // Thermal derate
    if (m_state.waterTemp > 110.0f) {
        m_state.thermalDerate = std::clamp(1.0f - (m_state.waterTemp - 110.0f) / 20.0f, 0.5f, 1.0f);
    } else {
        m_state.thermalDerate = 1.0f;
    }
    tq *= m_state.thermalDerate;
    m_state.torque = tq;
    m_state.power = tq * m_state.rpm * 2.0f * 3.14159265f / 60.0f / 1000.0f;

    // RPM dynamics: torque - load on inertia
    float alpha = (tq - load) / std::max(m_config.engineInertia, 0.05f);
    float omega = rpmToRadPerSec(m_state.rpm);
    omega += alpha * dt;
    m_state.rpm = std::clamp(radPerSecToRPM(omega), m_config.idleRPM * 0.5f, m_config.maxRPM * 1.05f);
    if (m_state.rpm < m_config.idleRPM && throttle < 0.05f)
        m_state.rpm = m_config.idleRPM;

    // Temps
    m_state.waterTemp += (throttle * 30.0f - (m_state.waterTemp - 40.0f) * 0.05f) * dt;
    m_state.oilTemp += (throttle * 25.0f - (m_state.oilTemp - 50.0f) * 0.03f) * dt;
    m_state.fuelFlow = calculateFuelConsumption(m_state.rpm, throttle);
}

void EngineModel::loadFromIni(const std::string& path) {
    std::ifstream f(path);
    if (!f) return;
    std::string line, section;
    auto trim = [](std::string s) {
        while (!s.empty() && (s.back()=='\r'||s.back()==' '||s.back()=='\t')) s.pop_back();
        size_t i = 0; while (i < s.size() && (s[i]==' '||s[i]=='\t')) ++i;
        return s.substr(i);
    };
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0]==';' || line[0]=='#') continue;
        if (line.front()=='[' && line.back()==']') {
            section = line.substr(1, line.size()-2);
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq+1));
        for (char& c : key) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (key == "MAX_RPM" || key == "LIMITER") m_config.maxRPM = m_config.revLimiter = std::stof(val);
        else if (key == "IDLE") m_config.idleRPM = std::stof(val);
        else if (key == "POWER" || key == "PEAK_POWER") m_config.peakPower = std::stof(val);
        else if (key == "TORQUE" || key == "PEAK_TORQUE") m_config.peakTorque = std::stof(val);
    }
    m_config.torqueCurve.clear();
    rebuildCurveIfEmpty();
}

EngineModel::EngineConfig EngineModel::getInline4_2000() {
    EngineConfig c; c.peakPower=150; c.peakTorque=220; c.peakTorqueRPM=4500; c.peakPowerRPM=7000; c.maxRPM=7800; c.revLimiter=7800; return c;
}
EngineModel::EngineConfig EngineModel::getV6_3000() {
    EngineConfig c; c.peakPower=250; c.peakTorque=320; c.peakTorqueRPM=4200; c.peakPowerRPM=6800; c.maxRPM=7500; return c;
}
EngineModel::EngineConfig EngineModel::getV8_4000() {
    EngineConfig c; c.peakPower=350; c.peakTorque=450; c.peakTorqueRPM=4000; c.peakPowerRPM=6500; c.maxRPM=7500; return c;
}
EngineModel::EngineConfig EngineModel::getV10_5000() {
    EngineConfig c; c.peakPower=450; c.peakTorque=500; c.peakTorqueRPM=5500; c.peakPowerRPM=8500; c.maxRPM=9000; c.revLimiter=9000; return c;
}
EngineModel::EngineConfig EngineModel::getV12_6000() {
    EngineConfig c; c.peakPower=550; c.peakTorque=600; c.peakTorqueRPM=5000; c.peakPowerRPM=8000; c.maxRPM=8500; return c;
}
EngineModel::EngineConfig EngineModel::getRotary_1300() {
    EngineConfig c; c.peakPower=180; c.peakTorque=220; c.peakTorqueRPM=5500; c.peakPowerRPM=8000; c.maxRPM=9000; return c;
}
EngineModel::EngineConfig EngineModel::getElectric() {
    EngineConfig c; c.peakPower=300; c.peakTorque=500; c.peakTorqueRPM=1000; c.peakPowerRPM=8000; c.maxRPM=12000; c.idleRPM=0; return c;
}

std::vector<EngineModel::TorquePoint> EngineModel::loadPowerLut(const std::string& /*path*/) { return {}; }
bool EngineModel::savePowerLut(const std::vector<TorquePoint>& /*curve*/, const std::string& /*path*/) { return false; }
std::vector<EngineModel::TorquePoint> EngineModel::interpolateCurve(const std::vector<TorquePoint>& points, int targetPoints) {
    return points; // simplified
}
bool EngineModel::validateConfig(const EngineConfig& config, std::string* error) {
    if (config.maxRPM <= config.idleRPM) {
        if (error) *error = "maxRPM must be > idleRPM";
        return false;
    }
    return true;
}

EngineModelManager::EngineModelManager() = default;
void EngineModelManager::loadFromIni(const std::string& p) { m_model.loadFromIni(p); }
void EngineModelManager::loadPowerLut(const std::string& /*p*/) {}
void EngineModelManager::saveToIni(const std::string& /*p*/) const {}
void EngineModelManager::savePowerLut(const std::string& /*p*/) const {}
void EngineModelManager::update(float dt, float throttle, float load) { m_model.update(dt, throttle, load); }
float EngineModelManager::getMaxPower() const { return m_model.getConfig().peakPower; }
float EngineModelManager::getMaxTorque() const { return m_model.getConfig().peakTorque; }
float EngineModelManager::get0100Time() const { return 4.0f; }
float EngineModelManager::getTopSpeed() const { return 280.0f; }
std::vector<float> EngineModelManager::getTorqueCurve() const {
    std::vector<float> v;
    for (auto& p : m_model.getConfig().torqueCurve) v.push_back(p.torque);
    return v;
}
std::vector<float> EngineModelManager::getPowerCurve() const {
    std::vector<float> v;
    for (auto& p : m_model.getConfig().torqueCurve) v.push_back(p.power);
    return v;
}
std::map<std::string, std::pair<float, float>> EngineModelManager::compareEngines(const EngineModelManager& other) const {
    std::map<std::string, std::pair<float, float>> m;
    m["power"] = {getMaxPower(), other.getMaxPower()};
    m["torque"] = {getMaxTorque(), other.getMaxTorque()};
    return m;
}

} // namespace physics
} // namespace ks
