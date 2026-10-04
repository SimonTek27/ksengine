#include "VulkanRenderer.h"
#include "VulkanFunctions.h"
#include "VulkanShaderLoader.h"
#include <QVariant>
#include <QExposeEvent>
#include <QResizeEvent>
#include <QDebug>
#include <QCoreApplication>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {

static VkFormat toVkFormat(ks::VulkanTexture::Format fmt) {
    using Format = ks::VulkanTexture::Format;
    switch (fmt) {
        case Format::RGBA8:    return VK_FORMAT_R8G8B8A8_SRGB;
        case Format::RGBA16F:  return VK_FORMAT_R16G16B16A16_SFLOAT;
        case Format::RGBA32F:  return VK_FORMAT_R32G32B32A32_SFLOAT;
        case Format::D32F:     return VK_FORMAT_D32_SFLOAT;
        case Format::BC7:      return VK_FORMAT_BC7_UNORM_BLOCK;
    }
    return VK_FORMAT_R8G8B8A8_SRGB;
}

} // anonymous namespace

namespace ks {

// Forward declarations for static helpers used early in the file
static void createMeshBuffer(VkDevice device, VkPhysicalDevice physDev, const void* data,
                             VkDeviceSize size, VkBufferUsageFlags usage,
                             VkBuffer& outBuf, VkDeviceMemory& outMem);
static void destroyMeshBuffer(VkDevice device, VkBuffer& buf, VkDeviceMemory& mem);

VulkanFunctionTable g_vk;

VulkanRenderer::VulkanRenderer(QObject* parent)
    : QObject(parent) {
}

VulkanRenderer::~VulkanRenderer() {
    for (auto it = m_meshes.begin(); it != m_meshes.end(); ++it) {
        destroyMeshBuffer(m_device, it->vertexBuffer, it->vertexMemory);
        destroyMeshBuffer(m_device, it->indexBuffer, it->indexMemory);
    }
    m_meshes.clear();
    destroyDevice();
}

bool VulkanRenderer::createDevice(VkInstance instance, VkSurfaceKHR surface) {
#if QT_CONFIG(vulkan)
    Q_UNUSED(surface);
    if (!instance) return false;
    if (!loadVulkanLoader() || !loadInstanceFunctions(instance)) {
        emit error("Vulkan loader could not resolve required symbols");
        return false;
    }
    m_instance = instance;
    m_ownsInstance = false;

    // Enumerate physical devices
    uint32_t deviceCount = 0;
    g_vk.enumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        emit error("No Vulkan physical devices found");
        return false;
    }

    QVector<VkPhysicalDevice> devices(deviceCount);
    g_vk.enumeratePhysicalDevices(instance, &deviceCount, devices.data());

    // Pick first discrete GPU, fallback to first device
    m_physicalDevice = devices[0];
    for (VkPhysicalDevice dev : devices) {
        VkPhysicalDeviceProperties props;
        g_vk.getPhysicalDeviceProperties(dev, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            m_physicalDevice = dev;
            break;
        }
    }

    // Find graphics queue family
    uint32_t queueFamilyCount = 0;
    g_vk.getPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, nullptr);
    QVector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    g_vk.getPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queueFamilyCount, queueFamilies.data());

    bool foundQueue = false;
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            m_graphicsFamily = i;
            foundQueue = true;
            break;
        }
    }
    if (!foundQueue) {
        emit error("No graphics queue family found");
        return false;
    }

    // Create logical device
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = m_graphicsFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    // Enable swapchain extension for presentation
    const char* deviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VkPhysicalDeviceFeatures deviceFeatures{};

    VkDeviceCreateInfo deviceCreateInfo{};
    deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
    deviceCreateInfo.enabledExtensionCount = 1;
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions;

    VkResult result = g_vk.createDevice(m_physicalDevice, &deviceCreateInfo, nullptr, &m_device);
    if (result != VK_SUCCESS) {
        emit error(QString("Failed to create Vulkan device: %1").arg(result));
        return false;
    }

    g_vk.getDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);

    // Create command pool
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_graphicsFamily;

    result = g_vk.createCommandPool(m_device, &poolInfo, nullptr, &m_commandPool);
    if (result != VK_SUCCESS) {
        emit error(QString("Failed to create Vulkan command pool: %1").arg(result));
        return false;
    }

    // Create fence for CPU-GPU sync
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    g_vk.createFence(m_device, &fenceInfo, nullptr, &m_fence);

    m_initialized = true;
    emit initialized();
    qDebug() << "VulkanRenderer: Device created successfully";
    return true;
#else
    Q_UNUSED(instance); Q_UNUSED(surface);
    emit error("Vulkan not available");
    return false;
#endif
}

