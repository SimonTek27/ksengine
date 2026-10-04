#pragma once

#include <QtCore/QLibrary>
#include <QtCore/QDebug>

#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

namespace ks {

struct VulkanFunctionTable {
    PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
    PFN_vkCreateInstance createInstance = nullptr;
    PFN_vkDestroyInstance destroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices enumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties getPhysicalDeviceProperties = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties getPhysicalDeviceMemoryProperties = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties getPhysicalDeviceQueueFamilyProperties = nullptr;
    PFN_vkCreateDevice createDevice = nullptr;
    PFN_vkDestroyDevice destroyDevice = nullptr;
    PFN_vkGetDeviceQueue getDeviceQueue = nullptr;
    PFN_vkCreateCommandPool createCommandPool = nullptr;
    PFN_vkDestroyCommandPool destroyCommandPool = nullptr;
    PFN_vkCreateShaderModule createShaderModule = nullptr;
    PFN_vkDestroyShaderModule destroyShaderModule = nullptr;
    PFN_vkCreateBuffer createBuffer = nullptr;
    PFN_vkDestroyBuffer destroyBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements getBufferMemoryRequirements = nullptr;
    PFN_vkAllocateMemory allocateMemory = nullptr;
    PFN_vkFreeMemory freeMemory = nullptr;
    PFN_vkBindBufferMemory bindBufferMemory = nullptr;
    PFN_vkMapMemory mapMemory = nullptr;
    PFN_vkUnmapMemory unmapMemory = nullptr;
    PFN_vkCreateDescriptorSetLayout createDescriptorSetLayout = nullptr;
    PFN_vkDestroyDescriptorSetLayout destroyDescriptorSetLayout = nullptr;
    PFN_vkCreateDescriptorPool createDescriptorPool = nullptr;
    PFN_vkDestroyDescriptorPool destroyDescriptorPool = nullptr;
    PFN_vkAllocateDescriptorSets allocateDescriptorSets = nullptr;
    PFN_vkFreeDescriptorSets freeDescriptorSets = nullptr;
    PFN_vkUpdateDescriptorSets updateDescriptorSets = nullptr;
    PFN_vkCreatePipelineLayout createPipelineLayout = nullptr;
    PFN_vkDestroyPipelineLayout destroyPipelineLayout = nullptr;
    PFN_vkCreateGraphicsPipelines createGraphicsPipelines = nullptr;
    PFN_vkDestroyPipeline destroyPipeline = nullptr;
    PFN_vkCreateRenderPass createRenderPass = nullptr;
    PFN_vkDestroyRenderPass destroyRenderPass = nullptr;
    PFN_vkCreateFramebuffer createFramebuffer = nullptr;
    PFN_vkDestroyFramebuffer destroyFramebuffer = nullptr;
    PFN_vkCreateImageView createImageView = nullptr;
    PFN_vkDestroyImageView destroyImageView = nullptr;
    PFN_vkCreateImage createImage = nullptr;
    PFN_vkDestroyImage destroyImage = nullptr;
    PFN_vkGetImageMemoryRequirements getImageMemoryRequirements = nullptr;
    PFN_vkBindImageMemory bindImageMemory = nullptr;
    PFN_vkCreateSampler createSampler = nullptr;
    PFN_vkDestroySampler destroySampler = nullptr;
    PFN_vkCmdBeginRenderPass cmdBeginRenderPass = nullptr;
    PFN_vkCmdEndRenderPass cmdEndRenderPass = nullptr;
    PFN_vkCmdBindPipeline cmdBindPipeline = nullptr;
    PFN_vkCmdBindDescriptorSets cmdBindDescriptorSets = nullptr;
    PFN_vkCmdDispatch cmdDispatch = nullptr;
    PFN_vkCmdDraw cmdDraw = nullptr;
    PFN_vkCmdDrawIndexed cmdDrawIndexed = nullptr;
    PFN_vkAllocateCommandBuffers allocateCommandBuffers = nullptr;
    PFN_vkFreeCommandBuffers freeCommandBuffers = nullptr;
    PFN_vkBeginCommandBuffer beginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer endCommandBuffer = nullptr;
    PFN_vkQueueSubmit queueSubmit = nullptr;
    PFN_vkQueueWaitIdle queueWaitIdle = nullptr;
    PFN_vkCreateFence createFence = nullptr;
    PFN_vkDestroyFence destroyFence = nullptr;
    PFN_vkWaitForFences waitForFences = nullptr;
    PFN_vkResetFences resetFences = nullptr;
    PFN_vkCmdSetViewport cmdSetViewport = nullptr;
    PFN_vkCmdSetScissor cmdSetScissor = nullptr;
    PFN_vkCmdBindVertexBuffers cmdBindVertexBuffers = nullptr;
    PFN_vkCmdBindIndexBuffer cmdBindIndexBuffer = nullptr;
    PFN_vkResetCommandBuffer resetCommandBuffer = nullptr;
    PFN_vkCmdPipelineBarrier cmdPipelineBarrier = nullptr;
    PFN_vkCmdCopyBuffer cmdCopyBuffer = nullptr;
    PFN_vkCmdCopyBufferToImage cmdCopyBufferToImage = nullptr;
    PFN_vkCmdCopyImageToBuffer cmdCopyImageToBuffer = nullptr;
    PFN_vkCmdBlitImage cmdBlitImage = nullptr;

