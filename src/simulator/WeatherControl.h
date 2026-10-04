#pragma once
#include <string>
#include <cctype>
#include <cmath>
#include <algorithm>

namespace ks {
namespace sim {

struct WeatherPreset {
    std::string id = "dry";
    std::string name = "Dry";
    float ambientC = 22.f;
    float trackC = 28.f;
    float wetness = 0.f;
    float rain = 0.f;
    float windMs = 0.f;
    float windDirDeg = 0.f;
    float fog = 0.f;
    float cloud = 0.2f;
};

inline WeatherPreset presetDry() {
    WeatherPreset p; p.id = "dry"; p.name = "Dry"; p.ambientC = 26.f; p.trackC = 32.f; return p;
}
inline WeatherPreset presetDamp() {
    WeatherPreset p; p.id = "damp"; p.name = "Damp"; p.ambientC = 18.f; p.trackC = 20.f;
    p.wetness = 0.35f; p.cloud = 0.7f; p.rain = 0.05f; return p;
}
inline WeatherPreset presetWet() {
    WeatherPreset p; p.id = "wet"; p.name = "Wet"; p.ambientC = 14.f; p.trackC = 15.f;
    p.wetness = 0.85f; p.rain = 0.6f; p.cloud = 0.95f; p.fog = 0.15f; return p;
}
inline WeatherPreset presetByName(std::string name) {
    for (char& c : name) c = (char)tolower((unsigned char)c);
    if (name.find("wet") != std::string::npos) return presetWet();
    if (name.find("damp") != std::string::npos) return presetDamp();
    return presetDry();
}

struct TimeOfDayState {
    float hours = 12.f;
    void setHours(float h) { hours = std::fmod(std::max(0.f, h), 24.f); }
    float daylight() const {
        if (hours < 5.f || hours > 21.f) return 0.05f;
        if (hours >= 6.f && hours <= 18.f) {
            float t = (hours - 6.f) / 12.f;
            return 0.3f + 0.7f * std::sin(t * 3.14159265f);
        }
        return 0.15f;
    }
};

class WeatherControl {
public:
    WeatherPreset& weather() { return m_weather; }
    const WeatherPreset& weather() const { return m_weather; }
    TimeOfDayState& time() { return m_time; }
    const TimeOfDayState& time() const { return m_time; }
    void applyPreset(const std::string& name) { m_weather = presetByName(name); }
    void setTimeOfDay(float hours) { m_time.setHours(hours); }
    void tick(float dt, bool advanceTime = false, float hoursPerSecond = 0.f) {
        if (advanceTime && hoursPerSecond != 0.f)
            m_time.setHours(m_time.hours + hoursPerSecond * dt);
    }
private:
    WeatherPreset m_weather = presetDry();
    TimeOfDayState m_time;
};

} // namespace sim
} // namespace ks
