#include "NativeRenderer.h"
#include "ShadowSystem.h"
#include "ui/UiGpuPass.h"
#include "ui/UiRenderer.h"
#include "engine/Math/OcclusionTest.h"
// Roadmap P1: the CPU particle system whose quad layout the sprite draw
// below consumes (kFloatsPerVertex) — the constants are shared with
// particle.vert, which cannot include this header, so the shader's offsets
// are asserted against them at the pipeline instead.
#include "engine/Graphics/ParticleSystem.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <cstddef>

// No Qt anywhere in this file. Unlike ks::VulkanRenderer::createDevice()
// (whose entire body is wrapped in `#if QT_CONFIG(vulkan)` and calls into a
// QLibrary-loaded function table), this links directly against the Vulkan
// loader — the same way SimulatorApp.cpp already creates its VkInstance and
// Win32 surface.

namespace ks::sim {

using engine::graphics::ParticleSystem;

namespace {

// ks::sim::mat4/vec3 and ks::math::mat4/vec3 are layout-identical duplicates
// (the engine copy is the canonical one, the simulator kept its own for
// source compatibility). The culling helpers live in ks::math, so bridge the
// two explicitly instead of relying on that coincidence staying true.
ks::math::mat4 asMath(const mat4& m) {
    ks::math::mat4 r;
    std::memcpy(r.m, m.m, sizeof(m.m));
    return r;
}

ks::math::vec3 asMath(const vec3& v) { return {v.x, v.y, v.z}; }

// Metres by which an occluder must beat an object's nearest point before the
// object is dropped. Absorbs roughly one frame of camera/object motion
// between the grid's frame and the frame being culled; too large and distant
// objects stop being culled at all (depth is hyperbolic, a fixed margin eats
// an ever larger share of the depth range as distance grows).
constexpr float kOcclusionMarginMeters = 1.0f;

// Local-space AABB of an uploaded mesh. Empty meshes leave hasBounds false
// so the culling test skips them rather than testing a degenerate box.
void computeBounds(NativeMesh& mesh) {
    mesh.hasBounds = false;
    if (mesh.vertices.empty()) return;
    mesh.boundsMin = mesh.boundsMax = vec3{mesh.vertices[0].px, mesh.vertices[0].py, mesh.vertices[0].pz};
    for (const NativeVertex& v : mesh.vertices) {
        mesh.boundsMin.x = std::min(mesh.boundsMin.x, v.px);
        mesh.boundsMin.y = std::min(mesh.boundsMin.y, v.py);
        mesh.boundsMin.z = std::min(mesh.boundsMin.z, v.pz);
        mesh.boundsMax.x = std::max(mesh.boundsMax.x, v.px);
        mesh.boundsMax.y = std::max(mesh.boundsMax.y, v.py);
        mesh.boundsMax.z = std::max(mesh.boundsMax.z, v.pz);
    }
    mesh.hasBounds = true;
}

uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t typeBits, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
    }
    return 0;
}

// Strict variant: reports "no matching type" instead of silently returning
// index 0, which may not even be part of typeBits and would turn a missing
// match into a confusing vkAllocateMemory failure much later.
bool findMemoryTypeStrict(VkPhysicalDevice pd, uint32_t typeBits, VkMemoryPropertyFlags props,
                          uint32_t& out) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) {
            out = i;
            return true;
        }
    }
    return false;
}

std::vector<char> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::ate | std::ios::binary);
    if (!f.is_open()) return {};
    size_t size = static_cast<size_t>(f.tellg());
    std::vector<char> data(size);
    f.seekg(0);
    f.read(data.data(), static_cast<std::streamsize>(size));
    return data;
}

VkShaderModule createShaderModule(VkDevice device, const std::vector<char>& code) {
    if (code.empty()) return VK_NULL_HANDLE;
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule mod = VK_NULL_HANDLE;
    vkCreateShaderModule(device, &ci, nullptr, &mod);
    return mod;
}

bool createBuffer(VkPhysicalDevice pd, VkDevice device, VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags props, VkBuffer& buffer, VkDeviceMemory& memory) {
    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = size;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &ci, nullptr, &buffer) != VK_SUCCESS) return false;

    VkMemoryRequirements mr;
    vkGetBufferMemoryRequirements(device, buffer, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = findMemoryType(pd, mr.memoryTypeBits, props);
    if (vkAllocateMemory(device, &mai, nullptr, &memory) != VK_SUCCESS) return false;
    vkBindBufferMemory(device, buffer, memory, 0);
    return true;
}

} // namespace

NativeRenderer::~NativeRenderer() { shutdown(); }

bool NativeRenderer::createDevice(VkInstance instance, VkSurfaceKHR surface) {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) { fprintf(stderr, "[NativeRenderer] no Vulkan physical devices\n"); return false; }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    m_physicalDevice = devices[0];
    for (auto dev : devices) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { m_physicalDevice = dev; break; }
    }

    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qFamilies(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &qfCount, qFamilies.data());

    bool found = false;
    for (uint32_t i = 0; i < qfCount; ++i) {
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, surface, &presentSupport);
        if ((qFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport) {
            m_graphicsQueueFamily = i;
            found = true;
            break;
        }
    }
    if (!found) { fprintf(stderr, "[NativeRenderer] no graphics+present queue family\n"); return false; }

    float priority = 1.0f;
    VkDeviceQueueCreateInfo qCi{};
    qCi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qCi.queueFamilyIndex = m_graphicsQueueFamily;
    qCi.queueCount = 1;
    qCi.pQueuePriorities = &priority;

    // VK_EXT_swapchain_colorspace is an *instance* extension (it only relaxes
    // surface/swapchain usage and adds no commands), so availability comes
    // from the instance list — vkEnumerateInstanceExtensionProperties needs
    // no instance handle. SimulatorApp's createVulkanInstance() is what
    // actually enables it; both run the same query, so the two agree by
    // construction.
    uint32_t instExtCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &instExtCount, nullptr);
    std::vector<VkExtensionProperties> instExts(instExtCount);
    if (instExtCount) {
        vkEnumerateInstanceExtensionProperties(nullptr, &instExtCount, instExts.data());
        for (const auto& e : instExts) {
            if (std::strcmp(e.extensionName, VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME) == 0) {
                m_swapchainColorSpaceExt = true;
                break;
            }
        }
    }

    const char* extensions[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    VkPhysicalDeviceFeatures features{};

    VkDeviceCreateInfo devCi{};
    devCi.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    devCi.queueCreateInfoCount = 1;
    devCi.pQueueCreateInfos = &qCi;
    devCi.enabledExtensionCount = 1;
    devCi.ppEnabledExtensionNames = extensions;
    devCi.pEnabledFeatures = &features;

    if (vkCreateDevice(m_physicalDevice, &devCi, nullptr, &m_device) != VK_SUCCESS) {
        fprintf(stderr, "[NativeRenderer] vkCreateDevice failed\n");
        return false;
    }
    vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);

    return createCommandPoolAndBuffer() && createSyncObjects();
}

bool NativeRenderer::createCommandPoolAndBuffer() {
    VkCommandPoolCreateInfo poolCi{};
    poolCi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCi.queueFamilyIndex = m_graphicsQueueFamily;
    poolCi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(m_device, &poolCi, nullptr, &m_commandPool) != VK_SUCCESS) return false;

    VkCommandBufferAllocateInfo cbAi{};
    cbAi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbAi.commandPool = m_commandPool;
    cbAi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbAi.commandBufferCount = 1;
    return vkAllocateCommandBuffers(m_device, &cbAi, &m_commandBuffer) == VK_SUCCESS;
}

bool NativeRenderer::createSyncObjects() {
    VkSemaphoreCreateInfo semCi{};
    semCi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceCi{};
    fenceCi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCi.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    // m_renderFinished is per-swapchain-image and therefore created in
    // createSwapChain(), once the actual image count is known.
    return vkCreateSemaphore(m_device, &semCi, nullptr, &m_imageAvailable) == VK_SUCCESS &&
           vkCreateFence(m_device, &fenceCi, nullptr, &m_inFlightFence) == VK_SUCCESS;
}

bool NativeRenderer::createDepthResources(uint32_t width, uint32_t height) {
    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.extent = {width, height, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.format = VK_FORMAT_D32_SFLOAT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    // SAMPLED: the occlusion downsample pass reads the depth buffer back out
    // of this image (see recordOcclusionPass).
    ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device, &ci, nullptr, &m_depthImage) != VK_SUCCESS) return false;

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(m_device, m_depthImage, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = findMemoryType(m_physicalDevice, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(m_device, &mai, nullptr, &m_depthMemory) != VK_SUCCESS) return false;
    vkBindImageMemory(m_device, m_depthImage, m_depthMemory, 0);

    VkImageViewCreateInfo vCi{};
    vCi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vCi.image = m_depthImage;
    vCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vCi.format = VK_FORMAT_D32_SFLOAT;
    vCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    vCi.subresourceRange.levelCount = 1;
    vCi.subresourceRange.layerCount = 1;
    return vkCreateImageView(m_device, &vCi, nullptr, &m_depthView) == VK_SUCCESS;
}

void NativeRenderer::destroySwapChain() {
    if (!m_device) return;
    // Extent-bound and swapchain-format-bound, so it has to go with the
    // swapchain; the next endFrame() with deferred on rebuilds it lazily.
    destroyDeferredResources();
    // Extent-bound too (grid dimensions + readback size follow the depth
    // image), and its readback buffer must not outlive the frame that was
    // about to consume it.
    destroyOcclusionResources();
    // Same reasoning for the screenshot buffer (size follows the extent).
    destroyScreenshotResources();
    for (auto fb : m_swapChainFramebuffers) if (fb) vkDestroyFramebuffer(m_device, fb, nullptr);
    m_swapChainFramebuffers.clear();
    for (auto view : m_swapChainImageViews) if (view) vkDestroyImageView(m_device, view, nullptr);
    m_swapChainImageViews.clear();
    for (auto sem : m_renderFinished) if (sem) vkDestroySemaphore(m_device, sem, nullptr);
    m_renderFinished.clear();
    m_swapChainImages.clear();
    if (m_depthView) { vkDestroyImageView(m_device, m_depthView, nullptr); m_depthView = VK_NULL_HANDLE; }
    if (m_depthImage) { vkDestroyImage(m_device, m_depthImage, nullptr); m_depthImage = VK_NULL_HANDLE; }
    if (m_depthMemory) { vkFreeMemory(m_device, m_depthMemory, nullptr); m_depthMemory = VK_NULL_HANDLE; }
    if (m_renderPass) { vkDestroyRenderPass(m_device, m_renderPass, nullptr); m_renderPass = VK_NULL_HANDLE; }
    if (m_swapChain) { vkDestroySwapchainKHR(m_device, m_swapChain, nullptr); m_swapChain = VK_NULL_HANDLE; }
}

bool NativeRenderer::createSwapChain(VkSurfaceKHR surface, uint32_t width, uint32_t height) {
    // WM_SIZE can land while the previous frame is still queued or being
    // presented; everything destroySwapChain() tears down (framebuffers,
    // image views, per-image render-finished semaphores, the swapchain) may
    // still be referenced, so drain the device before dropping any of it.
    if (m_device) vkDeviceWaitIdle(m_device);
    destroySwapChain();

    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, surface, &caps);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, surface, &formatCount, formats.data());
    VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : formats[0];
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { chosen = f; break; }
    }

    // ---- HDR10 (ST.2084/PQ) swapchain, opt-in via setHdrOutput(). Every
    // reason to decline is a hard reason to stay on the SDR surface above,
    // and each one is logged so a silent "HDR did nothing" can be diagnosed.
    m_hdrOutput = false;
    if (m_hdrRequested) {
        auto refuse = [](const char* why) {
            fprintf(stderr, "[NativeRenderer] HDR10 requested but %s — falling back to the SDR swapchain\n", why);
        };
        if (!(m_deferred || m_taa)) {
            refuse("the forward path has no display pass (call setDeferred(true))");
        } else if (!m_swapchainColorSpaceExt) {
            refuse("VK_EXT_swapchain_colorspace is not available from the loader");
        } else {
            VkFormatProperties fp{};
            vkGetPhysicalDeviceFormatProperties(m_physicalDevice, VK_FORMAT_A2B10G10R10_UNORM_PACK32, &fp);
            if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
                refuse("VK_FORMAT_A2B10G10R10_UNORM_PACK32 cannot be a color attachment here");
            } else {
                for (const auto& f : formats) {
                    if (f.format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 &&
                        f.colorSpace == VK_COLOR_SPACE_HDR10_ST2084_EXT) {
                        chosen = f;
                        m_hdrOutput = true;
                        break;
                    }
                }
                if (!m_hdrOutput) refuse("the surface advertises no A2B10G10R10/HDR10_ST2084 mode");
            }
        }
    }
    m_swapChainColorSpace = chosen.colorSpace;
    fprintf(stderr, "[NativeRenderer] swapchain: %s (format %d, colorSpace %d)\n",
            m_hdrOutput ? "HDR10 / PQ (10-bit)" : "SDR (8-bit)",
            static_cast<int>(chosen.format), static_cast<int>(chosen.colorSpace));
    if (m_hdrOutput)
        fprintf(stderr, "[NativeRenderer] HDR10 luminance: white %.0f nits, peak %.0f nits "
                        "(KS_HDR_WHITE_NITS overrides the white)\n",
                static_cast<double>(m_hdrWhiteNits), static_cast<double>(kHdrPeakNits));
    m_swapChainFormat = chosen.format;

    VkExtent2D extent = caps.currentExtent.width != UINT32_MAX
        ? caps.currentExtent
        : VkExtent2D{std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width),
                     std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height)};
    if (extent.width == 0 || extent.height == 0) return false;
    m_swapChainExtent = extent;

    uint32_t imageCount = caps.minImageCount + 1;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    VkSwapchainCreateInfoKHR sCi{};
    sCi.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    sCi.surface = surface;
    sCi.minImageCount = imageCount;
    sCi.imageFormat = chosen.format;
    sCi.imageColorSpace = chosen.colorSpace;
    sCi.imageExtent = extent;
    sCi.imageArrayLayers = 1;
    sCi.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    sCi.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    sCi.preTransform = caps.currentTransform;
    sCi.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sCi.presentMode = VK_PRESENT_MODE_FIFO_KHR; // vsync; always supported
    sCi.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(m_device, &sCi, nullptr, &m_swapChain) != VK_SUCCESS) {
        fprintf(stderr, "[NativeRenderer] vkCreateSwapchainKHR failed\n");
        return false;
    }

    uint32_t actualCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &actualCount, nullptr);
    m_swapChainImages.resize(actualCount);
    vkGetSwapchainImagesKHR(m_device, m_swapChain, &actualCount, m_swapChainImages.data());

    m_swapChainImageViews.resize(actualCount);
    for (uint32_t i = 0; i < actualCount; ++i) {
        VkImageViewCreateInfo vCi{};
        vCi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vCi.image = m_swapChainImages[i];
        vCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vCi.format = m_swapChainFormat;
        vCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vCi.subresourceRange.levelCount = 1;
        vCi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_device, &vCi, nullptr, &m_swapChainImageViews[i]) != VK_SUCCESS) return false;
    }

    // One render-finished semaphore per swapchain image, so a frame that
    // re-signals for image i can never collide with a still-pending present
    // of image i (see the member comment). destroySwapChain() drops them.
    m_renderFinished.resize(actualCount);
    {
        VkSemaphoreCreateInfo semCi{};
        semCi.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (uint32_t i = 0; i < actualCount; ++i) {
            if (vkCreateSemaphore(m_device, &semCi, nullptr, &m_renderFinished[i]) != VK_SUCCESS) {
                fprintf(stderr, "[NativeRenderer] renderFinished[%u] semaphore creation failed\n", i);
                return false;
            }
        }
    }

    if (!createDepthResources(extent.width, extent.height)) return false;

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
    // STORE (was DONT_CARE): the occlusion downsample samples the depth
    // buffer after this pass ends, so its contents must survive the pass.
    depthAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAtt.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription atts[2] = {colorAtt, depthAtt};
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpCi{};
    rpCi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpCi.attachmentCount = 2;
    rpCi.pAttachments = atts;
    rpCi.subpassCount = 1;
    rpCi.pSubpasses = &subpass;
    rpCi.dependencyCount = 1;
    rpCi.pDependencies = &dep;
    if (vkCreateRenderPass(m_device, &rpCi, nullptr, &m_renderPass) != VK_SUCCESS) return false;

    m_swapChainFramebuffers.resize(actualCount);
    for (uint32_t i = 0; i < actualCount; ++i) {
        VkImageView fbAtts[2] = {m_swapChainImageViews[i], m_depthView};
        VkFramebufferCreateInfo fbCi{};
        fbCi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbCi.renderPass = m_renderPass;
        fbCi.attachmentCount = 2;
        fbCi.pAttachments = fbAtts;
        fbCi.width = extent.width;
        fbCi.height = extent.height;
        fbCi.layers = 1;
        if (vkCreateFramebuffer(m_device, &fbCi, nullptr, &m_swapChainFramebuffers[i]) != VK_SUCCESS) return false;
    }

    return true;
}

bool NativeRenderer::recreateSwapChain(uint32_t width, uint32_t height) {
    if (m_device) vkDeviceWaitIdle(m_device);
    // Caller (SimulatorApp) owns the VkSurfaceKHR; recreateSwapChain here
    // assumes createSwapChain() was already called once with it, since this
    // class doesn't store the surface handle itself (SimulatorApp does).
    // In practice call createSwapChain() again with the stored surface from
    // the WM_SIZE handler instead of this convenience wrapper if that
    // ever becomes awkward.
    fprintf(stderr, "[NativeRenderer] recreateSwapChain: call createSwapChain(surface, %u, %u) directly from the resize handler\n", width, height);
    return false;
}

