#pragma once
/** Qt-free cascaded shadow map stub for SimulatorApp (no Qt / no heavy Vulkan yet). */

#include <cstdint>
#include <vulkan/vulkan.h>

namespace ks {
namespace sim {

class CascadedShadowMap {
public:
    bool initialize() { return true; }

    bool initialize(VkPhysicalDevice, VkDevice, VkCommandPool, VkQueue, const char*) {
        return true; // stub: no shadow passes yet
    }

    void shutdown() {}
    void resize(uint32_t, uint32_t) {}
    void setCascadeCount(int) {}
    bool isReady() const { return false; }
};

} // namespace sim
} // namespace ks
