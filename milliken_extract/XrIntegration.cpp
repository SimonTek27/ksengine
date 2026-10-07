#include "XrIntegration.h"
#include <string>
#include <cstdio>
#if defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)

namespace ks {
namespace device {

XrIntegration* XrIntegration::s_instance = nullptr;

XrIntegration* XrIntegration::instance()
{
    if (!s_instance) s_instance = new XrIntegration();
    return s_instance;
}

XrIntegration::XrIntegration()
{
    s_instance = this;
    m_manager = XrManager::instance();
    m_viewportRenderer = new XrViewportRenderer();
    m_input = new XrInput(m_manager);
}

XrIntegration::~XrIntegration()
{
    shutdown();
    if (s_instance == this) s_instance = nullptr;
}

void XrIntegration::setVulkanDevice(VkDevice device, VkPhysicalDevice physicalDevice,
                                     VkInstance vkInstance, uint32_t queueFamilyIndex,
                                     uint32_t queueIndex, VkCommandPool commandPool,
                                     VkQueue graphicsQueue)
{
    m_vkDevice = device;
    m_vkPhysicalDevice = physicalDevice;
    m_vkInstance = vkInstance;
    m_queueFamilyIndex = queueFamilyIndex;
    m_queueIndex = queueIndex;
    m_commandPool = commandPool;
    m_graphicsQueue = graphicsQueue;
}

bool XrIntegration::initialize(const std::string& applicationName)
{
    if (m_initialized) return true;

    if (!m_viewportRenderer->initialize(m_vkDevice, m_vkPhysicalDevice, m_vkInstance,
                                         m_queueFamilyIndex, m_queueIndex,
                                         m_commandPool, m_graphicsQueue)) {
        if (onError) onError("Failed to initialize VR viewport renderer");
        return false;
    }

    m_initialized = true;
    if (onInitializedChanged) onInitializedChanged(true);
    return true;
}

void XrIntegration::shutdown()
{
    if (!m_initialized) return;

    stopVR();
    m_viewportRenderer->shutdown();
    m_initialized = false;
    if (onInitializedChanged) onInitializedChanged(false);
}

bool XrIntegration::startVR()
{
    if (!m_initialized) return false;

    if (!m_manager->isSessionRunning()) {
        if (onError) onError("VR session not ready yet. Ensure XrManager is initialized.");
        return false;
    }

    if (onSessionActiveChanged) onSessionActiveChanged(true);
    return true;
}

void XrIntegration::stopVR()
{
    if (m_manager->isSessionRunning()) {
        m_manager->shutdown();
    }

    m_viewportRenderer->shutdown();
    m_initialized = false;
    if (onSessionActiveChanged) onSessionActiveChanged(false);
    if (onInitializedChanged) onInitializedChanged(false);
}

bool XrIntegration::renderFrame()
{
    if (!m_initialized) return false;
    return m_viewportRenderer->renderFrame();
}

bool XrIntegration::pollEvents()
{
    if (!m_manager) return false;
    return m_manager->pollEvents();
}

} // namespace device
} // namespace ks

#endif // defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)
