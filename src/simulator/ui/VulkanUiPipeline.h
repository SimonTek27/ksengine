#pragma once
/**
 * VulkanUiPipeline — full UI overlay pipeline (Qt-free).
 *
 * Creates: descriptor set layout + pool, sampler, R8 atlas image,
 * host-visible VB/IB, graphics pipeline (alpha blend, no depth).
 *
 * Shaders: load SPIR-V via create(..., vertSpv, fragSpv) or from files.
 * GLSL sources live in ui/shaders/ui.{vert,frag}.glsl (compile offline).
 */
#include "UiGpuPass.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <cstdio>
#include <string>

namespace ks {
namespace sim {
namespace ui {

class VulkanUiPipeline {
public:
    struct CreateInfo {
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;
        uint32_t queueFamily = 0;
        uint32_t subpass = 0;
        // SPIR-V bytes (required unless loadSpirvFromFiles succeeds)
        const uint32_t* vertSpv = nullptr;
        size_t vertSpvWords = 0;
        const uint32_t* fragSpv = nullptr;
        size_t fragSpvWords = 0;
        // Optional file paths (used if pointers null)
        std::string vertSpvPath;
        std::string fragSpvPath;
    };

    ~VulkanUiPipeline() { destroy(); }

    bool create(const CreateInfo& ci) {
        destroy();
        m_phys = ci.physicalDevice;
        m_dev = ci.device;
        m_queue = ci.queue;
        m_renderPass = ci.renderPass;
        m_qfamily = ci.queueFamily;
        if (!m_dev || !m_phys || !m_renderPass) {
            std::fprintf(stderr, "VulkanUiPipeline: missing device/physical/renderPass\n");
            return false;
        }

        std::vector<uint32_t> vert, frag;
        const uint32_t* vptr = ci.vertSpv;
        size_t vwords = ci.vertSpvWords;
        const uint32_t* fptr = ci.fragSpv;
        size_t fwords = ci.fragSpvWords;
        if (!vptr || !fptr) {
            if (!loadSpirvFile(ci.vertSpvPath.empty() ? "system/shaders/ui.vert.spv" : ci.vertSpvPath, vert) ||
                !loadSpirvFile(ci.fragSpvPath.empty() ? "system/shaders/ui.frag.spv" : ci.fragSpvPath, frag)) {
                std::fprintf(stderr,
                    "VulkanUiPipeline: need SPIR-V (CreateInfo::vertSpv/fragSpv or .spv files)\n");
                return false;
            }
            vptr = vert.data(); vwords = vert.size();
            fptr = frag.data(); fwords = frag.size();
        }

        if (!createDescriptors()) return false;
        if (!createSampler()) return false;
        if (!createPipeline(vptr, vwords, fptr, fwords, ci.subpass)) return false;
        if (!createDynamicBuffers()) return false;

        m_ok = true;
        std::fprintf(stderr, "VulkanUiPipeline: ready\n");
        return true;
    }

    void destroy() {
        if (!m_dev) { m_ok = false; return; }
        vkDeviceWaitIdle(m_dev);
        destroyBuffer(m_vb, m_vbMem);
        destroyBuffer(m_ib, m_ibMem);
        if (m_atlasView) { vkDestroyImageView(m_dev, m_atlasView, nullptr); m_atlasView = VK_NULL_HANDLE; }
        if (m_atlasImage) { vkDestroyImage(m_dev, m_atlasImage, nullptr); m_atlasImage = VK_NULL_HANDLE; }
        if (m_atlasMem) { vkFreeMemory(m_dev, m_atlasMem, nullptr); m_atlasMem = VK_NULL_HANDLE; }
        if (m_sampler) { vkDestroySampler(m_dev, m_sampler, nullptr); m_sampler = VK_NULL_HANDLE; }
        if (m_pipeline) { vkDestroyPipeline(m_dev, m_pipeline, nullptr); m_pipeline = VK_NULL_HANDLE; }
        if (m_pipelineLayout) { vkDestroyPipelineLayout(m_dev, m_pipelineLayout, nullptr); m_pipelineLayout = VK_NULL_HANDLE; }
        if (m_descPool) { vkDestroyDescriptorPool(m_dev, m_descPool, nullptr); m_descPool = VK_NULL_HANDLE; }
        if (m_descLayout) { vkDestroyDescriptorSetLayout(m_dev, m_descLayout, nullptr); m_descLayout = VK_NULL_HANDLE; }
        m_descSet = VK_NULL_HANDLE;
        m_ok = false;
    }

    bool isReady() const { return m_ok; }

