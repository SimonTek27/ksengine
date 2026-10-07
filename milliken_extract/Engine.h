#pragma once

#include <functional>
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <chrono>
#include <algorithm>
#include <vector>
#include <string>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include "EngineModule.h"

namespace ks::engine {

using Entity = uint32_t;
constexpr Entity INVALID_ENTITY = 0;

/**
 * Thread-safe ECS registry.
 * - Writers (create/destroy/emplace/remove) take exclusive lock.
 * - Readers (get/has/view/tryGet) take shared lock.
 *
 * Deadlock policy: this class never invokes user callbacks, so it cannot
 * re-enter itself through application code.
 *
 * Pointers from get() are only valid until the next exclusive write on
 * another thread. Prefer tryGet() when crossing threads.
 */
class Registry {
public:
    Entity create() {
        std::unique_lock lock(m_mutex);
        return ++m_nextId;
    }

    void destroy(Entity e) {
        std::unique_lock lock(m_mutex);
        for (auto &kv : m_storages)
            kv.second->remove(e);
    }

    template<typename T, typename... Args>
    T& emplace(Entity e, Args&&... args) {
        std::unique_lock lock(m_mutex);
        auto &s = storageUnlocked<T>();
        return s.emplace(e, T(std::forward<Args>(args)...));
    }

    template<typename T>
    T* get(Entity e) {
        std::shared_lock lock(m_mutex);
        auto it = m_storages.find(std::type_index(typeid(T)));
        if (it == m_storages.end()) return nullptr;
        return static_cast<Storage<T>*>(it->second.get())->get(e);
    }

    template<typename T>
    bool has(Entity e) const {
        std::shared_lock lock(m_mutex);
        auto it = m_storages.find(std::type_index(typeid(T)));
        if (it == m_storages.end()) return false;
        return static_cast<Storage<T>*>(it->second.get())->has(e);
    }

    template<typename T>
    void remove(Entity e) {
        std::unique_lock lock(m_mutex);
        auto it = m_storages.find(std::type_index(typeid(T)));
        if (it != m_storages.end())
            it->second->remove(e);
    }

    template<typename T>
    std::vector<Entity> view() const {
        std::shared_lock lock(m_mutex);
        auto it = m_storages.find(std::type_index(typeid(T)));
        if (it == m_storages.end()) return {};
        return static_cast<Storage<T>*>(it->second.get())->entities();
    }

    /** Copy component by value (safe across threads). */
    template<typename T>
    bool tryGet(Entity e, T& out) const {
        std::shared_lock lock(m_mutex);
        auto it = m_storages.find(std::type_index(typeid(T)));
        if (it == m_storages.end()) return false;
        T* p = static_cast<Storage<T>*>(it->second.get())->get(e);
        if (!p) return false;
        out = *p;
        return true;
    }

private:
    struct IStorage {
        virtual ~IStorage() {}
        virtual void remove(Entity) = 0;
    };

    template<typename T>
    struct Storage : IStorage {
        std::unordered_map<Entity, T> data;
        T& emplace(Entity e, T v) {
            data[e] = std::move(v);
            return data[e];
        }
        T* get(Entity e) {
            auto it = data.find(e);
            return it == data.end() ? nullptr : &it->second;
        }
        bool has(Entity e) const { return data.find(e) != data.end(); }
        void remove(Entity e) override { data.erase(e); }
        std::vector<Entity> entities() const {
            std::vector<Entity> out;
            out.reserve(data.size());
            for (const auto& kv : data) out.push_back(kv.first);
            return out;
        }
    };

    template<typename T>
    Storage<T>& storageUnlocked() {
        auto idx = std::type_index(typeid(T));
        auto it = m_storages.find(idx);
        if (it == m_storages.end()) {
            auto s = std::make_unique<Storage<T>>();
            auto *ptr = s.get();
            m_storages[idx] = std::move(s);
            return *ptr;
        }
        return *static_cast<Storage<T>*>(it->second.get());
    }

    mutable std::shared_mutex m_mutex;
    Entity m_nextId = 0;
    std::unordered_map<std::type_index, std::unique_ptr<IStorage>> m_storages;
};

/**
 * Fixed-timestep loop — Qt-free, thread-safe, deadlock-safe.
 *
 * Anti-deadlock rules:
 *  1. Never hold m_mutex while invoking user callbacks.
 *  2. Re-entrant advance()/step() from inside a tick callback is ignored
 *     (m_dispatching guard) to avoid nested accum mutation and lock cycles.
 *  3. All ticks for one advance/step are computed under lock, then fired
 *     as a batch outside the lock.
 */
class EngineLoop {
public:
    using TickCallback = std::function<void(double)>;

    void setTickCallback(TickCallback cb) {
        std::lock_guard lock(m_mutex);
        m_onTick = std::move(cb);
    }

