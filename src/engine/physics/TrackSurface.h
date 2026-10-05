#pragma once

/**
 * @file TrackSurface.h
 * @brief Spatial grip / wet / rubber / marbles grid — Qt-free (Sprint 2)
 */

#include "PhysicsCoreTypes.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>
#include <string>
#include <map>
#include <cctype>

namespace ks {
namespace physics {

struct SurfaceSample {
    float grip = 1.0f;
    float wetness = 0.0f;
    float marbles = 0.0f;
    float temperature = 25.0f;
    float rubber = 0.0f;
};

class TrackSurface {
public:
    static TrackSurface& instance() {
        static TrackSurface s;
        return s;
    }

    void configure(int res = 256, float worldSize = 2000.0f) {
        m_res = std::clamp(res, 32, 512);
        m_worldSize = worldSize;
        const int n = m_res * m_res;
        m_rubber.assign(n, 0.0f);
        m_wet.assign(n, m_globalWet);
        m_marbles.assign(n, 0.0f);
        m_temp.assign(n, m_baseTemp);
    }

    void setBaseGrip(float g) { m_baseGrip = std::clamp(g, 0.5f, 1.5f); }
    void setWetness(float w) {
        m_globalWet = std::clamp(w, 0.0f, 1.0f);
        std::fill(m_wet.begin(), m_wet.end(), m_globalWet);
    }
    void setRubber(float r) { m_globalRubber = std::clamp(r, 0.0f, 1.0f); }
    void setTemperature(float t) {
        m_baseTemp = t;
        std::fill(m_temp.begin(), m_temp.end(), t);
    }

    void setMaterialGrip(const std::string& key, float friction) {
        std::string k = key;
        for (char& c : k) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        m_materials[k] = std::clamp(friction, 0.05f, 2.0f);
    }
    float materialGrip(const std::string& key) const {
        std::string k = key;
        for (char& c : k) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
        auto it = m_materials.find(k);
        return it != m_materials.end() ? it->second : m_baseGrip;
    }
    void setOffTrackGrip(float g) { m_offTrackGrip = std::clamp(g, 0.05f, 1.0f); }
    void setKerbGrip(float g) { m_kerbGrip = std::clamp(g, 0.2f, 1.5f); }
    float offTrackGrip() const { return m_offTrackGrip; }
    float kerbGrip() const { return m_kerbGrip; }

    SurfaceSample sample(const PhysVec3& worldPos) const {
        SurfaceSample s;
        const int idx = cellIndex(worldPos);
        const float rubber = idx >= 0 ? m_rubber[static_cast<size_t>(idx)] : m_globalRubber;
        const float wet = idx >= 0 ? m_wet[static_cast<size_t>(idx)] : m_globalWet;
        const float marbles = idx >= 0 ? m_marbles[static_cast<size_t>(idx)] : 0.0f;
        const float temp = idx >= 0 ? m_temp[static_cast<size_t>(idx)] : m_baseTemp;
        s.rubber = std::clamp(rubber + m_globalRubber, 0.0f, 1.0f);
        s.wetness = wet;
        s.marbles = marbles;
        s.temperature = temp;
        float base = m_baseGrip;
        if (idx < 0)
            base = m_offTrackGrip;
        s.grip = base * (1.0f - wet * 0.45f) * (1.0f + s.rubber * 0.12f) *
                 (1.0f - marbles * 0.15f);
        s.grip = std::clamp(s.grip, 0.05f, 2.0f);
        return s;
    }

    float getGrip(const PhysVec3& p) const { return sample(p).grip; }

    void depositRubber(const PhysVec3& p, float amount, float radius = 2.0f) {
        eachInRadius(p, radius, [&](int idx, float w) {
            m_rubber[static_cast<size_t>(idx)] =
                std::clamp(m_rubber[static_cast<size_t>(idx)] + amount * w, 0.0f, 1.0f);
        });
    }

    void addMarbles(const PhysVec3& p, float amount, float radius = 3.0f) {
        eachInRadius(p, radius, [&](int idx, float w) {
            m_marbles[static_cast<size_t>(idx)] =
                std::clamp(m_marbles[static_cast<size_t>(idx)] + amount * w, 0.0f, 1.0f);
        });
    }

    void syncFromWeather(float trackWetness, float trackTemp) {
        setWetness(trackWetness);
        setTemperature(trackTemp);
    }

    TrackSurface() { configure(); }

private:
    int cellIndex(const PhysVec3& p) const {
        if (m_res <= 0) return -1;
        const float half = m_worldSize * 0.5f;
        const float u = (p.x + half) / m_worldSize;
        const float v = (p.z + half) / m_worldSize;
        if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) return -1;
        const int ix = std::clamp(static_cast<int>(u * m_res), 0, m_res - 1);
        const int iy = std::clamp(static_cast<int>(v * m_res), 0, m_res - 1);
        return iy * m_res + ix;
    }

    void eachInRadius(const PhysVec3& p, float radius, const std::function<void(int, float)>& fn) {
        if (m_res <= 0) return;
        const float cell = m_worldSize / static_cast<float>(m_res);
        const int rCells = std::max(1, static_cast<int>(radius / cell) + 1);
        const int cx = cellIndex(p);
        if (cx < 0) return;
        const int cx0 = cx % m_res;
        const int cy0 = cx / m_res;
        for (int dy = -rCells; dy <= rCells; ++dy) {
            for (int dx = -rCells; dx <= rCells; ++dx) {
                const int ix = cx0 + dx;
                const int iy = cy0 + dy;
                if (ix < 0 || iy < 0 || ix >= m_res || iy >= m_res) continue;
                const int idx = iy * m_res + ix;
                const float wx = (static_cast<float>(ix) + 0.5f) / m_res * m_worldSize - m_worldSize * 0.5f;
                const float wz = (static_cast<float>(iy) + 0.5f) / m_res * m_worldSize - m_worldSize * 0.5f;
                const float d = std::sqrt((wx - p.x) * (wx - p.x) + (wz - p.z) * (wz - p.z));
                if (d > radius) continue;
                const float w = 1.0f - d / radius;
                fn(idx, w);
            }
        }
    }

    int m_res = 256;
    float m_worldSize = 2000.0f;
    float m_baseGrip = 1.0f;
    float m_globalWet = 0.0f;
    float m_globalRubber = 0.0f;
    float m_baseTemp = 25.0f;
    float m_offTrackGrip = 0.55f;
    float m_kerbGrip = 0.85f;
    std::map<std::string, float> m_materials;
    std::vector<float> m_rubber, m_wet, m_marbles, m_temp;
};

} // namespace physics
} // namespace ks
