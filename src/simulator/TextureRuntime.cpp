#include "TextureRuntime.h"

#include "engine/FileFormat/DdsReader.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <string_view>
#include <vector>

namespace ks::sim {

namespace {

constexpr std::uint32_t kFallbackWidth = 1;
constexpr std::uint32_t kFallbackHeight = 1;
constexpr VkFormat kTextureFormat = VK_FORMAT_R8G8B8A8_UNORM;

void transitionImage(VkCommandBuffer commandBuffer, VkImage image,
                     VkImageLayout oldLayout, VkImageLayout newLayout,
                     VkAccessFlags srcAccessMask, VkAccessFlags dstAccessMask,
                     VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                     std::uint32_t baseMipLevel = 0, std::uint32_t levelCount = 1) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = baseMipLevel;
    barrier.subresourceRange.levelCount = levelCount;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr,
                         0, nullptr, 1, &barrier);
}

} // namespace

// Box-filter one mip level: dst(x,y) = average of the source box
// [2x, min(2x+2, srcW)) x [2y, min(2y+2, srcH)). The *last* box extends to
// the source edge, so an odd source size folds its remainder into the final
// row/column instead of dropping it, while every destination dimension stays
// max(1, floor(src/2)) as the Vulkan mip layout requires. Averaging RGBA
// (not RGB) keeps alpha coherent for cutout foliage.
void TextureRuntime::downsampleBox(const std::uint8_t* src, std::uint32_t srcW,
                                   std::uint32_t srcH, std::uint8_t* dst,
                                   std::uint32_t dstW, std::uint32_t dstH) {
    for (std::uint32_t y = 0; y < dstH; ++y) {
        const std::uint32_t y0 = y * 2;
        const std::uint32_t y1 = (y + 1 == dstH) ? srcH : std::min(srcH, y0 + 2);
        for (std::uint32_t x = 0; x < dstW; ++x) {
            const std::uint32_t x0 = x * 2;
            const std::uint32_t x1 = (x + 1 == dstW) ? srcW : std::min(srcW, x0 + 2);
            std::uint32_t sum[4] = {0, 0, 0, 0};
            std::uint32_t count = 0;
            for (std::uint32_t sy = y0; sy < y1; ++sy) {
                for (std::uint32_t sx = x0; sx < x1; ++sx) {
                    const std::uint8_t* p = src + (static_cast<std::size_t>(sy) * srcW + sx) * 4;
                    sum[0] += p[0];
                    sum[1] += p[1];
                    sum[2] += p[2];
                    sum[3] += p[3];
                    ++count;
                }
            }
            std::uint8_t* q = dst + (static_cast<std::size_t>(y) * dstW + x) * 4;
            // count >= 1 always (dst dims derive from non-zero src dims).
            for (std::uint32_t c = 0; c < 4; ++c)
                q[c] = static_cast<std::uint8_t>((sum[c] + count / 2) / count);
        }
    }
}

TextureRuntime::TextureRuntime(VkDevice device, VkPhysicalDevice physicalDevice,
                               VkCommandPool commandPool, VkQueue transferQueue,
                               bool anisotropyEnabled)
    : m_device(device),
      m_physicalDevice(physicalDevice),
      m_commandPool(commandPool),
      m_transferQueue(transferQueue),
      m_anisotropyEnabled(anisotropyEnabled) {
    // Device limit drives maxAnisotropy: use all of it (16 on desktop GPUs,
    // never below 8 in practice), and never more than the limit allows.
    // Queried before the fallbacks below, which create samplers too.
    if (m_physicalDevice) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
        m_maxAnisotropy = std::max(1.0f, properties.limits.maxSamplerAnisotropy);
    }
    if (!createFallback(m_fallback, 255, 255, 255, 255)) {
        std::fprintf(stderr, "[TextureRuntime] white fallback creation failed\n");
    }
    if (!createFallback(m_flatFallback, 0, 0, 255, 255)) {
        std::fprintf(stderr, "[TextureRuntime] flat normal fallback creation failed\n");
    }
}

TextureRuntime::~TextureRuntime() {
    clear();
    destroyTexture(m_fallback);
    destroyTexture(m_flatFallback);
}