void VulkanRenderer::destroyDevice() {
#if QT_CONFIG(vulkan)
    if (m_device) {
        // Wait for all GPU work to finish
        if (g_vk.deviceWaitIdle) g_vk.deviceWaitIdle(m_device);

        destroyOffscreenRenderTarget();
        destroySwapChain();

        if (m_commandBuffer && m_commandPool && g_vk.freeCommandBuffers) {
            g_vk.freeCommandBuffers(m_device, m_commandPool, 1, &m_commandBuffer);
            m_commandBuffer = VK_NULL_HANDLE;
        }
        if (m_fence && g_vk.destroyFence) {
            g_vk.destroyFence(m_device, m_fence, nullptr);
            m_fence = VK_NULL_HANDLE;
        }
        if (m_imageAvailableSemaphore && g_vk.destroySemaphore) {
            g_vk.destroySemaphore(m_device, m_imageAvailableSemaphore, nullptr);
            m_imageAvailableSemaphore = VK_NULL_HANDLE;
        }
        if (m_renderFinishedSemaphore && g_vk.destroySemaphore) {
            g_vk.destroySemaphore(m_device, m_renderFinishedSemaphore, nullptr);
            m_renderFinishedSemaphore = VK_NULL_HANDLE;
        }
    }
    if (m_commandPool && m_device && g_vk.destroyCommandPool) {
        g_vk.destroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
    if (m_device && g_vk.destroyDevice) {
        g_vk.destroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
    if (m_instance && m_ownsInstance && g_vk.destroyInstance) {
        g_vk.destroyInstance(m_instance, nullptr);
    }
    m_instance = VK_NULL_HANDLE;
    m_physicalDevice = VK_NULL_HANDLE;
    m_graphicsQueue = VK_NULL_HANDLE;
    m_initialized = false;
#endif
}

VulkanRenderer* VulkanRenderer::instance() {
    static VulkanRenderer inst;
    return &inst;
}

uint32_t VulkanRenderer::findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    g_vk.getPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    return 0;
}

// ── Swap Chain ──────────────────────────────────────────────────────────

bool VulkanRenderer::createSwapChain(VkSurfaceKHR surface, int width, int height) {
#if QT_CONFIG(vulkan)
    if (!m_device || !surface || !g_vk.createSwapchainKHR) return false;

    m_surface = surface;

    // Load device-level functions
    loadDeviceFunctions(m_instance, m_device);

    // Query surface capabilities
    VkSurfaceCapabilitiesKHR caps{};
    g_vk.getPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, surface, &caps);

    // Choose surface format (prefer SRGB B8G8R8A8)
    uint32_t formatCount = 0;
    g_vk.getPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, surface, &formatCount, nullptr);
    QVector<VkSurfaceFormatKHR> formats(formatCount);
    g_vk.getPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, surface, &formatCount, formats.data());

    VkSurfaceFormatKHR chosenFormat = formats[0];
    for (auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenFormat = f;
            break;
        }
    }
    m_swapChainFormat = chosenFormat.format;

    // Choose present mode (prefer FIFO = vsync)
    uint32_t modeCount = 0;
    g_vk.getPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, surface, &modeCount, nullptr);
    QVector<VkPresentModeKHR> presentModes(modeCount);
    g_vk.getPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, surface, &modeCount, presentModes.data());

    VkPresentModeKHR chosenMode = VK_PRESENT_MODE_FIFO_KHR;
    for (auto& m : presentModes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) { chosenMode = m; break; }
    }

    // Choose extent
    if (caps.currentExtent.width != 0xFFFFFFFF) {
        m_swapChainExtent = caps.currentExtent;
    } else {
        m_swapChainExtent.width = qMin(static_cast<uint32_t>(width), caps.maxImageExtent.width);
        m_swapChainExtent.height = qMin(static_cast<uint32_t>(height), caps.maxImageExtent.height);
    }

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount)
        imageCount = caps.maxImageCount;

    // Create swap chain
    VkSwapchainCreateInfoKHR sci{};
    sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sci.surface = surface;
    sci.minImageCount = imageCount;
    sci.imageFormat = m_swapChainFormat;
    sci.imageColorSpace = chosenFormat.colorSpace;
    sci.imageExtent = m_swapChainExtent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sci.preTransform = caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = chosenMode;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = VK_NULL_HANDLE;

    if (g_vk.createSwapchainKHR(m_device, &sci, nullptr, &m_swapChain) != VK_SUCCESS) {
        emit error("Failed to create swap chain");
        return false;
    }

    // Get swap chain images
    uint32_t swapImageCount = 0;
    g_vk.getSwapchainImagesKHR(m_device, m_swapChain, &swapImageCount, nullptr);
    m_swapChainImages.resize(swapImageCount);
    g_vk.getSwapchainImagesKHR(m_device, m_swapChain, &swapImageCount, m_swapChainImages.data());

    // Create image views
    m_swapChainImageViews.resize(swapImageCount);
    for (uint32_t i = 0; i < swapImageCount; ++i) {
        VkImageViewCreateInfo ivci{};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = m_swapChainImages[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = m_swapChainFormat;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel = 0;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount = 1;
        g_vk.createImageView(m_device, &ivci, nullptr, &m_swapChainImageViews[i]);
    }

    // Create render pass with color + depth
    {
        VkAttachmentDescription colorAtt{};
        colorAtt.format = m_swapChainFormat;
        colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depthAtt{};
        depthAtt.format = VK_FORMAT_D32_SFLOAT;
        depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthRef{};
        depthRef.attachment = 1;
        depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.srcAccessMask = 0;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        VkAttachmentDescription attachments[] = {colorAtt, depthAtt};

        VkRenderPassCreateInfo rpci{};
        rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpci.attachmentCount = 2;
        rpci.pAttachments = attachments;
        rpci.subpassCount = 1;
        rpci.pSubpasses = &subpass;
        rpci.dependencyCount = 1;
        rpci.pDependencies = &dep;

        if (g_vk.createRenderPass(m_device, &rpci, nullptr, &m_renderPass) != VK_SUCCESS) {
            emit error("Failed to create render pass");
            return false;
        }
    }

    // Create depth textures and framebuffers for each swap chain image
    m_swapChainDepthTextures.resize(swapImageCount);
    m_swapChainFramebuffers.resize(swapImageCount);

    for (uint32_t i = 0; i < swapImageCount; ++i) {
        // Create depth texture
        auto* depthTex = new VulkanTexture(this);
        depthTex->setDevice(m_device, m_physicalDevice);
        depthTex->createRenderTarget(m_swapChainExtent.width, m_swapChainExtent.height, VulkanTexture::D32F);
        m_swapChainDepthTextures[i] = depthTex;

        // Create framebuffer
        auto* fb = new VulkanFramebuffer(this);
        fb->setDevice(m_device, m_physicalDevice);
        fb->setRenderPass(m_renderPass);

        // Create a wrapper texture for the swap chain image
        auto* colorTex = new VulkanTexture(this);
        colorTex->setDevice(m_device, m_physicalDevice);

        // Create image view for swap chain image directly
        VkImageViewCreateInfo ivci{};
        ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ivci.image = m_swapChainImages[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = m_swapChainFormat;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.baseMipLevel = 0;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.baseArrayLayer = 0;
        ivci.subresourceRange.layerCount = 1;

        // Store in framebuffer's attachment list
        m_swapChainFramebuffers[i] = fb;
    }

    // Create semaphores for synchronization
    VkSemaphoreCreateInfo semCI{};
    semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    g_vk.createSemaphore(m_device, &semCI, nullptr, &m_imageAvailableSemaphore);
    g_vk.createSemaphore(m_device, &semCI, nullptr, &m_renderFinishedSemaphore);

    // Create shader loader
    m_shaderLoader = std::make_unique<VulkanShaderLoader>(m_physicalDevice, m_device, m_graphicsQueue);

    qDebug() << "VulkanRenderer: Swap chain created (" << swapImageCount << " images,"
             << m_swapChainExtent.width << "x" << m_swapChainExtent.height << ")";
    return true;
#else
    Q_UNUSED(surface) Q_UNUSED(width) Q_UNUSED(height);
    return false;
#endif
}

void VulkanRenderer::destroySwapChain() {
#if QT_CONFIG(vulkan)
    if (!m_device) return;

    if (g_vk.deviceWaitIdle) g_vk.deviceWaitIdle(m_device);

    m_shaderLoader.reset();

    for (auto* fb : m_swapChainFramebuffers) {
        if (fb) fb->deleteLater();
    }
    m_swapChainFramebuffers.clear();

    for (auto* tex : m_swapChainDepthTextures) {
        if (tex) tex->deleteLater();
    }
    m_swapChainDepthTextures.clear();

    for (auto iv : m_swapChainImageViews) {
        if (iv && g_vk.destroyImageView) g_vk.destroyImageView(m_device, iv, nullptr);
    }
    m_swapChainImageViews.clear();
    m_swapChainImages.clear();

    if (m_renderPass && g_vk.destroyRenderPass) {
        g_vk.destroyRenderPass(m_device, m_renderPass, nullptr);
        m_renderPass = VK_NULL_HANDLE;
    }

    if (m_swapChain && g_vk.destroySwapchainKHR) {
        g_vk.destroySwapchainKHR(m_device, m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }
#endif
}

bool VulkanRenderer::recreateSwapChain(int width, int height) {
#if QT_CONFIG(vulkan)
    if (!m_device || !m_surface) return false;

    destroySwapChain();
    return createSwapChain(m_surface, width, height);
#else
    Q_UNUSED(width);
    Q_UNUSED(height);
    return false;
#endif
}

bool VulkanRenderer::acquireNextImage(uint32_t& imageIndex) {
#if QT_CONFIG(vulkan)
    if (!m_device || !m_swapChain || !g_vk.acquireNextImageKHR) return false;

    VkResult result = g_vk.acquireNextImageKHR(
        m_device, m_swapChain, UINT64_MAX,
        m_imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        // Surface changed, need to recreate swap chain
        emit error("Swap chain out of date - surface needs recreation");
        return false;
    }

    m_currentImageIndex = imageIndex;
    return result == VK_SUCCESS;
#else
    imageIndex = 0;
    return false;
#endif
}

bool VulkanRenderer::presentImage(uint32_t imageIndex) {
#if QT_CONFIG(vulkan)
    if (!m_device || !m_swapChain || !g_vk.queuePresentKHR) return false;

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &m_renderFinishedSemaphore;
    pi.swapchainCount = 1;
    pi.pSwapchains = &m_swapChain;
    pi.pImageIndices = &imageIndex;

    VkResult result = g_vk.queuePresentKHR(m_graphicsQueue, &pi);
    return result == VK_SUCCESS;
#else
    Q_UNUSED(imageIndex);
    return false;
#endif
}

bool VulkanRenderer::initialize() {
#if QT_CONFIG(vulkan)
    if (!loadVulkanLoader() || !g_vk.createInstance) {
        // Fallback: mark as initialized without device (software mode)
        m_initialized = true;
        emit initialized();
        qDebug() << "VulkanRenderer initialized (no loader - software mode)";
        return true;
    }

    // Try to create a Vulkan instance and device
    VkInstanceCreateInfo instanceCreateInfo{};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCreateInfo.enabledExtensionCount = 0;

    VkInstance testInstance = VK_NULL_HANDLE;
    VkResult result = g_vk.createInstance(&instanceCreateInfo, nullptr, &testInstance);

    if (result == VK_SUCCESS && testInstance) {
        bool deviceOk = createDevice(testInstance);
        m_ownsInstance = true;
        return deviceOk;
    }

    // Fallback: mark as initialized without device (software mode)
    m_initialized = true;
    emit initialized();
    qDebug() << "VulkanRenderer initialized (no device - software mode)";
    return true;
#else
    emit error("Vulkan not available - Qt built without Vulkan support");
    return false;
#endif
}

void VulkanRenderer::beginFrame() {
    m_stats.drawCalls = 0;
    m_stats.verticesProcessed = 0;
    m_stats.indicesProcessed = 0;
    m_currentPipeline = VK_NULL_HANDLE;
    m_renderPassActive = false;

    if (m_viewportWidth <= 0 || m_viewportHeight <= 0) {
        m_viewportWidth = 1920;
        m_viewportHeight = 1080;
    }

    // Software fallback framebuffer
    m_frameBuffer = QImage(m_viewportWidth, m_viewportHeight, QImage::Format_ARGB32);
    m_frameBuffer.fill(m_clearColor);

    // Real Vulkan: begin command buffer recording
    if (!m_device || !g_vk.beginCommandBuffer)
        return;

    // Acquire swap chain image if available
    uint32_t imageIndex = 0;
    if (m_swapChain && m_imageAvailableSemaphore) {
        if (!acquireNextImage(imageIndex)) {
            return;
        }

        // Wait for previous frame on this image
        if (m_fence) {
            g_vk.waitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);
            g_vk.resetFences(m_device, 1, &m_fence);
        }
    } else {
        // Fallback: wait for previous frame
        if (m_fence) {
            g_vk.waitForFences(m_device, 1, &m_fence, VK_TRUE, UINT64_MAX);
            g_vk.resetFences(m_device, 1, &m_fence);
        }
    }

    // Reset or allocate command buffer
    if (m_commandBuffer) {
        g_vk.resetCommandBuffer(m_commandBuffer, 0);
    } else {
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = m_commandPool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        if (g_vk.allocateCommandBuffers(m_device, &ai, &m_commandBuffer) != VK_SUCCESS) {
            m_commandBuffer = VK_NULL_HANDLE;
            return;
        }
    }

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (g_vk.beginCommandBuffer(m_commandBuffer, &bi) != VK_SUCCESS) {
        return;
    }

    // Begin render pass
    m_renderPassActive = true;
    VkClearValue clearColor;
    clearColor.color.float32[0] = m_clearColor.redF();
    clearColor.color.float32[1] = m_clearColor.greenF();
    clearColor.color.float32[2] = m_clearColor.blueF();
    clearColor.color.float32[3] = m_clearColor.alphaF();

    VkClearValue clearDepth;
    clearDepth.depthStencil = {1.0f, 0};

    VkClearValue clearValues[] = {clearColor, clearDepth};

    VkRenderPassBeginInfo rpBi{};
    rpBi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBi.renderPass = m_renderPass;
    rpBi.renderArea.offset = {0, 0};
    rpBi.renderArea.extent = {static_cast<uint32_t>(m_viewportWidth), static_cast<uint32_t>(m_viewportHeight)};
    rpBi.clearValueCount = 2;
    rpBi.pClearValues = clearValues;

    // Use swap chain framebuffer if available, otherwise use active framebuffer
    if (m_swapChain && m_swapChainFramebuffers.size() > static_cast<int>(m_currentImageIndex)) {
        rpBi.framebuffer = m_swapChainFramebuffers[m_currentImageIndex]->framebuffer();
    } else {
        VulkanFramebuffer* fb = VulkanFramebuffer::activeFramebuffer();
        if (fb && fb->framebuffer()) {
            rpBi.framebuffer = fb->framebuffer();
        } else {
            m_renderPassActive = false;
            return;
        }
    }

    g_vk.cmdBeginRenderPass(m_commandBuffer, &rpBi, VK_SUBPASS_CONTENTS_INLINE);

    // Set dynamic viewport and scissor
    VkViewport vp;
    vp.x = static_cast<float>(m_viewportX);
    vp.y = static_cast<float>(m_viewportY);
    vp.width = static_cast<float>(m_viewportWidth);
    vp.height = static_cast<float>(m_viewportHeight);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;
    g_vk.cmdSetViewport(m_commandBuffer, 0, 1, &vp);

    VkRect2D scissor;
    scissor.offset = {0, 0};
    scissor.extent = {static_cast<uint32_t>(m_viewportWidth), static_cast<uint32_t>(m_viewportHeight)};
    g_vk.cmdSetScissor(m_commandBuffer, 0, 1, &scissor);
}

void VulkanRenderer::endFrame() {
    // End real Vulkan render pass and submit
    if (m_commandBuffer && g_vk.endCommandBuffer) {
        if (m_renderPassActive)
            g_vk.cmdEndRenderPass(m_commandBuffer);
        m_renderPassActive = false;
        g_vk.endCommandBuffer(m_commandBuffer);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &m_commandBuffer;

        // Wait on image available, signal render finished
        if (m_imageAvailableSemaphore && m_renderFinishedSemaphore && m_swapChain) {
            si.waitSemaphoreCount = 1;
            si.pWaitSemaphores = &m_imageAvailableSemaphore;
            si.pWaitDstStageMask = &waitStage;
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores = &m_renderFinishedSemaphore;
        }

        g_vk.queueSubmit(m_graphicsQueue, 1, &si, m_fence);

        // Present if swap chain is active
        if (m_swapChain) {
            presentImage(m_currentImageIndex);
        }
    }

    emit frameRendered();
}

void VulkanRenderer::setViewport(int x, int y, int width, int height) {
    m_viewportX = x;
    m_viewportY = y;
    m_viewportWidth = width;
    m_viewportHeight = height;
}

void VulkanRenderer::clear(const QColor& color) {
    m_clearColor = color;
}

static QVector4D vertToVec4(const QVector3D& v) { return QVector4D(v, 1.0f); }

void VulkanRenderer::drawMesh(int vertexCount, int firstVertex) {
    m_stats.drawCalls++;
    m_stats.verticesProcessed += vertexCount;

    // Real Vulkan path
    if (m_commandBuffer && m_currentPipeline) {
        g_vk.cmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_currentPipeline);

        // Look up mesh for vertex buffer binding
        QString meshName = m_uniforms.value("_currentMesh", QString()).toString();
        if (!meshName.isEmpty() && m_meshes.contains(meshName)) {
            Mesh& mesh = m_meshes[meshName];
            if (mesh.vertexBuffer) {
                VkDeviceSize offset = 0;
                g_vk.cmdBindVertexBuffers(m_commandBuffer, 0, 1, &mesh.vertexBuffer, &offset);
            }
        }

        g_vk.cmdDraw(m_commandBuffer, vertexCount, 1, firstVertex, 0);
        return;
    }

    // Software fallback
    if (m_frameBuffer.isNull()) return;

    QMatrix4x4 mvp = m_projectionMatrix * m_viewMatrix * m_modelMatrix;
    QColor baseColor = m_uniforms.value("color", QColor(180, 180, 200)).value<QColor>();

    QString meshName = m_uniforms.value("_currentMesh", QString()).toString();
    Mesh* mesh = nullptr;
    if (!meshName.isEmpty() && m_meshes.contains(meshName))
        mesh = &m_meshes[meshName];

    for (int i = firstVertex; i + 2 < vertexCount; i += 3) {
        QVector4D clip[3];
        QPointF screen[3];
        bool valid = true;
        for (int j = 0; j < 3; ++j) {
            QVector3D pos;
            if (mesh && (i + j) < mesh->vertices.size())
                pos = mesh->vertices[i + j].position;
            else
                pos = QVector3D((float)(i + j), (float)(i + j), 0);
            clip[j] = mvp * vertToVec4(pos);
            if (qFuzzyIsNull(clip[j].w())) { valid = false; break; }
            clip[j] /= clip[j].w();
            screen[j] = QPointF((clip[j].x() * 0.5f + 0.5f) * m_viewportWidth,
                                (-clip[j].y() * 0.5f + 0.5f) * m_viewportHeight);
        }
        if (!valid) continue;

        float area = (screen[1].x() - screen[0].x()) * (screen[2].y() - screen[0].y()) -
                     (screen[2].x() - screen[0].x()) * (screen[1].y() - screen[0].y());
        if (area <= 0) continue;

        int minX = qMax(0, (int)std::min({screen[0].x(), screen[1].x(), screen[2].x()}));
        int maxX = qMin(m_viewportWidth - 1, (int)std::max({screen[0].x(), screen[1].x(), screen[2].x()}));
        int minY = qMax(0, (int)std::min({screen[0].y(), screen[1].y(), screen[2].y()}));
        int maxY = qMin(m_viewportHeight - 1, (int)std::max({screen[0].y(), screen[1].y(), screen[2].y()}));

        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                QPointF p(x + 0.5f, y + 0.5f);
                float w0 = (screen[1].x() - p.x()) * (screen[2].y() - screen[1].y()) -
                           (screen[2].x() - screen[1].x()) * (screen[1].y() - p.y());
                float w1 = (screen[2].x() - p.x()) * (screen[0].y() - screen[2].y()) -
                           (screen[0].x() - screen[2].x()) * (screen[2].y() - p.y());
                float w2 = (screen[0].x() - p.x()) * (screen[1].y() - screen[0].y()) -
                           (screen[1].x() - screen[0].x()) * (screen[0].y() - p.y());
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                m_frameBuffer.setPixelColor(x, y, baseColor);
            }
        }
    }
}