bool NativeRenderer::loadPipelines(const std::string& shaderDir) {
    m_shaderDir = shaderDir;
    m_vertModule = createShaderModule(m_device, readFile(shaderDir + "/native_forward.vert.spv"));
    m_fragModule = createShaderModule(m_device, readFile(shaderDir + "/native_forward.frag.spv"));
    if (!m_vertModule || !m_fragModule) {
        fprintf(stderr, "[NativeRenderer] failed to load native_forward.{vert,frag}.spv from %s\n", shaderDir.c_str());
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = m_vertModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = m_fragModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{0, sizeof(NativeVertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[4] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(NativeVertex, px)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(NativeVertex, nx)},
        {2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(NativeVertex, u)},
        {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(NativeVertex, r)},
    };
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 4;
    vertexInput.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAtt{};
    blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAtt;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{};
    dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dynStates;

    // Descriptor set 0: frame-global UBO (light + cascade matrices) + shadow
    // cascade array sampler — the persistent layout/pool/set/UBO/dummy
    // texture are created here and written each frame in endFrame().
    if (!createFrameDescriptorResources()) {
        fprintf(stderr, "[NativeRenderer] failed to create frame descriptor resources\n");
        return false;
    }

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.size = sizeof(float) * 16 * 2; // model + mvp

    VkPipelineLayoutCreateInfo layoutCi{};
    layoutCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutCi.setLayoutCount = 1;
    layoutCi.pSetLayouts = &m_frameSetLayout;
    layoutCi.pushConstantRangeCount = 1;
    layoutCi.pPushConstantRanges = &pcRange;
    if (vkCreatePipelineLayout(m_device, &layoutCi, nullptr, &m_pipelineLayout) != VK_SUCCESS) return false;

    VkGraphicsPipelineCreateInfo pipeCi{};
    pipeCi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeCi.stageCount = 2;
    pipeCi.pStages = stages;
    pipeCi.pVertexInputState = &vertexInput;
    pipeCi.pInputAssemblyState = &inputAssembly;
    pipeCi.pViewportState = &viewportState;
    pipeCi.pRasterizationState = &rasterizer;
    pipeCi.pMultisampleState = &multisampling;
    pipeCi.pColorBlendState = &colorBlend;
    pipeCi.pDepthStencilState = &depthStencil;
    pipeCi.pDynamicState = &dynState;
    pipeCi.layout = m_pipelineLayout;
    pipeCi.renderPass = m_renderPass;
    pipeCi.subpass = 0;

    return vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeCi, nullptr, &m_pipeline) == VK_SUCCESS;
}

// Layout must match the `FrameData` uniform block declared in
// native_forward.frag, deferred_lighting.frag and taa.frag. std140 puts the
// first five members at 0..256 exactly as before; viewProj/prevViewProj/
// taaParams/fogColor/fogParams are appended after them, so a shader whose
// block simply stops earlier still sees byte-identical offsets for every
// member it declares (Vulkan only requires the buffer range to cover the
// shader's block, not to match it). Shaders that *do* read the trailing
// members must therefore declare every member in between, in this order.
struct FrameDataUBO {
    float sunDirection[4];
    float sunColor[4];
    float cascadeViewProj[3][16];
    float cascadeSplits[4];
    float cameraPos[4];
    float viewProj[16];       // unjittered, current frame
    float prevViewProj[16];   // unjittered, previous frame
    float taaParams[4];       // x = history feedback, 0 = no temporal blend
    float fogColor[4];        // rgb = fog/sky tint, a unused
    float fogParams[4];       // x = density, y = height falloff, z = start distance, w = max opacity
    float aoParams[4];        // x = radius, y = bias, z = strength, w = 1 when enabled (KS_SSAO)
    float ssrParams[4];       // x = max distance, y = intensity, z = roughness cutoff, w = 1 when enabled (KS_SSR)
    float motionBlurParams[4]; // x = strength, y = sample count, z = max length, w = 1 when enabled (KS_MOTIONBLUR)
};
static_assert(sizeof(FrameDataUBO) == 480, "FrameDataUBO is read as a std140 uniform block");

// Push constants of the display pass. Byte layout must match `ToneMapPC` in
// tonemap.frag (std430-like: the leading vec3 takes 12 bytes, everything
// after it packs on 4) — spirv-dis confirms offsets 0/12/16/20/24/28/32/36/40/44.
struct TonemapPC {
    float colorFilter[3]; // 0
    float exposure;       // 12
    float gamma;          // 16
    float whitePoint;     // 20
    float saturation;     // 24
    float contrast;       // 28
    int32_t mode;         // 32  0 none, 1 Reinhard, 2 ACES, 3 Uncharted2, 4 Filmic
    int32_t hdrOutput;    // 36  1 = PQ / Rec.2020
    float whiteNits;      // 40
    float peakNits;       // 44
};
static_assert(sizeof(TonemapPC) == 48, "TonemapPC must match tonemap.frag's ToneMapPC");

// Bright-pass push constants (glareExtract.frag): threshold + soft knee in
// scene-linear units, i.e. 1.0 is "already brighter than SDR paper white".
struct GlarePC {
    float threshold = 1.0f;
    float knee = 0.5f;
    float pad0 = 0.0f;
    float pad1 = 0.0f;
};
static_assert(sizeof(GlarePC) == 16, "GlarePC must match glareExtract.frag's PushConstants");

// Blur push constants (bloomBlur.frag): one texel step along the blur axis.
struct BlurPC {
    float dirX = 0.0f;
    float dirY = 0.0f;
    float pad0 = 0.0f;
    float pad1 = 0.0f;
};
static_assert(sizeof(BlurPC) == 16, "BlurPC must match bloomBlur.frag's BlurPC");


// Halton low-discrepancy sequence — the jitter source for TAA. Eight samples
// gives a dense enough sub-pixel pattern that edges resolve without the
// visible 2x2 grid a naive checkerboard jitter produces.
static float halton(int index, int base) {
    float f = 1.0f, r = 0.0f;
    while (index > 0) {
        f /= static_cast<float>(base);
        r += f * static_cast<float>(index % base);
        index /= base;
    }
    return r;
}

// Full-resolution colour target that is both rendered into and sampled back
// out — used for the deferred lighting output and the two TAA history slots.
static bool createSampledTarget(VkPhysicalDevice pd, VkDevice device, uint32_t width, uint32_t height,
                                VkFormat format, VkImage& image, VkDeviceMemory& memory, VkImageView& view) {
    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {width, height, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device, &ci, nullptr, &image) != VK_SUCCESS) return false;

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(device, image, &mr);
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = findMemoryType(pd, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &mai, nullptr, &memory) != VK_SUCCESS) return false;
    vkBindImageMemory(device, image, memory, 0);

    VkImageViewCreateInfo vCi{};
    vCi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vCi.image = image;
    vCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vCi.format = format;
    vCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vCi.subresourceRange.levelCount = 1;
    vCi.subresourceRange.layerCount = 1;
    return vkCreateImageView(device, &vCi, nullptr, &view) == VK_SUCCESS;
}

bool NativeRenderer::createDummyShadowTexture() {
    // Optimal tiling + a one-shot staging upload, not the previous
    // linear-tiled/PREINITIALIZED + host map shortcut: vkCreateImage rejected
    // that combination with VK_ERROR_FORMAT_NOT_SUPPORTED (-11) on this
    // driver, which failed createFrameDescriptorResources() and therefore the
    // whole loadPipelines() — i.e. no renderer at all, not "no dummy".
    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.extent = {1, 1, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 3; // matches sampler2DArray indexing (cascade 0..2)
    ci.format = VK_FORMAT_R32_SFLOAT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkResult res = vkCreateImage(m_device, &ci, nullptr, &m_dummyShadowImage);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "[NativeRenderer] dummy shadow: vkCreateImage failed (%d)\n", int(res));
        return false;
    }

    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(m_device, m_dummyShadowImage, &mr);
    uint32_t memType = 0;
    if (!findMemoryTypeStrict(m_physicalDevice, mr.memoryTypeBits,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memType) &&
        !findMemoryTypeStrict(m_physicalDevice, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, memType)) {
        fprintf(stderr, "[NativeRenderer] dummy shadow: no suitable memory type\n");
        return false;
    }
    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = memType;
    res = vkAllocateMemory(m_device, &mai, nullptr, &m_dummyShadowMemory);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "[NativeRenderer] dummy shadow: vkAllocateMemory failed (%d)\n", int(res));
        return false;
    }
    if ((res = vkBindImageMemory(m_device, m_dummyShadowImage, m_dummyShadowMemory, 0)) != VK_SUCCESS) {
        fprintf(stderr, "[NativeRenderer] dummy shadow: vkBindImageMemory failed (%d)\n", int(res));
        return false;
    }

    // Fill every layer with 1.0 (max depth => sampleShadow() never finds the
    // fragment "in front of" this, so the dummy always reads as fully lit),
    // through a staging buffer since the image itself is now device-local.
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    const float onePerLayer[3] = {1.0f, 1.0f, 1.0f};
    if (!createBuffer(m_physicalDevice, m_device, sizeof(onePerLayer),
                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      staging, stagingMem)) {
        fprintf(stderr, "[NativeRenderer] dummy shadow: staging buffer failed\n");
        return false;
    }
    void* mapped = nullptr;
    vkMapMemory(m_device, stagingMem, 0, sizeof(onePerLayer), 0, &mapped);
    if (mapped) {
        std::memcpy(mapped, onePerLayer, sizeof(onePerLayer));
        vkUnmapMemory(m_device, stagingMem);
    }

    VkCommandBufferBeginInfo cbBegin{};
    cbBegin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    cbBegin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkResetCommandBuffer(m_commandBuffer, 0);
    vkBeginCommandBuffer(m_commandBuffer, &cbBegin);

    VkImageMemoryBarrier toDst{};
    toDst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toDst.srcAccessMask = 0;
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = m_dummyShadowImage;
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 3};
    vkCmdPipelineBarrier(m_commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toDst);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 3};
    // 2D array, one texel per layer: extent.depth must be 1 (VUID
    // vkCmdCopyBufferToImage-dstImage-07980) — the three layers are covered
    // by layerCount, and the 12-byte staging buffer matches 3 x 4 bytes.
    region.imageExtent = {1, 1, 1};
    vkCmdCopyBufferToImage(m_commandBuffer, staging, m_dummyShadowImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier toRead = toDst;
    toRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toRead.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    vkCmdPipelineBarrier(m_commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toRead);

    vkEndCommandBuffer(m_commandBuffer);
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &m_commandBuffer;
    res = vkQueueSubmit(m_graphicsQueue, 1, &submit, VK_NULL_HANDLE);
    if (res == VK_SUCCESS) res = vkQueueWaitIdle(m_graphicsQueue);
    vkDestroyBuffer(m_device, staging, nullptr);
    vkFreeMemory(m_device, stagingMem, nullptr);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "[NativeRenderer] dummy shadow: staging upload failed (%d)\n", int(res));
        return false;
    }

    VkImageViewCreateInfo vCi{};
    vCi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vCi.image = m_dummyShadowImage;
    vCi.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vCi.format = VK_FORMAT_R32_SFLOAT;
    vCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vCi.subresourceRange.levelCount = 1;
    vCi.subresourceRange.layerCount = 3;
    if ((res = vkCreateImageView(m_device, &vCi, nullptr, &m_dummyShadowView)) != VK_SUCCESS) {
        fprintf(stderr, "[NativeRenderer] dummy shadow: vkCreateImageView failed (%d)\n", int(res));
        return false;
    }

    VkSamplerCreateInfo sCi{};
    sCi.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sCi.magFilter = VK_FILTER_NEAREST;
    sCi.minFilter = VK_FILTER_NEAREST;
    sCi.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sCi.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sCi.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sCi.maxLod = 1.0f;
    if ((res = vkCreateSampler(m_device, &sCi, nullptr, &m_dummyShadowSampler)) != VK_SUCCESS) {
        fprintf(stderr, "[NativeRenderer] dummy shadow: vkCreateSampler failed (%d)\n", int(res));
        return false;
    }
    return true;
}

bool NativeRenderer::createFrameDescriptorResources() {
    // Every failure below reports *which* step broke: "it failed somewhere"
    // is unactionable, and this path failing means the whole forward pipeline
    // never gets built (loadPipelines() returns before creating it).
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo dslCi{};
    dslCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dslCi.bindingCount = 2;
    dslCi.pBindings = bindings;
    VkResult res = vkCreateDescriptorSetLayout(m_device, &dslCi, nullptr, &m_frameSetLayout);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "[NativeRenderer] frame descriptor: set layout failed (%d)\n", int(res));
        return false;
    }

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolCi{};
    poolCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCi.maxSets = 1;
    poolCi.poolSizeCount = 2;
    poolCi.pPoolSizes = poolSizes;
    res = vkCreateDescriptorPool(m_device, &poolCi, nullptr, &m_descriptorPool);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "[NativeRenderer] frame descriptor: pool failed (%d)\n", int(res));
        return false;
    }

    VkDescriptorSetAllocateInfo dsAi{};
    dsAi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAi.descriptorPool = m_descriptorPool;
    dsAi.descriptorSetCount = 1;
    dsAi.pSetLayouts = &m_frameSetLayout;
    res = vkAllocateDescriptorSets(m_device, &dsAi, &m_frameSet);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "[NativeRenderer] frame descriptor: allocate failed (%d)\n", int(res));
        return false;
    }

    if (!createBuffer(m_physicalDevice, m_device, sizeof(FrameDataUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      m_frameUBO, m_frameUBOMemory)) {
        fprintf(stderr, "[NativeRenderer] frame descriptor: UBO allocation failed\n");
        return false;
    }
    VkResult mapRes = vkMapMemory(m_device, m_frameUBOMemory, 0, sizeof(FrameDataUBO), 0, &m_frameUBOMapped);
    if (mapRes != VK_SUCCESS || !m_frameUBOMapped) {
        fprintf(stderr, "[NativeRenderer] frame descriptor: UBO map failed (%d)\n", int(mapRes));
        return false;
    }

    if (!createDummyShadowTexture()) {
        fprintf(stderr, "[NativeRenderer] frame descriptor: dummy shadow texture failed\n");
        return false;
    }

    // Bind the dummy shadow texture initially; writeFrameDescriptorSet() is
    // called again with the real cascade view once a CascadedShadowMap is
    // attached and initialized (see endFrame()).
    writeFrameDescriptorSet(m_dummyShadowView, m_dummyShadowSampler);
    return true;
}

void NativeRenderer::writeFrameDescriptorSet(VkImageView shadowView, VkSampler shadowSampler) {
    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = m_frameUBO;
    bufInfo.range = sizeof(FrameDataUBO);

    VkDescriptorImageInfo imgInfo{};
    imgInfo.sampler = shadowSampler;
    imgInfo.imageView = shadowView;
    // Both the dummy (uploaded through a staging copy in
    // createDummyShadowTexture) and the real cascade array end up in
    // SHADER_READ_ONLY_OPTIMAL, so one layout covers either source.
    imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_frameSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &bufInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_frameSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &imgInfo;

    vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);
}

void NativeRenderer::uploadMesh(NativeMesh& mesh) {
    VkDeviceSize vSize = sizeof(NativeVertex) * mesh.vertices.size();
    VkDeviceSize iSize = sizeof(uint32_t) * mesh.indices.size();

    createBuffer(m_physicalDevice, m_device, vSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                mesh.vertexBuffer, mesh.vertexMemory);
    void* vData = nullptr;
    vkMapMemory(m_device, mesh.vertexMemory, 0, vSize, 0, &vData);
    std::memcpy(vData, mesh.vertices.data(), static_cast<size_t>(vSize));
    vkUnmapMemory(m_device, mesh.vertexMemory);

    if (!mesh.indices.empty()) {
        createBuffer(m_physicalDevice, m_device, iSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    mesh.indexBuffer, mesh.indexMemory);
        void* iData = nullptr;
        vkMapMemory(m_device, mesh.indexMemory, 0, iSize, 0, &iData);
        std::memcpy(iData, mesh.indices.data(), static_cast<size_t>(iSize));
        vkUnmapMemory(m_device, mesh.indexMemory);
    }
    // NOTE: host-visible/coherent buffers, not device-local + staged. Simpler
    // and correct; revisit for perf once real car/track meshes (tens of
    // thousands of verts) are flowing through here instead of test geometry.
}

bool NativeRenderer::loadMeshFromFile(const std::string& name, const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) { fprintf(stderr, "[NativeRenderer] cannot open mesh cache %s\n", path.c_str()); return false; }

    char magic[4];
    f.read(magic, 4);
    const bool v2 = std::memcmp(magic, "NMS2", 4) == 0;
    if (!v2 && std::memcmp(magic, "NMSH", 4) != 0) {
        fprintf(stderr, "[NativeRenderer] bad mesh cache magic in %s\n", path.c_str());
        return false;
    }

    uint32_t vCount = 0, iCount = 0;
    f.read(reinterpret_cast<char*>(&vCount), 4);
    f.read(reinterpret_cast<char*>(&iCount), 4);

    NativeMesh mesh;
    if (v2) {
        // KN5 authored distance window (Roadmap 2.4); "NMSH" keeps the
        // LodWindow default = no window.
        f.read(reinterpret_cast<char*>(&mesh.lod.in), 4);
        f.read(reinterpret_cast<char*>(&mesh.lod.out), 4);
    }
    mesh.vertices.resize(vCount);
    mesh.indices.resize(iCount);
    f.read(reinterpret_cast<char*>(mesh.vertices.data()), static_cast<std::streamsize>(sizeof(NativeVertex) * vCount));
    f.read(reinterpret_cast<char*>(mesh.indices.data()), static_cast<std::streamsize>(sizeof(uint32_t) * iCount));
    if (!f) { fprintf(stderr, "[NativeRenderer] truncated mesh cache %s\n", path.c_str()); return false; }

    setMesh(name, mesh);
    return true;
}

int NativeRenderer::loadMeshesFromManifest(const std::string& dir) {
    std::ifstream manifest(dir + "/manifest.txt");
    if (!manifest.is_open()) {
        fprintf(stderr, "[NativeRenderer] no manifest.txt in %s\n", dir.c_str());
        return 0;
    }
    int loaded = 0;
    std::string name;
    while (std::getline(manifest, name)) {
        if (name.empty()) continue;
        if (loadMeshFromFile(name, dir + "/" + name + ".nmsh")) {
            m_staticSceneMeshNames.push_back(name);
            ++loaded;
        }
    }
    return loaded;
}

void NativeRenderer::drawStaticScene() {
    for (const auto& name : m_staticSceneMeshNames) {
        drawMesh(name, mat4());
    }
}

void NativeRenderer::setMesh(const std::string& name, const NativeMesh& meshIn) {
    destroyMesh(name);
    NativeMesh mesh = meshIn;
    computeBounds(mesh); // before upload: culling needs the local-space AABB
    uploadMesh(mesh);
    m_meshes[name] = mesh;
}