VkImageView TextureRuntime::get(const std::string& path, VkSampler& outSampler,
                                bool flatNormalFallback) {
    const Texture& fallback = flatNormalFallback ? m_flatFallback : m_fallback;
    outSampler = fallback.sampler;
    if (!ready() || path.empty()) return fallback.view;

    const std::string key = normalizedKey(path);
    if (const auto found = m_textures.find(key); found != m_textures.end()) {
        outSampler = found->second.sampler;
        return found->second.view;
    }
    if (m_failedPaths.find(key) != m_failedPaths.end()) return fallback.view;

    Texture texture;
    if (!loadDds(path, texture)) {
        m_failedPaths.insert(key);
        return fallback.view;
    }

    const auto [inserted, wasInserted] = m_textures.emplace(key, texture);
    if (!wasInserted) {
        destroyTexture(texture);
        outSampler = inserted->second.sampler;
        return inserted->second.view;
    }
    outSampler = inserted->second.sampler;
    return inserted->second.view;
}

void TextureRuntime::clear() {
    for (auto& [_, texture] : m_textures) {
        destroyTexture(texture);
    }
    m_textures.clear();
    m_failedPaths.clear();
}

bool TextureRuntime::createFallback(Texture& out, std::uint8_t r, std::uint8_t g,
                                    std::uint8_t b, std::uint8_t a) {
    const std::array<std::uint8_t, 4> rgba{r, g, b, a};
    return uploadRgba(rgba.data(), kFallbackWidth, kFallbackHeight, out);
}

bool TextureRuntime::loadDds(const std::string& path, Texture& outTexture) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        std::fprintf(stderr, "[TextureRuntime] cannot open texture %s\n", path.c_str());
        return false;
    }

    const std::streampos end = file.tellg();
    if (end <= 0) {
        std::fprintf(stderr, "[TextureRuntime] texture is empty: %s\n", path.c_str());
        return false;
    }
    const auto byteCount = static_cast<std::size_t>(end);
    std::vector<char> bytes(byteCount);
    file.seekg(0);
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file) {
        std::fprintf(stderr, "[TextureRuntime] cannot read texture %s\n", path.c_str());
        return false;
    }

    const std::string_view source(bytes.data(), bytes.size());
    const engine::fileformat::DdsImage image = engine::fileformat::decodeDds(source);
    if (!image.ok() || image.info.width <= 0 || image.info.height <= 0 || image.rgba.empty()) {
        std::fprintf(stderr, "[TextureRuntime] DDS decode failed for %s: %s\n", path.c_str(),
                     image.info.error.c_str());
        return false;
    }

    return uploadRgba(image.rgba.data(), static_cast<std::uint32_t>(image.info.width),
                      static_cast<std::uint32_t>(image.info.height), outTexture);
}

