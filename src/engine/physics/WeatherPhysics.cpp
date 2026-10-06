#include "WeatherPhysics.h"

#include <algorithm>

namespace ks {
namespace physics {

void WeatherSimulator::reset() {
    m_weather = WeatherState{};
    recomputeEffects();
    if (onWeatherChanged) onWeatherChanged(m_weather);
}

void WeatherSimulator::recomputeEffects() {
    m_effects.aquaplaningRisk = m_weather.aquaplaningRisk();
    m_effects.trackGripReduction = m_weather.gripReduction();
    m_effects.airDensity = m_weather.airDensity;
    // Wind force direction from windDirection degrees (0 = North = -Z)
    float rad = m_weather.windDirection * Constants::DEG_TO_RAD;
    float w = m_weather.windSpeed;
    m_effects.windForceX = std::sin(rad) * w;
    m_effects.windForceY = 0.0f;
    m_effects.windForceZ = -std::cos(rad) * w;
}

void WeatherSimulator::update(float dt) {
    if (!m_running || dt <= 0.0f) return;
    dt *= m_timeMultiplier;
    dt = std::clamp(dt, 1e-4f, 1.0f);

    // Rain adds wetness; dry slowly
    if (m_weather.rainIntensity > 0.05f) {
        m_weather.trackWetness = std::clamp(
            m_weather.trackWetness + m_weather.rainIntensity * 0.00015f * dt, 0.0f, 1.0f);
    } else {
        m_weather.trackWetness = std::max(0.0f, m_weather.trackWetness - 0.002f * dt);
    }

    // Ambient influences track temp slowly
    m_weather.trackTemp += (m_weather.ambientTemp + 5.0f - m_weather.trackTemp) * 0.01f * dt;

    // Density vs temp (ideal gas rough around ISA)
    m_weather.airDensity = Constants::DEFAULT_AIR_DENSITY *
        (288.15f / (273.15f + m_weather.ambientTemp));

    recomputeEffects();
    if (onWeatherChanged) onWeatherChanged(m_weather);
}

void WeatherSimulator::applyDry() {
    m_weather.rainIntensity = 0.0f;
    m_weather.trackWetness = 0.0f;
    m_weather.humidity = 0.4f;
    m_weather.cloudCover = 0.1f;
    recomputeEffects();
    if (onWeatherChanged) onWeatherChanged(m_weather);
}

void WeatherSimulator::applyLightRain() {
    m_weather.rainIntensity = 2.0f;
    m_weather.trackWetness = 0.25f;
    m_weather.humidity = 0.8f;
    m_weather.cloudCover = 0.7f;
    recomputeEffects();
    if (onWeatherChanged) onWeatherChanged(m_weather);
}

void WeatherSimulator::applyHeavyRain() {
    m_weather.rainIntensity = 12.0f;
    m_weather.trackWetness = 0.7f;
    m_weather.humidity = 0.95f;
    m_weather.cloudCover = 1.0f;
    recomputeEffects();
    if (onWeatherChanged) onWeatherChanged(m_weather);
}

void WeatherSimulator::applyWetTrack() {
    m_weather.rainIntensity = 0.0f;
    m_weather.trackWetness = 0.5f;
    recomputeEffects();
    if (onWeatherChanged) onWeatherChanged(m_weather);
}

void WeatherSimulator::applyCold() {
    m_weather.ambientTemp = 5.0f;
    m_weather.trackTemp = 8.0f;
    recomputeEffects();
    if (onWeatherChanged) onWeatherChanged(m_weather);
}

} // namespace physics
} // namespace ks