void NativeRenderer::destroyMesh(const std::string& name) {
    auto it = m_meshes.find(name);
    if (it == m_meshes.end()) return;
    if (it->second.vertexBuffer) vkDestroyBuffer(m_device, it->second.vertexBuffer, nullptr);
    if (it->second.vertexMemory) vkFreeMemory(m_device, it->second.vertexMemory, nullptr);
    if (it->second.indexBuffer) vkDestroyBuffer(m_device, it->second.indexBuffer, nullptr);
    if (it->second.indexMemory) vkFreeMemory(m_device, it->second.indexMemory, nullptr);
    m_meshes.erase(it);
    m_staticSceneMeshNames.erase(
        std::remove(m_staticSceneMeshNames.begin(), m_staticSceneMeshNames.end(), name),
        m_staticSceneMeshNames.end());
}

void NativeRenderer::clearStaticScene() {
    // destroyMesh() erases the name from m_staticSceneMeshNames as a side
    // effect, so iterate a copy: track switch (roadmap 1.2) frees the
    // previous track's GPU buffers before the next bake is loaded.
    const std::vector<std::string> names = m_staticSceneMeshNames;
    for (const auto& name : names) destroyMesh(name);
    m_staticSceneMeshNames.clear();
}

bool NativeRenderer::ensureScreenshotBuffer() {
    const VkDeviceSize size = static_cast<VkDeviceSize>(m_swapChainExtent.width) *
                              static_cast<VkDeviceSize>(m_swapChainExtent.height) * 4u;
    if (m_shotBuffer && m_shotSize == size) return true;
    destroyScreenshotResources();
    if (!createBuffer(m_physicalDevice, m_device, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      m_shotBuffer, m_shotMemory)) {
        return false;
    }
    m_shotSize = size;
    if (vkMapMemory(m_device, m_shotMemory, 0, size, 0, &m_shotMapped) != VK_SUCCESS) {
        destroyScreenshotResources();
        return false;
    }
    return true;
}

// Records barrier -> vkCmdCopyImageToBuffer -> barrier into m_commandBuffer.
// Runs inside endFrame() while the command buffer is still open; the
// backbuffer is in PRESENT_SRC here on both the forward path (render pass
// finalLayout) and the deferred path (display pass finalLayout).
void NativeRenderer::recordScreenshotCopy() {
    if (!ensureScreenshotBuffer()) {
        m_screenshotPending = false;
        return;
    }
    VkImage image = m_swapChainImages[m_currentImageIndex];

    VkImageMemoryBarrier toSrc{};
    toSrc.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    toSrc.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toSrc.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toSrc.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toSrc.image = image;
    toSrc.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    toSrc.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toSrc.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(m_commandBuffer,
                         VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toSrc);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {m_swapChainExtent.width, m_swapChainExtent.height, 1};
    vkCmdCopyImageToBuffer(m_commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           m_shotBuffer, 1, &region);

    VkImageMemoryBarrier toPresent = toSrc;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    toPresent.dstAccessMask = 0;
    vkCmdPipelineBarrier(m_commandBuffer,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &toPresent);

    m_screenshotRecorded = true;
    m_screenshotPending = false;
}

void NativeRenderer::destroyScreenshotResources() {
    if (m_shotMapped && m_shotMemory) {
        vkUnmapMemory(m_device, m_shotMemory);
        m_shotMapped = nullptr;
    }
    if (m_shotBuffer) vkDestroyBuffer(m_device, m_shotBuffer, nullptr);
    if (m_shotMemory) vkFreeMemory(m_device, m_shotMemory, nullptr);
    m_shotBuffer = VK_NULL_HANDLE;
    m_shotMemory = VK_NULL_HANDLE;
    m_shotSize = 0;
    m_screenshotPending = false;
    m_screenshotRecorded = false;
    m_screenshotReady = false;
    m_screenshotPixels.clear();
}

bool NativeRenderer::beginFrame() {
    vkWaitForFences(m_device, 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);

    // The occlusion grid copied at the end of the previous frame is complete
    // and host-visible once that fence has signalled, so this is the one
    // stall-free point to consume it. Linearise the raw NDC depths once here
    // (metres) so drawMesh() only ever compares metres against metres.
    // Without a fresh copy — first frame, resize, disabled, build failed —
    // m_occlGridValid stays false and the test simply does not run.
    if (m_occlPending) {
        m_occlPending = false;
        const size_t count = static_cast<size_t>(m_occlGridW) * static_cast<size_t>(m_occlGridH);
        if (m_occlReadbackMapped && count > 0) {
            const float* src = static_cast<const float*>(m_occlReadbackMapped);
            m_occlGrid.resize(count);
            for (size_t i = 0; i < count; ++i)
                m_occlGrid[i] = ks::math::occlusion::linearizeDepth(src[i], m_occlR22, m_occlR23);
            m_occlGridValid = true;
        }
    }

    VkResult acquireResult = vkAcquireNextImageKHR(m_device, m_swapChain, UINT64_MAX,
                                                   m_imageAvailable, VK_NULL_HANDLE, &m_currentImageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) return false; // caller should recreate the swapchain
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) return false;

    m_drawList.clear();
    m_mainDrawList.clear();
    m_stats = FrameStats{};
    // Culling runs in drawMesh(), which is called *between* beginFrame() and
    // endFrame(), so the frustum has to be rebuilt here — after setCamera()
    // has had its say for this frame and before anything is queued.
    if (m_frustumCulling && m_cameraValid)
        m_frustum = ks::math::Frustum::fromViewProj(asMath(m_proj * m_view));
    if (m_frustumCulling && m_cameraValid && getenv("KS_CULL_DEBUG") != nullptr) {
        static bool s_planes = false;
            if (!s_planes) {
                s_planes = true;
                fprintf(stderr, "[cull] proj r00=%.4f r11=%.4f r22=%.4f r23=%.4f\n",
                        m_proj(0, 0), m_proj(1, 1), m_proj(2, 2), m_proj(2, 3));
                fprintf(stderr, "[cull] view translation = %.3f %.3f %.3f\n",
                        m_view(0, 3), m_view(1, 3), m_view(2, 3));
                for (int i = 0; i < ks::math::Frustum::kPlaneCount; ++i) {
                    const auto& p = m_frustum.plane(i);
                    fprintf(stderr, "[cull] plane%d = %.4f %.4f %.4f | d=%.4f\n", i, p.a, p.b, p.c, p.d);
                }
            }
    }
    return true;
}

void NativeRenderer::drawMesh(const std::string& name, const mat4& modelMatrix) {
    // Queued, not drawn immediately: the shadow cascades and the main color
    // pass both need to render this exact instance list (see endFrame()),
    // so recording once and replaying it twice is what makes cascade
    // shadows line up with what's actually on screen instead of assuming
    // every caster sits at the identity transform.
    auto it = m_meshes.find(name);
    if (it == m_meshes.end()) return;
    ++m_stats.submitted;

    // The occlusion grid only exists once a previous frame produced one
    // (never on the first frame after a start/resize).
    const bool occlTest = m_occlusionCulling && m_occlGridValid &&
                          m_prevViewProjValid && !m_occlGrid.empty();

    // Authored KN5 distance window (Roadmap 2.4): only meshes that actually
    // carry one pay for the extra test, so legacy caches and runtime meshes
    // keep the exact pre-2.4 path.
    const bool hasLodWindow = ks::scene::hasAuthoredWindow(it->second.lod);

    if (m_cameraValid && it->second.hasBounds &&
        (m_frustumCulling || occlTest || hasLodWindow)) {
        ks::math::vec3 wmin, wmax;
        ks::math::Frustum::transformBounds(asMath(modelMatrix),
                                           asMath(it->second.boundsMin),
                                           asMath(it->second.boundsMax), wmin, wmax);
        if (hasLodWindow) {
            // Nearest-point distance: a mesh you are standing inside never
            // reads as "far" no matter how large its bounds are.
            const float dist =
                ks::scene::distanceToBounds(asMath(m_camPosWS), wmin, wmax);
            if (!ks::scene::inLodWindow(dist, it->second.lod)) {
                ++m_stats.culled; // past (or before) its window: skip both
                                  // the color pass and the shadow list
                static bool s_lodDebug = getenv("KS_CULL_DEBUG") != nullptr;
                if (s_lodDebug && m_stats.culled <= 8) {
                    fprintf(stderr, "[lod] %s dist=%.1f window=[%.1f..%.1f]\n",
                            name.c_str(), dist, it->second.lod.in,
                            it->second.lod.out);
                }
                return;
            }
        }
        if (m_frustumCulling && !m_frustum.intersects(wmin, wmax)) {
            ++m_stats.culled;
            static bool s_cullDebug = getenv("KS_CULL_DEBUG") != nullptr;
            if (s_cullDebug && m_stats.culled <= 8) {
                fprintf(stderr,
                        "[cull] %s local=[%.2f %.2f %.2f]..[%.2f %.2f %.2f] "
                        "world=[%.2f %.2f %.2f]..[%.2f %.2f %.2f]\n",
                        name.c_str(), it->second.boundsMin.x, it->second.boundsMin.y,
                        it->second.boundsMin.z, it->second.boundsMax.x, it->second.boundsMax.y,
                        it->second.boundsMax.z, wmin.x, wmin.y, wmin.z, wmax.x, wmax.y, wmax.z);
            }
            return;
        }
        // Projected with m_prevViewProj — the same matrices the grid in
        // m_occlGrid was rendered with, so screen rect and depths line up
        // exactly (see the m_occlR22/m_occlR23 note in the header).
        if (occlTest &&
            ks::math::occlusion::aabbOccluded(asMath(m_prevViewProj), wmin, wmax,
                                              m_occlGrid.data(), m_occlGridW, m_occlGridH,
                                              m_occlFullW, m_occlFullH, kOcclusionMarginMeters)) {
            ++m_stats.occluded;
            static bool s_occlDebug = getenv("KS_OCCL_DEBUG") != nullptr;
            if (s_occlDebug && m_stats.occluded <= 8) {
                fprintf(stderr, "[occl] %s world=[%.2f %.2f %.2f]..[%.2f %.2f %.2f] "
                                "grid=%dx%d full=%dx%d\n",
                        name.c_str(), wmin.x, wmin.y, wmin.z, wmax.x, wmax.y, wmax.z,
                        m_occlGridW, m_occlGridH, m_occlFullW, m_occlFullH);
            }
            // Shadow list still gets it: an object the camera cannot see can
            // still cast a shadow the camera *can* see.
            m_drawList.push_back({name, modelMatrix});
            return;
        }
    }
    ++m_stats.drawn;
    m_drawList.push_back({name, modelMatrix});
    m_mainDrawList.push_back({name, modelMatrix});
}