    // Swap chain / surface / presentation
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR getPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR getPhysicalDeviceSurfaceFormatsKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR getPhysicalDeviceSurfacePresentModesKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR getPhysicalDeviceSurfaceSupportKHR = nullptr;
    PFN_vkCreateSwapchainKHR createSwapchainKHR = nullptr;
    PFN_vkDestroySwapchainKHR destroySwapchainKHR = nullptr;
    PFN_vkGetSwapchainImagesKHR getSwapchainImagesKHR = nullptr;
    PFN_vkAcquireNextImageKHR acquireNextImageKHR = nullptr;
    PFN_vkQueuePresentKHR queuePresentKHR = nullptr;
    PFN_vkCreateSemaphore createSemaphore = nullptr;
    PFN_vkDestroySemaphore destroySemaphore = nullptr;
    PFN_vkDeviceWaitIdle deviceWaitIdle = nullptr;

    bool loaded = false;
    bool attempted = false;
};

extern VulkanFunctionTable g_vk;

template <typename Fn>
static void resolveGlobal(VulkanFunctionTable& table, Fn& fn, const char* name) {
    fn = reinterpret_cast<Fn>(table.getInstanceProcAddr(nullptr, name));
}

template <typename Fn>
static void resolveInstance(VulkanFunctionTable& table, VkInstance inst, Fn& fn, const char* name) {
    fn = reinterpret_cast<Fn>(table.getInstanceProcAddr(inst, name));
}

inline bool loadVulkanLoader() {
    if (g_vk.attempted) return g_vk.loaded;
    g_vk.attempted = true;

    QLibrary lib(QStringLiteral("vulkan-1"));
    if (!lib.load()) {
        qDebug() << "Vulkan: vulkan-1 loader not available";
        return false;
    }

    g_vk.getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        lib.resolve("vkGetInstanceProcAddr"));
    if (!g_vk.getInstanceProcAddr) {
        qDebug() << "Vulkan: vkGetInstanceProcAddr not found";
        return false;
    }

    resolveGlobal(g_vk, g_vk.createInstance, "vkCreateInstance");
    if (!g_vk.createInstance) {
        qDebug() << "Vulkan: vkCreateInstance not found";
        return false;
    }

    g_vk.loaded = true;
    return true;
}