    void start(double fixedDt = 0.001) {
        std::function<void()> startedCb;
        {
            std::lock_guard lock(m_mutex);
            m_fixedDt = fixedDt;
            m_accum = 0;
            m_clock = std::chrono::steady_clock::now();
            m_running.store(true, std::memory_order_release);
            startedCb = m_onStarted;
        }
        // Callback outside lock
        if (startedCb) startedCb();
    }

    void stop() {
        std::function<void()> stoppedCb;
        {
            std::lock_guard lock(m_mutex);
            m_running.store(false, std::memory_order_release);
            stoppedCb = m_onStopped;
        }
        if (stoppedCb) stoppedCb();
    }

    bool isRunning() const {
        return m_running.load(std::memory_order_acquire);
    }

    void setFixedDt(double dt) {
        std::lock_guard lock(m_mutex);
        m_fixedDt = dt;
    }

    double fixedDt() const {
        std::lock_guard lock(m_mutex);
        return m_fixedDt;
    }

    /** Advance using real elapsed time since last call. */
    void advance() {
        dispatchElapsed(/*useClock=*/true, 0.0);
    }

    /** Inject a fixed amount of simulation time. */
    void step(double elapsed) {
        dispatchElapsed(/*useClock=*/false, elapsed);
    }

    void setStartedCallback(std::function<void()> cb) {
        std::lock_guard lock(m_mutex);
        m_onStarted = std::move(cb);
    }
    void setStoppedCallback(std::function<void()> cb) {
        std::lock_guard lock(m_mutex);
        m_onStopped = std::move(cb);
    }

private:
    void dispatchElapsed(bool useClock, double injectedElapsed) {
        if (!m_running.load(std::memory_order_acquire)) return;

        // Re-entrancy guard: if a tick callback calls advance()/step() again,
        // drop the nested call instead of risking lock/accum corruption.
        bool expected = false;
        if (!m_dispatching.compare_exchange_strong(
                expected, true,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return;
        }

        // RAII-ish clear of dispatching flag
        struct DispatchGuard {
            std::atomic<bool>& flag;
            ~DispatchGuard() { flag.store(false, std::memory_order_release); }
        } guard{m_dispatching};

        TickCallback tickCb;
        double fixedDt = 0.001;
        int tickCount = 0;

        {
            std::lock_guard lock(m_mutex);

            double elapsed = injectedElapsed;
            if (useClock) {
                auto now = std::chrono::steady_clock::now();
                elapsed = std::chrono::duration<double>(now - m_clock).count();
                m_clock = now;
            }
            if (elapsed < 0.0) elapsed = 0.0;
            if (elapsed > 0.1) elapsed = 0.1; // spiral-of-death clamp

            m_accum += elapsed;
            fixedDt = m_fixedDt;
            if (fixedDt <= 0.0) fixedDt = 0.001;

            // Compute how many fixed ticks to fire under the lock
            while (m_accum >= fixedDt) {
                m_accum -= fixedDt;
                ++tickCount;
                // Safety: never more than 64 ticks per dispatch
                if (tickCount >= 64) {
                    m_accum = 0;
                    break;
                }
            }
            tickCb = m_onTick;
        }
        // ---- lock released ----

        for (int i = 0; i < tickCount; ++i) {
            if (tickCb) tickCb(fixedDt);
        }
    }

    mutable std::mutex m_mutex;
    TickCallback m_onTick;
    std::function<void()> m_onStarted;
    std::function<void()> m_onStopped;
    std::chrono::steady_clock::time_point m_clock;
    double m_fixedDt = 0.001;
    double m_accum = 0;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_dispatching{false};
};

/**
 * Engine singleton — thread-safe, deadlock-safe lifecycle.
 *
 * Anti-deadlock rules:
 *  1. Never hold m_mutex while running user code (modules, systems, callbacks).
 *  2. Snapshot vectors/callbacks under lock, release, then invoke.
 *  3. m_inTick marks the fixed-tick phase so nested mutations are safe
 *     (they only affect the *next* tick's snapshot).
 *  4. Lock order if both are needed: Engine mutex → EngineLoop mutex
 *     (never the reverse from Engine-owned code paths).
 */
class Engine {
public:
    static Engine& instance() {
        static Engine e;
        return e;
    }

    bool initialize() {
        std::vector<EngineModule*> modules;
        std::function<void()> initCb;
        {
            std::unique_lock lock(m_mutex);
            if (m_initialized) return true;
            std::sort(m_modules.begin(), m_modules.end(),
                      [](auto *a, auto *b) { return a->priority() > b->priority(); });
            modules = m_modules;
            // Install tick bridge while holding Engine lock; setTickCallback
            // only locks EngineLoop (order: Engine → Loop).
            m_loop.setTickCallback([this](double dt) { onFixedTick(dt); });
            m_initialized = true;
            initCb = m_onInitialized;
        }
        // ---- lock released before module init & callback ----

        for (auto *m : modules) {
            if (!m->initialize()) {
                std::fprintf(stderr, "Engine: module failed: %s\n", m->moduleName().c_str());
                // Roll back initialized flag without holding lock across shutdown
                {
                    std::unique_lock lock(m_mutex);
                    m_initialized = false;
                    m_loop.setTickCallback(nullptr);
                }
                return false;
            }
        }

        if (initCb) initCb();
        return true;
    }