    /** Wire into UiGpuPass hooks. */
    void bindTo(UiGpuPass& pass) {
        pass.onUploadAtlas = [this](const uint8_t* r8, int w, int h) {
            this->uploadAtlas(r8, w, h);
        };
        pass.onUploadDynamic = [this](const UiVertex* v, uint32_t vc,
                                      const uint32_t* i, uint32_t ic) {
            this->uploadDynamic(v, vc, i, ic);
        };
        pass.onDraw = [this](uint32_t vc, uint32_t ic, float sw, float sh) {
            this->recordDraw(m_cmd, vc, ic, sw, sh);
        };
        pass.m_pipeline = m_pipeline;
        pass.m_vb = m_vb;
        pass.m_ib = m_ib;
        pass.m_atlasImage = m_atlasImage;
        pass.m_atlasView = m_atlasView;
        pass.m_sampler = m_sampler;
        pass.m_descSet = m_descSet;
    }

    void setCommandBuffer(VkCommandBuffer cmd) { m_cmd = cmd; }

    bool uploadAtlas(const uint8_t* r8, int w, int h) {
        if (!m_ok || !r8 || w <= 0 || h <= 0) return false;
        // Recreate image if size changed
        if (m_atlasImage && (w != m_atlasW || h != m_atlasH)) {
            vkDestroyImageView(m_dev, m_atlasView, nullptr);
            vkDestroyImage(m_dev, m_atlasImage, nullptr);
            vkFreeMemory(m_dev, m_atlasMem, nullptr);
            m_atlasView = VK_NULL_HANDLE;
            m_atlasImage = VK_NULL_HANDLE;
            m_atlasMem = VK_NULL_HANDLE;
        }
        m_atlasW = w;
        m_atlasH = h;

        if (!m_atlasImage) {
            if (!createImage2D(static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                               VK_FORMAT_R8_UNORM,
                               VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               m_atlasImage, m_atlasMem))
                return false;
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vi.image = m_atlasImage;
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = VK_FORMAT_R8_UNORM;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.layerCount = 1;
            if (vkCreateImageView(m_dev, &vi, nullptr, &m_atlasView) != VK_SUCCESS)
                return false;
        }

        // Staging buffer
        VkDeviceSize size = static_cast<VkDeviceSize>(w) * static_cast<VkDeviceSize>(h);
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        if (!createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          staging, stagingMem))
            return false;
        void* mapped = nullptr;
        vkMapMemory(m_dev, stagingMem, 0, size, 0, &mapped);
        std::memcpy(mapped, r8, static_cast<size_t>(size));
        vkUnmapMemory(m_dev, stagingMem);

        // One-shot command buffer for layout transition + copy
        VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pci.queueFamilyIndex = m_qfamily;
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        VkCommandPool pool = VK_NULL_HANDLE;
        vkCreateCommandPool(m_dev, &pci, nullptr, &pool);
        VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ai.commandPool = pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(m_dev, &ai, &cmd);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &bi);

        transitionImage(cmd, m_atlasImage, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1};
        vkCmdCopyBufferToImage(cmd, staging, m_atlasImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        transitionImage(cmd, m_atlasImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        vkEndCommandBuffer(cmd);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        vkQueueSubmit(m_queue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_queue);

        vkFreeCommandBuffers(m_dev, pool, 1, &cmd);
        vkDestroyCommandPool(m_dev, pool, nullptr);
        destroyBuffer(staging, stagingMem);

        // Update descriptor
        VkDescriptorImageInfo ii{};
        ii.sampler = m_sampler;
        ii.imageView = m_atlasView;
        ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        VkWriteDescriptorSet wd{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wd.dstSet = m_descSet;
        wd.dstBinding = 0;
        wd.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wd.descriptorCount = 1;
        wd.pImageInfo = &ii;
        vkUpdateDescriptorSets(m_dev, 1, &wd, 0, nullptr);
        return true;
    }

    bool uploadDynamic(const UiVertex* v, uint32_t vc, const uint32_t* i, uint32_t ic) {
        if (!m_ok) return false;
        m_vc = vc;
        m_ic = ic;
        if (vc && v && m_vbMapped) {
            const size_t bytes = std::min(static_cast<size_t>(vc), static_cast<size_t>(UiGpuPass::kMaxVertices))
                                 * sizeof(UiVertex);
            std::memcpy(m_vbMapped, v, bytes);
        }
        if (ic && i && m_ibMapped) {
            const size_t bytes = std::min(static_cast<size_t>(ic), static_cast<size_t>(UiGpuPass::kMaxIndices))
                                 * sizeof(uint32_t);
            std::memcpy(m_ibMapped, i, bytes);
        }
        return true;
    }

    void recordDraw(VkCommandBuffer cmd, uint32_t /*vc*/, uint32_t ic, float screenW, float screenH) {
        if (!m_ok || !cmd || ic == 0 || !m_pipeline) return;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout,
                                0, 1, &m_descSet, 0, nullptr);

        float pc[2] = {screenW, screenH};
        vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), pc);

        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_vb, &off);
        vkCmdBindIndexBuffer(cmd, m_ib, 0, VK_INDEX_TYPE_UINT32);