void VulkanRenderer::drawIndexedMesh(int indexCount, int firstIndex) {
    m_stats.drawCalls++;
    m_stats.indicesProcessed += indexCount;

    // Real Vulkan path
    if (m_commandBuffer && m_currentPipeline) {
        g_vk.cmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_currentPipeline);

        QString meshName = m_uniforms.value("_currentMesh", QString()).toString();
        if (!meshName.isEmpty() && m_meshes.contains(meshName)) {
            Mesh& mesh = m_meshes[meshName];
            if (mesh.vertexBuffer) {
                VkDeviceSize offset = 0;
                g_vk.cmdBindVertexBuffers(m_commandBuffer, 0, 1, &mesh.vertexBuffer, &offset);
            }
            if (mesh.indexBuffer) {
                g_vk.cmdBindIndexBuffer(m_commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
            }
        }

        g_vk.cmdDrawIndexed(m_commandBuffer, indexCount, 1, firstIndex, 0, 0);
        return;
    }

    // Software fallback
    if (m_frameBuffer.isNull()) return;

    QMatrix4x4 mvp = m_projectionMatrix * m_viewMatrix * m_modelMatrix;
    QColor baseColor = m_uniforms.value("color", QColor(180, 180, 200)).value<QColor>();
    QString meshName = m_uniforms.value("_currentMesh", QString()).toString();

    Mesh* mesh = nullptr;
    if (!meshName.isEmpty() && m_meshes.contains(meshName))
        mesh = &m_meshes[meshName];

    for (int i = firstIndex; i + 2 < indexCount; i += 3) {
        QVector4D clip[3];
        QPointF screen[3];
        bool valid = true;
        for (int j = 0; j < 3; ++j) {
            int idx = (mesh && (i + j) < mesh->indices.size()) ? mesh->indices[i + j] : (i + j);
            QVector3D pos;
            if (mesh && idx < mesh->vertices.size())
                pos = mesh->vertices[idx].position;
            else
                pos = QVector3D((float)idx, (float)idx, 0);
            clip[j] = mvp * vertToVec4(pos);
            if (qFuzzyIsNull(clip[j].w())) { valid = false; break; }
            clip[j] /= clip[j].w();
            screen[j] = QPointF((clip[j].x() * 0.5f + 0.5f) * m_viewportWidth,
                                (-clip[j].y() * 0.5f + 0.5f) * m_viewportHeight);
        }
        if (!valid) continue;

        float area = (screen[1].x() - screen[0].x()) * (screen[2].y() - screen[0].y()) -
                     (screen[2].x() - screen[0].x()) * (screen[1].y() - screen[0].y());
        if (area <= 0) continue;

        int minX = qMax(0, (int)std::min({screen[0].x(), screen[1].x(), screen[2].x()}));
        int maxX = qMin(m_viewportWidth - 1, (int)std::max({screen[0].x(), screen[1].x(), screen[2].x()}));
        int minY = qMax(0, (int)std::min({screen[0].y(), screen[1].y(), screen[2].y()}));
        int maxY = qMin(m_viewportHeight - 1, (int)std::max({screen[0].y(), screen[1].y(), screen[2].y()}));

        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                QPointF p(x + 0.5f, y + 0.5f);
                float w0 = (screen[1].x() - p.x()) * (screen[2].y() - screen[1].y()) -
                           (screen[2].x() - screen[1].x()) * (screen[1].y() - p.y());
                float w1 = (screen[2].x() - p.x()) * (screen[0].y() - screen[2].y()) -
                           (screen[0].x() - screen[2].x()) * (screen[2].y() - p.y());
                float w2 = (screen[0].x() - p.x()) * (screen[1].y() - screen[0].y()) -
                           (screen[1].x() - screen[0].x()) * (screen[0].y() - p.y());
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                m_frameBuffer.setPixelColor(x, y, baseColor);
            }
        }
    }
}

void VulkanRenderer::setProjectionMatrix(const QMatrix4x4& matrix) {
    m_projectionMatrix = matrix;
}

void VulkanRenderer::setViewMatrix(const QMatrix4x4& matrix) {
    m_viewMatrix = matrix;
}

void VulkanRenderer::setModelMatrix(const QMatrix4x4& matrix) {
    m_modelMatrix = matrix;
}

void VulkanRenderer::setPipeline(VkPipeline pipeline) {
    m_currentPipeline = pipeline;
}

void VulkanRenderer::bindShader(const QString& name) {
    m_currentShader = name;
}

void VulkanRenderer::unbindShader() {
    m_currentShader.clear();
}

void VulkanRenderer::setUniform(const QString& name, const QVariant& value) {
    m_uniforms[name] = value;
}

void VulkanRenderer::setTexture(const QString& name, const QImage& image) {
    m_textures[name] = image;
}

