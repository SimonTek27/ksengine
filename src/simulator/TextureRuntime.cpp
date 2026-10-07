#include "TextureRuntime.h"

#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace ks::sim {

// ---------------------------------------------------------------------
// DDS header (Windows BITMAPINFOHEADER-based, as used by Assetto Corsa)
// ---------------------------------------------------------------------
#pragma pack(push,1)
struct DdsHeader {
    std::uint32_t magic;          // = 0x20534444 ("DDS ")
    std::uint32_t size;           // = 124 (for DX10+ extension)
    std::uint32_t flags;          // bitfield describing contents
    std::uint32_t height;
    std::uint32_t width;
    std::uint32_t pitchOrLinearSize;
    std::uint32_t depth;
    std::uint32_t mipMapCount;
    std::uint32_t reserved1[11];
    std::uint32_t pixelFormatSize;     // = 32
    std::uint32_t pixelFormatFlags;    // e.g. DDS_FOURCC, DDS_RGB
    std::uint32_t pixelFormatFourCC;   // 'DXT1', 'DXT3', 'DXT5', etc.
    std::uint32_t pixelFormatRGBBitCount;
    std::uint32_t pixelFormatRedMask;
    std::uint32_t pixelFormatGreenMask;
    std::uint32_t pixelFormatBlueMask;
    std::uint32_t pixelFormatAlphaMask;
    std::uint32_t caps;              // legacy caps
    std::uint32_t caps2;
    std::uint32_t caps3;
    std::uint32_t caps4;
    std::uint32_t reserved2;
};
#pragma pack(pop)

// DDS magic
inline constexpr std::uint32_t DDS_MAGIC = 0x20534444;

// Helper: convert DDS FourCC to Vulkan format (simplified)
inline VkFormat ddsFormatToVk(std::uint32_t fourCC) {
    switch (fourCC) {
        case 0x31545844: return VK_FORMAT_BC1_RGB_UNORM_BLOCK;   // DXT1
        case 0x33545844: return VK_FORMAT_BC2_UNORM_BLOCK;     // DXT3
        case 0x35545844: return VK_FORMAT_BC3_UNORM_BLOCK;     // DXT5
        case 0x41545844: return VK_FORMAT_BC4_UNORM_BLOCK;     // R8 (height/normal height)
        case 0x42545844: return VK_FORMAT_BC5_UNORM_BLOCK;     // RG8 (normal map)
        default: return VK_FORMAT_UNKNOWN;
    }
}

// ---------------------------------------------------------------------
TextureRuntime::TextureRuntime(VkDevice device, VkPhysicalDevice physicalDevice,
                               VkCommandPool commandPool, VkQueue transferQueue)
    : m_device(device), m_physicalDevice(physicalDevice),
      m_commandPool(commandPool), m_transferQueue(transferQueue) {
    createWhiteFallback(m_whiteImg, m_whiteMem, m_whiteView, m_whiteSampler);
}

TextureRuntime::~TextureRuntime() {
    clear();
}

void TextureRuntime::clear() {
    for (auto& kv : m_views) vkDestroyImageView(m_device, kv.second, nullptr);
    for (auto& kv : m_samplers) vkDestroySampler(m_device, kv.second, nullptr);
    for (auto& kv : m_memories) {
        if (kv.second.mem != VK_NULL_HANDLE)
            vkFreeMemory(m_device, kv.second.mem, nullptr);
    }
    m_views.clear(); m_samplers.clear(); m_memories.clear();

    // Destroy white fallback
    if (m_whiteView) vkDestroyImageView(m_device, m_whiteView, nullptr);
    if (m_whiteSampler) vkDestroySampler(m_device, m_whiteSampler, nullptr);
    if (m_whiteImg) vkDestroyImage(m_device, m_whiteImg, nullptr);
    if (m_whiteMem != VK_NULL_HANDLE)
        vkFreeMemory(m_device, m_whiteMem, nullptr);
}

// ---- white 1×1 fallback ------------------------------------------------
void TextureRuntime::createWhiteFallback(VkImage& outImg, VkDeviceMemory& outMem,
                                         VkImageView& outView, VkSampler& outSampler) {
    // Create image
    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.extent.width = 1;
    imgInfo.extent.height = 1;
    imgInfo.extent.depth = 1;
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.format = VK_FORMAT_BC1_RGB_UNORM_BLOCK;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imgInfo.usage = VK_IMAGE_SAMPLED_BIT | VK_IMAGE_TRANSFER_DST_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.flags = 0;

    VkResult res = vkCreateImage(m_device, &imgInfo, nullptr, &outImg);
    if (res != VK_SUCCESS) {
        imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        vkCreateImage(m_device, &imgInfo, nullptr, &outImg);
    }

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_device, outImg, &memReq);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
    for (std::uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
            (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            allocInfo.memoryTypeIndex = i;
            break;
        }
    }
    vkAllocateMemory(m_device, &allocInfo, nullptr, &outMem);
    vkBindImageMemory(m_device, outImg, outMem, 0);

    // NOTE: In a full implementation we would record a command buffer that:
    //   - transitions the image layout
    //   - uploads a white pixel via vkCmdCopyBufferToImage or vkMapMemory
    //   - transitions back to SHADER_READ_ONLY_OPTIMAL
    // For this stub we skip the actual data upload and just set up the view/sampler.

    // Create view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = outImg;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imgInfo.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(m_device, &viewInfo, nullptr, &outView);

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    samplerInfo.maxAnisotropy = props.limits.maxSamplerAnisotropy;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;
    vkCreateSampler(m_device, &samplerInfo, nullptr, &outSampler);
}

