#pragma once

#include "VulkanRenderer.h"
#include "ShaderParamRegistry.h"
#include "../Config/ConfigLoader.h"
#include "../Config/PPFilterPreset.h"
#include <QString>
#include <QMap>
#include <QVector>
#include <QMatrix4x4>
#include <QVector3D>
#include <QVector4D>
#include <QVector2D>
#include <memory>
#include <map>

namespace ks {

struct UniformBuffer {
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceMemory m_memory = VK_NULL_HANDLE;
    VkDeviceSize m_size = 0;
    void* m_mapped = nullptr;

    UniformBuffer(VkPhysicalDevice physicalDevice, VkDevice device) : m_physicalDevice(physicalDevice), m_device(device) {}
    ~UniformBuffer() { destroy(); }
    UniformBuffer(const UniformBuffer&) = delete;
    UniformBuffer& operator=(const UniformBuffer&) = delete;

    void create(VkDeviceSize size, VkBufferUsageFlags usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    void destroy();
    void* map();
    void unmap();
};

struct ShaderModule {
    VkDevice m_device = VK_NULL_HANDLE;
    VkShaderModule m_module = VK_NULL_HANDLE;
    QString m_glslSource;
    VkShaderStageFlagBits m_stage = VK_SHADER_STAGE_ALL;

    ShaderModule() = default;
    explicit ShaderModule(VkDevice device) : m_device(device) {}
    ~ShaderModule() { destroy(); }
    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;
    ShaderModule(ShaderModule&& other) noexcept : m_device(other.m_device), m_module(other.m_module), m_glslSource(std::move(other.m_glslSource)), m_stage(other.m_stage) { other.m_module = VK_NULL_HANDLE; }
    ShaderModule& operator=(ShaderModule&& other) noexcept { if (this != &other) { destroy(); m_device = other.m_device; m_module = other.m_module; m_glslSource = std::move(other.m_glslSource); m_stage = other.m_stage; other.m_module = VK_NULL_HANDLE; } return *this; }

    void createFromGLSL(const QString& glsl, VkShaderStageFlagBits stage);
    void createFromSPIRV(const QByteArray& spirv);
    void createFromFile(const QString& path);
    void destroy();
};

class VulkanShaderLoader {
public:
    struct CameraUBO {
        QMatrix4x4 view;
        QMatrix4x4 projection;
        QMatrix4x4 viewProjection;
        QVector4D cameraPosition;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
    };

    struct MaterialUBO {
        QVector4D albedo;
        float normalScale = 1.0f;
        float metallic = 0.0f;
        float roughness = 0.5f;
        QVector4D emissiveColor;
        float emissiveIntensity = 0.0f;
        QVector2D uvScale;
        QVector2D uvOffset;
    };

    struct LightingUBO {
        QVector3D ambientColor;
        float ambientIntensity = 1.0f;
        QVector3D sunDirection;
        float sunIntensity = 1.0f;
        QVector3D sunColor;
        int toneMapping = 0;
        float exposure = 1.0f;
    };

    struct MaterialParams {
        QVector4D albedo;
        float normalScale = 1.0f;
        float metallic = 0.0f;
        float roughness = 0.5f;
        QVector4D emissiveColor;
        float emissiveIntensity = 0.0f;
        QVector2D uvScale;
        QVector2D uvOffset;
    };

    struct LightingSettings {
        float ambientColor[3] = {};
        float ambientIntensity = 100.0f;
        float sunDirection[3] = {};
        float sunIntensity = 100.0f;
        float sunColor[3] = {};
        float hdrExposure = 1.0f;
        int toneMapping = 0;
    };

    struct GraphicsSettings {
        float mipLodBias = 0.0f;
        float shadowMapBias = 0.001f;
    };

    VulkanShaderLoader(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue);
    virtual ~VulkanShaderLoader();

    void applyLightingFromConfig(const LightingSettings& settings);
    void applyGraphicsFromConfig(const GraphicsSettings& settings);

    // Uniform buffer updates
    void updateCameraUBO(const QMatrix4x4& view, const QMatrix4x4& proj,
                         const QVector3D& cameraPos, float nearPlane, float farPlane);
    void updateMaterialUBO(const MaterialParams& params);
    void updateLightingUBO(const LightingUBO& lighting);

    // Shader loading
    bool loadShader(const QString& name, VkShaderStageFlagBits stage, const QString& path);

