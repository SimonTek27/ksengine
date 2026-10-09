#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <vulkan/vulkan.h>

namespace ks::sim {

// Runtime cache for textures extracted by Kn5Baker. DDS files are decoded to
// RGBA8 on the CPU through the shared Qt-free DdsReader, given a CPU-built
// mip chain, uploaded once with a staging buffer, then retained until
// clear() or destruction. The cache owns every Vulkan handle it returns;
// callers only borrow the view and sampler.
class TextureRuntime final {
public:
    // anisotropyEnabled must mirror the samplerAnisotropy flag the device
    // was created with (NativeRenderer passes what it enabled); without the
    // feature the samplers stay isotropic — fail-open, never a validation
    // error. Anisotropy then runs at the device's maxSamplerAnisotropy.
    TextureRuntime(VkDevice device, VkPhysicalDevice physicalDevice,
                   VkCommandPool commandPool, VkQueue transferQueue,
                   bool anisotropyEnabled = false);
    ~TextureRuntime();

    TextureRuntime(const TextureRuntime&) = delete;
    TextureRuntime& operator=(const TextureRuntime&) = delete;
    TextureRuntime(TextureRuntime&&) = delete;
    TextureRuntime& operator=(TextureRuntime&&) = delete;

    // Resolves a DDS path to a sampled image. Invalid, missing or unsupported
    // files consistently return a valid fallback instead of an invalid
    // descriptor: the white1×1 (albedo and generic use), or — with
    // flatNormalFallback — the flat (0,0,255) normal so a lost normal map
    // degrades to "no perturbation" instead of tilting every surface.
    // Texture uploads are synchronous and must happen on the render thread
    // while no frame command buffer is being recorded.
    VkImageView get(const std::string& path, VkSampler& outSampler,
                    bool flatNormalFallback = false);

    // Drops successfully loaded texture images but keeps the fallback valid.
    // Failed paths are also forgotten so a corrected file can be retried.
    void clear();

    // Both fallbacks must exist: get() promises a *valid* view for any path.
    bool ready() const noexcept {
        return m_fallback.view != VK_NULL_HANDLE && m_flatFallback.view != VK_NULL_HANDLE;
    }
    std::size_t cachedTextureCount() const noexcept { return m_textures.size(); }

private:
    struct Texture {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
    };

    bool createFallback(Texture& out, std::uint8_t r, std::uint8_t g,
                        std::uint8_t b, std::uint8_t a);
    bool loadDds(const std::string& path, Texture& outTexture);
    bool uploadRgba(const std::uint8_t* rgba, std::uint32_t width,
                    std::uint32_t height, Texture& outTexture);
    // mipLevels drives maxLod: a 1-mip fallback keeps maxLod 0 (the old
    // behaviour), a generated chain lets the sampler reach its last level.
    bool createSampler(VkSampler& outSampler, std::uint32_t mipLevels) const;
    bool findMemoryType(std::uint32_t typeBits, VkMemoryPropertyFlags properties,
                        std::uint32_t& outIndex) const;
    void destroyTexture(Texture& texture) const noexcept;
    static std::string normalizedKey(const std::string& path);
    // CPU box-filter step for the mip chain: dst = ceil-safe half of src,
    // 2x2 average with clamped edges (odd source sizes drop the remainder
    // row/column — visually irrelevant and keeps level dims spec-exact).
    static void downsampleBox(const std::uint8_t* src, std::uint32_t srcW,
                              std::uint32_t srcH, std::uint8_t* dst,
                              std::uint32_t dstW, std::uint32_t dstH);

    VkDevice m_device = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkQueue m_transferQueue = VK_NULL_HANDLE;

    // samplerAnisotropy as enabled on the owning device; maxAnisotropy is
    // the device limit (never lower it, never exceed it).
    bool m_anisotropyEnabled = false;
    float m_maxAnisotropy = 1.0f;

    Texture m_fallback;      // white: albedo / generic missing texture
    Texture m_flatFallback;  // (0,0,255): missing normal map = no perturbation
    std::unordered_map<std::string, Texture> m_textures;
    std::unordered_set<std::string> m_failedPaths;
};

} // namespace ks::sim