    void shutdown() {
        std::vector<EngineModule*> modules;
        std::function<void()> doneCb;
        {
            std::unique_lock lock(m_mutex);
            if (!m_initialized) return;
            // stop() will release Loop lock before its own callback
            m_initialized = false;
            modules.assign(m_modules.rbegin(), m_modules.rend());
            doneCb = m_onShutdownCompleted;
            m_loop.setTickCallback(nullptr);
        }
        // ---- lock released ----

        m_loop.stop();
        for (auto *m : modules)
            m->shutdown();
        if (doneCb) doneCb();
    }

    void start() {
        double dt;
        std::function<void()> cb;
        {
            std::unique_lock lock(m_mutex);
            dt = m_fixedDt;
            cb = m_onStarted;
        }
        m_loop.start(dt);
        if (cb) cb();
    }

    void stop() {
        std::function<void()> cb;
        {
            std::unique_lock lock(m_mutex);
            cb = m_onStopped;
        }
        m_loop.stop();
        if (cb) cb();
    }

    bool isRunning() const { return m_loop.isRunning(); }

    void registerModule(EngineModule* m) {
        std::unique_lock lock(m_mutex);
        m_modules.push_back(m);
    }

    void unregisterModule(EngineModule* m) {
        std::unique_lock lock(m_mutex);
        m_modules.erase(
            std::remove(m_modules.begin(), m_modules.end(), m),
            m_modules.end());
    }

    Registry& registry() { return m_registry; }
    EngineLoop& loop() { return m_loop; }

    double fixedDt() const {
        std::shared_lock lock(m_mutex);
        return m_fixedDt;
    }

    void setFixedDt(double dt) {
        std::unique_lock lock(m_mutex);
        m_fixedDt = dt;
        m_loop.setFixedDt(dt);
    }

    uint64_t tickCount() const {
        return m_tickCount.load(std::memory_order_acquire);
    }

    double time() const {
        const uint64_t ticks = m_tickCount.load(std::memory_order_acquire);
        std::shared_lock lock(m_mutex);
        return static_cast<double>(ticks) * m_fixedDt;
    }

    void addSystem(std::function<void(double)> fn) {
        std::unique_lock lock(m_mutex);
        m_systems.push_back(std::move(fn));
        // Safe during onFixedTick: the running snapshot was already copied.
    }

    // Event callbacks — setters never run user code under the lock
    void setInitializedCallback(std::function<void()> cb) {
        std::unique_lock lock(m_mutex);
        m_onInitialized = std::move(cb);
    }
    void setShutdownCompletedCallback(std::function<void()> cb) {
        std::unique_lock lock(m_mutex);
        m_onShutdownCompleted = std::move(cb);
    }
    void setStartedCallback(std::function<void()> cb) {
        std::unique_lock lock(m_mutex);
        m_onStarted = std::move(cb);
    }
    void setStoppedCallback(std::function<void()> cb) {
        std::unique_lock lock(m_mutex);
        m_onStopped = std::move(cb);
    }
    void setFixedTickCallback(std::function<void(double)> cb) {
        std::unique_lock lock(m_mutex);
        m_onFixedTick = std::move(cb);
    }

    /** True while systems / fixedTick callback are running. */
    bool isInTick() const {
        return m_inTick.load(std::memory_order_acquire);
    }

private:
    Engine() = default;

    void onFixedTick(double dt) {
        m_tickCount.fetch_add(1, std::memory_order_acq_rel);

        std::vector<std::function<void(double)>> systems;
        std::function<void(double)> fixedCb;
        {
            std::shared_lock lock(m_mutex);
            systems = m_systems;   // snapshot
            fixedCb = m_onFixedTick;
        }
        // ---- lock released before any user code ----

        m_inTick.store(true, std::memory_order_release);
        struct TickGuard {
            std::atomic<bool>& flag;
            ~TickGuard() { flag.store(false, std::memory_order_release); }
        } tickGuard{m_inTick};

        for (auto &fn : systems) {
            if (fn) fn(dt);
        }
        if (fixedCb) fixedCb(dt);
    }

    mutable std::shared_mutex m_mutex;
    bool m_initialized = false;
    double m_fixedDt = 0.001;
    std::atomic<uint64_t> m_tickCount{0};
    std::atomic<bool> m_inTick{false};
    EngineLoop m_loop;
    Registry m_registry;
    std::vector<EngineModule*> m_modules;
    std::vector<std::function<void(double)>> m_systems;

    std::function<void()> m_onInitialized;
    std::function<void()> m_onShutdownCompleted;
    std::function<void()> m_onStarted;
    std::function<void()> m_onStopped;
    std::function<void(double)> m_onFixedTick;
};

} // namespace ks::engine
