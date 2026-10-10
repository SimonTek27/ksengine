#pragma once
#include "KsExport.h"
#include "KsQtFreeGuard.h"
#include "EngineModule.h"
#include "scene/Registry.h"
#include "scene/Components.h"
#include "scene/SceneModule.h"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>

namespace ks {

/**
 * Core engine — Qt-free.
 * Fixed-timestep tick + module registry + callback events (no QObject).
 */
class KSENGINE_API Engine {
public:
    static Engine& instance() {
        static Engine s;
        return s;
    }

    bool initialize();

    void shutdown();

    bool isInitialized() const { return m_initialized; }
    bool isRunning() const { return m_running; }

    void start() { m_running = true; }
    void stop() { m_running = false; }

    void setFixedDt(double dt) { m_fixedDt = dt > 1e-5 ? dt : 1.0 / 120.0; }
    double fixedDt() const { return m_fixedDt; }

    void registerModule(const std::string& name, std::shared_ptr<EngineModule> mod) {
        std::unique_lock lock(m_mutex);
        m_modules[name] = std::move(mod);
    }

    void unregisterModule(const std::string& name) {
        std::unique_lock lock(m_mutex);
        m_modules.erase(name);
    }

    /** Advance simulation; call from main loop with wall-clock dt. */
    void tick(double realDt) {
        if (!m_running) return;
        if (m_inTick.exchange(true)) return; // reentrancy guard

        m_accum += realDt;
        const double maxCatchUp = m_fixedDt * 8.0;
        if (m_accum > maxCatchUp) m_accum = maxCatchUp;

        while (m_accum >= m_fixedDt) {
            std::vector<std::shared_ptr<EngineModule>> snapshot;
            {
                std::shared_lock lock(m_mutex);
                snapshot.reserve(m_modules.size());
                for (auto& kv : m_modules)
                    if (kv.second) snapshot.push_back(kv.second);
            }
            // m_modules is a hash map, so iteration order is not a contract.
            // Sort by priority so module ordering is well defined; stable, so
            // modules sharing a priority keep their registration order.
            std::stable_sort(snapshot.begin(), snapshot.end(),
                             [](const std::shared_ptr<EngineModule>& a,
                                const std::shared_ptr<EngineModule>& b) {
                                 return a->priority() < b->priority();
                             });
            for (auto& m : snapshot)
                m->update(m_fixedDt);
            m_accum -= m_fixedDt;
            ++m_tickCount;
            if (onStepCompleted) onStepCompleted(m_fixedDt);
        }

        m_inTick = false;
    }

    uint64_t tickCount() const { return m_tickCount; }

    ecs::Registry& registry() { return m_registry; }
    const ecs::Registry& registry() const { return m_registry; }

    ecs::Entity createEntity(const std::string& name = std::string());

    void destroyEntity(ecs::Entity e);

    std::function<void(double dt)> onStepCompleted;

private:
    Engine() = default;

    mutable std::shared_mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<EngineModule>> m_modules;
    bool m_initialized = false;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_inTick{false};
    double m_fixedDt = 1.0 / 120.0;
    double m_accum = 0.0;
    uint64_t m_tickCount = 0;
    ecs::Registry m_registry;
};

} // namespace ks
