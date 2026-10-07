#pragma once
#include "../EngineModule.h"
#include <QVector3D>
#include <QVector>
#include <QHash>
#include <QtGlobal>
#include <cmath>

namespace ks::engine::physics {

struct SurfaceSample {
    float grip = 1.0f;
    float wetness = 0.0f;
    float marbles = 0.0f;
    float temperature = 25.0f;
    float rubber = 0.0f;
};

class TrackSurface : public EngineModule {
public:
    static TrackSurface& instance(){ static TrackSurface s; return s; }
    QString moduleName() const override { return "TrackSurface"; }
    QString moduleId() const override { return "ks.tracksurface"; }
    bool initialize() override {
        configure(256, 2000.0f);
        m_initialized = true;
        return true;
    }
    void shutdown() override { m_initialized = false; }

    void configure(int res = 256, float worldSize = 2000.0f) {
        m_res = qBound(32, res, 512);
        m_worldSize = worldSize;
        int n = m_res * m_res;
        m_rubber.fill(0.0f, n);
        m_wet.fill(0.0f, n);
        m_marbles.fill(0.0f, n);
        m_temp.fill(25.0f, n);
    }
    void setBaseGrip(float g){ m_baseGrip = qBound(0.5f, g, 1.5f); }
    void setWetness(float w){
        m_globalWet = qBound(0.0f, w, 1.0f);
        std::fill(m_wet.begin(), m_wet.end(), m_globalWet);
    }
    void setRubber(float r){
        m_globalRubber = qBound(0.0f, r, 1.0f);
    }
    void setTemperature(float t){ m_baseTemp = t; std::fill(m_temp.begin(), m_temp.end(), t); }

    SurfaceSample sample(const QVector3D& worldPos) const {
        SurfaceSample s;
        int idx = cellIndex(worldPos);
        float rubber = idx >= 0 ? m_rubber[idx] : m_globalRubber;
        float wet = idx >= 0 ? m_wet[idx] : m_globalWet;
        float marbles = idx >= 0 ? m_marbles[idx] : 0.0f;
        float temp = idx >= 0 ? m_temp[idx] : m_baseTemp;
        s.rubber = qBound(0.0f, rubber + m_globalRubber, 1.0f);
        s.wetness = wet;
        s.marbles = marbles;
        s.temperature = temp;
        s.grip = m_baseGrip * (1.0f - wet * 0.45f) * (1.0f + s.rubber * 0.12f) * (1.0f - marbles * 0.15f);
        return s;
    }
    float getGrip(const QVector3D& p) const { return sample(p).grip; }

    void depositRubber(const QVector3D& p, float amount, float radius = 2.0f) {
        eachInRadius(p, radius, [&](int idx, float w){ m_rubber[idx] = qBound(0.0f, m_rubber[idx] + amount * w, 1.0f); });
    }
    void addMarbles(const QVector3D& p, float amount, float radius = 3.0f) {
        eachInRadius(p, radius, [&](int idx, float w){ m_marbles[idx] = qBound(0.0f, m_marbles[idx] + amount * w, 1.0f); });
    }
    void dryCell(const QVector3D& p, float amount, float radius = 4.0f) {
        eachInRadius(p, radius, [&](int idx, float w){ m_wet[idx] = qBound(0.0f, m_wet[idx] - amount * w, 1.0f); });
    }
    void evolve(float dt, float rainIntensity = 0.0f, float sunDry = 0.01f) {
        if (rainIntensity > 0.001f) {
            for (auto& w : m_wet) w = qBound(0.0f, w + rainIntensity * dt * 0.05f, 1.0f);
            for (auto& r : m_rubber) r = qBound(0.0f, r - rainIntensity * dt * 0.01f, 1.0f);
        } else if (sunDry > 0.0f) {
            for (auto& w : m_wet) w = qBound(0.0f, w - sunDry * dt * 0.02f, 1.0f);
        }
    }
    void reset(){ m_rubber.fill(0.0f); m_wet.fill(m_globalWet); m_marbles.fill(0.0f); m_temp.fill(m_baseTemp); }

private:
    int cellIndex(const QVector3D& p) const {
        if (m_res <= 0 || m_rubber.size() != m_res * m_res) return -1;
        float half = m_worldSize * 0.5f;
        float fx = (p.x() + half) / m_worldSize;
        float fz = (p.z() + half) / m_worldSize;
        if (fx < 0 || fx >= 1 || fz < 0 || fz >= 1) return -1;
        int cx = int(fx * m_res), cz = int(fz * m_res);
        return cz * m_res + cx;
    }
    template<typename Fn>
    void eachInRadius(const QVector3D& p, float radius, Fn fn) const {
        if (m_res <= 0) return;
        float half = m_worldSize * 0.5f;
        float cell = m_worldSize / float(m_res);
        int rc = int(std::ceil(radius / cell));
        float fx = (p.x() + half) / m_worldSize;
        float fz = (p.z() + half) / m_worldSize;
        int ccx = int(fx * m_res), ccz = int(fz * m_res);
        TrackSurface* self = const_cast<TrackSurface*>(this);
        for (int dz = -rc; dz <= rc; ++dz) for (int dx = -rc; dx <= rc; ++dx) {
            int cx = ccx + dx, cz = ccz + dz;
            if (cx < 0 || cz < 0 || cx >= m_res || cz >= m_res) continue;
            float dist = std::sqrt(float(dx*dx + dz*dz)) * cell;
            if (dist > radius) continue;
            float w = 1.0f - dist / radius;
            int idx = cz * m_res + cx;
            fn(idx, w);
            Q_UNUSED(self);
        }
    }
    int m_res = 256;
    float m_worldSize = 2000.0f;
    float m_baseGrip = 1.0f, m_globalWet = 0, m_globalRubber = 0, m_baseTemp = 25.0f;
    QVector<float> m_rubber, m_wet, m_marbles, m_temp;
};

} // namespace ks::engine::physics