bool TextureRuntime::uploadRgba(const std::uint8_t* rgba, std::uint32_t width,
                                std::uint32_t height, Texture& outTexture) {
    if (!rgba || width == 0 || height == 0 || !m_device || !m_physicalDevice ||
        !m_commandPool || !m_transferQueue) {
        return false;
    }
    const std::size_t pixelCount = static_cast<std::size_t>(width) * height;
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 4) return false;
    const VkDeviceSize byteCount = static_cast<VkDeviceSize>(pixelCount * 4);

    // RENDERING_AI_BRIEF P0: CPU mip chain (box filter) so minification
    // samples a real LOD instead of aliasing mip 0. The DDS reader hands us
    // top level only, so every level below is derived here and the whole
    // chain uploads in one staging call. Textures smaller than 2px yield a
    // 1-level chain — byte-identical to the old path.
    const std::uint32_t maxDim = std::max(width, height);
    std::uint32_t mipLevels = 1;
    for (std::uint32_t dim = maxDim; dim > 1; dim >>= 1) ++mipLevels;

    std::vector<std::uint8_t> chain;
    std::vector<VkDeviceSize> levelOffsets(mipLevels);
    std::vector<std::uint32_t> levelW(mipLevels);
    std::vector<std::uint32_t> levelH(mipLevels);
    {
        // Geometric series converges to < 2x the base level; pre-reserve to
        // avoid realloc churn while the base pointer is being read.
        chain.reserve(static_cast<std::size_t>(byteCount) * 2);
        chain.resize(static_cast<std::size_t>(byteCount));
        std::memcpy(chain.data(), rgba, static_cast<std::size_t>(byteCount));
        levelOffsets[0] = 0;
        levelW[0] = width;
        levelH[0] = height;
        for (std::uint32_t level = 1; level < mipLevels; ++level) {
            const std::uint32_t srcW = levelW[level - 1];
            const std::uint32_t srcH = levelH[level - 1];
            const std::uint32_t dstW = std::max(1u, srcW / 2);
            const std::uint32_t dstH = std::max(1u, srcH / 2);
            const std::size_t dstOffset = chain.size();
            chain.resize(dstOffset + static_cast<std::size_t>(dstW) * dstH * 4);
            downsampleBox(chain.data() + levelOffsets[level - 1], srcW, srcH,
                          chain.data() + dstOffset, dstW, dstH);
            levelOffsets[level] = static_cast<VkDeviceSize>(dstOffset);
            levelW[level] = dstW;
            levelH[level] = dstH;
        }
    }
    const VkDeviceSize chainBytes = static_cast<VkDeviceSize>(chain.size());

    VkFormatProperties formatProperties{};
    vkGetPhysicalDeviceFormatProperties(m_physicalDevice, kTextureFormat, &formatProperties);
    if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) == 0) {
        std::fprintf(stderr, "[TextureRuntime] RGBA8 sampled images are unsupported\n");
        return false;
    }

    Texture texture;
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = kTextureFormat;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device, &imageInfo, nullptr, &texture.image) != VK_SUCCESS) return false;

    VkMemoryRequirements imageRequirements{};
    vkGetImageMemoryRequirements(m_device, texture.image, &imageRequirements);
    std::uint32_t imageMemoryType = 0;
    if (!findMemoryType(imageRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        imageMemoryType)) {
        destroyTexture(texture);
        return false;
    }
    VkMemoryAllocateInfo imageAllocation{};
    imageAllocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imageAllocation.allocationSize = imageRequirements.size;
    imageAllocation.memoryTypeIndex = imageMemoryType;
    if (vkAllocateMemory(m_device, &imageAllocation, nullptr, &texture.memory) != VK_SUCCESS ||
        vkBindImageMemory(m_device, texture.image, texture.memory, 0) != VK_SUCCESS) {
        destroyTexture(texture);
        return false;
    }

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = chainBytes;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(m_device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        destroyTexture(texture);
        return false;
    }

    auto destroyStaging = [&] {
        if (stagingBuffer) vkDestroyBuffer(m_device, stagingBuffer, nullptr);
        if (stagingMemory) vkFreeMemory(m_device, stagingMemory, nullptr);
    };

    VkMemoryRequirements stagingRequirements{};
    vkGetBufferMemoryRequirements(m_device, stagingBuffer, &stagingRequirements);
    std::uint32_t stagingMemoryType = 0;
    const VkMemoryPropertyFlags stagingProperties =
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    if (!findMemoryType(stagingRequirements.memoryTypeBits, stagingProperties, stagingMemoryType)) {
        destroyStaging();
        destroyTexture(texture);
        return false;
    }
    VkMemoryAllocateInfo stagingAllocation{};
    stagingAllocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAllocation.allocationSize = stagingRequirements.size;
    stagingAllocation.memoryTypeIndex = stagingMemoryType;
    if (vkAllocateMemory(m_device, &stagingAllocation, nullptr, &stagingMemory) != VK_SUCCESS ||
        vkBindBufferMemory(m_device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS) {
        destroyStaging();
        destroyTexture(texture);
        return false;
    }

    void* mapped = nullptr;
    if (vkMapMemory(m_device, stagingMemory, 0, chainBytes, 0, &mapped) != VK_SUCCESS || !mapped) {
        destroyStaging();
        destroyTexture(texture);
        return false;
    }
    std::memcpy(mapped, chain.data(), static_cast<std::size_t>(chainBytes));
    vkUnmapMemory(m_device, stagingMemory);

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo commandAllocation{};
    commandAllocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandAllocation.commandPool = m_commandPool;
    commandAllocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandAllocation.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(m_device, &commandAllocation, &commandBuffer) != VK_SUCCESS) {
        destroyStaging();
        destroyTexture(texture);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    const VkResult beginResult = vkBeginCommandBuffer(commandBuffer, &beginInfo);
    if (beginResult != VK_SUCCESS) {
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
        destroyStaging();
        destroyTexture(texture);
        return false;
    }

    // Whole chain in one transition, one multi-region copy, one transition
    // out: the layout covers every level, so no per-level barrier dance.
    transitionImage(commandBuffer, texture.image, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    0, mipLevels);
    std::vector<VkBufferImageCopy> copies(mipLevels);
    for (std::uint32_t level = 0; level < mipLevels; ++level) {
        VkBufferImageCopy& copy = copies[level];
        copy.bufferOffset = levelOffsets[level];
        copy.bufferRowLength = 0;   // tightly packed: derived from extent
        copy.bufferImageHeight = 0;
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = level;
        copy.imageSubresource.baseArrayLayer = 0;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {levelW[level], levelH[level], 1};
    }
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, texture.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           mipLevels, copies.data());
    transitionImage(commandBuffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, mipLevels);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
        destroyStaging();
        destroyTexture(texture);
        return false;
    }
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer;
    const VkResult submitResult = vkQueueSubmit(m_transferQueue, 1, &submit, VK_NULL_HANDLE);
    const VkResult waitResult = submitResult == VK_SUCCESS ? vkQueueWaitIdle(m_transferQueue)
                                                            : submitResult;
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer);
    destroyStaging();
    if (waitResult != VK_SUCCESS) {
        destroyTexture(texture);
        return false;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = kTextureFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &viewInfo, nullptr, &texture.view) != VK_SUCCESS ||
        !createSampler(texture.sampler, mipLevels)) {
        destroyTexture(texture);
        return false;
    }

    outTexture = texture;
    return true;
}

