#include "Engine.h"
#include "hw/HardwareBackend.h"

#include <cstdio>

namespace ks {

bool Engine::initialize() {
    std::unique_lock lock(m_mutex);
    if (m_initialized) return true;
    m_initialized = true;
    m_running = false;
    m_accum = 0.0;
    // The engine proper never touches a device: the hardware backends compile
    // into ksenginehw and report what the machine actually answers. Asked once,
    // here, so a start-up log says what audio, video and controls were found.
    const std::string hardware = hw::summarize();
    std::fprintf(stderr, "[engine] %s\n", hardware.c_str());
    return true;
}

void Engine::shutdown() {
    std::unique_lock lock(m_mutex);
    m_running = false;
    m_modules.clear();
    m_initialized = false;
    m_registry.clear();
}

ecs::Entity Engine::createEntity(const std::string& name) {
    const ecs::Entity e = m_registry.create();
    if (e != ecs::kNullEntity && !name.empty())
        m_registry.emplace<ecs::Name>(e, ecs::Name{name});
    return e;
}

void Engine::destroyEntity(ecs::Entity e) { m_registry.destroy(e); }

} // namespace ks
