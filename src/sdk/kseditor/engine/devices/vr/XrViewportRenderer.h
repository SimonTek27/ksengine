#pragma once

#include <QObject>
#include <QMatrix4x4>
#include <QVector3D>
#include <QVector>
#include <vulkan/vulkan.h>

#include "XrManager.h"

namespace ks {
namespace vr {

class XrViewportRenderer : public QObject {
    Q_OBJECT
public:
    explicit XrViewportRenderer(QObject* parent = nullptr);
    ~XrViewportRenderer();

    bool initialize(VkDevice device, VkPhysicalDevice physicalDevice, VkInstance vkInstance,
                    uint32_t queueFamilyIndex, uint32_t queueIndex,
                    VkCommandPool commandPool, VkQueue graphicsQueue);
    void shutdown();

    bool isInitialized() const { return m_initialized; }

    bool renderFrame();
    bool isSessionActive() const;

    void setClearColor(float r, float g, float b, float a);

    void setDrawCallback(std::function<void(VkCommandBuffer cmd, int eyeIndex,
                                            const QMatrix4x4& view, const QMatrix4x4& proj)> callback) {
        m_drawCallback = callback;
    }

    struct EyeFramebuffer {
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkImageView colorView = VK_NULL_HANDLE;
        VkImageView depthView = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore semaphore = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
    };

    EyeFramebuffer& eyeFramebuffer(int index) { return m_eyeFBs[index]; }

signals:
    void frameRendered();
    void error(const QString& message);

private:
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

    std::function<void(VkCommandBuffer, int, const QMatrix4x4&, const QMatrix4x4&)> m_drawCallback;

    XrManager* m_xr = nullptr;
};

}} // namespace ks::vr