bool TextureRuntime::createSampler(VkSampler& outSampler, std::uint32_t mipLevels) const {
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.mipLodBias = 0.0f;
    // P0 anisotropy: only when the owning device enabled samplerAnisotropy
    // (mirrored in the ctor arg) — otherwise VUID forbids anisotropyEnable.
    // maxAnisotropy is the device limit queried at construction (>= 1).
    samplerInfo.anisotropyEnable = m_anisotropyEnabled ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = m_anisotropyEnabled ? m_maxAnisotropy : 1.0f;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.minLod = 0.0f;
    // Reach the last generated level; clamps to what the view actually has,
    // so a 1-mip fallback behaves exactly as before (maxLod effectively 0).
    samplerInfo.maxLod = mipLevels > 0 ? static_cast<float>(mipLevels - 1) : 0.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    return vkCreateSampler(m_device, &samplerInfo, nullptr, &outSampler) == VK_SUCCESS;
}

bool TextureRuntime::findMemoryType(std::uint32_t typeBits,
                                    VkMemoryPropertyFlags properties,
                                    std::uint32_t& outIndex) const {
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memoryProperties);
    for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        const bool compatible = (typeBits & (1u << index)) != 0;
        const bool hasProperties =
            (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties;
        if (compatible && hasProperties) {
            outIndex = index;
            return true;
        }
    }
    return false;
}

void TextureRuntime::destroyTexture(Texture& texture) const noexcept {
    if (!m_device) return;
    if (texture.sampler) vkDestroySampler(m_device, texture.sampler, nullptr);
    if (texture.view) vkDestroyImageView(m_device, texture.view, nullptr);
    if (texture.image) vkDestroyImage(m_device, texture.image, nullptr);
    if (texture.memory) vkFreeMemory(m_device, texture.memory, nullptr);
    texture = {};
}

std::string TextureRuntime::normalizedKey(const std::string& path) {
    std::string key = path;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return key;
}

} // namespace ks::sim