// ---------------------------------------------------------------------
// DDS loading (minimal BC1/BC3)
// ---------------------------------------------------------------------
bool TextureRuntime::loadDds(const std::string& relPath, VkImage& outImg,
                             VkDeviceMemory& outMem, VkFormat& outFmt) {
    std::ifstream file(relPath, std::ios::binary);
    if (!file.is_open()) return false;

    DdsHeader hdr;
    file.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (hdr.magic != DDS_MAGIC) return false;

    VkFormat format = ddsFormatToVk(hdr.pixelFormatFourCC);
    if (format == VK_FORMAT_UNKNOWN) return false;

    outFmt = format;

    std::uint32_t blockSize = (format == VK_FORMAT_BC1_RGB_UNORM_BLOCK) ? 8 : 16;
    std::uint32_t widthBlocks  = (hdr.width + 3) / 4;
    std::uint32_t heightBlocks = (hdr.height + 3) / 4;
    std::uint32_t dataSize     = blockSize * widthBlocks * heightBlocks;

    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.extent.width = hdr.width;
    imgInfo.extent.height = hdr.height;
    imgInfo.extent.depth = 1;
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.format = format;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imgInfo.usage = VK_IMAGE_SAMPLED_BIT | VK_IMAGE_TRANSFER_DST_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.flags = 0;
    imgInfo.extent.depth = 1;

    VkResult res = vkCreateImage(m_device, &imgInfo, nullptr, &outImg);
    if (res != VK_SUCCESS) return false;

    VkMemoryRequirements memReq;
    vkGetImageMemoryRequirements(m_device, outImg, &memReq);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReq.size;
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &memProps);
    for (std::uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if (memProps.memoryTypes[i].propertyFlags &
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            allocInfo.memoryTypeIndex = i;
            break;
        }
    }
    vkAllocateMemory(m_device, &allocInfo, nullptr, &outMem);
    vkBindImageMemory(m_device, outImg, outMem, 0);

    // The actual DDS block decode (decompressing BC1/BC3) is omitted for brevity.
    // In production you would read the raw block data, decompress, and upload via
    // a staging buffer + vkCmdCopyBufferToImage. The white 1×1 fallback below
    // ensures the renderer never sees a missing-bind error.
    (void)dataSize; // suppress unused-parameter warning

    return true;
}

// ---------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------
VkImageView TextureRuntime::get(const std::string& relPath,
                                VkSampler& outSampler) {
    auto low = relPath;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
    auto it = m_views.find(low);
    if (it != m_views.end()) {
        outSampler = m_samplers[low];
        return it->second;
    }

    VkImage img = VK_NULL_HANDLE;
    VkDeviceMemory mem = VK_NULL_HANDLE;
    VkFormat fmt = VK_FORMAT_UNKNOWN;
    if (loadDds(relPath, img, mem, fmt)) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = img;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = fmt;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        VkImageView view;
        VkResult res = vkCreateImageView(m_device, &viewInfo, nullptr, &view);
        if (res != VK_SUCCESS) {
            view = m_whiteView;
        }

        auto sit = m_samplers.find(low);
        VkSampler sam;
        if (sit != m_samplers.end()) {
            sam = sit->second;
        } else {
            VkSamplerCreateInfo sci{};
            sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sci.magFilter = VK_FILTER_LINEAR;
            sci.minFilter = VK_FILTER_LINEAR;
            sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
            sci.maxAnisotropy = props.limits.maxSamplerAnisotropy;
            sci.unnormalizedCoordinates = VK_FALSE;
            sci.compareEnable = VK_FALSE;
            sci.compareOp = VK_COMPARE_OP_ALWAYS;
            sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            sci.mipLodBias = 0.0f;
            sci.minLod = 0.0f;
            sci.maxLod = 0.0f;
            vkCreateSampler(m_device, &sci, nullptr, &sam);
            m_samplers[low] = sam;
        }

        m_views[low] = view;
        m_memories[low] = {mem};
        outSampler = sam;
        return view;
    }

    // Failed to load DDS → return white fallback
    outSampler = m_whiteSampler;
    return m_whiteView;
}

// ---------------------------------------------------------------------
} // namespace ks::sim