#pragma once

#include <functional>
#include <string>
#include <cstdint>

#include "XrManager.h"

#if __has_include(<vulkan/vulkan.h>)
#include <vulkan/vulkan.h>
#endif

namespace ks {
namespace device {

/**
 * Per-eye Vulkan framebuffer renderer for OpenXR — Qt-free.
 */
class XrViewportRenderer {
public:
    XrViewportRenderer();
    ~XrViewportRenderer();

    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice,
                    VkInstance vkInstance, uint32_t queueFamilyIndex,
                    uint32_t queueIndex, VkCommandPool commandPool, VkQueue graphicsQueue);
    void shutdown();
    bool isInitialized() const { return m_initialized; }
    bool isSessionActive() const;

    void setClearColor(float r, float g, float b, float a = 1.0f);

    bool renderFrame();

    /** Draw callback: (cmd, eyeIndex, view, projection) */
    void setDrawCallback(
        std::function<void(VkCommandBuffer, int, const XrMat4&, const XrMat4&)> callback) {
        m_drawCallback = std::move(callback);
    }

    bool beginFrame();
    bool renderEye(int eyeIndex);
    bool endFrame();

    std::function<void(const std::string& message)> onError;

private:
    struct EyeFramebuffer {
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkImageView colorView = VK_NULL_HANDLE;
        VkImageView depthView = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore semaphore = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
    };

    bool createEyeFramebuffers();
    void destroyEyeFramebuffers();
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect);

    bool m_initialized = false;

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;

    EyeFramebuffer m_eyeFBs[2];

    float m_clearColor[4] = {0.1f, 0.1f, 0.1f, 1.0f};

    std::function<void(VkCommandBuffer, int, const XrMat4&, const XrMat4&)> m_drawCallback;

    XrManager* m_xr = nullptr;
};

} // namespace device
} // namespace ks
