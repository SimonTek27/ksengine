#pragma once

#include "XrManager.h"
#include "XrViewportRenderer.h"
#include "XrInput.h"
#include "XrConfig.h"

namespace ks {
namespace vr {

class XrIntegration : public QObject {
    Q_OBJECT
public:
    static XrIntegration* instance();

    bool initialize(const QString& applicationName = "ksEditor VR");
    void shutdown();
    bool isInitialized() const { return m_initialized; }
    bool isSessionActive() const { return m_viewportRenderer && m_viewportRenderer->isSessionActive(); }

    XrManager* xrManager() { return m_manager; }
    XrViewportRenderer* viewportRenderer() { return m_viewportRenderer; }
    XrInput* xrInput() { return m_input; }
    XrSettings& settings() { return m_settings; }

    bool hasVulkanDevice() const {
        return m_vkDevice != VK_NULL_HANDLE;
    }

    void setVulkanDevice(VkDevice device, VkPhysicalDevice physicalDevice,
                         VkInstance vkInstance, uint32_t queueFamilyIndex,
                         uint32_t queueIndex, VkCommandPool commandPool, VkQueue graphicsQueue);

    bool startVR();
    void stopVR();
    bool renderFrame();
    bool pollEvents();

signals:
    void initializedChanged(bool initialized);
    void sessionActiveChanged(bool active);
    void error(const QString& message);

private:
    XrIntegration(QObject* parent = nullptr);
    ~XrIntegration();
    Q_DISABLE_COPY(XrIntegration)

    static XrIntegration* s_instance;

    bool m_initialized = false;
    XrManager* m_manager = nullptr;
    XrViewportRenderer* m_viewportRenderer = nullptr;
    XrInput* m_input = nullptr;
    XrSettings m_settings;

    VkDevice m_vkDevice = VK_NULL_HANDLE;
    VkPhysicalDevice m_vkPhysicalDevice = VK_NULL_HANDLE;
    VkInstance m_vkInstance = VK_NULL_HANDLE;
    uint32_t m_queueFamilyIndex = 0;
    uint32_t m_queueIndex = 0;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
};

}} // namespace ks::vr