void NativeRenderer::endFrame() {
    vkResetFences(m_device, 1, &m_inFlightFence);
    vkResetCommandBuffer(m_commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(m_commandBuffer, &beginInfo);

    // ---- Shadow cascades: same draw list, light-space matrices instead of
    // the camera's view-projection. ----
    if (m_shadowMap && m_shadowMap->isInitialized()) {
        // Real camera clip range (was hardcoded 0.5/500): the cascades have
        // to cover exactly what the camera can see, or geometry beyond 500
        // world units silently loses its shadows.
        m_shadowMap->update(m_view, m_proj, m_camNear, m_camFar, m_sun.direction);
        for (int c = 0; c < m_shadowMap->cascadeCount(); ++c) {
            m_shadowMap->beginCascadePass(m_commandBuffer, c);
            for (const auto& draw : m_drawList) {
                auto it = m_meshes.find(draw.meshName);
                if (it == m_meshes.end() || !it->second.vertexBuffer) continue;
                const NativeMesh& mesh = it->second;
                VkBuffer vBufs[] = {mesh.vertexBuffer};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, vBufs, offsets);
                m_shadowMap->pushLightSpaceMatrix(m_commandBuffer, c, draw.model);
                if (mesh.indexBuffer) {
                    vkCmdBindIndexBuffer(m_commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                    vkCmdDrawIndexed(m_commandBuffer, static_cast<uint32_t>(mesh.indices.size()), 1, 0, 0, 0);
                } else {
                    vkCmdDraw(m_commandBuffer, static_cast<uint32_t>(mesh.vertices.size()), 1, 0, 0);
                }
            }
            m_shadowMap->endCascadePass(m_commandBuffer);
        }
    }

    // Lazy build: the forward path above never pays for any of this, and a
    // failed build silently falls back to forward instead of going black.
    // TAA implies deferred — its resolve pass is the thing that reads the
    // GBuffer world positions back out for reprojection.
    const bool deferred = (m_deferred || m_taa) && ensureDeferredResources();
    const bool useTaa = deferred && m_taa;

    // Jitter only the main-pass projection. The shadow cascades above and
    // both reprojection matrices below stay unjittered, otherwise the motion
    // vectors would carry the jitter and TAA would smear instead of resolve.
    mat4 projRender = m_proj;
    if (useTaa && m_swapChainExtent.width > 0) {
        m_jitterIndex = (m_jitterIndex + 1) % kJitterSamples;
        const float w = static_cast<float>(m_swapChainExtent.width);
        const float h = static_cast<float>(m_swapChainExtent.height);
        // One pixel spans 2/NDC units, so (halton - 0.5) * 2/N is a uniform
        // half-pixel offset per axis.
        projRender(0, 2) += (halton(m_jitterIndex + 1, 2) - 0.5f) * 2.0f / w;
        projRender(1, 2) += (halton(m_jitterIndex + 1, 3) - 0.5f) * 2.0f / h;
    }

    const mat4 viewProjUnjit = m_proj * m_view;
    const mat4 prevViewProjUnjit = m_prevViewProjValid ? m_prevViewProj : viewProjUnjit;

    // ---- Frame-global lighting data, shared by whichever geometry pass
    // runs below (forward or GBuffer). ----
    FrameDataUBO frameData{};
    frameData.sunDirection[0] = m_sun.direction.x;
    frameData.sunDirection[1] = m_sun.direction.y;
    frameData.sunDirection[2] = m_sun.direction.z;
    frameData.sunColor[0] = m_sun.color.x;
    frameData.sunColor[1] = m_sun.color.y;
    frameData.sunColor[2] = m_sun.color.z;
    frameData.sunColor[3] = m_sun.intensity;
    frameData.cameraPos[0] = m_camPosWS.x;
    frameData.cameraPos[1] = m_camPosWS.y;
    frameData.cameraPos[2] = m_camPosWS.z;
    std::memcpy(frameData.viewProj, viewProjUnjit.data(), sizeof(float) * 16);
    std::memcpy(frameData.prevViewProj, prevViewProjUnjit.data(), sizeof(float) * 16);
    // 0 on the first frame after a (re)build, so undefined history can never
    // show up as a flash of garbage.
    frameData.taaParams[0] = (useTaa && m_historyValid) ? kTaaFeedback : 0.0f;
    frameData.fogColor[0] = m_fogColor.x;
    frameData.fogColor[1] = m_fogColor.y;
    frameData.fogColor[2] = m_fogColor.z;
    frameData.fogColor[3] = 1.0f;
    frameData.fogParams[0] = m_fogDensity;
    frameData.fogParams[1] = m_fogHeightFalloff;
    frameData.fogParams[2] = 0.0f;                       // start distance
    frameData.fogParams[3] = 0.9f;                       // max opacity
    // SSAO exists only in the deferred lighting shader, so it is fed on the
    // deferred path only; on the forward path the parameter is ignored anyway
    // and writing 0 keeps the UBO byte-identical for a shader that never reads
    // it past fogParams.
    frameData.aoParams[0] = m_ssaoRadius;
    frameData.aoParams[1] = m_ssaoBias;
    frameData.aoParams[2] = m_ssaoIntensity;
    frameData.aoParams[3] = (m_ssao && m_deferred) ? 1.0f : 0.0f;
    // SSR and motion blur are the same story: both live in a deferred-only
    // shader (the lighting march and the resolve gather respectively), both
    // are off unless something asked for them.
    frameData.ssrParams[0] = m_ssrMaxDistance;
    frameData.ssrParams[1] = m_ssrIntensity;
    frameData.ssrParams[2] = m_ssrRoughnessThreshold;
    frameData.ssrParams[3] = (m_ssr && m_deferred) ? 1.0f : 0.0f;
    frameData.motionBlurParams[0] = m_mbStrength;
    frameData.motionBlurParams[1] = static_cast<float>(m_mbSamples);
    frameData.motionBlurParams[2] = m_mbMaxLength;
    frameData.motionBlurParams[3] = (m_motionBlur && m_deferred) ? 1.0f : 0.0f;

    // Either the real shadow cascade array or the 1x1 dummy (always-lit)
    // texture if no shadow map is attached/initialized yet.
    VkImageView shadowView = m_dummyShadowView;
    VkSampler shadowSampler = m_dummyShadowSampler;
    if (m_shadowMap && m_shadowMap->isInitialized()) {
        for (int c = 0; c < m_shadowMap->cascadeCount() && c < 3; ++c) {
            std::memcpy(frameData.cascadeViewProj[c], m_shadowMap->cascade(c).viewProj.data(), sizeof(float) * 16);
            frameData.cascadeSplits[c] = m_shadowMap->cascade(c).splitDepth;
        }
        shadowView = m_shadowMap->arrayView();
        shadowSampler = m_shadowMap->sampler();
    }
    writeFrameDescriptorSet(shadowView, shadowSampler);
    std::memcpy(m_frameUBOMapped, &frameData, sizeof(FrameDataUBO));

    // Replay of the queued instance list, shared by the forward and GBuffer
    // passes: both bind m_pipelineLayout (set0 = FrameData + shadow array,
    // push constants = model + mvp), so only the pipeline object differs.
    // m_mainDrawList, not m_drawList: the shadow cascades above keep drawing
    // occluded objects (they may still cast visible shadows), the camera
    // passes must not.
    auto recordDrawList = [&]() {
        static bool s_recordDebug = getenv("KS_RECORD_DEBUG") != nullptr;
        for (const auto& draw : m_mainDrawList) {
            auto it = m_meshes.find(draw.meshName);
            if (it == m_meshes.end()) continue;
            const NativeMesh& mesh = it->second;
            if (!mesh.vertexBuffer) {
                // Passed CPU-side frustum culling (so it counts as "drawn")
                // but never made it to the GPU — an upload that failed or was
                // abandoned. Report once per name instead of dropping the
                // instance silently every frame.
                if (m_missingBufferLogged.insert(draw.meshName).second)
                    std::fprintf(stderr, "[draw] %s: no vertex buffer, instance skipped\n",
                                 draw.meshName.c_str());
                continue;
            }

            struct { mat4 model; mat4 mvp; } pc;
            pc.model = draw.model;
            pc.mvp = projRender * m_view * draw.model;
            if (s_recordDebug) {
                std::fprintf(stderr,
                             "[draw] %s verts=%zu idx=%zu mvp=(%.3f %.3f %.3f %.3f)\n",
                             draw.meshName.c_str(), mesh.vertices.size(), mesh.indices.size(),
                             pc.mvp(0, 0), pc.mvp(1, 1), pc.mvp(2, 2), pc.mvp(3, 3));
            }
            vkCmdPushConstants(m_commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

            VkBuffer vBufs[] = {mesh.vertexBuffer};
            VkDeviceSize offsets[] = {0};
            vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, vBufs, offsets);

            if (mesh.indexBuffer) {
                vkCmdBindIndexBuffer(m_commandBuffer, mesh.indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(m_commandBuffer, static_cast<uint32_t>(mesh.indices.size()), 1, 0, 0, 0);
            } else {
                vkCmdDraw(m_commandBuffer, static_cast<uint32_t>(mesh.vertices.size()), 1, 0, 0);
            }
        }
    };

    VkViewport vp{0, 0, float(m_swapChainExtent.width), float(m_swapChainExtent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, m_swapChainExtent};

    if (deferred) {
        // ---- Pass 1: GBuffer — geometry attributes into three colour
        // targets plus a private depth buffer (kept separate from
        // m_depthImage so the forward path's depth layout is untouched). ----
        VkClearValue gClears[4];
        for (int i = 0; i < 3; ++i) gClears[i].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
        gClears[3].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo gBegin{};
        gBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        gBegin.renderPass = m_gbufferRenderPass;
        gBegin.framebuffer = m_gbufferFramebuffers[m_currentImageIndex];
        gBegin.renderArea.extent = m_swapChainExtent;
        gBegin.clearValueCount = 4;
        gBegin.pClearValues = gClears;
        vkCmdBeginRenderPass(m_commandBuffer, &gBegin, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdSetViewport(m_commandBuffer, 0, 1, &vp);
        vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gbufferPipeline);
        vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                0, 1, &m_frameSet, 0, nullptr);
        recordDrawList();
        // Roadmap P1: sprite quads into the same GBuffer, so the lighting
        // pass below lights them exactly like geometry (they carry a
        // camera-facing normal and a real world position).
        recordParticles(projRender * m_view, deferred);
        vkCmdEndRenderPass(m_commandBuffer);

        // ---- Pass 2: fullscreen lighting + volumetric fog into an
        // offscreen HDR target, sampling the GBuffer written above. ----
        VkClearValue lClear{};
        lClear.color = {{m_fogColor.x, m_fogColor.y, m_fogColor.z, 1.0f}};

        VkRenderPassBeginInfo lBegin{};
        lBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        lBegin.renderPass = m_lightingRenderPass;
        lBegin.framebuffer = m_lightingFramebuffer;
        lBegin.renderArea.extent = m_swapChainExtent;
        lBegin.clearValueCount = 1;
        lBegin.pClearValues = &lClear;
        vkCmdBeginRenderPass(m_commandBuffer, &lBegin, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdSetViewport(m_commandBuffer, 0, 1, &vp);
        vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
        writeLightingDescriptorSet(shadowView, shadowSampler);
        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lightingPipeline);
        vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_lightingLayout,
                                0, 1, &m_lightingSet, 0, nullptr);
        vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(m_commandBuffer);

        // ---- Pass 3: temporal resolve into the history slot the *next*
        // frame reads — and, from there, the bloom extract and the display
        // pass. Stays scene-linear HDR: with TAA off this degenerates to an
        // exact HDR copy (feedback is 0 in the UBO). Reading
        // history[readIdx] while writing history[1 - readIdx] is what keeps
        // the two out of each other's way inside one subpass. ----
        const int historyWrite = 1 - m_historyIndex;

        VkClearValue rClear{};
        rClear.color = {{m_fogColor.x, m_fogColor.y, m_fogColor.z, 1.0f}};

        VkRenderPassBeginInfo rBegin{};
        rBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rBegin.renderPass = m_resolveRenderPass;
        rBegin.framebuffer = m_resolveFramebuffers[historyWrite];
        rBegin.renderArea.extent = m_swapChainExtent;
        rBegin.clearValueCount = 1;
        rBegin.pClearValues = &rClear;
        vkCmdBeginRenderPass(m_commandBuffer, &rBegin, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdSetViewport(m_commandBuffer, 0, 1, &vp);
        vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
        writeResolveDescriptorSet();
        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_resolvePipeline);
        vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_resolveLayout,
                                0, 1, &m_resolveSet, 0, nullptr);
        vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(m_commandBuffer);

        // ---- Pass 4: bloom — bright-pass extract and both blur axes, half
        // resolution, all through the one shared render pass. The extract
        // reads the frame the resolve just wrote (post-TAA, so the glow does
        // not shimmer), the horizontal blur reads its output, the vertical
        // blur reads that and writes the result back to slot A, which is
        // what the display pass samples. ----
        writeDisplayDescriptorSets(m_historyViews[historyWrite]);
        {
            VkViewport bvp{0, 0, float(m_bloomExtent.width), float(m_bloomExtent.height), 0.0f, 1.0f};
            VkRect2D bscissor{{0, 0}, m_bloomExtent};

            auto bloomPass = [&](VkFramebuffer fb, VkPipeline pipeline, VkDescriptorSet set,
                                 const void* push, uint32_t pushSize) {
                VkClearValue clear{};
                clear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

                VkRenderPassBeginInfo b{};
                b.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
                b.renderPass = m_bloomRenderPass;
                b.framebuffer = fb;
                b.renderArea.extent = m_bloomExtent;
                b.clearValueCount = 1;
                b.pClearValues = &clear;
                vkCmdBeginRenderPass(m_commandBuffer, &b, VK_SUBPASS_CONTENTS_INLINE);
                vkCmdSetViewport(m_commandBuffer, 0, 1, &bvp);
                vkCmdSetScissor(m_commandBuffer, 0, 1, &bscissor);
                vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_bloomLayout,
                                        0, 1, &set, 0, nullptr);
                vkCmdPushConstants(m_commandBuffer, m_bloomLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, pushSize, push);
                vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
                vkCmdEndRenderPass(m_commandBuffer);
            };

            GlarePC glare{};
            bloomPass(m_bloomFramebuffers[0], m_bloomExtractPipeline, m_bloomExtractSet, &glare, sizeof(glare));

            BlurPC blurH{1.0f / float(m_bloomExtent.width), 0.0f, 0.0f, 0.0f};
            bloomPass(m_bloomFramebuffers[1], m_bloomBlurPipeline, m_bloomBlurSets[0], &blurH, sizeof(blurH));

            BlurPC blurV{0.0f, 1.0f / float(m_bloomExtent.height), 0.0f, 0.0f};
            bloomPass(m_bloomFramebuffers[0], m_bloomBlurPipeline, m_bloomBlurSets[1], &blurV, sizeof(blurV));
        }

        // ---- Pass 5: display — the only writer of the swapchain now.
        // Tone-maps the resolved frame, folds in the bloom, and encodes for
        // whichever surface the swapchain was created with: sRGB OETF on the
        // 8-bit path, PQ/Rec.2020 on HDR10. ----
        {
            VkClearValue dClear{};
            dClear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};

            VkRenderPassBeginInfo dBegin{};
            dBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            dBegin.renderPass = m_displayRenderPass;
            dBegin.framebuffer = m_displayFramebuffers[m_currentImageIndex];
            dBegin.renderArea.extent = m_swapChainExtent;
            dBegin.clearValueCount = 1;
            dBegin.pClearValues = &dClear;
            vkCmdBeginRenderPass(m_commandBuffer, &dBegin, VK_SUBPASS_CONTENTS_INLINE);

            vkCmdSetViewport(m_commandBuffer, 0, 1, &vp);
            vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
            vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_displayPipeline);
            vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_displayLayout,
                                    0, 1, &m_displaySet, 0, nullptr);

            TonemapPC t{};
            t.colorFilter[0] = t.colorFilter[1] = t.colorFilter[2] = 1.0f;
            t.exposure = m_exposure;
            // The shader already ends with an exact sRGB OETF, so the extra
            // pow(1/2.2) must be a no-op: feeding 2.2 here double-encoded the
            // SDR image (everything ~20% too bright — the fog background read
            // 236/255 instead of the 214 the linear value maps to).
            t.gamma = 1.0f;
            t.whitePoint = 11.2f;   // Uncharted2 paper white (its default)
            t.saturation = 1.0f;
            t.contrast = 1.0f;
            t.mode = m_tonemapMode;
            t.hdrOutput = m_hdrOutput ? 1 : 0;
            t.whiteNits = m_hdrWhiteNits;
            t.peakNits = kHdrPeakNits;
            vkCmdPushConstants(m_commandBuffer, m_displayLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(t), &t);
            vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
            vkCmdEndRenderPass(m_commandBuffer);
        }
    } else {
        // An HDR10 swapchain with no display pass would encode nothing, so
        // say so once instead of shipping a silently wrong image.
        static bool s_hdrNoDisplayWarned = false;
        if (m_hdrOutput && !s_hdrNoDisplayWarned) {
            s_hdrNoDisplayWarned = true;
            fprintf(stderr, "[NativeRenderer] WARNING: HDR10 swapchain but the deferred/display path is "
                            "not running — image will be wrong (enable KS_RENDER=deferred|taa or run with KS_HDR=0)\n");
        }
        // ---- Main color pass, same draw list again. ----
        VkClearValue clears[2];
        clears[0].color = {{m_fogColor.x, m_fogColor.y, m_fogColor.z, 1.0f}};
        clears[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = m_renderPass;
        rpBegin.framebuffer = m_swapChainFramebuffers[m_currentImageIndex];
        rpBegin.renderArea.extent = m_swapChainExtent;
        rpBegin.clearValueCount = 2;
        rpBegin.pClearValues = clears;
        vkCmdBeginRenderPass(m_commandBuffer, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdSetViewport(m_commandBuffer, 0, 1, &vp);
        vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                0, 1, &m_frameSet, 0, nullptr);
        recordDrawList();
        // Roadmap P1: sprite quads, alpha blended over whatever the pass has
        // drawn so far (their own pipeline owns no descriptor set, so this
        // only rebinds pipeline + push constants).
        recordParticles(projRender * m_view, deferred);
        vkCmdEndRenderPass(m_commandBuffer);
    }

    // ---- Occlusion grid for the *next* frame: max-pool the depth buffer
    // whichever geometry pass above wrote it, then copy the small grid to the
    // CPU. Runs after the geometry passes (which have left their depth image
    // in DEPTH_STENCIL_ATTACHMENT_OPTIMAL) and before submission; nothing
    // else in the frame reads or writes that depth image afterwards.
    if (m_occlusionCulling && !m_occlusionFailed && ensureOcclusionResources()) {
        VkImage depthImage = deferred ? m_gbufferDepthImage : m_depthImage;
        VkImageView depthView = deferred ? m_gbufferDepthView : m_depthView;
        if (depthImage && depthView)
            recordOcclusionPass(depthImage, depthView);
    }

    // One-shot color readback (requestScreenshot()): the backbuffer is in
    // PRESENT_SRC on both paths at this point, so the copy is just two
    // barriers around vkCmdCopyImageToBuffer.
    if (m_screenshotPending) {
        if (m_swapChainFormat == VK_FORMAT_B8G8R8A8_UNORM)
            recordScreenshotCopy();
        else
            m_screenshotPending = false; // unsupported format (e.g. HDR10)
    }

    if (deferred) {
        m_historyValid = true;
        m_historyIndex ^= 1;
    }
    m_prevViewProj = viewProjUnjit;
    m_prevViewProjValid = true;
    // Depth parameters of the projection that rendered the grid just copied
    // (jitter only touches rows 0/1, so m_proj's row 2 is jitter-free). The
    // next beginFrame() pairs these with m_prevViewProj when linearising.
    m_occlR22 = m_proj(2, 2);
    m_occlR23 = m_proj(2, 3);

    vkEndCommandBuffer(m_commandBuffer);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &m_imageAvailable;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &m_commandBuffer;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &m_renderFinished[m_currentImageIndex];
    vkQueueSubmit(m_graphicsQueue, 1, &submit, m_inFlightFence);

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &m_renderFinished[m_currentImageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &m_swapChain;
    present.pImageIndices = &m_currentImageIndex;
    vkQueuePresentKHR(m_graphicsQueue, &present);

    if (m_screenshotRecorded) {
        m_screenshotRecorded = false;
        m_screenshotPending = false;
        // The copy ran in the submit signalled by m_inFlightFence; the
        // present above does not have to complete for the buffer to hold the
        // pixels. One-off stall, only on requested frames.
        vkWaitForFences(m_device, 1, &m_inFlightFence, VK_TRUE, UINT64_MAX);
        const size_t bytes = static_cast<size_t>(m_swapChainExtent.width) *
                             static_cast<size_t>(m_swapChainExtent.height) * 4u;
        const auto* src = static_cast<const unsigned char*>(m_shotMapped);
        m_screenshotPixels.assign(src, src + bytes);
        m_screenshotReady = true;
    }
}

void NativeRenderer::shutdown() {
    if (!m_device) return;
    vkDeviceWaitIdle(m_device);

    // Torn down first so no deferred pipeline outlives m_pipelineLayout.
    destroyDeferredResources();

    for (auto& [name, mesh] : m_meshes) {
        if (mesh.vertexBuffer) vkDestroyBuffer(m_device, mesh.vertexBuffer, nullptr);
        if (mesh.vertexMemory) vkFreeMemory(m_device, mesh.vertexMemory, nullptr);
        if (mesh.indexBuffer) vkDestroyBuffer(m_device, mesh.indexBuffer, nullptr);
        if (mesh.indexMemory) vkFreeMemory(m_device, mesh.indexMemory, nullptr);
    }
    m_meshes.clear();

    if (m_pipeline) vkDestroyPipeline(m_device, m_pipeline, nullptr);
    if (m_pipelineLayout) vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    if (m_vertModule) vkDestroyShaderModule(m_device, m_vertModule, nullptr);
    if (m_fragModule) vkDestroyShaderModule(m_device, m_fragModule, nullptr);

    // Particle sprites (roadmap P1): pipelines + modules + the mapped quad
    // buffer. destroyDeferredResources() above already dropped the GBuffer
    // half if it existed; the rest is not swapchain-bound.
    destroyParticlePipelines();
    destroyParticleBuffer();
    m_particleVerts.clear();

    if (m_frameUBOMapped) { vkUnmapMemory(m_device, m_frameUBOMemory); m_frameUBOMapped = nullptr; }
    if (m_frameUBO) vkDestroyBuffer(m_device, m_frameUBO, nullptr);
    if (m_frameUBOMemory) vkFreeMemory(m_device, m_frameUBOMemory, nullptr);
    if (m_descriptorPool) vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr); // also frees m_frameSet
    if (m_frameSetLayout) vkDestroyDescriptorSetLayout(m_device, m_frameSetLayout, nullptr);
    if (m_dummyShadowSampler) vkDestroySampler(m_device, m_dummyShadowSampler, nullptr);
    if (m_dummyShadowView) vkDestroyImageView(m_device, m_dummyShadowView, nullptr);
    if (m_dummyShadowImage) vkDestroyImage(m_device, m_dummyShadowImage, nullptr);
    if (m_dummyShadowMemory) vkFreeMemory(m_device, m_dummyShadowMemory, nullptr);

    destroySwapChain();

    if (m_imageAvailable) vkDestroySemaphore(m_device, m_imageAvailable, nullptr);
    if (m_inFlightFence) vkDestroyFence(m_device, m_inFlightFence, nullptr);
    if (m_commandPool) vkDestroyCommandPool(m_device, m_commandPool, nullptr);

    vkDestroyDevice(m_device, nullptr);
    m_device = VK_NULL_HANDLE;
    m_physicalDevice = VK_NULL_HANDLE;
}

void NativeRenderer::drawUi(const ui::UiRenderer& ui) {
    if (!m_uiPass) {
        m_uiPass = std::make_shared<ui::UiGpuPass>();
        m_uiPass->initialize(ui.font());
    }
    m_uiPass->uploadFrame(ui);
    m_uiPass->draw();
}

// ---------------------------------------------------------------------------
// Occlusion culling resources - an 8x8-tile max-pool of the depth buffer that
// is copied to the CPU and consumed by drawMesh() on the *next* frame
// (predicate: engine/Math/OcclusionTest.h). Built lazily on the first
// endFrame() that needs it, torn down with the swapchain, and self-disabling
// on any failure (missing shader, no host-coherent memory, ...).
// ---------------------------------------------------------------------------

