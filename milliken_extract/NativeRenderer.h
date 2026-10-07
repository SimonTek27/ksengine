#pragma once

/**
 * Qt-free native Vulkan renderer for SimulatorApp runtime.
 * Device/swapchain + command buffers; meshes via .nmsh bake (no KN5/QString at runtime).
 */

#include "GpuProfiler.h"
#include "UiRenderer.h"

#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// Minimal math if MathTypes.h is not available in this tree
#ifndef KS_MATH_TYPES_INCLUDED
struct vec3 {
    float x = 0, y = 0, z = 0;
    vec3() = default;
    vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};
struct mat4 {
    float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    float operator()(int row, int col) const { return m[col * 4 + row]; }
    mat4 inverse() const {
        // Identity fallback for header-only consumers; full inverse in .cpp
        return mat4{};
    }
};
#endif

namespace ks::sim {

class CascadedShadowMap;

struct NativeVertex {
    float px = 0, py = 0, pz = 0;
    float nx = 0, ny = 0, nz = 0;
    float u = 0, v = 0;
    float r = 1, g = 1, b = 1, a = 1;
};
static_assert(sizeof(NativeVertex) == sizeof(float) * 12, "NativeVertex packing");

struct NativeMesh {
    std::vector<NativeVertex> vertices;
    std::vector<uint32_t> indices;
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    VkBuffer indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory = VK_NULL_HANDLE;
};

struct DirectionalLight {
    vec3 direction{0.3f, -0.8f, 0.2f};
    vec3 color{1.0f, 0.95f, 0.9f};
    float intensity = 1.0f;
};

class NativeRenderer {
public:
    ~NativeRenderer();
    NativeRenderer() = default;
    NativeRenderer(const NativeRenderer&) = delete;
    NativeRenderer& operator=(const NativeRenderer&) = delete;

    bool createDevice(VkInstance instance, VkSurfaceKHR surface);
    bool createSwapChain(VkSurfaceKHR surface, uint32_t width, uint32_t height);
    bool recreateSwapChain(uint32_t width, uint32_t height);
    void attachShadowMap(CascadedShadowMap* shadowMap) { m_shadowMap = shadowMap; }

    bool isInitialized() const { return m_device != VK_NULL_HANDLE; }
    void shutdown();

    bool loadPipelines(const std::string& shaderDir);
    bool loadMeshFromFile(const std::string& name, const std::string& path);
    int loadMeshesFromManifest(const std::string& dir);
    void drawStaticScene();
    void setMesh(const std::string& name, const NativeMesh& mesh);
    void destroyMesh(const std::string& name);

    void setCamera(const mat4& view, const mat4& proj) {
        m_view = view;
        m_proj = proj;
        mat4 invView = view.inverse();
        m_camPosWS = vec3(invView(0, 3), invView(1, 3), invView(2, 3));
    }
    void setSun(const DirectionalLight& light) { m_sun = light; }

    bool beginFrame();
    void drawMesh(const std::string& name, const mat4& modelMatrix);
    void endFrame();

    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkDevice device() const { return m_device; }
    VkQueue graphicsQueue() const { return m_graphicsQueue; }
    VkCommandPool commandPool() const { return m_commandPool; }
    VkCommandBuffer currentCommandBuffer() const { return m_commandBuffer; }

    GpuProfiler& gpuProfiler() { return m_gpuProfiler; }
    const GpuProfiler& gpuProfiler() const { return m_gpuProfiler; }
    void setGpuProfilingEnabled(bool e) { m_gpuProfiler.setEnabled(e); }

    UiRenderer& uiRenderer() { return m_uiRenderer; }
    const UiRenderer& uiRenderer() const { return m_uiRenderer; }
    void drawUiOverlay();

private:
    bool createCommandPoolAndBuffer();
    bool createSyncObjects();
    bool createDepthResources(uint32_t width, uint32_t height);
    void destroySwapChain();
    void uploadMesh(NativeMesh& mesh);
    bool createFrameDescriptorResources();
    bool createDummyShadowTexture();
    void writeFrameDescriptorSet(VkImageView shadowView, VkSampler shadowSampler);

    GpuProfiler m_gpuProfiler;
    UiRenderer m_uiRenderer;

    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    uint32_t m_graphicsQueueFamily = 0;
    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;

    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
    VkFormat m_swapChainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D m_swapChainExtent{};
    std::vector<VkImage> m_swapChainImages;
    std::vector<VkImageView> m_swapChainImageViews;
    std::vector<VkFramebuffer> m_swapChainFramebuffers;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;

    VkImage m_depthImage = VK_NULL_HANDLE;
    VkDeviceMemory m_depthMemory = VK_NULL_HANDLE;
    VkImageView m_depthView = VK_NULL_HANDLE;

    VkSemaphore m_imageAvailable = VK_NULL_HANDLE;
    VkSemaphore m_renderFinished = VK_NULL_HANDLE;
    VkFence m_inFlightFence = VK_NULL_HANDLE;
    uint32_t m_currentImageIndex = 0;

    VkShaderModule m_vertModule = VK_NULL_HANDLE;
    VkShaderModule m_fragModule = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    VkDescriptorSetLayout m_frameSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_frameSet = VK_NULL_HANDLE;
    VkBuffer m_frameUBO = VK_NULL_HANDLE;
    VkDeviceMemory m_frameUBOMemory = VK_NULL_HANDLE;
    void* m_frameUBOMapped = nullptr;

    VkImage m_dummyShadowImage = VK_NULL_HANDLE;
    VkDeviceMemory m_dummyShadowMemory = VK_NULL_HANDLE;
    VkImageView m_dummyShadowView = VK_NULL_HANDLE;
    VkSampler m_dummyShadowSampler = VK_NULL_HANDLE;

    std::unordered_map<std::string, NativeMesh> m_meshes;
    std::vector<std::string> m_staticSceneMeshNames;

    struct QueuedDraw { std::string meshName; mat4 model; };
    std::vector<QueuedDraw> m_drawList;

    mat4 m_view;
    mat4 m_proj;
    vec3 m_camPosWS;
    DirectionalLight m_sun;
    CascadedShadowMap* m_shadowMap = nullptr;
};

} // namespace ks::sim
