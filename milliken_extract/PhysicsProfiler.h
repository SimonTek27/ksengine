#pragma once

/**
 * @file PhysicsProfiler.h
 * @brief Lightweight frame/section/subsystem profiler — Qt-free
 *
 * Accumulates wall time per named section and Subsystem enum.
 * Use PROFILE_SECTION("name") RAII or begin/end pairs.
 */

#include <array>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace ks {
namespace physics {

class PhysicsProfiler {
public:
    enum Subsystem {
        Engine = 0,
        Drivetrain,
        Differential,
        Brakes,
        Aero,
        Suspension,
        Tires,
        VehicleDynamics,
        DamageModel,
        WeatherPhysics,
        Total,
        kSubsystemCount
    };

    static const char* subsystemName(Subsystem s);

    static PhysicsProfiler& instance();

    void setEnabled(bool e) { m_enabled = e; }
    bool isEnabled() const { return m_enabled; }

    void beginFrame();
    void endFrame();
    void beginSection(const std::string& name);
    void endSection(const std::string& name);
    void beginSubsystem(Subsystem s);
    void endSubsystem(Subsystem s);

    /** Reset accumulators (keeps enable flag). */
    void reset();

    double frameTimeMs() const { return m_lastFrameMs; }
    double avgFrameTimeMs() const { return m_avgFrameMs; }
    int fps() const { return m_lastFrameMs > 0.01 ? static_cast<int>(1000.0 / m_lastFrameMs) : 0; }
    int frameCount() const { return m_frameCount; }

    double sectionMs(const std::string& name) const;
    double sectionAvgUs(const std::string& name) const;
    int sectionHits(const std::string& name) const;
    double subsystemMs(Subsystem s) const;
    double subsystemAvgUs(Subsystem s) const;
    int subsystemHits(Subsystem s) const;

    struct SectionReport {
        std::string name;
        double totalMs = 0;
        double avgUs = 0;
        int hits = 0;
    };
    std::vector<SectionReport> sectionsSorted() const;

    /** Print human-readable report to FILE (default stderr). */
    void report(FILE* out = stderr) const;

    void setGpuFrameTimeMs(double ms) { m_gpuFrameMs = ms; }
    void setGpuAvgFrameTimeMs(double ms) { m_gpuAvgFrameMs = ms; }
    double gpuFrameTimeMs() const { return m_gpuFrameMs; }
    double gpuAvgFrameTimeMs() const { return m_gpuAvgFrameMs; }
    int gpuFps() const {
        return m_gpuFrameMs > 0.01 ? static_cast<int>(1000.0 / m_gpuFrameMs) : 0;
    }

private:
    PhysicsProfiler() = default;
    using clock = std::chrono::steady_clock;

    struct Accum {
        double totalNs = 0;
        int hits = 0;
    };

    bool m_enabled = true;
    clock::time_point m_frameStart{};
    double m_lastFrameMs = 0.0;
    double m_avgFrameMs = 0.0;
    int m_frameCount = 0;
    double m_gpuFrameMs = 0.0;
    double m_gpuAvgFrameMs = 0.0;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, clock::time_point> m_sectionStart;
    std::unordered_map<std::string, Accum> m_sectionAccum;
    std::array<clock::time_point, kSubsystemCount> m_subStart{};
    std::array<bool, kSubsystemCount> m_subActive{};
    std::array<Accum, kSubsystemCount> m_subAccum{};
};

struct ProfilerSection {
    explicit ProfilerSection(const std::string& name) : m_name(name) {
        PhysicsProfiler::instance().beginSection(m_name);
    }
    ~ProfilerSection() { PhysicsProfiler::instance().endSection(m_name); }
    std::string m_name;
};

struct ProfilerSubsystem {
    explicit ProfilerSubsystem(PhysicsProfiler::Subsystem s) : m_s(s) {
        PhysicsProfiler::instance().beginSubsystem(m_s);
    }
    ~ProfilerSubsystem() { PhysicsProfiler::instance().endSubsystem(m_s); }
    PhysicsProfiler::Subsystem m_s;
};

#define PROFILE_FRAME() ::ks::physics::PhysicsProfiler::instance().beginFrame()
#define PROFILE_END_FRAME() ::ks::physics::PhysicsProfiler::instance().endFrame()
#define PROFILE_SECTION(name) ::ks::physics::ProfilerSection _ps_##__LINE__(name)
#define PROFILE_SUBSYSTEM(s) ::ks::physics::ProfilerSubsystem _pss_##__LINE__(s)

} // namespace physics
} // namespace ks