bool NativeRenderer::ensureOcclusionResources() {
    if (m_occlusionReady) return true;
    if (m_occlusionFailed) return false;
    if (!m_device || m_swapChainExtent.width == 0 || m_shaderDir.empty()) return false;

    // Reuses the lighting pass's fullscreen-triangle vertex stage; only the
    // max-pool fragment stage is specific to this pass.
    m_hizVertModule = createShaderModule(m_device, readFile(m_shaderDir + "/deferred_lighting.vert.spv"));
    m_hizFragModule = createShaderModule(m_device, readFile(m_shaderDir + "/hiz_downsample.frag.spv"));
    if (!m_hizVertModule || !m_hizFragModule) {
        fprintf(stderr, "[NativeRenderer] occlusion shaders missing from %s "
                        "(need deferred_lighting.vert.spv + hiz_downsample.frag.spv) — "
                        "occlusion culling disabled\n", m_shaderDir.c_str());
        m_occlusionFailed = true;
        destroyOcclusionResources();
        return false;
    }

    m_occlFullW = m_swapChainExtent.width;
    m_occlFullH = m_swapChainExtent.height;
    m_occlGridW = (m_occlFullW + ks::math::occlusion::kTileSize - 1) / ks::math::occlusion::kTileSize;
    m_occlGridH = (m_occlFullH + ks::math::occlusion::kTileSize - 1) / ks::math::occlusion::kTileSize;
    m_occlGridValid = false; // the old grid (if any) no longer matches extent

    auto fail = [&]() {
        destroyOcclusionResources();
        m_occlusionFailed = true;
        return false;
    };

    // ---- Grid image: written by the downsample pass, then read by the
    // readback copy (hence COLOR_ATTACHMENT | TRANSFER_SRC). ----
    {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_R32_SFLOAT;
        ci.extent = {static_cast<uint32_t>(m_occlGridW), static_cast<uint32_t>(m_occlGridH), 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(m_device, &ci, nullptr, &m_hizImage) != VK_SUCCESS) return fail();

        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(m_device, m_hizImage, &mr);
        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = findMemoryType(m_physicalDevice, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(m_device, &mai, nullptr, &m_hizMemory) != VK_SUCCESS) return fail();
        vkBindImageMemory(m_device, m_hizImage, m_hizMemory, 0);

        VkImageViewCreateInfo vCi{};
        vCi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vCi.image = m_hizImage;
        vCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vCi.format = VK_FORMAT_R32_SFLOAT;
        vCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vCi.subresourceRange.levelCount = 1;
        vCi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_device, &vCi, nullptr, &m_hizView) != VK_SUCCESS) return fail();
    }

    // ---- Render pass: clear to 1.0 (the far plane in Vulkan depth), so a
    // tile the triangle somehow misses reads as infinitely far and can never
    // occlude. finalLayout TRANSFER_SRC lets the copy below run straight
    // after the pass without an extra layout barrier. ----
    {
        VkAttachmentDescription att{};
        att.format = VK_FORMAT_R32_SFLOAT;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        VkSubpassDependency deps[2]{};
        // In: retire this frame's depth read (barriered before the pass) and
        // the previous frame's use of the grid image (its copy — the fence
        // wait in beginFrame already ordered that in time, the dependency
        // keeps the layouts/accesses legal on top of it).
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                               VK_PIPELINE_STAGE_TRANSFER_BIT;
        deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                VK_ACCESS_SHADER_READ_BIT |
                                VK_ACCESS_TRANSFER_READ_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        // Out: hand the finished grid to the readback copy.
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        VkRenderPassCreateInfo rpCi{};
        rpCi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpCi.attachmentCount = 1;
        rpCi.pAttachments = &att;
        rpCi.subpassCount = 1;
        rpCi.pSubpasses = &subpass;
        rpCi.dependencyCount = 2;
        rpCi.pDependencies = deps;
        if (vkCreateRenderPass(m_device, &rpCi, nullptr, &m_hizRenderPass) != VK_SUCCESS) return fail();
    }

    {
        VkImageView att = m_hizView;
        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = m_hizRenderPass;
        fb.attachmentCount = 1;
        fb.pAttachments = &att;
        fb.width = static_cast<uint32_t>(m_occlGridW);
        fb.height = static_cast<uint32_t>(m_occlGridH);
        fb.layers = 1;
        if (vkCreateFramebuffer(m_device, &fb, nullptr, &m_hizFramebuffer) != VK_SUCCESS) return fail();
    }

    // ---- Descriptor for the depth image. Which image it names depends on
    // the geometry path that ran (forward depth vs GBuffer depth), so the
    // actual write happens per frame in recordOcclusionPass(). ----
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo dslCi{};
        dslCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslCi.bindingCount = 1;
        dslCi.pBindings = &binding;
        if (vkCreateDescriptorSetLayout(m_device, &dslCi, nullptr, &m_hizSetLayout) != VK_SUCCESS) return fail();

        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
        VkDescriptorPoolCreateInfo poolCi{};
        poolCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolCi.maxSets = 1;
        poolCi.poolSizeCount = 1;
        poolCi.pPoolSizes = &poolSize;
        if (vkCreateDescriptorPool(m_device, &poolCi, nullptr, &m_hizPool) != VK_SUCCESS) return fail();

        VkDescriptorSetAllocateInfo dsAi{};
        dsAi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAi.descriptorPool = m_hizPool;
        dsAi.descriptorSetCount = 1;
        dsAi.pSetLayouts = &m_hizSetLayout;
        if (vkAllocateDescriptorSets(m_device, &dsAi, &m_hizSet) != VK_SUCCESS) return fail();

        // NEAREST: texelFetch in the shader ignores filtering anyway, and it
        // keeps the sampler legal for the depth aspect.
        VkSamplerCreateInfo sCi{};
        sCi.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sCi.magFilter = VK_FILTER_NEAREST;
        sCi.minFilter = VK_FILTER_NEAREST;
        sCi.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sCi.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sCi.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sCi.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sCi.maxLod = 1.0f;
        if (vkCreateSampler(m_device, &sCi, nullptr, &m_hizSampler) != VK_SUCCESS) return fail();
    }

    // ---- Readback buffer: ~W*H/64 floats (~130 KB at 1080p), persistently
    // mapped HOST_VISIBLE|HOST_COHERENT so beginFrame() can consume it after
    // the fence wait with no per-frame map/unmap and no invalidate. ----
    {
        const VkDeviceSize size = static_cast<VkDeviceSize>(m_occlGridW) *
                                  static_cast<VkDeviceSize>(m_occlGridH) * sizeof(float);
        VkBufferCreateInfo bCi{};
        bCi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bCi.size = size;
        bCi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bCi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_device, &bCi, nullptr, &m_occlReadback) != VK_SUCCESS) return fail();

        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(m_device, m_occlReadback, &mr);
        uint32_t typeIndex = 0;
        if (!findMemoryTypeStrict(m_physicalDevice, mr.memoryTypeBits,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                  VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  typeIndex)) {
            fprintf(stderr, "[NativeRenderer] no host-visible+coherent memory for the occlusion "
                            "readback — occlusion culling disabled\n");
            return fail();
        }
        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = typeIndex;
        if (vkAllocateMemory(m_device, &mai, nullptr, &m_occlReadbackMemory) != VK_SUCCESS) return fail();
        vkBindBufferMemory(m_device, m_occlReadback, m_occlReadbackMemory, 0);
        if (vkMapMemory(m_device, m_occlReadbackMemory, 0, size, 0, &m_occlReadbackMapped) != VK_SUCCESS)
            return fail();
    }

    // ---- Pipeline: fullscreen triangle (no vertex input, no depth, cull
    // off) into the single R32F grid attachment. ----
    {
        VkPipelineLayoutCreateInfo layoutCi{};
        layoutCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutCi.setLayoutCount = 1;
        layoutCi.pSetLayouts = &m_hizSetLayout;
        if (vkCreatePipelineLayout(m_device, &layoutCi, nullptr, &m_hizLayout) != VK_SUCCESS) return fail();

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAtt{};
        blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAtt;

        VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynState{};
        dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynState.dynamicStateCount = 2;
        dynState.pDynamicStates = dynStates;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = m_hizVertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = m_hizFragModule;
        stages[1].pName = "main";

        VkGraphicsPipelineCreateInfo pipeCi{};
        pipeCi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeCi.stageCount = 2;
        pipeCi.pStages = stages;
        pipeCi.pVertexInputState = &vertexInput;
        pipeCi.pInputAssemblyState = &inputAssembly;
        pipeCi.pViewportState = &viewportState;
        pipeCi.pRasterizationState = &rasterizer;
        pipeCi.pMultisampleState = &multisampling;
        pipeCi.pColorBlendState = &colorBlend;
        pipeCi.pDepthStencilState = nullptr;
        pipeCi.pDynamicState = &dynState;
        pipeCi.layout = m_hizLayout;
        pipeCi.renderPass = m_hizRenderPass;
        pipeCi.subpass = 0;
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeCi, nullptr, &m_hizPipeline) != VK_SUCCESS)
            return fail();
    }

    m_occlusionReady = true;
    fprintf(stderr, "[NativeRenderer] occlusion grid %dx%d tiles (tile=%d px) @ %dx%d depth\n",
            m_occlGridW, m_occlGridH, ks::math::occlusion::kTileSize, m_occlFullW, m_occlFullH);
    return true;
}

void NativeRenderer::destroyOcclusionResources() {
    if (!m_device) return;

    if (m_hizPipeline) { vkDestroyPipeline(m_device, m_hizPipeline, nullptr); m_hizPipeline = VK_NULL_HANDLE; }
    if (m_hizLayout) { vkDestroyPipelineLayout(m_device, m_hizLayout, nullptr); m_hizLayout = VK_NULL_HANDLE; }
    if (m_hizVertModule) { vkDestroyShaderModule(m_device, m_hizVertModule, nullptr); m_hizVertModule = VK_NULL_HANDLE; }
    if (m_hizFragModule) { vkDestroyShaderModule(m_device, m_hizFragModule, nullptr); m_hizFragModule = VK_NULL_HANDLE; }

    // Destroying the pool frees the set allocated from it.
    if (m_hizPool) { vkDestroyDescriptorPool(m_device, m_hizPool, nullptr); m_hizPool = VK_NULL_HANDLE; }
    m_hizSet = VK_NULL_HANDLE;
    if (m_hizSetLayout) { vkDestroyDescriptorSetLayout(m_device, m_hizSetLayout, nullptr); m_hizSetLayout = VK_NULL_HANDLE; }
    if (m_hizSampler) { vkDestroySampler(m_device, m_hizSampler, nullptr); m_hizSampler = VK_NULL_HANDLE; }

    if (m_hizRenderPass) { vkDestroyRenderPass(m_device, m_hizRenderPass, nullptr); m_hizRenderPass = VK_NULL_HANDLE; }
    if (m_hizFramebuffer) { vkDestroyFramebuffer(m_device, m_hizFramebuffer, nullptr); m_hizFramebuffer = VK_NULL_HANDLE; }
    if (m_hizView) { vkDestroyImageView(m_device, m_hizView, nullptr); m_hizView = VK_NULL_HANDLE; }
    if (m_hizImage) { vkDestroyImage(m_device, m_hizImage, nullptr); m_hizImage = VK_NULL_HANDLE; }
    if (m_hizMemory) { vkFreeMemory(m_device, m_hizMemory, nullptr); m_hizMemory = VK_NULL_HANDLE; }

    if (m_occlReadbackMapped) {
        vkUnmapMemory(m_device, m_occlReadbackMemory);
        m_occlReadbackMapped = nullptr;
    }
    if (m_occlReadback) { vkDestroyBuffer(m_device, m_occlReadback, nullptr); m_occlReadback = VK_NULL_HANDLE; }
    if (m_occlReadbackMemory) { vkFreeMemory(m_device, m_occlReadbackMemory, nullptr); m_occlReadbackMemory = VK_NULL_HANDLE; }

    // A grid that was about to be consumed no longer exists: invalidate both
    // halves of the handshake so drawMesh() stops testing against stale or
    // half-freed data (this runs on resize, before the next frame starts).
    m_occlusionReady = false;
    m_occlPending = false;
    m_occlGridValid = false;
}

void NativeRenderer::recordOcclusionPass(VkImage depthImage, VkImageView depthView) {
    // ---- 1. Geometry depth writes -> shader reads. The render pass above
    // left the image in DEPTH_STENCIL_ATTACHMENT_OPTIMAL with its writes
    // made available by the pass's final-layout transition. ----
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = depthImage;
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(m_commandBuffer,
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                             VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    // ---- 2. Point the descriptor at this frame's depth image (forward depth
    // or GBuffer depth — both are in SHADER_READ_ONLY right now). Safe to
    // rewrite every frame: the fence wait in beginFrame means no command
    // buffer still in flight references the set. ----
    {
        VkDescriptorImageInfo imgInfo{};
        imgInfo.sampler = m_hizSampler;
        imgInfo.imageView = depthView;
        imgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_hizSet;
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.pImageInfo = &imgInfo;
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);
    }

    // ---- 3. Downsample: one fullscreen triangle over the whole grid. ----
    const VkExtent2D gridExtent{static_cast<uint32_t>(m_occlGridW),
                                static_cast<uint32_t>(m_occlGridH)};
    {
        VkClearValue clear{};
        clear.color = {{1.0f, 0.0f, 0.0f, 1.0f}}; // 1.0 = far plane: never occludes

        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = m_hizRenderPass;
        rp.framebuffer = m_hizFramebuffer;
        rp.renderArea.extent = gridExtent;
        rp.clearValueCount = 1;
        rp.pClearValues = &clear;
        vkCmdBeginRenderPass(m_commandBuffer, &rp, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{0.0f, 0.0f, float(gridExtent.width), float(gridExtent.height), 0.0f, 1.0f};
        VkRect2D sc{{0, 0}, gridExtent};
        vkCmdSetViewport(m_commandBuffer, 0, 1, &vp);
        vkCmdSetScissor(m_commandBuffer, 0, 1, &sc);
        vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_hizPipeline);
        vkCmdBindDescriptorSets(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_hizLayout,
                                0, 1, &m_hizSet, 0, nullptr);
        vkCmdDraw(m_commandBuffer, 3, 1, 0, 0);
        vkCmdEndRenderPass(m_commandBuffer);
    }

    // ---- 4. Copy the finished grid (now in TRANSFER_SRC) to the readback
    // buffer; the fence wait in the next beginFrame() is what makes its
    // contents visible to the CPU. ----
    {
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {gridExtent.width, gridExtent.height, 1};
        vkCmdCopyImageToBuffer(m_commandBuffer, m_hizImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               m_occlReadback, 1, &region);
        m_occlPending = true;
    }
}

// ---------------------------------------------------------------------------
// Particles (roadmap ksengine-vs-cryengine P1): the GPU half of the sprite
// system.
//
// The simulation half is ks::engine::graphics::ParticleSystem (CPU side,
// fixed 1/120 s steps, buildQuads() packs the float[12] records this pass
// consumes). What lives here is the draw: one host-visible vertex buffer
// rewritten each frame plus two pipelines built from the same particle.vert
// — alpha blended into the forward pass, or written into the GBuffer so the
// deferred lighting pass lights it like everything else. Both are lazy: a
// frame with no particles creates no pipeline, no buffer and no shader
// module, and a missing .spv disables the draw for good instead of
// re-reading files every frame.
// ---------------------------------------------------------------------------

