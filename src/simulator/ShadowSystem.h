#pragma once

// Cascaded directional-light shadow mapping.
//
// Deliberately dependency-free: no Qt, no QObject, no QString, no QMap.
// SimulatorApp.cpp (src/simulator/SimulatorApp.cpp) is a native Win32 +
// raw-Vulkan executable that must start and run without Qt present — but it
// currently instantiates ks::VulkanRenderer, whose headers (VulkanRenderer.h,
// RenderSystem.h) pull in QObject/QVulkanWindow/QString/QMap. That is a
// pre-existing problem this file does not fix (see the note at the end of
// ShadowSystem.cpp); what this file *does* do is add a real, working,
// genuinely Qt-free shadow system so that piece of the "gap with CryEngine"
// doesn't make the Qt dependency any worse — SimulatorApp can drive shadows
// entirely through this class using only raw Vulkan handles it already has
// access to via VulkanRenderer::physicalDevice()/device()/graphicsQueue()/
// commandPool().

#include <vulkan/vulkan.h>
#include <string>
#include <array>
#include "MathTypes.h"

namespace ks::sim {

struct ShadowCascade {
    mat4 viewProj;
    float splitDepth = 0.0f;   // view-space depth of this cascade's far edge
};

class CascadedShadowMap {
public:
    static constexpr int kCascadeCount = 3;

    CascadedShadowMap() = default;
    ~CascadedShadowMap();
    CascadedShadowMap(const CascadedShadowMap&) = delete;
    CascadedShadowMap& operator=(const CascadedShadowMap&) = delete;

    // shaderDir must contain precompiled ksShadow.vert.spv / ksShadow.frag.spv
    // (this class never shells out to a shader compiler at runtime — that
    // would itself be a runtime dependency SimulatorApp shouldn't have).
    bool initialize(VkPhysicalDevice physicalDevice, VkDevice device,
                    VkCommandPool commandPool, VkQueue graphicsQueue,
                    const std::string& shaderDir, uint32_t resolution = 2048);
    void shutdown();
    bool isInitialized() const { return m_device != VK_NULL_HANDLE; }

    // Recomputes cascade splits and light-space matrices for the current
    // camera + sun direction. Call once per frame before rendering any
    // cascade (practical split scheme + frustum-fitted ortho projections,
    // the same approach used by CryEngine/most modern engines).
    void update(const mat4& camView, const mat4& camProj, float camNear, float camFar, vec3 sunDirectionWS);

    // Depth-only render pass for one cascade. Caller draws scene geometry
    // between begin/end, calling pushLightSpaceMatrix() per mesh (matches
    // ksShadow.vert's push_constant block: {mat4 lightSpaceMatrix; mat4 modelMatrix}).
    void beginCascadePass(VkCommandBuffer cmd, int cascadeIndex) const;
    void endCascadePass(VkCommandBuffer cmd) const;
    void pushLightSpaceMatrix(VkCommandBuffer cmd, int cascadeIndex, const mat4& modelMatrix) const;

    // Live stack size (1..kCascadeCount). video.shadowCascades can shrink it:
    // one cascade is a third of the shadow cost, three is the old default.
    int cascadeCount() const { return m_cascadeCount; }
    // Both recreate the attachments once and keep pipeline and render pass.
    bool setCascadeCount(int count);
    bool setResolution(uint32_t resolution);
    const ShadowCascade& cascade(int i) const { return m_cascades[static_cast<size_t>(i)]; }
    VkPipeline pipeline() const { return m_pipeline; }
    VkPipelineLayout pipelineLayout() const { return m_pipelineLayout; }
    // Single view over all cascade layers, for sampling as sampler2DArray
    // in the lighting shader (pbr.frag).
    VkImageView arrayView() const { return m_arrayView; }
    VkSampler sampler() const { return m_sampler; }
    uint32_t resolution() const { return m_resolution; }

private:
    bool reinitialize(int cascades, uint32_t resolution);
    void applySplitFractions();

    std::string m_shaderDir;
    int m_cascadeCount = kCascadeCount;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    uint32_t m_resolution = 2048;

    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMemory = VK_NULL_HANDLE;
    VkImageView m_arrayView = VK_NULL_HANDLE;
    std::array<VkImageView, kCascadeCount> m_layerViews{};
    std::array<VkFramebuffer, kCascadeCount> m_framebuffers{};
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;

    // Shared transient hardware depth buffer used while rasterizing each
    // cascade (not sampled afterward; only the color-attachment "manual
    // depth" written by ksShadow.frag is).
    VkImage m_transientDepthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_transientDepthMemory = VK_NULL_HANDLE;
    VkImageView m_transientDepthView = VK_NULL_HANDLE;

    VkShaderModule m_vertModule = VK_NULL_HANDLE;
    VkShaderModule m_fragModule = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    std::array<ShadowCascade, kCascadeCount> m_cascades{};
    std::array<float, kCascadeCount> m_splitFractions{};
};

} // namespace ks::sim