    // Pipeline creation
    bool createPipeline(const QString& name,
                        const QVector<VkPipelineShaderStageCreateInfo>& stages,
                        const VkPipelineVertexInputStateCreateInfo& vertexInput,
                        const VkPipelineInputAssemblyStateCreateInfo& inputAssembly,
                        const VkPipelineViewportStateCreateInfo& viewportState,
                        const VkPipelineRasterizationStateCreateInfo& rasterizer,
                        const VkPipelineMultisampleStateCreateInfo& multisampling,
                        const VkPipelineColorBlendStateCreateInfo& colorBlend,
                        const VkPipelineDepthStencilStateCreateInfo& depthStencil,
                        VkRenderPass renderPass);

    // Descriptor set management
    bool createDescriptorSetLayout(const QString& name);
    bool createDescriptorPool();
    bool allocateDescriptorSets(const QString& layoutName, const QString& setName);
    bool writeDescriptorSets(const QString& setName,
                             const QVector<VkDescriptorBufferInfo>& bufferInfos,
                             const QVector<VkDescriptorImageInfo>& imageInfos = {});
    bool createStorageBuffer(const QString& name, VkDeviceSize size, VkBufferUsageFlags extraUsage = 0);
    bool createSampler(const QString& name, VkFilter magFilter = VK_FILTER_LINEAR,
                       VkFilter minFilter = VK_FILTER_LINEAR,
                       VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);

    // Rendering commands
    void beginRenderPass(VkCommandBuffer cmdBuffer, VkRenderPass renderPass,
                         VkFramebuffer framebuffer, const QSize& size);
    void endRenderPass(VkCommandBuffer cmdBuffer);
    void bindPipeline(VkCommandBuffer cmdBuffer, const QString& name);
    void bindDescriptorSets(VkCommandBuffer cmdBuffer, const QString& pipelineName,
                            VkCommandBuffer* cmdBufferRef = nullptr);

    // Accessors
    VkDescriptorSetLayout descriptorSetLayout(const QString& name) const;
    VkPipeline pipeline(const QString& name) const;
    QStringList loadedShaderNames() const { return m_shaderModules.keys(); }
    QStringList loadedPipelineNames() const { return m_pipelines.keys(); }

protected:
    void cleanup();

    VkPhysicalDevice m_physicalDevice;
    VkDevice m_device;
    VkQueue m_graphicsQueue;

    // Uniform buffer objects
    std::unique_ptr<UniformBuffer> m_cameraUBO;
    std::unique_ptr<UniformBuffer> m_materialUBO;
    std::unique_ptr<UniformBuffer> m_lightingUBO;

    // Storage buffers
    std::map<QString, std::unique_ptr<UniformBuffer>> m_storageBuffers;

    // Shader modules map
    QMap<QString, ShaderModule*> m_shaderModules;

    // Pipelines and layouts
    QMap<QString, VkPipeline> m_pipelines;
    QMap<QString, VkPipelineLayout> m_pipelineLayouts;

    // Descriptor sets
    QMap<QString, VkDescriptorSetLayout> m_descriptorSetLayouts;
    QMap<QString, VkDescriptorSet> m_descriptorSets;
    QMap<QString, VkSampler> m_samplers;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

    // Pipeline cache
    VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;
};

// ── Post-process filter renderer ────────────────────────────────────────

class PPFilterRenderer : public VulkanShaderLoader {
public:
    PPFilterRenderer(VkPhysicalDevice physicalDevice, VkDevice device,
                     VkQueue graphicsQueue, VkCommandPool commandPool);
    ~PPFilterRenderer() override;

    bool initialize(const QString& shaderDir, VkRenderPass renderPass,
                    const VkPipelineVertexInputStateCreateInfo& vertexInput);
    void updateFromPreset(const PPFilterPreset& preset);

    struct PPParamsUBO {
        float exposure = 1.0f;
        float gamma = 1.2f;
        int toneMappingFunc = -1;
        float saturation = 0.95f;
        float brightness = 1.0f;
        float contrast = 1.0f;
        float vignetteStrength = 0.035f;
        float chromaticAberration = 0.005f;
        float pad[3] = {};
    };

private:
    VkCommandPool m_commandPool;
    std::unique_ptr<UniformBuffer> m_ppParamsUBO;
};

} // namespace ks
