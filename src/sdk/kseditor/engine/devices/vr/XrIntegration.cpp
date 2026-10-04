#include "XrIntegration.h"
#if defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)

namespace ks {
namespace vr {

XrIntegration* XrIntegration::s_instance = nullptr;

XrIntegration* XrIntegration::instance()
{
    if (!s_instance) s_instance = new XrIntegration();
    return s_instance;
}

XrIntegration::XrIntegration(QObject* parent)
    : QObject(parent)
{
    s_instance = this;
    m_manager = XrManager::instance();
    m_viewportRenderer = new XrViewportRenderer(this);
    m_input = new XrInput(m_manager, this);
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

bool XrIntegration::initialize(const QString& applicationName)
{
    if (m_initialized) return true;

    if (!m_viewportRenderer->initialize(m_vkDevice, m_vkPhysicalDevice, m_vkInstance,
                                         m_queueFamilyIndex, m_queueIndex,
                                         m_commandPool, m_graphicsQueue)) {
        emit error("Failed to initialize VR viewport renderer");
        return false;
    }

    m_initialized = true;
    emit initializedChanged(true);
    return true;
}

void XrIntegration::shutdown()
{
    if (!m_initialized) return;

    stopVR();
    m_viewportRenderer->shutdown();
    m_initialized = false;
    emit initializedChanged(false);
}

bool XrIntegration::startVR()
{
    if (!m_initialized) return false;

    if (!m_manager->isSessionRunning()) {
        emit error("VR session not ready yet. Ensure XrManager is initialized.");
        return false;
    }

    emit sessionActiveChanged(true);
    return true;
}

void XrIntegration::stopVR()
{
    if (m_manager->isSessionRunning()) {
        m_manager->shutdown();
    }

    m_viewportRenderer->shutdown();
    m_initialized = false;
    emit sessionActiveChanged(false);
    emit initializedChanged(false);
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

}} // namespace ks::vr

#endif // defined(XR_VERSION_1_0) || defined(XR_NULL_HANDLE)
