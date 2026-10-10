#pragma once
#include "KsExport.h"

/**
 * @file WeatherPhysics.h
 * @brief Weather state evolution and track grip effects — Qt-free
 */

#include "PhysicsCoreTypes.h"

#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace ks {
namespace physics {

struct WeatherEffects {
    float aquaplaningRisk = 0.0f;
    float trackGripReduction = 0.0f;
    float windForceX = 0.0f;
    float windForceY = 0.0f;
    float windForceZ = 0.0f;
    float airDensity = Constants::DEFAULT_AIR_DENSITY;
};

/**
 * Lightweight weather simulator: evolves WeatherState over time and
 * exposes grip / wind / density for the vehicle loop.
 */
class KSENGINE_API WeatherSimulator {
public:
    WeatherSimulator() = default;

    void setState(const WeatherState& s) { m_weather = s; recomputeEffects(); }
    const WeatherState& state() const { return m_weather; }
    WeatherState& state() { return m_weather; }
    const WeatherEffects& effects() const { return m_effects; }

    void setTimeMultiplier(float m) { m_timeMultiplier = m; }
    float timeMultiplier() const { return m_timeMultiplier; }

    void start() { m_running = true; if (onStarted) onStarted(); }
    void stop() { m_running = false; if (onStopped) onStopped(); }
    void reset();
    bool isRunning() const { return m_running; }

    /** Advance weather (rain accumulation, track drying, wind). */
    void update(float dt);

    // Presets
    void applyDry();
    void applyLightRain();
    void applyHeavyRain();
    void applyWetTrack();
    void applyCold();

    std::function<void(const WeatherState&)> onWeatherChanged;
    std::function<void()> onStarted;
    std::function<void()> onStopped;

private:
    void recomputeEffects();

    WeatherState m_weather;
    WeatherEffects m_effects;
    bool m_running = false;
    float m_timeMultiplier = 1.0f;
};

} // namespace physics
} // namespace ks