namespace {

// Layout of one particle vertex, in bytes. Backed by
// ParticleSystem::kFloatsPerVertex floats (12): centre, uv corner, tint,
// size, life, max life.
constexpr VkDeviceSize kParticleVertexStride = sizeof(float) * ParticleSystem::kFloatsPerVertex;
// mat4 viewProj + 3 x vec4 (camera right, camera up, params) — must match
// ParticlePC in particle.vert, whose spirv-dis offsets are 0/64/80/96.
constexpr uint32_t kParticlePushConstantSize = 112;

// Builds one of the two sprite pipelines. They share every piece of state
// except the render pass (and the colour-blend state, which the deferred
// GBuffer cannot have: RT0.a is ambient occlusion there, not sprite alpha).
VkPipeline createParticlePipeline(VkDevice device, VkPipelineLayout layout, VkRenderPass renderPass,
                                  uint32_t colorAttachmentCount, bool alphaBlend,
                                  VkShaderModule vert, VkShaderModule frag) {
    VkVertexInputBindingDescription binding{0, kParticleVertexStride, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[6] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT,     0},  // world centre
        {1, 0, VK_FORMAT_R32G32_SFLOAT,       12},  // sprite corner (uv)
        {2, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 20},  // tint rgb + base alpha
        {3, 0, VK_FORMAT_R32_SFLOAT,          36},  // size
        {4, 0, VK_FORMAT_R32_SFLOAT,          40},  // life
        {5, 0, VK_FORMAT_R32_SFLOAT,          44},  // max life
    };
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 6;
    vertexInput.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    // No culling: the billboard's winding is decided by the camera basis the
    // sprite is built from, and a camera roll must not be able to make the
    // whole particle system disappear.
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Straight-alpha over blend for the forward pass; the GBuffer variant
    // takes no blend state at all (it writes through to the lighting pass).
    VkPipelineColorBlendAttachmentState blendAtts[3]{};
    for (uint32_t i = 0; i < colorAttachmentCount; ++i) {
        blendAtts[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (alphaBlend) {
            blendAtts[i].blendEnable = VK_TRUE;
            blendAtts[i].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blendAtts[i].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAtts[i].colorBlendOp = VK_BLEND_OP_ADD;
            blendAtts[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAtts[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAtts[i].alphaBlendOp = VK_BLEND_OP_ADD;
        }
    }
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = colorAttachmentCount;
    colorBlend.pAttachments = blendAtts;

    // Depth test on, depth write off: a sprite must be hidden by the
    // geometry in front of it, but must never occlude anything itself —
    // overlapping particles have to blend, not z-fight.
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynState{};
    dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynState.dynamicStateCount = 2;
    dynState.pDynamicStates = dynStates;

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkGraphicsPipelineCreateInfo pipeCi{};
    pipeCi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeCi.stageCount = 2;
    pipeCi.pStages = stages;
    pipeCi.pVertexInputState = &vertexInput;
    pipeCi.pInputAssemblyState = &inputAssembly;
    pipeCi.pViewportState = &viewportState;
    pipeCi.pRasterizationState = &rasterizer;
    pipeCi.pMultisampleState = &multisampling;
    pipeCi.pColorBlendState = &colorBlend;
    pipeCi.pDepthStencilState = &depthStencil;
    pipeCi.pDynamicState = &dynState;
    pipeCi.layout = layout;
    pipeCi.renderPass = renderPass;
    pipeCi.subpass = 0;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeCi, nullptr, &pipeline) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return pipeline;
}

} // namespace

void NativeRenderer::setParticleVertices(const float* data, size_t floatCount) {
    if (!data || floatCount == 0) {
        m_particleVerts.clear();
        return;
    }
    m_particleVerts.assign(data, data + floatCount);
}

bool NativeRenderer::ensureParticleBuffer(VkDeviceSize bytes) {
    if (!m_device || bytes == 0) return false;
    if (m_particleBuffer && m_particleCapacity >= bytes) return true;

    // Grow, or first-time build: one frame in flight (beginFrame() waits on
    // the only fence), so whatever was bound last frame is long retired by
    // the time this replace happens.
    destroyParticleBuffer();
    if (!createBuffer(m_physicalDevice, m_device, bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      m_particleBuffer, m_particleMemory)) {
        std::fprintf(stderr, "[NativeRenderer] particle quad buffer (%llu bytes) failed\n",
                     static_cast<unsigned long long>(bytes));
        return false;
    }
    if (vkMapMemory(m_device, m_particleMemory, 0, bytes, 0, &m_particleMapped) != VK_SUCCESS ||
        !m_particleMapped) {
        std::fprintf(stderr, "[NativeRenderer] particle quad buffer: map failed\n");
        destroyParticleBuffer();
        return false;
    }
    m_particleCapacity = bytes;
    return true;
}

bool NativeRenderer::ensureParticlePipelines(bool deferred) {
    if (!m_device || m_particleShadersFailed) return false;

    if (!m_particleVertModule)
        m_particleVertModule = createShaderModule(m_device, readFile(m_shaderDir + "/particle.vert.spv"));
    if (!m_particleFragModule)
        m_particleFragModule = createShaderModule(m_device, readFile(m_shaderDir + "/particle.frag.spv"));
    if (!m_particleVertModule || !m_particleFragModule) {
        // The .spv set was not built for this configure — drop the draw for
        // good rather than re-reading (and re-failing) every frame.
        std::fprintf(stderr, "[NativeRenderer] particle shaders missing from %s — sprite draw disabled\n",
                     m_shaderDir.c_str());
        m_particleShadersFailed = true;
        return false;
    }

    if (!m_particleLayout) {
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pcRange.offset = 0;
        pcRange.size = kParticlePushConstantSize;
        VkPipelineLayoutCreateInfo layoutCi{};
        layoutCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        // Deliberately no descriptor sets: particle.frag/particle_gbuffer.frag
        // sample nothing (the sprite shape is procedural), so push constants
        // are the entire interface.
        layoutCi.setLayoutCount = 0;
        layoutCi.pSetLayouts = nullptr;
        layoutCi.pushConstantRangeCount = 1;
        layoutCi.pPushConstantRanges = &pcRange;
        if (vkCreatePipelineLayout(m_device, &layoutCi, nullptr, &m_particleLayout) != VK_SUCCESS) {
            std::fprintf(stderr, "[NativeRenderer] particle pipeline layout failed\n");
            m_particleShadersFailed = true;
            return false;
        }
    }

    if (!m_particleFwdPipeline) {
        if (!m_renderPass) return false; // no swapchain yet: try again next frame
        m_particleFwdPipeline = createParticlePipeline(m_device, m_particleLayout, m_renderPass, 1,
                                                       true, m_particleVertModule, m_particleFragModule);
        if (!m_particleFwdPipeline) {
            std::fprintf(stderr, "[NativeRenderer] forward particle pipeline failed\n");
            m_particleShadersFailed = true;
            return false;
        }
    }

    if (deferred) {
        if (!m_gbufferRenderPass) return false;
        if (!m_particleGbufPipeline && !m_particleGbufFailed) {
            if (!m_particleGbufFragModule)
                m_particleGbufFragModule =
                    createShaderModule(m_device, readFile(m_shaderDir + "/particle_gbuffer.frag.spv"));
            if (!m_particleGbufFragModule) {
                std::fprintf(stderr, "[NativeRenderer] particle_gbuffer.frag.spv missing from %s — "
                                     "particles stay off the deferred path\n", m_shaderDir.c_str());
                m_particleGbufFailed = true;
                return false;
            }
            m_particleGbufPipeline = createParticlePipeline(m_device, m_particleLayout, m_gbufferRenderPass,
                                                            3, false, m_particleVertModule,
                                                            m_particleGbufFragModule);
            if (!m_particleGbufPipeline) {
                std::fprintf(stderr, "[NativeRenderer] GBuffer particle pipeline failed\n");
                m_particleGbufFailed = true;
                return false;
            }
        }
        if (!m_particleGbufPipeline) return false;
    }

    return m_particleFwdPipeline != VK_NULL_HANDLE;
}

void NativeRenderer::recordParticles(const mat4& viewProjRender, bool deferred) {
    if (m_particleVerts.empty() || m_particleAlpha <= 0.0f) return;
    if (!ensureParticlePipelines(deferred)) return;

    VkPipeline pipeline = deferred ? m_particleGbufPipeline : m_particleFwdPipeline;
    if (!pipeline) return;

    const VkDeviceSize bytes = m_particleVerts.size() * sizeof(float);
    if (!ensureParticleBuffer(bytes)) return;
    std::memcpy(m_particleMapped, m_particleVerts.data(), static_cast<size_t>(bytes));

    struct ParticlePC {
        float viewProj[16];
        float cameraRight[4];
        float cameraUp[4];
        float params[4];
    } pc;
    static_assert(sizeof(ParticlePC) == kParticlePushConstantSize,
                  "ParticlePC must match ParticlePC in particle.vert");
    std::memcpy(pc.viewProj, viewProjRender.data(), sizeof(float) * 16);
    // v = R*w + t, so a view axis expressed in world space is the matching
    // *row* of R: right = row 0, up = row 1. cross(right, up) is then the
    // view +Z axis — back at the eye, which is the normal the GBuffer pass
    // wants for a camera-facing sprite.
    pc.cameraRight[0] = m_view(0, 0);
    pc.cameraRight[1] = m_view(1, 0);
    pc.cameraRight[2] = m_view(2, 0);
    pc.cameraRight[3] = 0.0f;
    pc.cameraUp[0] = m_view(0, 1);
    pc.cameraUp[1] = m_view(1, 1);
    pc.cameraUp[2] = m_view(2, 1);
    pc.cameraUp[3] = 0.0f;
    pc.params[0] = m_particleAlpha;
    pc.params[1] = pc.params[2] = pc.params[3] = 0.0f;

    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdPushConstants(m_commandBuffer, m_particleLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
    VkBuffer vBufs[] = {m_particleBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(m_commandBuffer, 0, 1, vBufs, offsets);
    vkCmdDraw(m_commandBuffer, static_cast<uint32_t>(m_particleVerts.size() / ParticleSystem::kFloatsPerVertex),
              1, 0, 0);
}

void NativeRenderer::destroyParticleBuffer() {
    if (!m_device) return;
    if (m_particleMapped) { vkUnmapMemory(m_device, m_particleMemory); m_particleMapped = nullptr; }
    if (m_particleBuffer) { vkDestroyBuffer(m_device, m_particleBuffer, nullptr); m_particleBuffer = VK_NULL_HANDLE; }
    if (m_particleMemory) { vkFreeMemory(m_device, m_particleMemory, nullptr); m_particleMemory = VK_NULL_HANDLE; }
    m_particleCapacity = 0;
}

void NativeRenderer::destroyParticleGbufPipeline() {
    // Only the half that was built against m_gbufferRenderPass: that render
    // pass dies with the swapchain, so the pipeline made from it must too
    // (the forward half is built against m_renderPass and is recreated the
    // same way... except it survives, since a rebuilt pass is compatible).
    if (!m_device) return;
    if (m_particleGbufPipeline) {
        vkDestroyPipeline(m_device, m_particleGbufPipeline, nullptr);
        m_particleGbufPipeline = VK_NULL_HANDLE;
    }
    if (m_particleGbufFragModule) {
        vkDestroyShaderModule(m_device, m_particleGbufFragModule, nullptr);
        m_particleGbufFragModule = VK_NULL_HANDLE;
    }
    m_particleGbufFailed = false;
}

void NativeRenderer::destroyParticlePipelines() {
    if (!m_device) return;
    destroyParticleGbufPipeline();
    if (m_particleFwdPipeline) {
        vkDestroyPipeline(m_device, m_particleFwdPipeline, nullptr);
        m_particleFwdPipeline = VK_NULL_HANDLE;
    }
    if (m_particleLayout) {
        vkDestroyPipelineLayout(m_device, m_particleLayout, nullptr);
        m_particleLayout = VK_NULL_HANDLE;
    }
    if (m_particleVertModule) { vkDestroyShaderModule(m_device, m_particleVertModule, nullptr); m_particleVertModule = VK_NULL_HANDLE; }
    if (m_particleFragModule) { vkDestroyShaderModule(m_device, m_particleFragModule, nullptr); m_particleFragModule = VK_NULL_HANDLE; }
    m_particleShadersFailed = false;
    m_particleGbufFailed = false;
}

// ---------------------------------------------------------------------------
// Deferred path — opt-in via setDeferred(). Built lazily on the first
// endFrame() that needs it and torn down with the swapchain, so the forward
// path never pays for any of this and a resize can never leave stale-sized
// targets behind. Every failure falls back to forward rather than black.
// ---------------------------------------------------------------------------

bool NativeRenderer::ensureDeferredResources() {
    if (m_deferredReady) return true;
    if (m_deferredFailed) return false;
    if (!m_device || !m_pipelineLayout || m_swapChainImageViews.empty() ||
        m_swapChainExtent.width == 0 || m_shaderDir.empty()) return false;

    m_gbufferVertModule = createShaderModule(m_device, readFile(m_shaderDir + "/gbuffer.vert.spv"));
    m_gbufferFragModule = createShaderModule(m_device, readFile(m_shaderDir + "/gbuffer.frag.spv"));
    m_lightingVertModule = createShaderModule(m_device, readFile(m_shaderDir + "/deferred_lighting.vert.spv"));
    m_lightingFragModule = createShaderModule(m_device, readFile(m_shaderDir + "/deferred_lighting.frag.spv"));
    m_resolveFragModule = createShaderModule(m_device, readFile(m_shaderDir + "/taa.frag.spv"));
    m_tonemapFragModule = createShaderModule(m_device, readFile(m_shaderDir + "/tonemap.frag.spv"));
    m_glareFragModule = createShaderModule(m_device, readFile(m_shaderDir + "/glareExtract.frag.spv"));
    m_blurFragModule = createShaderModule(m_device, readFile(m_shaderDir + "/bloomBlur.frag.spv"));
    if (!m_gbufferVertModule || !m_gbufferFragModule || !m_lightingVertModule || !m_lightingFragModule ||
        !m_resolveFragModule || !m_tonemapFragModule || !m_glareFragModule || !m_blurFragModule) {
        fprintf(stderr, "[NativeRenderer] deferred shaders missing from %s (need gbuffer.{vert,frag}, "
                        "deferred_lighting.{vert,frag}, taa.frag, tonemap.frag, glareExtract.frag and "
                        "bloomBlur.frag .spv) — staying on the forward path\n",
                m_shaderDir.c_str());
        if (m_hdrOutput)
            fprintf(stderr, "[NativeRenderer] WARNING: the swapchain is HDR10 but the display pass cannot "
                            "be built — the image will be wrong, run with KS_HDR=0\n");
        m_deferredFailed = true;
        destroyDeferredResources();
        return false;
    }

    // ---- GBuffer colour targets. Sampled by the lighting pass, so they
    // carry COLOR_ATTACHMENT | SAMPLED from the start. ----
    for (int i = 0; i < kGBufferCount; ++i) {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = m_gbufferFormats[i];
        ci.extent = {m_swapChainExtent.width, m_swapChainExtent.height, 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(m_device, &ci, nullptr, &m_gbufferImages[i]) != VK_SUCCESS) { destroyDeferredResources(); return false; }

        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(m_device, m_gbufferImages[i], &mr);
        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = findMemoryType(m_physicalDevice, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(m_device, &mai, nullptr, &m_gbufferMemory[i]) != VK_SUCCESS) { destroyDeferredResources(); return false; }
        vkBindImageMemory(m_device, m_gbufferImages[i], m_gbufferMemory[i], 0);

        VkImageViewCreateInfo vCi{};
        vCi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vCi.image = m_gbufferImages[i];
        vCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vCi.format = m_gbufferFormats[i];
        vCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        vCi.subresourceRange.levelCount = 1;
        vCi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_device, &vCi, nullptr, &m_gbufferViews[i]) != VK_SUCCESS) { destroyDeferredResources(); return false; }
    }

    // Private depth: sharing m_depthImage would mean juggling its layout
    // against the forward pass, and the two never run in the same frame.
    {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.format = VK_FORMAT_D32_SFLOAT;
        ci.extent = {m_swapChainExtent.width, m_swapChainExtent.height, 1};
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        // SAMPLED for the same reason as the forward depth image: the
        // occlusion downsample reads it back after the GBuffer pass.
        ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(m_device, &ci, nullptr, &m_gbufferDepthImage) != VK_SUCCESS) { destroyDeferredResources(); return false; }

        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(m_device, m_gbufferDepthImage, &mr);
        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = findMemoryType(m_physicalDevice, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(m_device, &mai, nullptr, &m_gbufferDepthMemory) != VK_SUCCESS) { destroyDeferredResources(); return false; }
        vkBindImageMemory(m_device, m_gbufferDepthImage, m_gbufferDepthMemory, 0);

        VkImageViewCreateInfo vCi{};
        vCi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vCi.image = m_gbufferDepthImage;
        vCi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vCi.format = VK_FORMAT_D32_SFLOAT;
        vCi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        vCi.subresourceRange.levelCount = 1;
        vCi.subresourceRange.layerCount = 1;
        if (vkCreateImageView(m_device, &vCi, nullptr, &m_gbufferDepthView) != VK_SUCCESS) { destroyDeferredResources(); return false; }
    }

    // ---- Lighting output (RGBA16F so accumulated light and fog survive the
    // round trip) plus two ping-ponged temporal history slots. ----
    if (!createSampledTarget(m_physicalDevice, m_device, m_swapChainExtent.width, m_swapChainExtent.height,
                             VK_FORMAT_R16G16B16A16_SFLOAT, m_hdrImage, m_hdrMemory, m_hdrView)) {
        destroyDeferredResources();
        return false;
    }
    for (int i = 0; i < kHistoryCount; ++i) {
        if (!createSampledTarget(m_physicalDevice, m_device, m_swapChainExtent.width, m_swapChainExtent.height,
                                 VK_FORMAT_R16G16B16A16_SFLOAT, m_historyImages[i], m_historyMemory[i],
                                 m_historyViews[i])) {
            destroyDeferredResources();
            return false;
        }
    }
    m_historyIndex = 0;
    m_historyValid = false;

    // ---- Bloom targets, half resolution (a wide gaussian here covers a
    // full-res radius for a quarter of the taps). Slot A ends up holding the
    // bright-pass result and then the final, vertically-blurred bloom; slot
    // B is horizontal-only scratch — ping-ponged so a pass never reads and
    // writes the same image. ----
    m_bloomExtent = {std::max(1u, m_swapChainExtent.width / 2),
                     std::max(1u, m_swapChainExtent.height / 2)};
    for (int i = 0; i < 2; ++i) {
        if (!createSampledTarget(m_physicalDevice, m_device, m_bloomExtent.width, m_bloomExtent.height,
                                 VK_FORMAT_R16G16B16A16_SFLOAT, m_bloomImages[i], m_bloomMemory[i],
                                 m_bloomViews[i])) {
            destroyDeferredResources();
            return false;
        }
    }

