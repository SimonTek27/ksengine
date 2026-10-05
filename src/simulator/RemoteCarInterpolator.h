#pragma once
/**
 * RemoteCarInterpolator — client-side snapshot buffer + interpolation (P0 multiplayer).
 *
 * Host sends car states ~20 Hz. Clients push each MSG_CAR_STATE into a short
 * ring buffer per carId and sample at render time with a fixed interpolation
 * delay (default 100 ms) so jitter does not teleport remote cars.
 */
#include "NetworkConfig.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <functional>

namespace ks {
namespace sim {

class RemoteCarInterpolator {
public:
    static constexpr int kBufferSize = 32;
    static constexpr double kDefaultDelaySec = 0.100; // 100 ms behind latest
    static constexpr double kMaxExtrapolateSec = 0.080;

    struct Snapshot {
        double t = 0.0;
        net::CarStateData state{};
    };

    void setDelay(double sec) {
        m_delay = std::clamp(sec, 0.0, 0.5);
    }
    double delay() const { return m_delay; }

    void clear() { m_buffers.clear(); }

    void remove(uint32_t carId) { m_buffers.erase(carId); }

    /** Push a network snapshot. `recvTime` is client local seconds (monotonic). */
    void push(uint32_t carId, const net::CarStateData& state, double recvTime) {
        auto& buf = m_buffers[carId];
        if (buf.size() >= static_cast<size_t>(kBufferSize))
            buf.erase(buf.begin());
        // Drop out-of-order / duplicate timestamps
        if (!buf.empty() && recvTime + 1e-6 < buf.back().t)
            return;
        Snapshot s;
        s.t = recvTime;
        s.state = state;
        s.state.carId = carId;
        buf.push_back(s);
        m_latestRecv = std::max(m_latestRecv, recvTime);
    }

    /** Sample all cars at renderTime - delay. Invokes fn(carId, interpolated). */
    void sampleAll(double renderTime, const std::function<void(uint32_t, const net::CarStateData&)>& fn) const {
        const double target = renderTime - m_delay;
        for (const auto& kv : m_buffers) {
            net::CarStateData out;
            if (sampleCar(kv.second, target, out))
                fn(kv.first, out);
        }
    }

    bool sample(uint32_t carId, double renderTime, net::CarStateData& out) const {
        auto it = m_buffers.find(carId);
        if (it == m_buffers.end()) return false;
        return sampleCar(it->second, renderTime - m_delay, out);
    }

    double latestRecvTime() const { return m_latestRecv; }
    size_t trackedCars() const { return m_buffers.size(); }

private:
    static float lerp(float a, float b, float t) { return a + (b - a) * t; }

    static float lerpAngle(float a, float b, float t) {
        float d = b - a;
        while (d > 3.14159265f) d -= 6.2831853f;
        while (d < -3.14159265f) d += 6.2831853f;
        return a + d * t;
    }

    static bool sampleCar(const std::vector<Snapshot>& buf, double target, net::CarStateData& out) {
        if (buf.empty()) return false;
        if (buf.size() == 1) {
            out = buf.front().state;
            return true;
        }

        // Before first sample: clamp
        if (target <= buf.front().t) {
            out = buf.front().state;
            return true;
        }

        // After last sample: limited extrapolation along velocity
        if (target >= buf.back().t) {
            const Snapshot& last = buf.back();
            const double dt = std::min(target - last.t, kMaxExtrapolateSec);
            out = last.state;
            out.posX += last.state.velX * static_cast<float>(dt);
            out.posY += last.state.velY * static_cast<float>(dt);
            out.posZ += last.state.velZ * static_cast<float>(dt);
            return true;
        }

        // Find segment [i, i+1] with t_i <= target <= t_{i+1}
        size_t i = 0;
        while (i + 1 < buf.size() && buf[i + 1].t < target)
            ++i;
        const Snapshot& a = buf[i];
        const Snapshot& b = buf[i + 1];
        const double span = b.t - a.t;
        const float u = (span > 1e-9) ? static_cast<float>((target - a.t) / span) : 0.f;

        out = a.state;
        out.carId = a.state.carId;
        out.posX = lerp(a.state.posX, b.state.posX, u);
        out.posY = lerp(a.state.posY, b.state.posY, u);
        out.posZ = lerp(a.state.posZ, b.state.posZ, u);
        out.rotX = lerpAngle(a.state.rotX, b.state.rotX, u);
        out.rotY = lerpAngle(a.state.rotY, b.state.rotY, u);
        out.rotZ = lerpAngle(a.state.rotZ, b.state.rotZ, u);
        out.velX = lerp(a.state.velX, b.state.velX, u);
        out.velY = lerp(a.state.velY, b.state.velY, u);
        out.velZ = lerp(a.state.velZ, b.state.velZ, u);
        out.speed = lerp(a.state.speed, b.state.speed, u);
        out.rpm = lerp(a.state.rpm, b.state.rpm, u);
        out.throttle = lerp(a.state.throttle, b.state.throttle, u);
        out.brake = lerp(a.state.brake, b.state.brake, u);
        out.steering = lerp(a.state.steering, b.state.steering, u);
        out.gear = (u < 0.5f) ? a.state.gear : b.state.gear;
        return true;
    }

    std::unordered_map<uint32_t, std::vector<Snapshot>> m_buffers;
    double m_delay = kDefaultDelaySec;
    double m_latestRecv = 0.0;
};

} // namespace sim
} // namespace ks
