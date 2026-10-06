#include "PacejkaTireModel.h"
#include "../../Config/IniFile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// ---------------------------------------------------------------------------
// PacejkaTireModel itself now lives in KsTireModel.cpp: PacejkaTireModel is
// an alias of KsTireModel (see PacejkaTireModel.h), so all model methods are
// defined there. This translation unit keeps only the TireModelManager
// implementation (4-wheel set + INI round-trip), which tests and the editor
// still use through this header.
// ---------------------------------------------------------------------------

namespace ks {
namespace physics {

namespace {

// Applies A1..A13 / B1..B8 / C1..C13 entries of an INI group onto c.
// Missing keys leave the current value untouched.
void applyCoeffGroup(const ks::config::IniFile& ini, const std::string& group,
                     PacejkaTireModel::TireCoefficients& c) {
    float* a[13] = {&c.a1, &c.a2, &c.a3, &c.a4, &c.a5, &c.a6, &c.a7,
                    &c.a8, &c.a9, &c.a10, &c.a11, &c.a12, &c.a13};
    float* b[8] = {&c.b1, &c.b2, &c.b3, &c.b4, &c.b5, &c.b6, &c.b7, &c.b8};
    float* cc[13] = {&c.c1, &c.c2, &c.c3, &c.c4, &c.c5, &c.c6, &c.c7,
                     &c.c8, &c.c9, &c.c10, &c.c11, &c.c12, &c.c13};
    char key[8];
    for (int i = 0; i < 13; ++i) {
        std::snprintf(key, sizeof(key), "A%d", i + 1);
        if (ini.contains(group, key)) *a[i] = static_cast<float>(ini.getDouble(group, key, *a[i]));
    }
    for (int i = 0; i < 8; ++i) {
        std::snprintf(key, sizeof(key), "B%d", i + 1);
        if (ini.contains(group, key)) *b[i] = static_cast<float>(ini.getDouble(group, key, *b[i]));
    }
    for (int i = 0; i < 13; ++i) {
        std::snprintf(key, sizeof(key), "C%d", i + 1);
        if (ini.contains(group, key)) *cc[i] = static_cast<float>(ini.getDouble(group, key, *cc[i]));
    }
}

void saveCoeffGroup(ks::config::IniFile& ini, const std::string& group,
                    const PacejkaTireModel::TireCoefficients& c) {
    const float* a[13] = {&c.a1, &c.a2, &c.a3, &c.a4, &c.a5, &c.a6, &c.a7,
                          &c.a8, &c.a9, &c.a10, &c.a11, &c.a12, &c.a13};
    const float* b[8] = {&c.b1, &c.b2, &c.b3, &c.b4, &c.b5, &c.b6, &c.b7, &c.b8};
    const float* cc[13] = {&c.c1, &c.c2, &c.c3, &c.c4, &c.c5, &c.c6, &c.c7,
                           &c.c8, &c.c9, &c.c10, &c.c11, &c.c12, &c.c13};
    char key[8];
    for (int i = 0; i < 13; ++i) {
        std::snprintf(key, sizeof(key), "A%d", i + 1);
        ini.setNumber(group, key, *a[i]);
    }
    for (int i = 0; i < 8; ++i) {
        std::snprintf(key, sizeof(key), "B%d", i + 1);
        ini.setNumber(group, key, *b[i]);
    }
    for (int i = 0; i < 13; ++i) {
        std::snprintf(key, sizeof(key), "C%d", i + 1);
        ini.setNumber(group, key, *cc[i]);
    }
}

// Flat keys first, then optional legacy wrapper groups.
void applyFlatAndFallbacks(const ks::config::IniFile& ini,
                           PacejkaTireModel::TireCoefficients& c) {
    applyCoeffGroup(ini, "", c);
    if (ini.contains("PACEJKA", "A1") || ini.contains("PACEJKA", "B2"))
        applyCoeffGroup(ini, "PACEJKA", c);
    if (ini.contains("TYRE", "A1") || ini.contains("TYRE", "B2"))
        applyCoeffGroup(ini, "TYRE", c);
}

} // namespace

// ---------------------------------------------------------------------------
// TireModelManager
// ---------------------------------------------------------------------------

TireModelManager::TireModelManager() {
    auto slick = PacejkaTireModel::getSlickTireCoefficients();
    for (int i = 0; i < 4; ++i)
        m_models[i].setCoefficients(slick);
}

void TireModelManager::setTireCompound(int compound) {
    m_compound = compound;
    PacejkaTireModel::TireCoefficients c;
    if (compound == 1) c = PacejkaTireModel::getWetTireCoefficients();
    else if (compound == 2) c = PacejkaTireModel::getStreetTireCoefficients();
    else if (compound == 3) c = PacejkaTireModel::getRallyTireCoefficients();
    else c = PacejkaTireModel::getSlickTireCoefficients();
    for (int i = 0; i < 4; ++i) m_models[i].setCoefficients(c);
}

void TireModelManager::setTirePressure(float frontPressure, float rearPressure) {
    m_frontPressure = frontPressure;
    m_rearPressure = rearPressure;
}

void TireModelManager::loadFromIni(const std::string& path) {
    ks::config::IniFile ini;
    if (!ini.load(path)) return;

    const char* wheelGroups[4] = {"FL", "FR", "RL", "RR"};
    for (int w = 0; w < 4; ++w) {
        PacejkaTireModel::TireCoefficients c = m_models[w].getCoefficients();
        applyFlatAndFallbacks(ini, c);
        // Axle group, then wheel-specific override.
        applyCoeffGroup(ini, (w < 2) ? "FRONT" : "REAR", c);
        if (ini.contains(wheelGroups[w], "A1") || ini.contains(wheelGroups[w], "B2"))
            applyCoeffGroup(ini, wheelGroups[w], c);
        if (PacejkaTireModel::validateCoefficients(c, nullptr))
            m_models[w].setCoefficients(c);
    }
    m_frontPressure = static_cast<float>(ini.getDouble("FRONT", "PRESSURE", m_frontPressure));
    m_rearPressure = static_cast<float>(ini.getDouble("REAR", "PRESSURE", m_rearPressure));
}

void TireModelManager::saveToIni(const std::string& path) const {
    ks::config::IniFile ini;
    const char* wheelGroups[4] = {"FL", "FR", "RL", "RR"};
    for (int w = 0; w < 4; ++w)
        saveCoeffGroup(ini, wheelGroups[w], m_models[w].getCoefficients());
    ini.setNumber("FRONT", "PRESSURE", m_frontPressure);
    ini.setNumber("REAR", "PRESSURE", m_rearPressure);
    ini.save(path);
}

std::vector<float> TireModelManager::calculateGripCircle(int wheel, float normalForce) const {
    std::vector<float> r;
    r.push_back(m_models[wheel].calculatePeakLongitudinalGrip(normalForce));
    r.push_back(m_models[wheel].calculatePeakLateralGrip(normalForce));
    return r;
}

std::vector<float> TireModelManager::calculateSlipCurve(int wheel, float normalForce) const {
    std::vector<float> r;
    auto curve = m_models[wheel].generateLateralCurve(12.0f, normalForce, 25);
    for (auto& p : curve) r.push_back(p.second);
    return r;
}

float TireModelManager::estimateLapTimeImpact(int /*wheel*/, float slipAngleChange) const {
    return slipAngleChange * 0.01f;
}

} // namespace physics
} // namespace ks
