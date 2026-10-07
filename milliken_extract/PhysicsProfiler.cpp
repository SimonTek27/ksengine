#include "PhysicsProfiler.h"

#include <algorithm>

namespace ks {
namespace physics {

const char* PhysicsProfiler::subsystemName(Subsystem s)
{
    switch (s) {
    case Engine: return "Engine";
    case Drivetrain: return "Drivetrain";
    case Differential: return "Differential";
    case Brakes: return "Brakes";
    case Aero: return "Aero";
    case Suspension: return "Suspension";
    case Tires: return "Tires";
    case VehicleDynamics: return "VehicleDynamics";
    case DamageModel: return "DamageModel";
    case WeatherPhysics: return "WeatherPhysics";
    case Total: return "Total";
    default: return "Unknown";
    }
}

PhysicsProfiler& PhysicsProfiler::instance()
{
    static PhysicsProfiler s;
    return s;
}

void PhysicsProfiler::reset()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sectionStart.clear();
    m_sectionAccum.clear();
    m_subActive.fill(false);
    for (auto& a : m_subAccum) a = Accum{};
    m_frameCount = 0;
    m_lastFrameMs = 0;
    m_avgFrameMs = 0;
}

void PhysicsProfiler::beginFrame()
{
    if (!m_enabled) return;
    m_frameStart = clock::now();
}

void PhysicsProfiler::endFrame()
{
    if (!m_enabled) return;
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - m_frameStart).count();
    m_lastFrameMs = ns / 1e6;
    ++m_frameCount;
    const double a = 0.05;
    m_avgFrameMs = (m_frameCount == 1) ? m_lastFrameMs : (m_avgFrameMs * (1.0 - a) + m_lastFrameMs * a);

    std::lock_guard<std::mutex> lock(m_mutex);
    auto& tot = m_subAccum[static_cast<int>(Total)];
    tot.totalNs += static_cast<double>(ns);
    ++tot.hits;
}

void PhysicsProfiler::beginSection(const std::string& name)
{
    if (!m_enabled) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_sectionStart[name] = clock::now();
}

void PhysicsProfiler::endSection(const std::string& name)
{
    if (!m_enabled) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sectionStart.find(name);
    if (it == m_sectionStart.end()) return;
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - it->second).count();
    m_sectionStart.erase(it);
    auto& a = m_sectionAccum[name];
    a.totalNs += static_cast<double>(ns);
    ++a.hits;
}

void PhysicsProfiler::beginSubsystem(Subsystem s)
{
    if (!m_enabled) return;
    const int i = static_cast<int>(s);
    if (i < 0 || i >= kSubsystemCount) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_subStart[static_cast<size_t>(i)] = clock::now();
    m_subActive[static_cast<size_t>(i)] = true;
}

void PhysicsProfiler::endSubsystem(Subsystem s)
{
    if (!m_enabled) return;
    const int i = static_cast<int>(s);
    if (i < 0 || i >= kSubsystemCount) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_subActive[static_cast<size_t>(i)]) return;
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        clock::now() - m_subStart[static_cast<size_t>(i)])
                        .count();
    m_subActive[static_cast<size_t>(i)] = false;
    auto& a = m_subAccum[static_cast<size_t>(i)];
    a.totalNs += static_cast<double>(ns);
    ++a.hits;
}

double PhysicsProfiler::sectionMs(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sectionAccum.find(name);
    if (it == m_sectionAccum.end()) return 0.0;
    return it->second.totalNs / 1e6;
}

double PhysicsProfiler::sectionAvgUs(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sectionAccum.find(name);
    if (it == m_sectionAccum.end() || it->second.hits == 0) return 0.0;
    return (it->second.totalNs / 1e3) / static_cast<double>(it->second.hits);
}

int PhysicsProfiler::sectionHits(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_sectionAccum.find(name);
    return it == m_sectionAccum.end() ? 0 : it->second.hits;
}

double PhysicsProfiler::subsystemMs(Subsystem s) const
{
    const int i = static_cast<int>(s);
    if (i < 0 || i >= kSubsystemCount) return 0.0;
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_subAccum[static_cast<size_t>(i)].totalNs / 1e6;
}

double PhysicsProfiler::subsystemAvgUs(Subsystem s) const
{
    const int i = static_cast<int>(s);
    if (i < 0 || i >= kSubsystemCount) return 0.0;
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto& a = m_subAccum[static_cast<size_t>(i)];
    if (a.hits == 0) return 0.0;
    return (a.totalNs / 1e3) / static_cast<double>(a.hits);
}

int PhysicsProfiler::subsystemHits(Subsystem s) const
{
    const int i = static_cast<int>(s);
    if (i < 0 || i >= kSubsystemCount) return 0;
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_subAccum[static_cast<size_t>(i)].hits;
}

std::vector<PhysicsProfiler::SectionReport> PhysicsProfiler::sectionsSorted() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<SectionReport> out;
    out.reserve(m_sectionAccum.size());
    for (const auto& kv : m_sectionAccum) {
        SectionReport r;
        r.name = kv.first;
        r.totalMs = kv.second.totalNs / 1e6;
        r.hits = kv.second.hits;
        r.avgUs = kv.second.hits ? (kv.second.totalNs / 1e3) / static_cast<double>(kv.second.hits) : 0.0;
        out.push_back(r);
    }
    std::sort(out.begin(), out.end(),
              [](const SectionReport& a, const SectionReport& b) { return a.totalMs > b.totalMs; });
    return out;
}

void PhysicsProfiler::report(FILE* out) const
{
    if (!out) out = stderr;
    std::fprintf(out, "=== PhysicsProfiler report ===\n");
    std::fprintf(out, "frames=%d  last=%.3f ms  avg=%.3f ms  fps~%d\n",
                 m_frameCount, m_lastFrameMs, m_avgFrameMs, fps());
    if (m_gpuFrameMs > 0.0)
        std::fprintf(out, "gpu last=%.3f ms  avg=%.3f ms  fps~%d\n",
                     m_gpuFrameMs, m_gpuAvgFrameMs, gpuFps());

    std::fprintf(out, "\n-- Subsystems --\n");
    std::fprintf(out, "%-18s %12s %12s %8s\n", "name", "total_ms", "avg_us", "hits");
    for (int i = 0; i < kSubsystemCount; ++i) {
        const auto s = static_cast<Subsystem>(i);
        const int h = subsystemHits(s);
        if (h == 0 && s != Total) continue;
        std::fprintf(out, "%-18s %12.3f %12.3f %8d\n",
                     subsystemName(s), subsystemMs(s), subsystemAvgUs(s), h);
    }

    const auto secs = sectionsSorted();
    if (!secs.empty()) {
        std::fprintf(out, "\n-- Sections --\n");
        std::fprintf(out, "%-28s %12s %12s %8s\n", "name", "total_ms", "avg_us", "hits");
        for (const auto& r : secs) {
            std::fprintf(out, "%-28s %12.3f %12.3f %8d\n",
                         r.name.c_str(), r.totalMs, r.avgUs, r.hits);
        }
    }
    std::fprintf(out, "==============================\n");
}

} // namespace physics
} // namespace ks