    // A descriptor may not name an image that is still UNDEFINED, and the
    // resolve pass only ever writes *one* history slot per frame — so both
    // need a one-time transition up front, before the first draw can legally
    // bind them (feedback is 0 that frame, so nothing is actually read yet).
    {
        VkCommandBufferAllocateInfo cbAi{};
        cbAi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cbAi.commandPool = m_commandPool;
        cbAi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbAi.commandBufferCount = 1;
        VkCommandBuffer cb = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(m_device, &cbAi, &cb) != VK_SUCCESS) { destroyDeferredResources(); return false; }

        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &begin);
        for (int i = 0; i < kHistoryCount; ++i) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = m_historyImages[i];
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.layerCount = 1;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &barrier);
        }
        vkEndCommandBuffer(cb);

        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cb;
        vkQueueSubmit(m_graphicsQueue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_graphicsQueue);
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &cb);
    }

    // ---- GBuffer render pass: 3 colour + depth, one subpass, and a final
    // dependency that hands the targets to the lighting pass as sampled
    // textures. ----
    {
        VkAttachmentDescription atts[4]{};
        for (int i = 0; i < 3; ++i) {
            atts[i].format = m_gbufferFormats[i];
            atts[i].samples = VK_SAMPLE_COUNT_1_BIT;
            atts[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            atts[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            atts[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            atts[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            atts[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            atts[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        atts[3].format = VK_FORMAT_D32_SFLOAT;
        atts[3].samples = VK_SAMPLE_COUNT_1_BIT;
        atts[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        atts[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        atts[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        atts[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        atts[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        atts[3].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRefs[3];
        for (int i = 0; i < 3; ++i) colorRefs[i] = {uint32_t(i), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{3, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 3;
        subpass.pColorAttachments = colorRefs;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rpCi{};
        rpCi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpCi.attachmentCount = 4;
        rpCi.pAttachments = atts;
        rpCi.subpassCount = 1;
        rpCi.pSubpasses = &subpass;
        rpCi.dependencyCount = 2;
        rpCi.pDependencies = deps;
        if (vkCreateRenderPass(m_device, &rpCi, nullptr, &m_gbufferRenderPass) != VK_SUCCESS) { destroyDeferredResources(); return false; }
    }

    // ---- Lighting render pass: one RGBA16F colour target (the HDR buffer),
    // no depth (fullscreen triangle), handed to the resolve pass as a
    // sampled texture. ----
    {
        VkAttachmentDescription att{};
        att.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        // Covers both sides of this pass: the GBuffer reads from the pass
        // above, and the HDR buffer's *previous* use was being sampled by
        // last frame's resolve — so its write needs that read retired too.
        deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rpCi{};
        rpCi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpCi.attachmentCount = 1;
        rpCi.pAttachments = &att;
        rpCi.subpassCount = 1;
        rpCi.pSubpasses = &subpass;
        rpCi.dependencyCount = 2;
        rpCi.pDependencies = deps;
        if (vkCreateRenderPass(m_device, &rpCi, nullptr, &m_lightingRenderPass) != VK_SUCCESS) { destroyDeferredResources(); return false; }
    }

    // ---- Resolve render pass: one RGBA16F attachment — the history slot
    // this frame writes, sampled back out by the bloom extract and the
    // display pass below. The swapchain deliberately does *not* live here
    // anymore: the backbuffer belongs to the display pass, which is the only
    // place the frame gets tone-mapped and encoded (sRGB or PQ). Its
    // external dependency covers both things the resolve samples but did not
    // write: the HDR buffer from the lighting pass above, and the other
    // history slot from the previous frame. ----
    {
        VkAttachmentDescription att{};
        att.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        // src side covers everything this pass samples but does not write:
        // the HDR buffer from the lighting pass above (color write) and the
        // history slot from the previous frame (fragment read — which is also
        // what makes overwriting the *other* slot safe, since it was sampled
        // last frame too).
        deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rpCi{};
        rpCi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpCi.attachmentCount = 1;
        rpCi.pAttachments = &att;
        rpCi.subpassCount = 1;
        rpCi.pSubpasses = &subpass;
        rpCi.dependencyCount = 2;
        rpCi.pDependencies = deps;
        if (vkCreateRenderPass(m_device, &rpCi, nullptr, &m_resolveRenderPass) != VK_SUCCESS) { destroyDeferredResources(); return false; }
    }

    // ---- Bloom render pass: one RGBA16F attachment at half resolution,
    // shared by the bright-pass extract and both blur axes (all three end in
    // SHADER_READ_ONLY, which is exactly what the next pass wants to sample,
    // and the external dependency below covers write->read and read->write
    // between consecutive passes of the chain). ----
    {
        VkAttachmentDescription att{};
        att.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        VkRenderPassCreateInfo rpCi{};
        rpCi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpCi.attachmentCount = 1;
        rpCi.pAttachments = &att;
        rpCi.subpassCount = 1;
        rpCi.pSubpasses = &subpass;
        rpCi.dependencyCount = 2;
        rpCi.pDependencies = deps;
        if (vkCreateRenderPass(m_device, &rpCi, nullptr, &m_bloomRenderPass) != VK_SUCCESS) { destroyDeferredResources(); return false; }
    }

    // ---- Display render pass: the backbuffer, and the only thing that ever
    // transitions it to PRESENT_SRC on this path now. Its external
    // dependency waits for the resolve pass's history write (what it
    // samples), and its own write is what the present engine waits on. ----
    {
        VkAttachmentDescription att{};
        att.format = m_swapChainFormat;
        att.samples = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        VkSubpassDependency deps[2]{};
        deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        deps[0].dstSubpass = 0;
        deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
        deps[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
        deps[1].srcSubpass = 0;
        deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        deps[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        deps[1].dstAccessMask = 0;

        VkRenderPassCreateInfo rpCi{};
        rpCi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpCi.attachmentCount = 1;
        rpCi.pAttachments = &att;
        rpCi.subpassCount = 1;
        rpCi.pSubpasses = &subpass;
        rpCi.dependencyCount = 2;
        rpCi.pDependencies = deps;
        if (vkCreateRenderPass(m_device, &rpCi, nullptr, &m_displayRenderPass) != VK_SUCCESS) { destroyDeferredResources(); return false; }
    }

    // ---- Framebuffers. GBuffer and lighting are single-instance (strictly
    // one frame in flight: beginFrame() waits on the only fence), the
    // resolve pass needs one per history slot, the bloom chain two (its two
    // ping-pong targets), and the display pass one per swapchain image. ----
    {
        const size_t swapCount = m_swapChainImageViews.size();
        m_gbufferFramebuffers.resize(swapCount);
        for (size_t i = 0; i < swapCount; ++i) {
            VkImageView gAtts[4] = {m_gbufferViews[0], m_gbufferViews[1], m_gbufferViews[2], m_gbufferDepthView};
            VkFramebufferCreateInfo fb{};
            fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass = m_gbufferRenderPass;
            fb.attachmentCount = 4;
            fb.pAttachments = gAtts;
            fb.width = m_swapChainExtent.width;
            fb.height = m_swapChainExtent.height;
            fb.layers = 1;
            if (vkCreateFramebuffer(m_device, &fb, nullptr, &m_gbufferFramebuffers[i]) != VK_SUCCESS) { destroyDeferredResources(); return false; }
        }

        {
            VkImageView lAtts[1] = {m_hdrView};
            VkFramebufferCreateInfo fb{};
            fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass = m_lightingRenderPass;
            fb.attachmentCount = 1;
            fb.pAttachments = lAtts;
            fb.width = m_swapChainExtent.width;
            fb.height = m_swapChainExtent.height;
            fb.layers = 1;
            if (vkCreateFramebuffer(m_device, &fb, nullptr, &m_lightingFramebuffer) != VK_SUCCESS) { destroyDeferredResources(); return false; }
        }

        m_resolveFramebuffers.resize(kHistoryCount);
        for (int w = 0; w < kHistoryCount; ++w) {
            VkImageView rAtts[1] = {m_historyViews[w]};
            VkFramebufferCreateInfo fb{};
            fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass = m_resolveRenderPass;
            fb.attachmentCount = 1;
            fb.pAttachments = rAtts;
            fb.width = m_swapChainExtent.width;
            fb.height = m_swapChainExtent.height;
            fb.layers = 1;
            if (vkCreateFramebuffer(m_device, &fb, nullptr, &m_resolveFramebuffers[w]) != VK_SUCCESS) { destroyDeferredResources(); return false; }
        }

        for (int i = 0; i < 2; ++i) {
            VkImageView bAtts[1] = {m_bloomViews[i]};
            VkFramebufferCreateInfo fb{};
            fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass = m_bloomRenderPass;
            fb.attachmentCount = 1;
            fb.pAttachments = bAtts;
            fb.width = m_bloomExtent.width;
            fb.height = m_bloomExtent.height;
            fb.layers = 1;
            if (vkCreateFramebuffer(m_device, &fb, nullptr, &m_bloomFramebuffers[i]) != VK_SUCCESS) { destroyDeferredResources(); return false; }
        }

        m_displayFramebuffers.resize(swapCount);
        for (size_t i = 0; i < swapCount; ++i) {
            VkImageView dAtts[1] = {m_swapChainImageViews[i]};
            VkFramebufferCreateInfo fb{};
            fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass = m_displayRenderPass;
            fb.attachmentCount = 1;
            fb.pAttachments = dAtts;
            fb.width = m_swapChainExtent.width;
            fb.height = m_swapChainExtent.height;
            fb.layers = 1;
            if (vkCreateFramebuffer(m_device, &fb, nullptr, &m_displayFramebuffers[i]) != VK_SUCCESS) { destroyDeferredResources(); return false; }
        }
    }

    // ---- Lighting descriptors: FrameData UBO, shadow cascade array, and
    // the three GBuffer targets. ----
    {
        VkDescriptorSetLayoutBinding bindings[5]{};
        bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        for (int i = 1; i < 5; ++i)
            bindings[i] = {uint32_t(i), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};

        VkDescriptorSetLayoutCreateInfo dslCi{};
        dslCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslCi.bindingCount = 5;
        dslCi.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(m_device, &dslCi, nullptr, &m_lightingSetLayout) != VK_SUCCESS) { destroyDeferredResources(); return false; }

        VkDescriptorPoolSize poolSizes[2]{};
        poolSizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
        poolSizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4};

        VkDescriptorPoolCreateInfo poolCi{};
        poolCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolCi.maxSets = 1;
        poolCi.poolSizeCount = 2;
        poolCi.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(m_device, &poolCi, nullptr, &m_lightingPool) != VK_SUCCESS) { destroyDeferredResources(); return false; }

        VkDescriptorSetAllocateInfo dsAi{};
        dsAi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAi.descriptorPool = m_lightingPool;
        dsAi.descriptorSetCount = 1;
        dsAi.pSetLayouts = &m_lightingSetLayout;
        if (vkAllocateDescriptorSets(m_device, &dsAi, &m_lightingSet) != VK_SUCCESS) { destroyDeferredResources(); return false; }

        VkSamplerCreateInfo sCi{};
        sCi.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sCi.magFilter = VK_FILTER_NEAREST;
        sCi.minFilter = VK_FILTER_NEAREST;
        sCi.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sCi.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sCi.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sCi.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sCi.maxLod = 1.0f;
        if (vkCreateSampler(m_device, &sCi, nullptr, &m_gbufferSampler) != VK_SUCCESS) { destroyDeferredResources(); return false; }

        writeLightingDescriptorSet(m_dummyShadowView, m_dummyShadowSampler);
    }

    // ---- Resolve descriptors: FrameData, GBuffer world positions, the HDR
    // buffer and whichever history slot this frame reads. ----
    {
        VkDescriptorSetLayoutBinding bindings[4]{};
        bindings[0] = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        for (int i = 1; i < 4; ++i)
            bindings[i] = {uint32_t(i), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};

        VkDescriptorSetLayoutCreateInfo dslCi{};
        dslCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslCi.bindingCount = 4;
        dslCi.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(m_device, &dslCi, nullptr, &m_resolveSetLayout) != VK_SUCCESS) { destroyDeferredResources(); return false; }

        VkDescriptorPoolSize poolSizes[2]{};
        poolSizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
        poolSizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3};

        VkDescriptorPoolCreateInfo poolCi{};
        poolCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolCi.maxSets = 1;
        poolCi.poolSizeCount = 2;
        poolCi.pPoolSizes = poolSizes;
        if (vkCreateDescriptorPool(m_device, &poolCi, nullptr, &m_resolvePool) != VK_SUCCESS) { destroyDeferredResources(); return false; }

        VkDescriptorSetAllocateInfo dsAi{};
        dsAi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAi.descriptorPool = m_resolvePool;
        dsAi.descriptorSetCount = 1;
        dsAi.pSetLayouts = &m_resolveSetLayout;
        if (vkAllocateDescriptorSets(m_device, &dsAi, &m_resolveSet) != VK_SUCCESS) { destroyDeferredResources(); return false; }

        // LINEAR, unlike the GBuffer sampler: reprojection lands on
        // continuous UVs between texels, and bilinear is what turns that
        // sub-pixel offset into a smooth sample instead of a stair-step.
        VkSamplerCreateInfo sCi{};
        sCi.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sCi.magFilter = VK_FILTER_LINEAR;
        sCi.minFilter = VK_FILTER_LINEAR;
        sCi.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sCi.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sCi.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sCi.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sCi.maxLod = 1.0f;
        if (vkCreateSampler(m_device, &sCi, nullptr, &m_hdrSampler) != VK_SUCCESS) { destroyDeferredResources(); return false; }
    }

    // ---- Bloom descriptors: one shared layout (a single combined sampler)
    // behind three sets — the bright-pass input, which is re-pointed to the
    // history slot *this* frame's resolve writes, and the two static blur
    // taps (A, then B). ----
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo dslCi{};
        dslCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslCi.bindingCount = 1;
        dslCi.pBindings = &binding;
        if (vkCreateDescriptorSetLayout(m_device, &dslCi, nullptr, &m_bloomInputSetLayout) != VK_SUCCESS) { destroyDeferredResources(); return false; }

        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3};
        VkDescriptorPoolCreateInfo poolCi{};
        poolCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolCi.maxSets = 3;
        poolCi.poolSizeCount = 1;
        poolCi.pPoolSizes = &poolSize;
        if (vkCreateDescriptorPool(m_device, &poolCi, nullptr, &m_bloomPool) != VK_SUCCESS) { destroyDeferredResources(); return false; }

        VkDescriptorSetLayout setLayouts[3] = {m_bloomInputSetLayout, m_bloomInputSetLayout, m_bloomInputSetLayout};
        VkDescriptorSetAllocateInfo dsAi{};
        dsAi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAi.descriptorPool = m_bloomPool;
        dsAi.descriptorSetCount = 3;
        dsAi.pSetLayouts = setLayouts;
        VkDescriptorSet sets[3]{};
        if (vkAllocateDescriptorSets(m_device, &dsAi, sets) != VK_SUCCESS) { destroyDeferredResources(); return false; }
        m_bloomExtractSet = sets[0];
        m_bloomBlurSets[0] = sets[1];   // horizontal blur: reads slot A
        m_bloomBlurSets[1] = sets[2];   // vertical blur:   reads slot B

        VkDescriptorImageInfo infos[2]{};
        for (int i = 0; i < 2; ++i) {
            infos[i].sampler = m_hdrSampler;
            infos[i].imageView = m_bloomViews[i];
            infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
        VkWriteDescriptorSet writes[2]{};
        for (int i = 0; i < 2; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = m_bloomBlurSets[i];
            writes[i].dstBinding = 0;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            writes[i].pImageInfo = &infos[i];
        }
        vkUpdateDescriptorSets(m_device, 2, writes, 0, nullptr);
    }

    // ---- Display descriptors: the resolved HDR frame + the finished bloom.
    // The frame half ping-pongs between history slots, so the set is written
    // every frame by writeDisplayDescriptorSets(). ----
    {
        VkDescriptorSetLayoutBinding bindings[2]{};
        for (int i = 0; i < 2; ++i)
            bindings[i] = {uint32_t(i), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};

        VkDescriptorSetLayoutCreateInfo dslCi{};
        dslCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslCi.bindingCount = 2;
        dslCi.pBindings = bindings;
        if (vkCreateDescriptorSetLayout(m_device, &dslCi, nullptr, &m_displaySetLayout) != VK_SUCCESS) { destroyDeferredResources(); return false; }

        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
        VkDescriptorPoolCreateInfo poolCi{};
        poolCi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolCi.maxSets = 1;
        poolCi.poolSizeCount = 1;
        poolCi.pPoolSizes = &poolSize;
        if (vkCreateDescriptorPool(m_device, &poolCi, nullptr, &m_displayPool) != VK_SUCCESS) { destroyDeferredResources(); return false; }

        VkDescriptorSetLayout setLayouts[1] = {m_displaySetLayout};
        VkDescriptorSetAllocateInfo dsAi{};
        dsAi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsAi.descriptorPool = m_displayPool;
        dsAi.descriptorSetCount = 1;
        dsAi.pSetLayouts = setLayouts;
        if (vkAllocateDescriptorSets(m_device, &dsAi, &m_displaySet) != VK_SUCCESS) { destroyDeferredResources(); return false; }
    }

    // ---- GBuffer pipeline: forward's vertex input and pipeline layout
    // (set0 + push constants are identical), three colour attachments. ----
    {
        VkVertexInputBindingDescription binding{0, sizeof(NativeVertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attrs[4] = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(NativeVertex, px)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(NativeVertex, nx)},
            {2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(NativeVertex, u)},
            {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(NativeVertex, r)},
        };
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = 4;
        vertexInput.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAtts[3]{};
        for (int i = 0; i < 3; ++i)
            blendAtts[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 3;
        colorBlend.pAttachments = blendAtts;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynState{};
        dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynState.dynamicStateCount = 2;
        dynState.pDynamicStates = dynStates;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = m_gbufferVertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = m_gbufferFragModule;
        stages[1].pName = "main";

        VkGraphicsPipelineCreateInfo pipeCi{};
        pipeCi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeCi.stageCount = 2;
        pipeCi.pStages = stages;
        pipeCi.pVertexInputState = &vertexInput;
        pipeCi.pInputAssemblyState = &inputAssembly;
        pipeCi.pViewportState = &viewportState;
        pipeCi.pRasterizationState = &rasterizer;
        pipeCi.pMultisampleState = &multisampling;
        pipeCi.pColorBlendState = &colorBlend;
        pipeCi.pDepthStencilState = &depthStencil;
        pipeCi.pDynamicState = &dynState;
        pipeCi.layout = m_pipelineLayout;
        pipeCi.renderPass = m_gbufferRenderPass;
        pipeCi.subpass = 0;
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeCi, nullptr, &m_gbufferPipeline) != VK_SUCCESS) {
            destroyDeferredResources();
            return false;
        }
    }

    // ---- Lighting pipeline: fullscreen triangle, no vertex input, no
    // depth, single colour attachment. ----
    {
        VkDescriptorSetLayout setLayouts[1] = {m_lightingSetLayout};
        VkPipelineLayoutCreateInfo layoutCi{};
        layoutCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutCi.setLayoutCount = 1;
        layoutCi.pSetLayouts = setLayouts;
        if (vkCreatePipelineLayout(m_device, &layoutCi, nullptr, &m_lightingLayout) != VK_SUCCESS) { destroyDeferredResources(); return false; }

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAtt{};
        blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAtt;

        VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynState{};
        dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynState.dynamicStateCount = 2;
        dynState.pDynamicStates = dynStates;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = m_lightingVertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = m_lightingFragModule;
        stages[1].pName = "main";

        VkGraphicsPipelineCreateInfo pipeCi{};
        pipeCi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeCi.stageCount = 2;
        pipeCi.pStages = stages;
        pipeCi.pVertexInputState = &vertexInput;
        pipeCi.pInputAssemblyState = &inputAssembly;
        pipeCi.pViewportState = &viewportState;
        pipeCi.pRasterizationState = &rasterizer;
        pipeCi.pMultisampleState = &multisampling;
        pipeCi.pColorBlendState = &colorBlend;
        pipeCi.pDepthStencilState = nullptr;
        pipeCi.pDynamicState = &dynState;
        pipeCi.layout = m_lightingLayout;
        pipeCi.renderPass = m_lightingRenderPass;
        pipeCi.subpass = 0;
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeCi, nullptr, &m_lightingPipeline) != VK_SUCCESS) {
            destroyDeferredResources();
            return false;
        }
    }

    // ---- Resolve pipeline: same fullscreen vertex stage as the lighting
    // pass, one colour attachment (the history slot — the swapchain moved to
    // the display pass). ----
    {
        VkDescriptorSetLayout setLayouts[1] = {m_resolveSetLayout};
        VkPipelineLayoutCreateInfo layoutCi{};
        layoutCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutCi.setLayoutCount = 1;
        layoutCi.pSetLayouts = setLayouts;
        if (vkCreatePipelineLayout(m_device, &layoutCi, nullptr, &m_resolveLayout) != VK_SUCCESS) { destroyDeferredResources(); return false; }

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAtts[1]{};
        blendAtts[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = blendAtts;

        VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynState{};
        dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynState.dynamicStateCount = 2;
        dynState.pDynamicStates = dynStates;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = m_lightingVertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = m_resolveFragModule;
        stages[1].pName = "main";

        VkGraphicsPipelineCreateInfo pipeCi{};
        pipeCi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeCi.stageCount = 2;
        pipeCi.pStages = stages;
        pipeCi.pVertexInputState = &vertexInput;
        pipeCi.pInputAssemblyState = &inputAssembly;
        pipeCi.pViewportState = &viewportState;
        pipeCi.pRasterizationState = &rasterizer;
        pipeCi.pMultisampleState = &multisampling;
        pipeCi.pColorBlendState = &colorBlend;
        pipeCi.pDepthStencilState = nullptr;
        pipeCi.pDynamicState = &dynState;
        pipeCi.layout = m_resolveLayout;
        pipeCi.renderPass = m_resolveRenderPass;
        pipeCi.subpass = 0;
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeCi, nullptr, &m_resolvePipeline) != VK_SUCCESS) {
            destroyDeferredResources();
            return false;
        }
    }

    // ---- Bloom pipelines: bright-pass extract and separable blur share one
    // layout (the single-sampler set + an identical 16-byte fragment push
    // range) and identical fullscreen state — only the fragment module
    // differs, so they are created back to back from the same pipeCi. ----
    {
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset = 0;
        pcRange.size = sizeof(GlarePC);

        VkDescriptorSetLayout setLayouts[1] = {m_bloomInputSetLayout};
        VkPipelineLayoutCreateInfo layoutCi{};
        layoutCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutCi.setLayoutCount = 1;
        layoutCi.pSetLayouts = setLayouts;
        layoutCi.pushConstantRangeCount = 1;
        layoutCi.pPushConstantRanges = &pcRange;
        if (vkCreatePipelineLayout(m_device, &layoutCi, nullptr, &m_bloomLayout) != VK_SUCCESS) { destroyDeferredResources(); return false; }

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAtt{};
        blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAtt;

        VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynState{};
        dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynState.dynamicStateCount = 2;
        dynState.pDynamicStates = dynStates;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = m_lightingVertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].pName = "main";

        VkGraphicsPipelineCreateInfo pipeCi{};
        pipeCi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeCi.stageCount = 2;
        pipeCi.pStages = stages;
        pipeCi.pVertexInputState = &vertexInput;
        pipeCi.pInputAssemblyState = &inputAssembly;
        pipeCi.pViewportState = &viewportState;
        pipeCi.pRasterizationState = &rasterizer;
        pipeCi.pMultisampleState = &multisampling;
        pipeCi.pColorBlendState = &colorBlend;
        pipeCi.pDepthStencilState = nullptr;
        pipeCi.pDynamicState = &dynState;
        pipeCi.layout = m_bloomLayout;
        pipeCi.renderPass = m_bloomRenderPass;
        pipeCi.subpass = 0;

        stages[1].module = m_glareFragModule;
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeCi, nullptr, &m_bloomExtractPipeline) != VK_SUCCESS) {
            destroyDeferredResources();
            return false;
        }
        stages[1].module = m_blurFragModule;
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeCi, nullptr, &m_bloomBlurPipeline) != VK_SUCCESS) {
            destroyDeferredResources();
            return false;
        }
    }

    // ---- Display pipeline: tonemap.frag — the only pipeline that writes the
    // backbuffer on this path, with the SDR/HDR10 encoding in its push
    // constants. ----
    {
        VkPushConstantRange pcRange{};
        pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        pcRange.offset = 0;
        pcRange.size = sizeof(TonemapPC);

        VkDescriptorSetLayout setLayouts[1] = {m_displaySetLayout};
        VkPipelineLayoutCreateInfo layoutCi{};
        layoutCi.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutCi.setLayoutCount = 1;
        layoutCi.pSetLayouts = setLayouts;
        layoutCi.pushConstantRangeCount = 1;
        layoutCi.pPushConstantRanges = &pcRange;
        if (vkCreatePipelineLayout(m_device, &layoutCi, nullptr, &m_displayLayout) != VK_SUCCESS) { destroyDeferredResources(); return false; }

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE;
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineColorBlendAttachmentState blendAtt{};
        blendAtt.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo colorBlend{};
        colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlend.attachmentCount = 1;
        colorBlend.pAttachments = &blendAtt;

        VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynState{};
        dynState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynState.dynamicStateCount = 2;
        dynState.pDynamicStates = dynStates;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = m_lightingVertModule;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = m_tonemapFragModule;
        stages[1].pName = "main";

        VkGraphicsPipelineCreateInfo pipeCi{};
        pipeCi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeCi.stageCount = 2;
        pipeCi.pStages = stages;
        pipeCi.pVertexInputState = &vertexInput;
        pipeCi.pInputAssemblyState = &inputAssembly;
        pipeCi.pViewportState = &viewportState;
        pipeCi.pRasterizationState = &rasterizer;
        pipeCi.pMultisampleState = &multisampling;
        pipeCi.pColorBlendState = &colorBlend;
        pipeCi.pDepthStencilState = nullptr;
        pipeCi.pDynamicState = &dynState;
        pipeCi.layout = m_displayLayout;
        pipeCi.renderPass = m_displayRenderPass;
        pipeCi.subpass = 0;
        if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipeCi, nullptr, &m_displayPipeline) != VK_SUCCESS) {
            destroyDeferredResources();
            return false;
        }
    }

    m_deferredReady = true;
    return true;
}