inline bool loadInstanceFunctions(VkInstance inst) {
    if (!g_vk.loaded || !inst) return false;
    resolveInstance(g_vk, inst, g_vk.destroyInstance, "vkDestroyInstance");
    resolveInstance(g_vk, inst, g_vk.enumeratePhysicalDevices, "vkEnumeratePhysicalDevices");
    resolveInstance(g_vk, inst, g_vk.getPhysicalDeviceProperties, "vkGetPhysicalDeviceProperties");
    resolveInstance(g_vk, inst, g_vk.getPhysicalDeviceMemoryProperties, "vkGetPhysicalDeviceMemoryProperties");
    resolveInstance(g_vk, inst, g_vk.getPhysicalDeviceQueueFamilyProperties, "vkGetPhysicalDeviceQueueFamilyProperties");
    resolveInstance(g_vk, inst, g_vk.createDevice, "vkCreateDevice");
    resolveInstance(g_vk, inst, g_vk.destroyDevice, "vkDestroyDevice");
    resolveInstance(g_vk, inst, g_vk.getDeviceQueue, "vkGetDeviceQueue");
    resolveInstance(g_vk, inst, g_vk.createCommandPool, "vkCreateCommandPool");
    resolveInstance(g_vk, inst, g_vk.destroyCommandPool, "vkDestroyCommandPool");
    resolveInstance(g_vk, inst, g_vk.createShaderModule, "vkCreateShaderModule");
    resolveInstance(g_vk, inst, g_vk.destroyShaderModule, "vkDestroyShaderModule");
    resolveInstance(g_vk, inst, g_vk.createBuffer, "vkCreateBuffer");
    resolveInstance(g_vk, inst, g_vk.destroyBuffer, "vkDestroyBuffer");
    resolveInstance(g_vk, inst, g_vk.getBufferMemoryRequirements, "vkGetBufferMemoryRequirements");
    resolveInstance(g_vk, inst, g_vk.allocateMemory, "vkAllocateMemory");
    resolveInstance(g_vk, inst, g_vk.freeMemory, "vkFreeMemory");
    resolveInstance(g_vk, inst, g_vk.bindBufferMemory, "vkBindBufferMemory");
    resolveInstance(g_vk, inst, g_vk.mapMemory, "vkMapMemory");
    resolveInstance(g_vk, inst, g_vk.unmapMemory, "vkUnmapMemory");
    resolveInstance(g_vk, inst, g_vk.createDescriptorSetLayout, "vkCreateDescriptorSetLayout");
    resolveInstance(g_vk, inst, g_vk.destroyDescriptorSetLayout, "vkDestroyDescriptorSetLayout");
    resolveInstance(g_vk, inst, g_vk.createDescriptorPool, "vkCreateDescriptorPool");
    resolveInstance(g_vk, inst, g_vk.destroyDescriptorPool, "vkDestroyDescriptorPool");
    resolveInstance(g_vk, inst, g_vk.allocateDescriptorSets, "vkAllocateDescriptorSets");
    resolveInstance(g_vk, inst, g_vk.freeDescriptorSets, "vkFreeDescriptorSets");
    resolveInstance(g_vk, inst, g_vk.updateDescriptorSets, "vkUpdateDescriptorSets");
    resolveInstance(g_vk, inst, g_vk.createPipelineLayout, "vkCreatePipelineLayout");
    resolveInstance(g_vk, inst, g_vk.destroyPipelineLayout, "vkDestroyPipelineLayout");
    resolveInstance(g_vk, inst, g_vk.createGraphicsPipelines, "vkCreateGraphicsPipelines");
    resolveInstance(g_vk, inst, g_vk.destroyPipeline, "vkDestroyPipeline");
    resolveInstance(g_vk, inst, g_vk.createRenderPass, "vkCreateRenderPass");
    resolveInstance(g_vk, inst, g_vk.destroyRenderPass, "vkDestroyRenderPass");
    resolveInstance(g_vk, inst, g_vk.createFramebuffer, "vkCreateFramebuffer");
    resolveInstance(g_vk, inst, g_vk.destroyFramebuffer, "vkDestroyFramebuffer");
    resolveInstance(g_vk, inst, g_vk.createImageView, "vkCreateImageView");
    resolveInstance(g_vk, inst, g_vk.destroyImageView, "vkDestroyImageView");
    resolveInstance(g_vk, inst, g_vk.createImage, "vkCreateImage");
    resolveInstance(g_vk, inst, g_vk.destroyImage, "vkDestroyImage");
    resolveInstance(g_vk, inst, g_vk.getImageMemoryRequirements, "vkGetImageMemoryRequirements");
    resolveInstance(g_vk, inst, g_vk.bindImageMemory, "vkBindImageMemory");
    resolveInstance(g_vk, inst, g_vk.createSampler, "vkCreateSampler");
    resolveInstance(g_vk, inst, g_vk.destroySampler, "vkDestroySampler");
    resolveInstance(g_vk, inst, g_vk.cmdBeginRenderPass, "vkCmdBeginRenderPass");
    resolveInstance(g_vk, inst, g_vk.cmdEndRenderPass, "vkCmdEndRenderPass");
    resolveInstance(g_vk, inst, g_vk.cmdBindPipeline, "vkCmdBindPipeline");
    resolveInstance(g_vk, inst, g_vk.cmdBindDescriptorSets, "vkCmdBindDescriptorSets");
    resolveInstance(g_vk, inst, g_vk.cmdDispatch, "vkCmdDispatch");
    resolveInstance(g_vk, inst, g_vk.cmdDraw, "vkCmdDraw");
    resolveInstance(g_vk, inst, g_vk.cmdDrawIndexed, "vkCmdDrawIndexed");
    resolveInstance(g_vk, inst, g_vk.allocateCommandBuffers, "vkAllocateCommandBuffers");
    resolveInstance(g_vk, inst, g_vk.freeCommandBuffers, "vkFreeCommandBuffers");
    resolveInstance(g_vk, inst, g_vk.beginCommandBuffer, "vkBeginCommandBuffer");
    resolveInstance(g_vk, inst, g_vk.endCommandBuffer, "vkEndCommandBuffer");
    resolveInstance(g_vk, inst, g_vk.queueSubmit, "vkQueueSubmit");
    resolveInstance(g_vk, inst, g_vk.queueWaitIdle, "vkQueueWaitIdle");
    resolveInstance(g_vk, inst, g_vk.createFence, "vkCreateFence");
    resolveInstance(g_vk, inst, g_vk.destroyFence, "vkDestroyFence");
    resolveInstance(g_vk, inst, g_vk.waitForFences, "vkWaitForFences");
    resolveInstance(g_vk, inst, g_vk.resetFences, "vkResetFences");
    resolveInstance(g_vk, inst, g_vk.cmdSetViewport, "vkCmdSetViewport");
    resolveInstance(g_vk, inst, g_vk.cmdSetScissor, "vkCmdSetScissor");
    resolveInstance(g_vk, inst, g_vk.cmdBindVertexBuffers, "vkCmdBindVertexBuffers");
    resolveInstance(g_vk, inst, g_vk.cmdBindIndexBuffer, "vkCmdBindIndexBuffer");
    resolveInstance(g_vk, inst, g_vk.resetCommandBuffer, "vkResetCommandBuffer");
    resolveInstance(g_vk, inst, g_vk.cmdPipelineBarrier, "vkCmdPipelineBarrier");
    resolveInstance(g_vk, inst, g_vk.cmdCopyBuffer, "vkCmdCopyBuffer");
    resolveInstance(g_vk, inst, g_vk.cmdCopyBufferToImage, "vkCmdCopyBufferToImage");
    resolveInstance(g_vk, inst, g_vk.cmdCopyImageToBuffer, "vkCmdCopyImageToBuffer");
    resolveInstance(g_vk, inst, g_vk.cmdBlitImage, "vkCmdBlitImage");

    // Swap chain / surface / presentation (device-level, resolved via vkGetDeviceProcAddr)
    // These are resolved lazily after device creation via loadDeviceFunctions()

    return g_vk.enumeratePhysicalDevices != nullptr
        && g_vk.createDevice != nullptr
        && g_vk.destroyDevice != nullptr;
}

