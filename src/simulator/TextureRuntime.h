#pragma once

#include "MathTypes.h"
#include <string>
#include <unordered_map>
#include <vulkan/vulkan.h>

namespace ks::sim {

// A tiny cache for DDS textures. Looks up by normalized path; returns a
// VkImageView that the renderer can bind into descriptor set 1 (albedo).
// If the file cannot be decoded, a white 1×1 image is returned so the
// renderer never sees a missing-bind error.
class TextureRuntime {
public:
    TextureRuntime(VkDevice device, VkPhysicalDevice physicalDevice,
                   VkCommandPool commandPool, VkQueue transferQueue);
    ~TextureRuntime();

    // Resolve (or create) a texture by its baked-path name (relative to the
    // manifest dir). Returns the image-view handle. `outSampler` out-param
    // receives a sampler suitable for the image (anisotropic if available).
    VkImageView get(const std::string& relPath,
                    VkSampler& outSampler);

    // Free all cached images / views / samplers.
    void clear();

private:
    VkDevice m_device;
    VkPhysicalDevice m_physicalDevice;
    VkCommandPool m_commandPool;
    VkQueue m_transferQueue;

    // image → view mapping; key = lowercased path
    std::unordered_map<std::string, VkImageView> m_views;
    // image → sampler mapping
    std::unordered_map<std::string, VkSampler> m_samplers;
    // per-image memory (free in clear)
    struct ImageMemory { VkDeviceMemory mem = VK_NULL_HANDLE; };
    std::unordered_map<std::string, ImageMemory> m_memories;

    // Create a 1×1 white image + view + sampler (used as fallback).
    void createWhiteFallback(VkImage& outImg, VkDeviceMemory& outMem,
                             VkImageView& outView, VkSampler& outSampler);

    // Decode a DDS file into an VkImage (RT format inferred from dxgiformat).
    // Returns false on failure (caller falls back to white).
    bool loadDds(const std::string& relPath, VkImage& outImg,
                 VkDeviceMemory& outMem, VkFormat& outFmt);
};

} // namespace ks::sim