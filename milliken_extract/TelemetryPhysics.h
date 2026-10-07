#pragma once
/** Telemetry sampling — Qt-free. */
#include <vector>
#include <string>
#include <cstdio>
#include <cstdint>

namespace ks {
namespace physics {

struct TelemetrySample {
    double time = 0;
    float speedKmh = 0;
    float rpm = 0;
    float throttle = 0;
    float brake = 0;
    float steer = 0;
    int gear = 0;
    float tireTemp[4] = {};
    float tireWear[4] = {};
};

class TelemetryPhysics {
public:
    void clear() { m_samples.clear(); }
    void push(const TelemetrySample& s) { m_samples.push_back(s); }
    const std::vector<TelemetrySample>& samples() const { return m_samples; }

    bool exportCsv(const std::string& path) const {
        FILE* f = std::fopen(path.c_str(), "w");
        if (!f) return false;
        std::fprintf(f, "time,speed,rpm,throttle,brake,steer,gear\n");
        for (const auto& s : m_samples) {
            std::fprintf(f, "%.4f,%.2f,%.0f,%.3f,%.3f,%.3f,%d\n",
                         s.time, s.speedKmh, s.rpm, s.throttle, s.brake, s.steer, s.gear);
        }
        std::fclose(f);
        return true;
    }

private:
    std::vector<TelemetrySample> m_samples;
};

} // namespace physics
} // namespace ks