inline bool loadDeviceFunctions(VkInstance inst, VkDevice dev) {
    if (!g_vk.loaded || !inst || !dev) return false;

    // Surface / swapchain functions are instance-level (VK_KHR_swapchain)
    // resolved via vkGetInstanceProcAddr
    // Surface functions (instance-level) - resolved via vkGetInstanceProcAddr
    g_vk.getPhysicalDeviceSurfaceCapabilitiesKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
        g_vk.getInstanceProcAddr(inst, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
    g_vk.getPhysicalDeviceSurfaceFormatsKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceFormatsKHR>(
        g_vk.getInstanceProcAddr(inst, "vkGetPhysicalDeviceSurfaceFormatsKHR"));
    g_vk.getPhysicalDeviceSurfacePresentModesKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfacePresentModesKHR>(
        g_vk.getInstanceProcAddr(inst, "vkGetPhysicalDeviceSurfacePresentModesKHR"));
    g_vk.getPhysicalDeviceSurfaceSupportKHR = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
        g_vk.getInstanceProcAddr(inst, "vkGetPhysicalDeviceSurfaceSupportKHR"));

    // Device-level functions resolved via vkGetInstanceProcAddr
    g_vk.createSwapchainKHR = reinterpret_cast<PFN_vkCreateSwapchainKHR>(
        g_vk.getInstanceProcAddr(inst, "vkCreateSwapchainKHR"));
    g_vk.destroySwapchainKHR = reinterpret_cast<PFN_vkDestroySwapchainKHR>(
        g_vk.getInstanceProcAddr(inst, "vkDestroySwapchainKHR"));
    g_vk.getSwapchainImagesKHR = reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
        g_vk.getInstanceProcAddr(inst, "vkGetSwapchainImagesKHR"));
    g_vk.acquireNextImageKHR = reinterpret_cast<PFN_vkAcquireNextImageKHR>(
        g_vk.getInstanceProcAddr(inst, "vkAcquireNextImageKHR"));
    g_vk.queuePresentKHR = reinterpret_cast<PFN_vkQueuePresentKHR>(
        g_vk.getInstanceProcAddr(inst, "vkQueuePresentKHR"));
    g_vk.createSemaphore = reinterpret_cast<PFN_vkCreateSemaphore>(
        g_vk.getInstanceProcAddr(inst, "vkCreateSemaphore"));
    g_vk.destroySemaphore = reinterpret_cast<PFN_vkDestroySemaphore>(
        g_vk.getInstanceProcAddr(inst, "vkDestroySemaphore"));
    g_vk.deviceWaitIdle = reinterpret_cast<PFN_vkDeviceWaitIdle>(
        g_vk.getInstanceProcAddr(inst, "vkDeviceWaitIdle"));

    return g_vk.createSwapchainKHR != nullptr;
}

} // namespace ks