void NativeRenderer::destroyDeferredResources() {
    if (!m_device) return;

    for (VkFramebuffer fb : m_gbufferFramebuffers) if (fb) vkDestroyFramebuffer(m_device, fb, nullptr);
    m_gbufferFramebuffers.clear();
    if (m_lightingFramebuffer) { vkDestroyFramebuffer(m_device, m_lightingFramebuffer, nullptr); m_lightingFramebuffer = VK_NULL_HANDLE; }
    for (VkFramebuffer fb : m_resolveFramebuffers) if (fb) vkDestroyFramebuffer(m_device, fb, nullptr);
    m_resolveFramebuffers.clear();
    for (int i = 0; i < 2; ++i)
        if (m_bloomFramebuffers[i]) { vkDestroyFramebuffer(m_device, m_bloomFramebuffers[i], nullptr); m_bloomFramebuffers[i] = VK_NULL_HANDLE; }
    for (VkFramebuffer fb : m_displayFramebuffers) if (fb) vkDestroyFramebuffer(m_device, fb, nullptr);
    m_displayFramebuffers.clear();

    if (m_gbufferPipeline) { vkDestroyPipeline(m_device, m_gbufferPipeline, nullptr); m_gbufferPipeline = VK_NULL_HANDLE; }
    // The particle sprite's GBuffer pipeline was made from m_gbufferRenderPass
    // (destroyed at the bottom of this function), so it goes with it and is
    // rebuilt lazily on the next frame that has particles to draw.
    destroyParticleGbufPipeline();
    if (m_lightingPipeline) { vkDestroyPipeline(m_device, m_lightingPipeline, nullptr); m_lightingPipeline = VK_NULL_HANDLE; }
    if (m_resolvePipeline) { vkDestroyPipeline(m_device, m_resolvePipeline, nullptr); m_resolvePipeline = VK_NULL_HANDLE; }
    if (m_bloomExtractPipeline) { vkDestroyPipeline(m_device, m_bloomExtractPipeline, nullptr); m_bloomExtractPipeline = VK_NULL_HANDLE; }
    if (m_bloomBlurPipeline) { vkDestroyPipeline(m_device, m_bloomBlurPipeline, nullptr); m_bloomBlurPipeline = VK_NULL_HANDLE; }
    if (m_displayPipeline) { vkDestroyPipeline(m_device, m_displayPipeline, nullptr); m_displayPipeline = VK_NULL_HANDLE; }
    if (m_lightingLayout) { vkDestroyPipelineLayout(m_device, m_lightingLayout, nullptr); m_lightingLayout = VK_NULL_HANDLE; }
    if (m_resolveLayout) { vkDestroyPipelineLayout(m_device, m_resolveLayout, nullptr); m_resolveLayout = VK_NULL_HANDLE; }
    if (m_bloomLayout) { vkDestroyPipelineLayout(m_device, m_bloomLayout, nullptr); m_bloomLayout = VK_NULL_HANDLE; }
    if (m_displayLayout) { vkDestroyPipelineLayout(m_device, m_displayLayout, nullptr); m_displayLayout = VK_NULL_HANDLE; }

    // Shader modules can go as soon as the pipelines built from them do.
    if (m_gbufferVertModule) { vkDestroyShaderModule(m_device, m_gbufferVertModule, nullptr); m_gbufferVertModule = VK_NULL_HANDLE; }
    if (m_gbufferFragModule) { vkDestroyShaderModule(m_device, m_gbufferFragModule, nullptr); m_gbufferFragModule = VK_NULL_HANDLE; }
    if (m_lightingVertModule) { vkDestroyShaderModule(m_device, m_lightingVertModule, nullptr); m_lightingVertModule = VK_NULL_HANDLE; }
    if (m_lightingFragModule) { vkDestroyShaderModule(m_device, m_lightingFragModule, nullptr); m_lightingFragModule = VK_NULL_HANDLE; }
    if (m_resolveFragModule) { vkDestroyShaderModule(m_device, m_resolveFragModule, nullptr); m_resolveFragModule = VK_NULL_HANDLE; }
    if (m_glareFragModule) { vkDestroyShaderModule(m_device, m_glareFragModule, nullptr); m_glareFragModule = VK_NULL_HANDLE; }
    if (m_blurFragModule) { vkDestroyShaderModule(m_device, m_blurFragModule, nullptr); m_blurFragModule = VK_NULL_HANDLE; }
    if (m_tonemapFragModule) { vkDestroyShaderModule(m_device, m_tonemapFragModule, nullptr); m_tonemapFragModule = VK_NULL_HANDLE; }

    // Destroying a pool frees every set allocated from it.
    if (m_lightingPool) { vkDestroyDescriptorPool(m_device, m_lightingPool, nullptr); m_lightingPool = VK_NULL_HANDLE; }
    m_lightingSet = VK_NULL_HANDLE;
    if (m_lightingSetLayout) { vkDestroyDescriptorSetLayout(m_device, m_lightingSetLayout, nullptr); m_lightingSetLayout = VK_NULL_HANDLE; }
    if (m_resolvePool) { vkDestroyDescriptorPool(m_device, m_resolvePool, nullptr); m_resolvePool = VK_NULL_HANDLE; }
    m_resolveSet = VK_NULL_HANDLE;
    if (m_resolveSetLayout) { vkDestroyDescriptorSetLayout(m_device, m_resolveSetLayout, nullptr); m_resolveSetLayout = VK_NULL_HANDLE; }
    if (m_bloomPool) { vkDestroyDescriptorPool(m_device, m_bloomPool, nullptr); m_bloomPool = VK_NULL_HANDLE; }
    m_bloomExtractSet = VK_NULL_HANDLE;
    m_bloomBlurSets[0] = m_bloomBlurSets[1] = VK_NULL_HANDLE;
    if (m_bloomInputSetLayout) { vkDestroyDescriptorSetLayout(m_device, m_bloomInputSetLayout, nullptr); m_bloomInputSetLayout = VK_NULL_HANDLE; }
    if (m_displayPool) { vkDestroyDescriptorPool(m_device, m_displayPool, nullptr); m_displayPool = VK_NULL_HANDLE; }
    m_displaySet = VK_NULL_HANDLE;
    if (m_displaySetLayout) { vkDestroyDescriptorSetLayout(m_device, m_displaySetLayout, nullptr); m_displaySetLayout = VK_NULL_HANDLE; }
    if (m_gbufferSampler) { vkDestroySampler(m_device, m_gbufferSampler, nullptr); m_gbufferSampler = VK_NULL_HANDLE; }
    if (m_hdrSampler) { vkDestroySampler(m_device, m_hdrSampler, nullptr); m_hdrSampler = VK_NULL_HANDLE; }

    if (m_gbufferRenderPass) { vkDestroyRenderPass(m_device, m_gbufferRenderPass, nullptr); m_gbufferRenderPass = VK_NULL_HANDLE; }
    if (m_lightingRenderPass) { vkDestroyRenderPass(m_device, m_lightingRenderPass, nullptr); m_lightingRenderPass = VK_NULL_HANDLE; }
    if (m_resolveRenderPass) { vkDestroyRenderPass(m_device, m_resolveRenderPass, nullptr); m_resolveRenderPass = VK_NULL_HANDLE; }
    if (m_bloomRenderPass) { vkDestroyRenderPass(m_device, m_bloomRenderPass, nullptr); m_bloomRenderPass = VK_NULL_HANDLE; }
    if (m_displayRenderPass) { vkDestroyRenderPass(m_device, m_displayRenderPass, nullptr); m_displayRenderPass = VK_NULL_HANDLE; }

    for (int i = 0; i < kGBufferCount; ++i) {
        if (m_gbufferViews[i]) { vkDestroyImageView(m_device, m_gbufferViews[i], nullptr); m_gbufferViews[i] = VK_NULL_HANDLE; }
        if (m_gbufferImages[i]) { vkDestroyImage(m_device, m_gbufferImages[i], nullptr); m_gbufferImages[i] = VK_NULL_HANDLE; }
        if (m_gbufferMemory[i]) { vkFreeMemory(m_device, m_gbufferMemory[i], nullptr); m_gbufferMemory[i] = VK_NULL_HANDLE; }
    }
    if (m_gbufferDepthView) { vkDestroyImageView(m_device, m_gbufferDepthView, nullptr); m_gbufferDepthView = VK_NULL_HANDLE; }
    if (m_gbufferDepthImage) { vkDestroyImage(m_device, m_gbufferDepthImage, nullptr); m_gbufferDepthImage = VK_NULL_HANDLE; }
    if (m_gbufferDepthMemory) { vkFreeMemory(m_device, m_gbufferDepthMemory, nullptr); m_gbufferDepthMemory = VK_NULL_HANDLE; }

    if (m_hdrView) { vkDestroyImageView(m_device, m_hdrView, nullptr); m_hdrView = VK_NULL_HANDLE; }
    if (m_hdrImage) { vkDestroyImage(m_device, m_hdrImage, nullptr); m_hdrImage = VK_NULL_HANDLE; }
    if (m_hdrMemory) { vkFreeMemory(m_device, m_hdrMemory, nullptr); m_hdrMemory = VK_NULL_HANDLE; }
    for (int i = 0; i < kHistoryCount; ++i) {
        if (m_historyViews[i]) { vkDestroyImageView(m_device, m_historyViews[i], nullptr); m_historyViews[i] = VK_NULL_HANDLE; }
        if (m_historyImages[i]) { vkDestroyImage(m_device, m_historyImages[i], nullptr); m_historyImages[i] = VK_NULL_HANDLE; }
        if (m_historyMemory[i]) { vkFreeMemory(m_device, m_historyMemory[i], nullptr); m_historyMemory[i] = VK_NULL_HANDLE; }
    }
    for (int i = 0; i < 2; ++i) {
        if (m_bloomViews[i]) { vkDestroyImageView(m_device, m_bloomViews[i], nullptr); m_bloomViews[i] = VK_NULL_HANDLE; }
        if (m_bloomImages[i]) { vkDestroyImage(m_device, m_bloomImages[i], nullptr); m_bloomImages[i] = VK_NULL_HANDLE; }
        if (m_bloomMemory[i]) { vkFreeMemory(m_device, m_bloomMemory[i], nullptr); m_bloomMemory[i] = VK_NULL_HANDLE; }
    }

    // Fresh history is undefined content — force the first frame after any
    // rebuild to take the current frame only.
    m_historyValid = false;
    m_historyIndex = 0;
    m_deferredReady = false;
}

void NativeRenderer::writeLightingDescriptorSet(VkImageView shadowView, VkSampler shadowSampler) {
    if (!m_lightingSet) return;

    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = m_frameUBO;
    bufInfo.range = sizeof(FrameDataUBO);

    VkDescriptorImageInfo shadowInfo{};
    shadowInfo.sampler = shadowSampler;
    shadowInfo.imageView = shadowView;
    // Same reason as writeFrameDescriptorSet(): the dummy is a linear,
    // never-transitioned image, so GENERAL is the layout that actually
    // matches it rather than SHADER_READ_ONLY_OPTIMAL.
    shadowInfo.imageLayout = (shadowView == m_dummyShadowView) ? VK_IMAGE_LAYOUT_GENERAL
                                                               : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo gInfo[kGBufferCount];
    for (int i = 0; i < kGBufferCount; ++i) {
        gInfo[i].sampler = m_gbufferSampler;
        gInfo[i].imageView = m_gbufferViews[i];
        gInfo[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkWriteDescriptorSet writes[5]{};
    for (int i = 0; i < 5; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = m_lightingSet;
        writes[i].dstBinding = uint32_t(i);
        writes[i].descriptorCount = 1;
    }
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &bufInfo;
    for (int i = 1; i < 5; ++i) {
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = (i == 1) ? &shadowInfo : &gInfo[i - 2];
    }

    vkUpdateDescriptorSets(m_device, 5, writes, 0, nullptr);
}

void NativeRenderer::writeResolveDescriptorSet() {
    if (!m_resolveSet) return;

    VkDescriptorBufferInfo bufInfo{};
    bufInfo.buffer = m_frameUBO;
    bufInfo.range = sizeof(FrameDataUBO);

    VkDescriptorImageInfo infos[3]{};
    infos[0].sampler = m_gbufferSampler;
    infos[0].imageView = m_gbufferViews[2];
    infos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    infos[1].sampler = m_hdrSampler;
    infos[1].imageView = m_hdrView;
    infos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    infos[2].sampler = m_hdrSampler;
    infos[2].imageView = m_historyViews[m_historyIndex];   // read slot; the
                                                           // write slot is
                                                           // 1 - m_historyIndex
    infos[2].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writes[4]{};
    for (int i = 0; i < 4; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = m_resolveSet;
        writes[i].dstBinding = uint32_t(i);
        writes[i].descriptorCount = 1;
    }
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[0].pBufferInfo = &bufInfo;
    for (int i = 1; i < 4; ++i) {
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &infos[i - 1];
    }

    vkUpdateDescriptorSets(m_device, 4, writes, 0, nullptr);
}

void NativeRenderer::writeDisplayDescriptorSets(VkImageView resolvedView) {
    if (!m_displaySet || !m_bloomExtractSet) return;

    // Both readers take the history slot the resolve pass wrote this frame
    // (post-TAA, scene-linear) — bloom from it, and the display pass from it
    // again — plus the finished bloom, which never changes identity.
    VkDescriptorImageInfo srcInfo{};
    srcInfo.sampler = m_hdrSampler;
    srcInfo.imageView = resolvedView;
    srcInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo bloomInfo{};
    bloomInfo.sampler = m_hdrSampler;
    bloomInfo.imageView = m_bloomViews[0];
    bloomInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet writes[3]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_bloomExtractSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &srcInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_displaySet;
    writes[1].dstBinding = 0;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &srcInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = m_displaySet;
    writes[2].dstBinding = 1;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].pImageInfo = &bloomInfo;

    vkUpdateDescriptorSets(m_device, 3, writes, 0, nullptr);
}

} // namespace ks::sim