static void createMeshBuffer(VkDevice device, VkPhysicalDevice physDev, const void* data,
                             VkDeviceSize size, VkBufferUsageFlags usage,
                             VkBuffer& outBuf, VkDeviceMemory& outMem) {
    outBuf = VK_NULL_HANDLE;
    outMem = VK_NULL_HANDLE;
    if (!data || size == 0) return;

    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (g_vk.createBuffer(device, &bi, nullptr, &outBuf) != VK_SUCCESS) {
        outBuf = VK_NULL_HANDLE;
        return;
    }

    VkMemoryRequirements mr;
    g_vk.getBufferMemoryRequirements(device, outBuf, &mr);

    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size;
    ai.memoryTypeIndex = VulkanRenderer::findMemoryType(physDev, mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (g_vk.allocateMemory(device, &ai, nullptr, &outMem) != VK_SUCCESS) {
        g_vk.destroyBuffer(device, outBuf, nullptr); outBuf = VK_NULL_HANDLE;
        return;
    }

    g_vk.bindBufferMemory(device, outBuf, outMem, 0);

    void* mapped = nullptr;
    g_vk.mapMemory(device, outMem, 0, size, 0, &mapped);
    if (mapped) {
        memcpy(mapped, data, static_cast<size_t>(size));
        g_vk.unmapMemory(device, outMem);
    }
}

static void destroyMeshBuffer(VkDevice device, VkBuffer& buf, VkDeviceMemory& mem) {
    if (device) {
        if (mem) { g_vk.freeMemory(device, mem, nullptr); mem = VK_NULL_HANDLE; }
        if (buf) { g_vk.destroyBuffer(device, buf, nullptr); buf = VK_NULL_HANDLE; }
    }
}

void VulkanRenderer::createMesh(const QString& name, const QVector<Vertex>& vertices, const QVector<quint32>& indices) {
    Mesh mesh;
    mesh.name = name;
    mesh.vertices = vertices;
    mesh.indices = indices;

    if (m_device && g_vk.createBuffer) {
        VkDeviceSize vertSize = static_cast<VkDeviceSize>(vertices.size() * sizeof(Vertex));
        VkDeviceSize idxSize = static_cast<VkDeviceSize>(indices.size() * sizeof(quint32));

        if (vertSize > 0) {
            createMeshBuffer(m_device, m_physicalDevice, vertices.constData(), vertSize,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                mesh.vertexBuffer, mesh.vertexMemory);
        }
        if (idxSize > 0) {
            createMeshBuffer(m_device, m_physicalDevice, indices.constData(), idxSize,
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                mesh.indexBuffer, mesh.indexMemory);
        }
    }

    m_meshes[name] = mesh;
}

VulkanRenderer::Mesh* VulkanRenderer::getMesh(const QString& name) {
    if (m_meshes.contains(name)) {
        return &m_meshes[name];
    }
    return nullptr;
}

void VulkanRenderer::destroyMesh(const QString& name) {
    if (!m_meshes.contains(name)) return;
    Mesh& mesh = m_meshes[name];
    destroyMeshBuffer(m_device, mesh.vertexBuffer, mesh.vertexMemory);
    destroyMeshBuffer(m_device, mesh.indexBuffer, mesh.indexMemory);
    m_meshes.remove(name);
}

VulkanWindow::VulkanWindow(QWindow* parent)
    : QWindow(parent) {
    setSurfaceType(QSurface::VulkanSurface);
}

VulkanWindow::~VulkanWindow() {
}

void VulkanWindow::setRenderer(VulkanRenderer* renderer) {
    m_renderer = renderer;
}

void VulkanWindow::exposeEvent(QExposeEvent* event) {
    Q_UNUSED(event);
    if (isExposed() && m_renderer) {
        renderFrame();
    }
}

void VulkanWindow::resizeEvent(QResizeEvent* event) {
    m_lastSize = event->size();
    if (m_renderer && m_lastSize.width() > 0 && m_lastSize.height() > 0) {
        m_renderer->recreateSwapChain(m_lastSize.width(), m_lastSize.height());
    }
}

void VulkanWindow::renderFrame() {
    if (m_renderer) {
        m_renderer->beginFrame();
        m_renderer->endFrame();
    }
}

// ── Offscreen Rendering ─────────────────────────────────────────────────

bool VulkanRenderer::createOffscreenRenderTarget(int width, int height, VkFormat colorFormat)
{
    destroyOffscreenRenderTarget();

    if (!m_device || !g_vk.createImage) return false;

    m_offscreenExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    m_offscreenColorFormat = colorFormat;

    // Create render pass
    VkAttachmentDescription colorAtt{};
    colorAtt.format = colorFormat;
    colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAtt.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAtt{};
    depthAtt.format = VK_FORMAT_D32_SFLOAT;
    depthAtt.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkAttachmentDescription attachments[2] = {colorAtt, depthAtt};

    VkRenderPassCreateInfo rpCi{};
    rpCi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpCi.attachmentCount = 2;
    rpCi.pAttachments = attachments;
    rpCi.subpassCount = 1;
    rpCi.pSubpasses = &subpass;
    rpCi.dependencyCount = 1;
    rpCi.pDependencies = &dep;

    if (g_vk.createRenderPass(m_device, &rpCi, nullptr, &m_offscreenRenderPass) != VK_SUCCESS) {
        emit error("Failed to create offscreen render pass");
        return false;
    }

    // Create color image
    VkImageCreateInfo imgCi{};
    imgCi.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCi.imageType = VK_IMAGE_TYPE_2D;
    imgCi.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    imgCi.mipLevels = 1;
    imgCi.arrayLayers = 1;
    imgCi.format = colorFormat;
    imgCi.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCi.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgCi.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imgCi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgCi.samples = VK_SAMPLE_COUNT_1_BIT;

    if (g_vk.createImage(m_device, &imgCi, nullptr, &m_offscreenColorImage) != VK_SUCCESS) {
        g_vk.destroyRenderPass(m_device, m_offscreenRenderPass, nullptr);
        m_offscreenRenderPass = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryRequirements cmr;
    g_vk.getImageMemoryRequirements(m_device, m_offscreenColorImage, &cmr);
    VkMemoryAllocateInfo cai{};
    cai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    cai.allocationSize = cmr.size;
    cai.memoryTypeIndex = findMemoryType(m_physicalDevice, cmr.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (g_vk.allocateMemory(m_device, &cai, nullptr, &m_offscreenColorMemory) != VK_SUCCESS) {
        g_vk.destroyImage(m_device, m_offscreenColorImage, nullptr); m_offscreenColorImage = VK_NULL_HANDLE;
        g_vk.destroyRenderPass(m_device, m_offscreenRenderPass, nullptr); m_offscreenRenderPass = VK_NULL_HANDLE;
        return false;
    }
    g_vk.bindImageMemory(m_device, m_offscreenColorImage, m_offscreenColorMemory, 0);

    VkImageViewCreateInfo civi{};
    civi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    civi.image = m_offscreenColorImage;
    civi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    civi.format = colorFormat;
    civi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    civi.subresourceRange.baseMipLevel = 0;
    civi.subresourceRange.levelCount = 1;
    civi.subresourceRange.baseArrayLayer = 0;
    civi.subresourceRange.layerCount = 1;
    g_vk.createImageView(m_device, &civi, nullptr, &m_offscreenColorView);

    // Create depth image
    imgCi.format = VK_FORMAT_D32_SFLOAT;
    imgCi.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (g_vk.createImage(m_device, &imgCi, nullptr, &m_offscreenDepthImage) != VK_SUCCESS) {
        destroyOffscreenRenderTarget();
        return false;
    }

    VkMemoryRequirements dmr;
    g_vk.getImageMemoryRequirements(m_device, m_offscreenDepthImage, &dmr);
    VkMemoryAllocateInfo dai{};
    dai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    dai.allocationSize = dmr.size;
    dai.memoryTypeIndex = findMemoryType(m_physicalDevice, dmr.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (g_vk.allocateMemory(m_device, &dai, nullptr, &m_offscreenDepthMemory) != VK_SUCCESS) {
        destroyOffscreenRenderTarget();
        return false;
    }
    g_vk.bindImageMemory(m_device, m_offscreenDepthImage, m_offscreenDepthMemory, 0);

    VkImageViewCreateInfo divi{};
    divi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    divi.image = m_offscreenDepthImage;
    divi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    divi.format = VK_FORMAT_D32_SFLOAT;
    divi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    divi.subresourceRange.baseMipLevel = 0;
    divi.subresourceRange.levelCount = 1;
    divi.subresourceRange.baseArrayLayer = 0;
    divi.subresourceRange.layerCount = 1;
    g_vk.createImageView(m_device, &divi, nullptr, &m_offscreenDepthView);

    // Create framebuffer
    VkImageView fbAttachments[2] = {m_offscreenColorView, m_offscreenDepthView};
    VkFramebufferCreateInfo fbCi{};
    fbCi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbCi.renderPass = m_offscreenRenderPass;
    fbCi.attachmentCount = 2;
    fbCi.pAttachments = fbAttachments;
    fbCi.width = static_cast<uint32_t>(width);
    fbCi.height = static_cast<uint32_t>(height);
    fbCi.layers = 1;
    if (g_vk.createFramebuffer(m_device, &fbCi, nullptr, &m_offscreenFramebuffer) != VK_SUCCESS) {
        destroyOffscreenRenderTarget();
        return false;
    }

    // Allocate command buffer
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = m_commandPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    if (g_vk.allocateCommandBuffers(m_device, &ai, &m_offscreenCommandBuffer) != VK_SUCCESS) {
        m_offscreenCommandBuffer = VK_NULL_HANDLE;
        destroyOffscreenRenderTarget();
        return false;
    }

    // Create fence
    VkFenceCreateInfo fenceCi{};
    fenceCi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    g_vk.createFence(m_device, &fenceCi, nullptr, &m_offscreenFence);

    return true;
}

void VulkanRenderer::destroyOffscreenRenderTarget()
{
    if (!m_device) return;
    if (g_vk.deviceWaitIdle) g_vk.deviceWaitIdle(m_device);

    if (m_offscreenFence && g_vk.destroyFence) {
        g_vk.destroyFence(m_device, m_offscreenFence, nullptr);
        m_offscreenFence = VK_NULL_HANDLE;
    }
    if (m_offscreenCommandBuffer && m_commandPool && g_vk.freeCommandBuffers) {
        g_vk.freeCommandBuffers(m_device, m_commandPool, 1, &m_offscreenCommandBuffer);
        m_offscreenCommandBuffer = VK_NULL_HANDLE;
    }
    if (m_offscreenFramebuffer && g_vk.destroyFramebuffer) {
        g_vk.destroyFramebuffer(m_device, m_offscreenFramebuffer, nullptr);
        m_offscreenFramebuffer = VK_NULL_HANDLE;
    }
    if (m_offscreenDepthView && g_vk.destroyImageView) {
        g_vk.destroyImageView(m_device, m_offscreenDepthView, nullptr);
        m_offscreenDepthView = VK_NULL_HANDLE;
    }
    if (m_offscreenDepthImage && g_vk.destroyImage) {
        g_vk.destroyImage(m_device, m_offscreenDepthImage, nullptr);
        m_offscreenDepthImage = VK_NULL_HANDLE;
    }
    if (m_offscreenDepthMemory && g_vk.freeMemory) {
        g_vk.freeMemory(m_device, m_offscreenDepthMemory, nullptr);
        m_offscreenDepthMemory = VK_NULL_HANDLE;
    }
    if (m_offscreenColorView && g_vk.destroyImageView) {
        g_vk.destroyImageView(m_device, m_offscreenColorView, nullptr);
        m_offscreenColorView = VK_NULL_HANDLE;
    }
    if (m_offscreenColorImage && g_vk.destroyImage) {
        g_vk.destroyImage(m_device, m_offscreenColorImage, nullptr);
        m_offscreenColorImage = VK_NULL_HANDLE;
    }
    if (m_offscreenColorMemory && g_vk.freeMemory) {
        g_vk.freeMemory(m_device, m_offscreenColorMemory, nullptr);
        m_offscreenColorMemory = VK_NULL_HANDLE;
    }
    if (m_offscreenRenderPass && g_vk.destroyRenderPass) {
        g_vk.destroyRenderPass(m_device, m_offscreenRenderPass, nullptr);
        m_offscreenRenderPass = VK_NULL_HANDLE;
    }
    m_offscreenExtent = {0, 0};
}

bool VulkanRenderer::renderOffscreen(int width, int height, const std::function<void()>& drawCommands, QImage& outImage)
{
    if (!m_device || !m_offscreenRenderPass || !m_offscreenFramebuffer) return false;

    if (width != static_cast<int>(m_offscreenExtent.width) ||
        height != static_cast<int>(m_offscreenExtent.height)) {
        if (!createOffscreenRenderTarget(width, height, m_offscreenColorFormat))
            return false;
    }

    // Wait for previous offscreen render
    if (m_offscreenFence) {
        g_vk.waitForFences(m_device, 1, &m_offscreenFence, VK_TRUE, UINT64_MAX);
        g_vk.resetFences(m_device, 1, &m_offscreenFence);
    }

    VkCommandBuffer cmd = m_offscreenCommandBuffer;
    g_vk.resetCommandBuffer(cmd, 0);

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    g_vk.beginCommandBuffer(cmd, &bi);

    VkClearValue clearColor;
    clearColor.color.float32[0] = 0.0f;
    clearColor.color.float32[1] = 0.0f;
    clearColor.color.float32[2] = 0.0f;
    clearColor.color.float32[3] = 1.0f;
    VkClearValue clearDepth;
    clearDepth.depthStencil = {1.0f, 0};
    VkClearValue clearValues[2] = {clearColor, clearDepth};

    VkRenderPassBeginInfo rpBi{};
    rpBi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpBi.renderPass = m_offscreenRenderPass;
    rpBi.framebuffer = m_offscreenFramebuffer;
    rpBi.renderArea.offset = {0, 0};
    rpBi.renderArea.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    rpBi.clearValueCount = 2;
    rpBi.pClearValues = clearValues;
    g_vk.cmdBeginRenderPass(cmd, &rpBi, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport vp{};
    vp.x = 0.0f; vp.y = 0.0f;
    vp.width = static_cast<float>(width);
    vp.height = static_cast<float>(height);
    vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
    g_vk.cmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    g_vk.cmdSetScissor(cmd, 0, 1, &scissor);

    // Save/restore renderer state for the callback
    VkCommandBuffer savedCmd = m_commandBuffer;
    bool savedPassActive = m_renderPassActive;
    m_commandBuffer = cmd;
    m_renderPassActive = true;
    m_viewportWidth = width;
    m_viewportHeight = height;

    if (drawCommands) drawCommands();

    m_renderPassActive = false;
    m_commandBuffer = savedCmd;
    m_renderPassActive = savedPassActive;

    g_vk.cmdEndRenderPass(cmd);
    g_vk.endCommandBuffer(cmd);

    // Submit
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    g_vk.queueSubmit(m_graphicsQueue, 1, &si, m_offscreenFence);

    if (m_offscreenFence) {
        g_vk.waitForFences(m_device, 1, &m_offscreenFence, VK_TRUE, UINT64_MAX);
    } else {
        g_vk.queueWaitIdle(m_graphicsQueue);
    }

    // Read back pixels
    VkBuffer readbackBuf = VK_NULL_HANDLE;
    VkDeviceMemory readbackMem = VK_NULL_HANDLE;
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width * height * 4);

    VkBufferCreateInfo rbCi{};
    rbCi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    rbCi.size = imageSize;
    rbCi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    rbCi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    bool readbackOk = false;
    if (g_vk.createBuffer(m_device, &rbCi, nullptr, &readbackBuf) == VK_SUCCESS) {
        VkMemoryRequirements rmr;
        g_vk.getBufferMemoryRequirements(m_device, readbackBuf, &rmr);
        VkMemoryAllocateInfo rai{};
        rai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        rai.allocationSize = rmr.size;
        rai.memoryTypeIndex = findMemoryType(m_physicalDevice, rmr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (g_vk.allocateMemory(m_device, &rai, nullptr, &readbackMem) == VK_SUCCESS) {
            g_vk.bindBufferMemory(m_device, readbackBuf, readbackMem, 0);

            // One more command buffer to copy image → buffer
            VkCommandBuffer copyCmd = VK_NULL_HANDLE;
            VkCommandBufferAllocateInfo cai{};
            cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cai.commandPool = m_commandPool;
            cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cai.commandBufferCount = 1;
            if (g_vk.allocateCommandBuffers(m_device, &cai, &copyCmd) == VK_SUCCESS) {
                g_vk.resetCommandBuffer(copyCmd, 0);
                VkCommandBufferBeginInfo cbi{};
                cbi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                cbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                g_vk.beginCommandBuffer(copyCmd, &cbi);

                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.image = m_offscreenColorImage;
                barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;
                barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                g_vk.cmdPipelineBarrier(copyCmd,
                    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                    VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);

                VkBufferImageCopy region{};
                region.bufferOffset = 0;
                region.bufferRowLength = 0;
                region.bufferImageHeight = 0;
                region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                region.imageSubresource.mipLevel = 0;
                region.imageSubresource.baseArrayLayer = 0;
                region.imageSubresource.layerCount = 1;
                region.imageOffset = {0, 0, 0};
                region.imageExtent = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
                g_vk.cmdCopyImageToBuffer(copyCmd, m_offscreenColorImage,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readbackBuf, 1, &region);
                g_vk.endCommandBuffer(copyCmd);

                VkSubmitInfo csi{};
                csi.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                csi.commandBufferCount = 1;
                csi.pCommandBuffers = &copyCmd;
                g_vk.queueSubmit(m_graphicsQueue, 1, &csi, VK_NULL_HANDLE);
                g_vk.queueWaitIdle(m_graphicsQueue);
                g_vk.freeCommandBuffers(m_device, m_commandPool, 1, &copyCmd);

                // Map and read back
                void* mapped = nullptr;
                if (g_vk.mapMemory(m_device, readbackMem, 0, imageSize, 0, &mapped) == VK_SUCCESS) {
                    QImage result(reinterpret_cast<const uchar*>(mapped), width, height, QImage::Format_RGBA8888);
                    outImage = result.copy(); // Deep copy
                    g_vk.unmapMemory(m_device, readbackMem);
                    readbackOk = true;
                }

                // Restore layout for next render
                VkCommandBuffer restoreCmd = VK_NULL_HANDLE;
                if (g_vk.allocateCommandBuffers(m_device, &cai, &restoreCmd) == VK_SUCCESS) {
                    g_vk.resetCommandBuffer(restoreCmd, 0);
                    g_vk.beginCommandBuffer(restoreCmd, &cbi);
                    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
                    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
                    g_vk.cmdPipelineBarrier(restoreCmd,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);
                    g_vk.endCommandBuffer(restoreCmd);
                    VkSubmitInfo rsi{};
                    rsi.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                    rsi.commandBufferCount = 1;
                    rsi.pCommandBuffers = &restoreCmd;
                    g_vk.queueSubmit(m_graphicsQueue, 1, &rsi, VK_NULL_HANDLE);
                    g_vk.queueWaitIdle(m_graphicsQueue);
                    g_vk.freeCommandBuffers(m_device, m_commandPool, 1, &restoreCmd);
                }
            }
        }
    }

    if (readbackBuf) g_vk.destroyBuffer(m_device, readbackBuf, nullptr);
    if (readbackMem) g_vk.freeMemory(m_device, readbackMem, nullptr);

    return readbackOk;
}

// ── Helper: compile GLSL to SPIR-V ──────────────────────────────────────

static QByteArray compileGlslToSpirv(const QString& glsl, const QString& stageFlag) {
    QString tmpPath = QDir::tempPath() + QStringLiteral("/ks_shader_%1.glsl")
        .arg(reinterpret_cast<quintptr>(&glsl), 0, 16);
    {
        QFile f(tmpPath);
        if (f.open(QIODevice::WriteOnly))
            f.write(glsl.toUtf8());
    }

    QProcess proc;
    QStringList args;
    args << QStringLiteral("-V") << tmpPath << QStringLiteral("-S") << stageFlag;
    proc.start(QStringLiteral("glslangValidator"), args);
    QByteArray spirv;
    if (proc.waitForFinished(30000) && proc.exitCode() == 0) {
        QString spvPath = tmpPath + QStringLiteral(".spv");
        QFile spvFile(spvPath);
        if (spvFile.open(QIODevice::ReadOnly)) {
            spirv = spvFile.readAll();
            spvFile.close();
            QFile::remove(spvPath);
        }
    }
    QFile::remove(tmpPath);

    return spirv;
}

// ── VulkanRenderPass ────────────────────────────────────────────────────

VulkanRenderPass::VulkanRenderPass(QObject* parent)
    : QObject(parent) {
}

VulkanRenderPass::~VulkanRenderPass() {
    destroy();
}

void VulkanRenderPass::setDevice(VkDevice device) {
    m_device = device;
}

void VulkanRenderPass::setColorFormat(VkFormat format) {
    m_colorFormat = format;
}

void VulkanRenderPass::setVertexShader(const QString& source) {
    m_vertexShader = source;
}

void VulkanRenderPass::setFragmentShader(const QString& source) {
    m_fragmentShader = source;
}

void VulkanRenderPass::setComputeShader(const QString& source) {
    m_computeShader = source;
}

void VulkanRenderPass::setDepthFormat(VkFormat format) {
    m_depthFormat = format;
}

void VulkanRenderPass::setEnableDepth(bool enable) {
    m_enableDepth = enable;
}

bool VulkanRenderPass::compile() {
    m_error.clear();

    if (m_vertexShader.isEmpty() && m_computeShader.isEmpty()) {
        m_error = QStringLiteral("No vertex or compute shader provided");
        emit error(m_error);
        return false;
    }
    if (m_vertexShader.isEmpty() && !m_fragmentShader.isEmpty()) {
        m_error = QStringLiteral("Vertex shader required when fragment shader is provided");
        emit error(m_error);
        return false;
    }

    // Software fallback: no device → just validate
    if (!m_device || !g_vk.createShaderModule) {
        m_compiled = true;
        emit compiled();
        return true;
    }

    // ── Compile vertex shader ──────────────────────────────────────────
    if (!m_vertexShader.isEmpty()) {
        QByteArray spirv = compileGlslToSpirv(m_vertexShader, QStringLiteral("vert"));
        if (spirv.isEmpty()) {
            m_error = QStringLiteral("Failed to compile vertex shader (glslangValidator not available)");
            emit error(m_error);
            return false;
        }
        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = static_cast<uint32_t>(spirv.size());
        ci.pCode = reinterpret_cast<const uint32_t*>(spirv.constData());
        if (g_vk.createShaderModule(m_device, &ci, nullptr, &m_vertModule) != VK_SUCCESS) {
            m_error = QStringLiteral("Failed to create vertex shader module");
            emit error(m_error);
            return false;
        }
    }

    // ── Compile fragment shader ────────────────────────────────────────
    if (!m_fragmentShader.isEmpty()) {
        QByteArray spirv = compileGlslToSpirv(m_fragmentShader, QStringLiteral("frag"));
        if (spirv.isEmpty()) {
            if (m_vertModule) { g_vk.destroyShaderModule(m_device, m_vertModule, nullptr); m_vertModule = VK_NULL_HANDLE; }
            m_error = QStringLiteral("Failed to compile fragment shader (glslangValidator not available)");
            emit error(m_error);
            return false;
        }
        VkShaderModuleCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = static_cast<uint32_t>(spirv.size());
        ci.pCode = reinterpret_cast<const uint32_t*>(spirv.constData());
        if (g_vk.createShaderModule(m_device, &ci, nullptr, &m_fragModule) != VK_SUCCESS) {
            g_vk.destroyShaderModule(m_device, m_vertModule, nullptr); m_vertModule = VK_NULL_HANDLE;
            m_error = QStringLiteral("Failed to create fragment shader module");
            emit error(m_error);
            return false;
        }
    }

    // ── Create render pass ─────────────────────────────────────────────
    QVector<VkAttachmentDescription> attachments;

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_colorFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments.append(colorAttachment);

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription depthAttachment{};
    bool hasDepth = m_enableDepth;
    if (hasDepth) {
        depthAttachment.format = m_depthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments.append(depthAttachment);
    }

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    if (hasDepth) {
        subpass.pDepthStencilAttachment = &depthRef;
    }

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    if (hasDepth) {
        dep.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    }

    VkRenderPassCreateInfo rpCi{};
    rpCi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpCi.attachmentCount = static_cast<uint32_t>(attachments.size());
    rpCi.pAttachments = attachments.constData();
    rpCi.subpassCount = 1;
    rpCi.pSubpasses = &subpass;
    rpCi.dependencyCount = 1;
    rpCi.pDependencies = &dep;

    if (g_vk.createRenderPass(m_device, &rpCi, nullptr, &m_renderPass) != VK_SUCCESS) {
        if (m_fragModule) { g_vk.destroyShaderModule(m_device, m_fragModule, nullptr); m_fragModule = VK_NULL_HANDLE; }
        if (m_vertModule) { g_vk.destroyShaderModule(m_device, m_vertModule, nullptr); m_vertModule = VK_NULL_HANDLE; }
        m_error = QStringLiteral("Failed to create render pass");
        emit error(m_error);
        return false;
    }

    // ── Create pipeline layout ─────────────────────────────────────────
    VkPipelineLayoutCreateInfo plCi{};
    plCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (g_vk.createPipelineLayout(m_device, &plCi, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        g_vk.destroyRenderPass(m_device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE;
        if (m_fragModule) { g_vk.destroyShaderModule(m_device, m_fragModule, nullptr); m_fragModule = VK_NULL_HANDLE; }
        if (m_vertModule) { g_vk.destroyShaderModule(m_device, m_vertModule, nullptr); m_vertModule = VK_NULL_HANDLE; }
        m_error = QStringLiteral("Failed to create pipeline layout");
        emit error(m_error);
        return false;
    }

    // ── Create graphics pipeline ───────────────────────────────────────
    QVector<VkPipelineShaderStageCreateInfo> stages;
    if (m_vertModule) {
        VkPipelineShaderStageCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        sci.stage = VK_SHADER_STAGE_VERTEX_BIT;
        sci.module = m_vertModule;
        sci.pName = "main";
        stages.append(sci);
    }
    if (m_fragModule) {
        VkPipelineShaderStageCreateInfo sci{};
        sci.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        sci.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        sci.module = m_fragModule;
        sci.pName = "main";
        stages.append(sci);
    }

    // Vertex input: position (vec3), normal (vec3), uv (vec2), color (vec4)
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(VulkanRenderer::Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[4]{};
    attrs[0].location = 0; attrs[0].binding = 0; attrs[0].format = VK_FORMAT_R32G32B32_SFLOAT;  attrs[0].offset = offsetof(VulkanRenderer::Vertex, position);
    attrs[1].location = 1; attrs[1].binding = 0; attrs[1].format = VK_FORMAT_R32G32B32_SFLOAT;  attrs[1].offset = offsetof(VulkanRenderer::Vertex, normal);
    attrs[2].location = 2; attrs[2].binding = 0; attrs[2].format = VK_FORMAT_R32G32_SFLOAT;      attrs[2].offset = offsetof(VulkanRenderer::Vertex, uv);
    attrs[3].location = 3; attrs[3].binding = 0; attrs[3].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[3].offset = offsetof(VulkanRenderer::Vertex, color);

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &binding;
    vi.vertexAttributeDescriptionCount = 4;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vs{};
    vs.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vs.viewportCount = 1;
    vs.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cbAtt{};
    cbAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cbAtt;

    // Depth stencil state
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = hasDepth ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = hasDepth ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    ds.depthBoundsTestEnable = VK_FALSE;
    ds.stencilTestEnable = VK_FALSE;

    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo gpCi{};
    gpCi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpCi.stageCount = static_cast<uint32_t>(stages.size());
    gpCi.pStages = stages.constData();
    gpCi.pVertexInputState = &vi;
    gpCi.pInputAssemblyState = &ia;
    gpCi.pViewportState = &vs;
    gpCi.pRasterizationState = &rs;
    gpCi.pMultisampleState = &ms;
    gpCi.pColorBlendState = &cb;
    gpCi.pDepthStencilState = &ds;
    gpCi.pDynamicState = &dyn;
    gpCi.layout = m_pipelineLayout;
    gpCi.renderPass = m_renderPass;
    gpCi.subpass = 0;

    if (g_vk.createGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &gpCi, nullptr, &m_pipeline) != VK_SUCCESS) {
        g_vk.destroyPipelineLayout(m_device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE;
        g_vk.destroyRenderPass(m_device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE;
        if (m_fragModule) { g_vk.destroyShaderModule(m_device, m_fragModule, nullptr); m_fragModule = VK_NULL_HANDLE; }
        if (m_vertModule) { g_vk.destroyShaderModule(m_device, m_vertModule, nullptr); m_vertModule = VK_NULL_HANDLE; }
        m_error = QStringLiteral("Failed to create graphics pipeline");
        emit error(m_error);
        return false;
    }

    m_compiled = true;
    emit compiled();
    return true;
}

void VulkanRenderPass::destroy() {
    if (m_device && g_vk.destroyPipeline) {
        if (m_pipeline)         { g_vk.destroyPipeline(m_device, m_pipeline, nullptr); m_pipeline = VK_NULL_HANDLE; }
        if (m_pipelineLayout)   { g_vk.destroyPipelineLayout(m_device, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
        if (m_renderPass)       { g_vk.destroyRenderPass(m_device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE; }
        if (m_fragModule)       { g_vk.destroyShaderModule(m_device, m_fragModule, nullptr); m_fragModule = VK_NULL_HANDLE; }
        if (m_vertModule)       { g_vk.destroyShaderModule(m_device, m_vertModule, nullptr); m_vertModule = VK_NULL_HANDLE; }
    }
    m_compiled = false;
    m_vertexShader.clear();
    m_fragmentShader.clear();
    m_computeShader.clear();
}

// ── VulkanBuffer ────────────────────────────────────────────────────────

VulkanBuffer::VulkanBuffer(Type type, QObject* parent)
    : QObject(parent), m_type(type) {
}

VulkanBuffer::~VulkanBuffer() {
    if (m_device && g_vk.destroyBuffer) {
        if (m_mappedPtr) { g_vk.unmapMemory(m_device, m_memory); m_mappedPtr = nullptr; }
        if (m_memory)    { g_vk.freeMemory(m_device, m_memory, nullptr); m_memory = VK_NULL_HANDLE; }
        if (m_buffer)    { g_vk.destroyBuffer(m_device, m_buffer, nullptr); m_buffer = VK_NULL_HANDLE; }
    }
    m_data.clear();
    m_size = 0;
    m_valid = false;
}

void VulkanBuffer::setDevice(VkDevice device, VkPhysicalDevice physicalDevice) {
    m_device = device;
    m_physicalDevice = physicalDevice;
}

void VulkanBuffer::allocate(quint64 size, const void* data) {
    m_size = size;

    // Software fallback
    if (!m_device || !g_vk.createBuffer) {
        if (data && size > 0) {
            m_data.resize(static_cast<int>(size));
            memcpy(m_data.data(), data, static_cast<size_t>(size));
        } else if (size > 0) {
            m_data.resize(static_cast<int>(size));
        }
        m_valid = true;
        emit allocated();
        return;
    }

    // Real Vulkan buffer
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    switch (m_type) {
        case VertexBuffer:   usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;   break;
        case IndexBuffer:    usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;    break;
        case UniformBuffer:  usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;  memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT; break;
        case StorageBuffer:  usage |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;  break;
    }

    VkBufferCreateInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (g_vk.createBuffer(m_device, &bi, nullptr, &m_buffer) != VK_SUCCESS) {
        qWarning() << "VulkanBuffer: Failed to create buffer";
        return;
    }

    VkMemoryRequirements memReqs;
    g_vk.getBufferMemoryRequirements(m_device, m_buffer, &memReqs);

    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = memReqs.size;
    ai.memoryTypeIndex = VulkanRenderer::findMemoryType(m_physicalDevice, memReqs.memoryTypeBits, memProps);

    if (g_vk.allocateMemory(m_device, &ai, nullptr, &m_memory) != VK_SUCCESS) {
        g_vk.destroyBuffer(m_device, m_buffer, nullptr); m_buffer = VK_NULL_HANDLE;
        qWarning() << "VulkanBuffer: Failed to allocate memory";
        return;
    }

    g_vk.bindBufferMemory(m_device, m_buffer, m_memory, 0);

    // Upload initial data
    if (data && size > 0) {
        void* mapped = nullptr;
        g_vk.mapMemory(m_device, m_memory, 0, size, 0, &mapped);
        if (mapped) {
            memcpy(mapped, data, static_cast<size_t>(size));
            g_vk.unmapMemory(m_device, m_memory);
        }
    }

    m_valid = true;
    emit allocated();
}

void VulkanBuffer::update(quint64 offset, quint64 size, const void* data) {
    if (!data || size == 0 || offset + size > m_size) {
        emit updated();
        return;
    }

    // Software fallback
    if (!m_device || !m_buffer) {
        int dstOffset = static_cast<int>(offset);
        int copySize = static_cast<int>(size);
        if (dstOffset + copySize > m_data.size())
            m_data.resize(dstOffset + copySize);
        memcpy(m_data.data() + dstOffset, data, static_cast<size_t>(copySize));
        emit updated();
        return;
    }

    // Real Vulkan: map and copy
    void* mapped = nullptr;
    g_vk.mapMemory(m_device, m_memory, offset, size, 0, &mapped);
    if (mapped) {
        memcpy(mapped, data, static_cast<size_t>(size));
        g_vk.unmapMemory(m_device, m_memory);
    }
    emit updated();
}

void* VulkanBuffer::map() {
    if (!m_device || !m_memory)
        return m_data.isEmpty() ? nullptr : m_data.data();

    if (!m_mappedPtr)
        g_vk.mapMemory(m_device, m_memory, 0, m_size, 0, &m_mappedPtr);
    return m_mappedPtr;
}

void VulkanBuffer::unmap() {
    if (m_device && m_mappedPtr) {
        g_vk.unmapMemory(m_device, m_memory);
        m_mappedPtr = nullptr;
    }
}

// ── VulkanTexture ───────────────────────────────────────────────────────

VulkanTexture::VulkanTexture(QObject* parent)
    : QObject(parent) {
}

VulkanTexture::~VulkanTexture() {
    if (m_device && g_vk.destroySampler) {
        if (m_sampler)    { g_vk.destroySampler(m_device, m_sampler, nullptr); m_sampler = VK_NULL_HANDLE; }
        if (m_imageView)  { g_vk.destroyImageView(m_device, m_imageView, nullptr); m_imageView = VK_NULL_HANDLE; }
        if (m_image)      { g_vk.destroyImage(m_device, m_image, nullptr); m_image = VK_NULL_HANDLE; }
        if (m_memory)     { g_vk.freeMemory(m_device, m_memory, nullptr); m_memory = VK_NULL_HANDLE; }
    }
}

void VulkanTexture::setDevice(VkDevice device, VkPhysicalDevice physicalDevice, VkQueue graphicsQueue) {
    m_device = device;
    m_physicalDevice = physicalDevice;
    m_graphicsQueue = graphicsQueue;
}

static VkImageAspectFlags formatToAspect(VkFormat fmt) {
    if (fmt == VK_FORMAT_D32_SFLOAT || fmt == VK_FORMAT_D16_UNORM ||
        fmt == VK_FORMAT_D24_UNORM_S8_UINT || fmt == VK_FORMAT_D32_SFLOAT_S8_UINT)
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    return VK_IMAGE_ASPECT_COLOR_BIT;
}

void VulkanTexture::createFromImage(const QImage& image) {
    m_width = image.width();
    m_height = image.height();
    m_format = RGBA8;
    m_vkFormat = VK_FORMAT_R8G8B8A8_SRGB;
    m_aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;

    // Software fallback
    if (!m_device || !g_vk.createImage) {
        m_imageData = image;
        emit created();
        return;
    }

    // Convert QImage to RGBA8888
    QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    QByteArray pixels(reinterpret_cast<const char*>(rgba.constBits()),
                      rgba.width() * rgba.height() * 4);

    // Create staging buffer
    VkBuffer stagingBuf = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;

    VkBufferCreateInfo sbCi{};
    sbCi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    sbCi.size = pixels.size();
    sbCi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    sbCi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (g_vk.createBuffer(m_device, &sbCi, nullptr, &stagingBuf) != VK_SUCCESS) {
        m_imageData = image;
        emit created();
        return;
    }

    VkMemoryRequirements smr;
    g_vk.getBufferMemoryRequirements(m_device, stagingBuf, &smr);

    VkMemoryAllocateInfo sai{};
    sai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    sai.allocationSize = smr.size;
    sai.memoryTypeIndex = VulkanRenderer::findMemoryType(m_physicalDevice, smr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (g_vk.allocateMemory(m_device, &sai, nullptr, &stagingMem) != VK_SUCCESS) {
        g_vk.destroyBuffer(m_device, stagingBuf, nullptr);
        m_imageData = image;
        emit created();
        return;
    }

    g_vk.bindBufferMemory(m_device, stagingBuf, stagingMem, 0);

    void* stagingData = nullptr;
    g_vk.mapMemory(m_device, stagingMem, 0, pixels.size(), 0, &stagingData);
    if (stagingData) {
        memcpy(stagingData, pixels.constData(), static_cast<size_t>(pixels.size()));
        g_vk.unmapMemory(m_device, stagingMem);
    }

    // Calculate mip levels
    m_mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(m_width, m_height)))) + 1;

    // Create VkImage
    VkImageCreateInfo imgCi{};
    imgCi.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCi.imageType = VK_IMAGE_TYPE_2D;
    imgCi.extent.width = static_cast<uint32_t>(m_width);
    imgCi.extent.height = static_cast<uint32_t>(m_height);
    imgCi.extent.depth = 1;
    imgCi.mipLevels = m_mipLevels;
    imgCi.arrayLayers = 1;
    imgCi.format = m_vkFormat;
    imgCi.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCi.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgCi.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgCi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgCi.samples = VK_SAMPLE_COUNT_1_BIT;

    if (g_vk.createImage(m_device, &imgCi, nullptr, &m_image) != VK_SUCCESS) {
        g_vk.freeMemory(m_device, stagingMem, nullptr);
        g_vk.destroyBuffer(m_device, stagingBuf, nullptr);
        m_imageData = image;
        emit created();
        return;
    }

    VkMemoryRequirements imr;
    g_vk.getImageMemoryRequirements(m_device, m_image, &imr);

    VkMemoryAllocateInfo iai{};
    iai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    iai.allocationSize = imr.size;
    iai.memoryTypeIndex = VulkanRenderer::findMemoryType(m_physicalDevice, imr.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (g_vk.allocateMemory(m_device, &iai, nullptr, &m_memory) != VK_SUCCESS) {
        g_vk.destroyImage(m_device, m_image, nullptr); m_image = VK_NULL_HANDLE;
        g_vk.freeMemory(m_device, stagingMem, nullptr);
        g_vk.destroyBuffer(m_device, stagingBuf, nullptr);
        m_imageData = image;
        emit created();
        return;
    }

    g_vk.bindImageMemory(m_device, m_image, m_memory, 0);

    // Upload pixel data via one-shot command buffer using a temporary pool
    VkCommandPool uploadPool = VK_NULL_HANDLE;
    VkCommandBuffer uploadCmd = VK_NULL_HANDLE;

    if (m_graphicsQueue && g_vk.createCommandPool) {
        VkCommandPoolCreateInfo poolCi{};
        poolCi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolCi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolCi.queueFamilyIndex = 0;
        if (g_vk.createCommandPool(m_device, &poolCi, nullptr, &uploadPool) == VK_SUCCESS) {
            VkCommandBufferAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            ai.commandPool = uploadPool;
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;
            if (g_vk.allocateCommandBuffers(m_device, &ai, &uploadCmd) == VK_SUCCESS) {
                VkCommandBufferBeginInfo bi{};
                bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

                if (g_vk.beginCommandBuffer(uploadCmd, &bi) == VK_SUCCESS) {
                    // Transition image layout: UNDEFINED → TRANSFER_DST_OPTIMAL
                    VkImageMemoryBarrier barrier{};
                    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.image = m_image;
                    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    barrier.subresourceRange.baseMipLevel = 0;
                    barrier.subresourceRange.levelCount = 1;
                    barrier.subresourceRange.baseArrayLayer = 0;
                    barrier.subresourceRange.layerCount = 1;
                    barrier.srcAccessMask = 0;
                    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

                    g_vk.cmdPipelineBarrier(uploadCmd,
                        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

                    // Copy buffer to image
                    VkBufferImageCopy region{};
                    region.bufferOffset = 0;
                    region.bufferRowLength = 0;
                    region.bufferImageHeight = 0;
                    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                    region.imageSubresource.mipLevel = 0;
                    region.imageSubresource.baseArrayLayer = 0;
                    region.imageSubresource.layerCount = 1;
                    region.imageOffset = {0, 0, 0};
                    region.imageExtent = {static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), 1};

                    g_vk.cmdCopyBufferToImage(uploadCmd, stagingBuf, m_image,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

                    // Transition image layout: TRANSFER_DST_OPTIMAL → SHADER_READ_ONLY_OPTIMAL
                    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
                    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

                    g_vk.cmdPipelineBarrier(uploadCmd,
                        VK_PIPELINE_STAGE_TRANSFER_BIT,
                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);

                    g_vk.endCommandBuffer(uploadCmd);

                    // Submit and wait
                    VkSubmitInfo si{};
                    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                    si.commandBufferCount = 1;
                    si.pCommandBuffers = &uploadCmd;

                    g_vk.queueSubmit(m_graphicsQueue, 1, &si, VK_NULL_HANDLE);
                    g_vk.queueWaitIdle(m_graphicsQueue);
                }
            }
        }
    }

    // Clean up staging and upload resources
    g_vk.freeMemory(m_device, stagingMem, nullptr);
    g_vk.destroyBuffer(m_device, stagingBuf, nullptr);
    if (uploadCmd && uploadPool) g_vk.freeCommandBuffers(m_device, uploadPool, 1, &uploadCmd);
    if (uploadPool) g_vk.destroyCommandPool(m_device, uploadPool, nullptr);

    // Create image view (full mip chain)
    VkImageViewCreateInfo ivCi{};
    ivCi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivCi.image = m_image;
    ivCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivCi.format = m_vkFormat;
    ivCi.subresourceRange.aspectMask = m_aspectMask;
    ivCi.subresourceRange.baseMipLevel = 0;
    ivCi.subresourceRange.levelCount = m_mipLevels;
    ivCi.subresourceRange.baseArrayLayer = 0;
    ivCi.subresourceRange.layerCount = 1;

    g_vk.createImageView(m_device, &ivCi, nullptr, &m_imageView);

    // Create sampler with mipmapping
    VkSamplerCreateInfo sCi{};
    sCi.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sCi.magFilter = VK_FILTER_LINEAR;
    sCi.minFilter = VK_FILTER_LINEAR;
    sCi.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sCi.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sCi.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sCi.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sCi.minLod = 0.0f;
    sCi.maxLod = static_cast<float>(m_mipLevels);
    sCi.mipLodBias = 0.0f;

    g_vk.createSampler(m_device, &sCi, nullptr, &m_sampler);

    m_imageData = image;
    emit created();
}

void VulkanTexture::createRenderTarget(int width, int height, Format format) {
    m_width = width;
    m_height = height;
    m_format = format;
    m_vkFormat = toVkFormat(format);
    m_aspectMask = formatToAspect(m_vkFormat);
    m_imageData = QImage(width, height, QImage::Format_ARGB32);
    m_imageData.fill(Qt::black);

    // Software fallback
    if (!m_device || !g_vk.createImage) {
        emit created();
        return;
    }

    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    if (m_aspectMask & VK_IMAGE_ASPECT_DEPTH_BIT) {
        usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        m_mipLevels = 1; // depth doesn't support mip chains
    } else {
        usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        m_mipLevels = static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1;
    }

    VkImageCreateInfo imgCi{};
    imgCi.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgCi.imageType = VK_IMAGE_TYPE_2D;
    imgCi.extent.width = static_cast<uint32_t>(width);
    imgCi.extent.height = static_cast<uint32_t>(height);
    imgCi.extent.depth = 1;
    imgCi.mipLevels = m_mipLevels;
    imgCi.arrayLayers = 1;
    imgCi.format = m_vkFormat;
    imgCi.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgCi.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgCi.usage = usage;
    imgCi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgCi.samples = VK_SAMPLE_COUNT_1_BIT;

    if (g_vk.createImage(m_device, &imgCi, nullptr, &m_image) != VK_SUCCESS)
        return;

    VkMemoryRequirements imr;
    g_vk.getImageMemoryRequirements(m_device, m_image, &imr);

    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = imr.size;
    ai.memoryTypeIndex = VulkanRenderer::findMemoryType(m_physicalDevice, imr.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (g_vk.allocateMemory(m_device, &ai, nullptr, &m_memory) != VK_SUCCESS) {
        g_vk.destroyImage(m_device, m_image, nullptr); m_image = VK_NULL_HANDLE;
        return;
    }

    g_vk.bindImageMemory(m_device, m_image, m_memory, 0);

    // Create image view
    VkImageViewCreateInfo ivCi{};
    ivCi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivCi.image = m_image;
    ivCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivCi.format = m_vkFormat;
    ivCi.subresourceRange.aspectMask = m_aspectMask;
    ivCi.subresourceRange.baseMipLevel = 0;
    ivCi.subresourceRange.levelCount = m_mipLevels;
    ivCi.subresourceRange.baseArrayLayer = 0;
    ivCi.subresourceRange.layerCount = 1;

    g_vk.createImageView(m_device, &ivCi, nullptr, &m_imageView);

    // Sampler for render targets (with mipmapping)
    VkSamplerCreateInfo sCi{};
    sCi.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sCi.magFilter = VK_FILTER_LINEAR;
    sCi.minFilter = VK_FILTER_LINEAR;
    sCi.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sCi.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sCi.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sCi.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sCi.minLod = 0.0f;
    sCi.maxLod = static_cast<float>(m_mipLevels);
    sCi.mipLodBias = 0.0f;

    g_vk.createSampler(m_device, &sCi, nullptr, &m_sampler);

    emit created();
}

void VulkanTexture::generateMipmaps() {
    // CPU fallback
    if (!m_device || !m_image || m_mipLevels <= 1) {
        if (m_imageData.isNull()) return;
        int w = m_width, h = m_height;
        while (w > 1 || h > 1) {
            w = qMax(1, w / 2);
            h = qMax(1, h / 2);
            QImage mip = m_imageData.scaled(w, h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            m_mipData.append(mip);
        }
        return;
    }

    // GPU mip generation via vkCmdBlitImage
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;

    VkCommandPoolCreateInfo poolCi{};
    poolCi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolCi.queueFamilyIndex = 0;
    if (g_vk.createCommandPool(m_device, &poolCi, nullptr, &pool) != VK_SUCCESS)
        return;

    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    if (g_vk.allocateCommandBuffers(m_device, &ai, &cmd) != VK_SUCCESS) {
        g_vk.destroyCommandPool(m_device, pool, nullptr);
        return;
    }

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (g_vk.beginCommandBuffer(cmd, &bi) != VK_SUCCESS) {
        g_vk.freeCommandBuffers(m_device, pool, 1, &cmd);
        g_vk.destroyCommandPool(m_device, pool, nullptr);
        return;
    }

    int32_t mipW = m_width, mipH = m_height;
    for (uint32_t i = 1; i < m_mipLevels; ++i) {
        int32_t nextW = qMax(1, mipW / 2);
        int32_t nextH = qMax(1, mipH / 2);

        // Barrier: previous level → TRANSFER_SRC_OPTIMAL
        // (level 0 is in SHADER_READ_ONLY_OPTIMAL after upload; later levels are in TRANSFER_DST_OPTIMAL)
        VkImageMemoryBarrier srcBarrier{};
        srcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        srcBarrier.image = m_image;
        srcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        srcBarrier.subresourceRange.baseMipLevel = i - 1;
        srcBarrier.subresourceRange.levelCount = 1;
        srcBarrier.subresourceRange.baseArrayLayer = 0;
        srcBarrier.subresourceRange.layerCount = 1;
        srcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        if (i == 1) {
            srcBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            srcBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        } else {
            srcBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            srcBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }

        // Barrier: current level → TRANSFER_DST_OPTIMAL
        VkImageMemoryBarrier dstBarrier{};
        dstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        dstBarrier.image = m_image;
        dstBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        dstBarrier.subresourceRange.baseMipLevel = i;
        dstBarrier.subresourceRange.levelCount = 1;
        dstBarrier.subresourceRange.baseArrayLayer = 0;
        dstBarrier.subresourceRange.layerCount = 1;
        dstBarrier.srcAccessMask = 0;
        dstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        dstBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

        VkImageMemoryBarrier barriers[2] = {srcBarrier, dstBarrier};
        g_vk.cmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 2, barriers);

        // Blit: previous level → current level
        VkImageBlit blit{};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipW, mipH, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = {nextW, nextH, 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        g_vk.cmdBlitImage(cmd, m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        mipW = nextW;
        mipH = nextH;
    }

    // Barrier: previous levels (TRANSFER_SRC_OPTIMAL) → SHADER_READ_ONLY_OPTIMAL
    VkImageMemoryBarrier finalBarriers[2]{};
    if (m_mipLevels > 1) {
        finalBarriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        finalBarriers[0].image = m_image;
        finalBarriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        finalBarriers[0].subresourceRange.baseMipLevel = 0;
        finalBarriers[0].subresourceRange.levelCount = m_mipLevels - 1;
        finalBarriers[0].subresourceRange.baseArrayLayer = 0;
        finalBarriers[0].subresourceRange.layerCount = 1;
        finalBarriers[0].srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        finalBarriers[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        finalBarriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        finalBarriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    // Barrier: last level (TRANSFER_DST_OPTIMAL) → SHADER_READ_ONLY_OPTIMAL
    finalBarriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    finalBarriers[1].image = m_image;
    finalBarriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    finalBarriers[1].subresourceRange.baseMipLevel = m_mipLevels - 1;
    finalBarriers[1].subresourceRange.levelCount = 1;
    finalBarriers[1].subresourceRange.baseArrayLayer = 0;
    finalBarriers[1].subresourceRange.layerCount = 1;
    finalBarriers[1].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    finalBarriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    finalBarriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    finalBarriers[1].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    uint32_t finalBarrierCount = (m_mipLevels > 1) ? 2 : 1;
    g_vk.cmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, finalBarrierCount, finalBarriers);

    g_vk.endCommandBuffer(cmd);

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;

    if (m_graphicsQueue) {
        g_vk.queueSubmit(m_graphicsQueue, 1, &si, VK_NULL_HANDLE);
        g_vk.queueWaitIdle(m_graphicsQueue);
    }

    g_vk.freeCommandBuffers(m_device, pool, 1, &cmd);
    g_vk.destroyCommandPool(m_device, pool, nullptr);
}

QImage VulkanTexture::imageData() const { return m_imageData; }
QVector<QImage> VulkanTexture::mipData() const { return m_mipData; }

// ── VulkanFramebuffer ───────────────────────────────────────────────────

static VulkanFramebuffer* s_activeFramebuffer = nullptr;

VulkanFramebuffer::VulkanFramebuffer(QObject* parent)
    : QObject(parent) {
}

VulkanFramebuffer::~VulkanFramebuffer() {
    if (m_device && g_vk.destroyFramebuffer) {
        if (m_framebuffer)    { g_vk.destroyFramebuffer(m_device, m_framebuffer, nullptr); m_framebuffer = VK_NULL_HANDLE; }
        if (m_colorImageView) { g_vk.destroyImageView(m_device, m_colorImageView, nullptr); m_colorImageView = VK_NULL_HANDLE; }
        if (m_depthImageView) { g_vk.destroyImageView(m_device, m_depthImageView, nullptr); m_depthImageView = VK_NULL_HANDLE; }
    }
}

void VulkanFramebuffer::setDevice(VkDevice device, VkPhysicalDevice physicalDevice) {
    m_device = device;
    m_physicalDevice = physicalDevice;
}

void VulkanFramebuffer::setRenderPass(VkRenderPass renderPass) {
    m_renderPass = renderPass;
}

void VulkanFramebuffer::attachColor(VulkanTexture* texture) {
    m_colorTexture = texture;
    // Clean up previous image view if any
    if (m_colorImageView && m_device) {
        g_vk.destroyImageView(m_device, m_colorImageView, nullptr);
        m_colorImageView = VK_NULL_HANDLE;
    }
    m_valid = m_colorTexture && m_depthTexture && m_renderPass;
    if (m_valid)
        recreateFramebuffer();
}

void VulkanFramebuffer::attachDepth(VulkanTexture* texture) {
    m_depthTexture = texture;
    if (m_depthImageView && m_device) {
        g_vk.destroyImageView(m_device, m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }
    m_valid = m_colorTexture && m_depthTexture && m_renderPass;
    if (m_valid)
        recreateFramebuffer();
}

void VulkanFramebuffer::recreateFramebuffer() {
    if (!m_device || !m_renderPass || !m_colorTexture || !m_colorTexture->image())
        return;

    // Destroy previous framebuffer and image views
    if (m_framebuffer) {
        g_vk.destroyFramebuffer(m_device, m_framebuffer, nullptr);
        m_framebuffer = VK_NULL_HANDLE;
    }
    if (m_colorImageView) {
        g_vk.destroyImageView(m_device, m_colorImageView, nullptr);
        m_colorImageView = VK_NULL_HANDLE;
    }
    if (m_depthImageView) {
        g_vk.destroyImageView(m_device, m_depthImageView, nullptr);
        m_depthImageView = VK_NULL_HANDLE;
    }

    // Create image view from color texture
    VkImageViewCreateInfo colorIvCi{};
    colorIvCi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    colorIvCi.image = m_colorTexture->image();
    colorIvCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    colorIvCi.format = m_colorTexture->vulkanFormat();
    colorIvCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorIvCi.subresourceRange.baseMipLevel = 0;
    colorIvCi.subresourceRange.levelCount = 1;
    colorIvCi.subresourceRange.baseArrayLayer = 0;
    colorIvCi.subresourceRange.layerCount = 1;

    if (g_vk.createImageView(m_device, &colorIvCi, nullptr, &m_colorImageView) != VK_SUCCESS) {
        m_valid = false;
        return;
    }

    QVector<VkImageView> attachments;
    attachments.append(m_colorImageView);

    // Create image view from depth texture if available
    if (m_depthTexture && m_depthTexture->image()) {
        VkImageViewCreateInfo depthIvCi{};
        depthIvCi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depthIvCi.image = m_depthTexture->image();
        depthIvCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthIvCi.format = m_depthTexture->vulkanFormat();
        depthIvCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthIvCi.subresourceRange.baseMipLevel = 0;
        depthIvCi.subresourceRange.levelCount = 1;
        depthIvCi.subresourceRange.baseArrayLayer = 0;
        depthIvCi.subresourceRange.layerCount = 1;

        if (g_vk.createImageView(m_device, &depthIvCi, nullptr, &m_depthImageView) == VK_SUCCESS)
            attachments.append(m_depthImageView);
    }

    // Create framebuffer
    VkFramebufferCreateInfo fbCi{};
    fbCi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbCi.renderPass = m_renderPass;
    fbCi.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbCi.pAttachments = attachments.constData();
    fbCi.width = static_cast<uint32_t>(m_colorTexture->width());
    fbCi.height = static_cast<uint32_t>(m_colorTexture->height());
    fbCi.layers = 1;

    if (g_vk.createFramebuffer(m_device, &fbCi, nullptr, &m_framebuffer) != VK_SUCCESS) {
        m_valid = false;
        return;
    }

    m_valid = true;
    emit created();
}

void VulkanFramebuffer::bind() {
    s_activeFramebuffer = this;
    m_bound = true;
}

void VulkanFramebuffer::unbind() {
    if (s_activeFramebuffer == this)
        s_activeFramebuffer = nullptr;
    m_bound = false;
}

VulkanFramebuffer* VulkanFramebuffer::activeFramebuffer() { return s_activeFramebuffer; }
bool VulkanFramebuffer::isBound() const { return m_bound; }
}