        // Full-screen viewport matching swapchain
        VkViewport vp{};
        vp.width = screenW;
        vp.height = screenH;
        vp.maxDepth = 1.f;
        vkCmdSetViewport(cmd, 0, 1, &vp);
        VkRect2D sc{{0, 0}, {static_cast<uint32_t>(screenW), static_cast<uint32_t>(screenH)}};
        vkCmdSetScissor(cmd, 0, 1, &sc);

        vkCmdDrawIndexed(cmd, ic, 1, 0, 0, 0);
    }

    VkPipeline pipeline() const { return m_pipeline; }
    VkDescriptorSet descriptorSet() const { return m_descSet; }

private:
    static bool loadSpirvFile(const std::string& path, std::vector<uint32_t>& out) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        f.seekg(0, std::ios::end);
        const auto sz = static_cast<size_t>(f.tellg());
        f.seekg(0, std::ios::beg);
        if (sz < 4 || (sz % 4) != 0) return false;
        out.resize(sz / 4);
        f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(sz));
        return f.good() || f.eof();
    }

    uint32_t findMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const {
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(m_phys, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
            if ((typeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
                return i;
        }
        return 0;
    }

    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps,
                      VkBuffer& buf, VkDeviceMemory& mem) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = size;
        bi.usage = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_dev, &bi, nullptr, &buf) != VK_SUCCESS) return false;
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(m_dev, buf, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, memProps);
        if (vkAllocateMemory(m_dev, &ai, nullptr, &mem) != VK_SUCCESS) return false;
        vkBindBufferMemory(m_dev, buf, mem, 0);
        return true;
    }

    void destroyBuffer(VkBuffer& buf, VkDeviceMemory& mem) {
        if (buf) { vkDestroyBuffer(m_dev, buf, nullptr); buf = VK_NULL_HANDLE; }
        if (mem) { vkFreeMemory(m_dev, mem, nullptr); mem = VK_NULL_HANDLE; }
    }

    bool createImage2D(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage,
                       VkImage& img, VkDeviceMemory& mem) {
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = fmt;
        ii.extent = {w, h, 1};
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = usage;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(m_dev, &ii, nullptr, &img) != VK_SUCCESS) return false;
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(m_dev, img, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(m_dev, &ai, nullptr, &mem) != VK_SUCCESS) return false;
        vkBindImageMemory(m_dev, img, mem, 0);
        return true;
    }

    void transitionImage(VkCommandBuffer cmd, VkImage image,
                         VkImageLayout oldL, VkImageLayout newL) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = oldL;
        b.newLayout = newL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.layerCount = 1;
        if (oldL == VK_IMAGE_LAYOUT_UNDEFINED && newL == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
            b.srcAccessMask = 0;
            b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        } else if (oldL == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
                   newL == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 0, nullptr, 0, nullptr, 1, &b);
        }
    }

    bool createDescriptors() {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        lci.bindingCount = 1;
        lci.pBindings = &binding;
        if (vkCreateDescriptorSetLayout(m_dev, &lci, nullptr, &m_descLayout) != VK_SUCCESS)
            return false;

        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps.descriptorCount = 1;
        VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pci.poolSizeCount = 1;
        pci.pPoolSizes = &ps;
        pci.maxSets = 1;
        if (vkCreateDescriptorPool(m_dev, &pci, nullptr, &m_descPool) != VK_SUCCESS)
            return false;

        VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        ai.descriptorPool = m_descPool;
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &m_descLayout;
        return vkAllocateDescriptorSets(m_dev, &ai, &m_descSet) == VK_SUCCESS;
    }

    bool createSampler() {
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = VK_FILTER_NEAREST;
        si.minFilter = VK_FILTER_NEAREST;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        return vkCreateSampler(m_dev, &si, nullptr, &m_sampler) == VK_SUCCESS;
    }

    bool createDynamicBuffers() {
        const VkDeviceSize vbSize = UiGpuPass::kMaxVertices * sizeof(UiVertex);
        const VkDeviceSize ibSize = UiGpuPass::kMaxIndices * sizeof(uint32_t);
        if (!createBuffer(vbSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          m_vb, m_vbMem))
            return false;
        if (!createBuffer(ibSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          m_ib, m_ibMem))
            return false;
        vkMapMemory(m_dev, m_vbMem, 0, vbSize, 0, &m_vbMapped);
        vkMapMemory(m_dev, m_ibMem, 0, ibSize, 0, &m_ibMapped);
        return true;
    }

    bool createPipeline(const uint32_t* vertSpv, size_t vertWords,
                        const uint32_t* fragSpv, size_t fragWords, uint32_t subpass) {
        VkShaderModule vertMod = VK_NULL_HANDLE, fragMod = VK_NULL_HANDLE;
        auto makeMod = [&](const uint32_t* code, size_t words, VkShaderModule& out) {
            VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            ci.codeSize = words * 4;
            ci.pCode = code;
            return vkCreateShaderModule(m_dev, &ci, nullptr, &out) == VK_SUCCESS;
        };
        if (!makeMod(vertSpv, vertWords, vertMod) || !makeMod(fragSpv, fragWords, fragMod))
            return false;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertMod;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragMod;
        stages[1].pName = "main";

        // UiVertex: xy, rgba, uv  → 8 floats
        VkVertexInputBindingDescription bind{};
        bind.binding = 0;
        bind.stride = sizeof(UiVertex);
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attrs[3]{};
        attrs[0].location = 0; attrs[0].binding = 0;
        attrs[0].format = VK_FORMAT_R32G32_SFLOAT; attrs[0].offset = offsetof(UiVertex, x);
        attrs[1].location = 1; attrs[1].binding = 0;
        attrs[1].format = VK_FORMAT_R32G32B32A32_SFLOAT; attrs[1].offset = offsetof(UiVertex, r);
        attrs[2].location = 2; attrs[2].binding = 0;
        attrs[2].format = VK_FORMAT_R32G32_SFLOAT; attrs[2].offset = offsetof(UiVertex, u);

        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &bind;
        vi.vertexAttributeDescriptionCount = 3;
        vi.pVertexAttributeDescriptions = attrs;

        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1;
        vp.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.f;

        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        // UI: no depth test/write

        VkPipelineColorBlendAttachmentState blend{};
        blend.blendEnable = VK_TRUE;
        blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.colorBlendOp = VK_BLEND_OP_ADD;
        blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alphaBlendOp = VK_BLEND_OP_ADD;
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1;
        cb.pAttachments = &blend;

        VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dynStates;

        VkPushConstantRange pcr{};
        pcr.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pcr.offset = 0;
        pcr.size = sizeof(float) * 2; // screenSize

        VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &m_descLayout;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &pcr;
        if (vkCreatePipelineLayout(m_dev, &plci, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            vkDestroyShaderModule(m_dev, vertMod, nullptr);
            vkDestroyShaderModule(m_dev, fragMod, nullptr);
            return false;
        }

        VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gp.stageCount = 2;
        gp.pStages = stages;
        gp.pVertexInputState = &vi;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState = &vp;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState = &ms;
        gp.pDepthStencilState = &ds;
        gp.pColorBlendState = &cb;
        gp.pDynamicState = &dyn;
        gp.layout = m_pipelineLayout;
        gp.renderPass = m_renderPass;
        gp.subpass = subpass;

        const VkResult r = vkCreateGraphicsPipelines(m_dev, VK_NULL_HANDLE, 1, &gp, nullptr, &m_pipeline);
        vkDestroyShaderModule(m_dev, vertMod, nullptr);
        vkDestroyShaderModule(m_dev, fragMod, nullptr);
        return r == VK_SUCCESS;
    }

    bool m_ok = false;
    VkPhysicalDevice m_phys = VK_NULL_HANDLE;
    VkDevice m_dev = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    uint32_t m_qfamily = 0;
    VkCommandBuffer m_cmd = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_descLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descSet = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;

    VkImage m_atlasImage = VK_NULL_HANDLE;
    VkDeviceMemory m_atlasMem = VK_NULL_HANDLE;
    VkImageView m_atlasView = VK_NULL_HANDLE;
    int m_atlasW = 0, m_atlasH = 0;

    VkBuffer m_vb = VK_NULL_HANDLE, m_ib = VK_NULL_HANDLE;
    VkDeviceMemory m_vbMem = VK_NULL_HANDLE, m_ibMem = VK_NULL_HANDLE;
    void* m_vbMapped = nullptr;
    void* m_ibMapped = nullptr;
    uint32_t m_vc = 0, m_ic = 0;

    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

} // namespace ui
} // namespace sim
} // namespace ks
