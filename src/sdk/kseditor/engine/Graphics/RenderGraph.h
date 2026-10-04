#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QVariant>
#include <QVector3D>
#include <QVector4D>
#include <QUuid>
#include <QQuaternion>
#include <QMatrix4x4>
#include <functional>
#include <vulkan/vulkan.h>

namespace ks {

class VulkanRenderer;

// Render graph for frame composition
class RenderGraph : public QObject
{
    Q_OBJECT

public:
    struct Resource {
        enum class Type { Buffer, Image, External };
        Type type = Type::Image;
        QString name;
        
        // For images
        VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
        VkExtent2D extent = {0, 0};
        uint32_t mipLevels = 1;
        uint32_t arrayLayers = 1;
        VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        
        // For buffers
        VkDeviceSize size = 0;
        VkBufferUsageFlags bufferUsage = 0;
        
        // External resource (swapchain image, etc.)
        VkImage externalImage = VK_NULL_HANDLE;
        VkImageView externalView = VK_NULL_HANDLE;
        
        // Internal image handles (populated during resource creation)
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        
        bool persistent = false;
    };

    struct Pass {
        QString name;
        QVector<QString> inputs;      // Resource names read
        QVector<QString> outputs;     // Resource names written
        QVector<QString> resolves;    // Resolve targets
        QString depthStencil;         // Depth/stencil attachment
        
        // Pass execution callback
        std::function<void(VkCommandBuffer, const QMap<QString, Resource*>&)> execute;
        
        // Optional: compute pass
        bool isCompute = false;
        VkPipeline computePipeline = VK_NULL_HANDLE;
        VkPipelineLayout computeLayout = VK_NULL_HANDLE;
        QVector<VkDescriptorSet> computeDescriptorSets;
        VkExtent3D computeDispatchSize = {1, 1, 1};
    };

    struct FrameData {
        QMap<QString, Resource> resources;
        QVector<Pass> passes;
        QMap<QString, VkFramebuffer> framebuffers;
        QMap<QString, VkImageView> resourceViews;
    };

    explicit RenderGraph(VulkanRenderer* renderer, QObject* parent = nullptr);
    ~RenderGraph();

    void addResource(const Resource& resource);
    void addPass(const Pass& pass);
    
    // Build and compile the render graph
    bool compile();
    
    // Execute the render graph
    void execute(VkCommandBuffer cmdBuffer);
    
    // Get compiled frame data
    const FrameData& getFrameData() const { return m_frameData; }
    FrameData& getFrameData() { return m_frameData; }

    // Resource access
    Resource* getResource(const QString& name);
    const Resource* getResource(const QString& name) const;
    
    // Convenience: add common render targets
    void addColorTarget(const QString& name, VkFormat format, VkExtent2D extent,
                        VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    void addDepthTarget(const QString& name, VkExtent2D extent, VkFormat format = VK_FORMAT_D32_SFLOAT);
    void addSwapchainTarget(const QString& name, VkImage image, VkImageView view, VkFormat format, VkExtent2D extent);

    void clear();
    
    // Graph visualization/debug
    QJsonObject toJson() const;

signals:
    void compiled();
    void error(const QString& message);

private:
    void buildExecutionOrder();
    void allocateResources();
    void createFramebuffers();
    void createResourceViews();
    void computeResourceLifetimes();
    
    VulkanRenderer* m_renderer = nullptr;
    FrameData m_frameData;
    
    // Dependency tracking
    QMap<QString, QVector<QString>> m_resourceProducers;
    QMap<QString, QVector<QString>> m_resourceConsumers;
    QVector<QString> m_executionOrder;
    bool m_compiled = false;
};

// PBR Material system
class PBRMaterial : public QObject
{
    Q_OBJECT

public:
    enum class AlphaMode { Opaque, Mask, Blend };
    enum class Workflow { MetallicRoughness, SpecularGlossiness };

    struct TextureInfo {
        QString name;
        VkImageView view = VK_NULL_HANDLE;
        VkSampler sampler = VK_NULL_HANDLE;
        VkDescriptorImageInfo descriptorInfo{};
        float scale = 1.0f;
        bool enabled = true;
    };

    struct PBRParameters {
        // Base color / albedo
        QVector4D baseColorFactor = {1, 1, 1, 1};
        TextureInfo baseColorTexture;
        
        // Metallic/Roughness workflow
        float metallicFactor = 1.0f;
        float roughnessFactor = 1.0f;
        TextureInfo metallicRoughnessTexture;
        
        // Specular/Glossiness workflow
        QVector3D specularFactor = {1, 1, 1};
        float glossinessFactor = 1.0f;
        TextureInfo specularGlossinessTexture;
        
        // Normal map
        float normalScale = 1.0f;
        TextureInfo normalTexture;
        
        // Occlusion
        float occlusionStrength = 1.0f;
        TextureInfo occlusionTexture;
        
        // Emissive
        QVector3D emissiveFactor = {0, 0, 0};
        TextureInfo emissiveTexture;
        
        // Clearcoat (for car paint)
        float clearcoatFactor = 0.0f;
        float clearcoatRoughness = 0.0f;
        TextureInfo clearcoatTexture;
        TextureInfo clearcoatRoughnessTexture;
        TextureInfo clearcoatNormalTexture;
        float clearcoatNormalScale = 1.0f;
        
        // Transmission (for glass)
        float transmissionFactor = 0.0f;
        TextureInfo transmissionTexture;
        float ior = 1.5f;
        
        // Sheen (for fabric)
        QVector3D sheenColorFactor = {0, 0, 0};
        float sheenRoughnessFactor = 0.0f;
        TextureInfo sheenColorTexture;
        TextureInfo sheenRoughnessTexture;
        
        // Anisotropy
        float anisotropyStrength = 0.0f;
        float anisotropyRotation = 0.0f;
        TextureInfo anisotropyTexture;
        
        // General
        AlphaMode alphaMode = AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
        Workflow workflow = Workflow::MetallicRoughness;
    };

    explicit PBRMaterial(QObject* parent = nullptr);
    ~PBRMaterial();

    void setParameters(const PBRParameters& params);
    PBRParameters& parameters() { return m_params; }
    const PBRParameters& parameters() const { return m_params; }

    // Pipeline creation
    VkPipeline createPipeline(VkDevice device, VkPipelineLayout layout, VkRenderPass renderPass,
                              const QVector<VkVertexInputBindingDescription>& bindings,
                              const QVector<VkVertexInputAttributeDescription>& attrs);
    void setShaderModules(VkShaderModule vert, VkShaderModule frag) { m_vertModule = vert; m_fragModule = frag; }
    
    // Descriptor set management
    VkDescriptorSetLayout createDescriptorSetLayout(VkDevice device);
    void updateDescriptorSet(VkDevice device, VkDescriptorSet set, VkDescriptorPool pool);

    // Texture loading helpers
    bool loadTexture(VkDevice device, VkPhysicalDevice physDev, VkQueue queue, VkCommandPool pool,
                     const QString& name, const QString& filePath);
    
    static QVector<VkDescriptorSetLayoutBinding> getStandardBindings();
    static QVector<VkDescriptorSetLayoutBinding> getTextureBindings(uint32_t baseBinding);

signals:
    void parametersChanged();
    void pipelineCreated(VkPipeline pipeline);

private:
    PBRParameters m_params;
    VkDevice m_device = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorLayout = VK_NULL_HANDLE;
    VkShaderModule m_vertModule = VK_NULL_HANDLE;
    VkShaderModule m_fragModule = VK_NULL_HANDLE;
    QMap<QString, TextureInfo> m_textures;
    
    VkSampler createSampler(VkDevice device, VkFilter filter = VK_FILTER_LINEAR,
                           VkSamplerAddressMode mode = VK_SAMPLER_ADDRESS_MODE_REPEAT);
};

// PBR Pipeline factory
class PBRPipelineFactory : public QObject
{
    Q_OBJECT

public:
    struct PipelineConfig {
        bool enableDepthTest = true;
        bool enableDepthWrite = true;
        VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
        VkFrontFace frontFace = VK_FRONT_FACE_CLOCKWISE;
        VkPolygonMode polygonMode = VK_POLYGON_MODE_FILL;
        VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
        bool enableBlend = false;
        VkBlendFactor srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        VkBlendFactor dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        VkBlendOp colorBlendOp = VK_BLEND_OP_ADD;
        bool enableAlphaToCoverage = false;
    };

    explicit PBRPipelineFactory(VulkanRenderer* renderer, QObject* parent = nullptr);
    
    VkPipeline createStandardPBR(VkDevice device, VkRenderPass renderPass, VkPipelineLayout layout,
                                 const PipelineConfig& config = {});
    VkPipeline createUnlit(VkDevice device, VkRenderPass renderPass, VkPipelineLayout layout);
    VkPipeline createSkybox(VkDevice device, VkRenderPass renderPass, VkPipelineLayout layout);
    VkPipeline createShadow(VkDevice device, VkRenderPass renderPass, VkPipelineLayout layout);
    VkPipeline createWireframe(VkDevice device, VkRenderPass renderPass, VkPipelineLayout layout);
    VkPipeline createCustom(VkDevice device, VkRenderPass renderPass, VkPipelineLayout layout,
                            const QString& vertShader, const QString& fragShader,
                            const PipelineConfig& config = {});

    VkPipelineLayout createStandardLayout(VkDevice device, uint32_t descriptorSetCount = 3);
    VkDescriptorSetLayout createMaterialLayout(VkDevice device);
    VkDescriptorSetLayout createCameraLayout(VkDevice device);
    VkDescriptorSetLayout createLightLayout(VkDevice device);
    VkDescriptorSetLayout createTextureArrayLayout(VkDevice device, uint32_t textureCount);

private:
    VulkanRenderer* m_renderer = nullptr;
    QMap<QString, VkPipeline> m_cachedPipelines;
    QMap<QString, VkPipelineLayout> m_cachedLayouts;
    QMap<QString, VkDescriptorSetLayout> m_cachedDescriptorLayouts;
    
    VkPipeline createPipeline(VkDevice device, VkRenderPass renderPass, VkPipelineLayout layout,
                              const QString& vertShader, const QString& fragShader,
                              const PipelineConfig& config, bool hasDepth = true);
};

// Scene graph with ECS-style components
class RenderSceneGraph : public QObject
{
    Q_OBJECT

public:
    struct Entity {
        QUuid id;
        QString name;
        QString tag;
        bool active = true;
        bool visible = true;
        QUuid parent;
        QVector<QUuid> children;
        QVector<QUuid> components;
    };

    struct TransformComponent {
        QUuid entityId;
        QVector3D position = {0, 0, 0};
        QQuaternion rotation = QQuaternion();
        QVector3D scale = {1, 1, 1};
        QMatrix4x4 localMatrix;
        QMatrix4x4 worldMatrix;
        QUuid parent = QUuid();
        QVector<QUuid> children;
        bool dirty = true;
    };

    struct MeshComponent {
        QUuid entityId;
        QString meshName;
        QString materialName;
        bool castShadows = true;
        bool receiveShadows = true;
        int renderLayer = 0;
    };

    struct LightComponent {
        enum class Type { Directional, Point, Spot, Area };
        QUuid entityId;
        Type type = Type::Directional;
        QVector3D color = {1, 1, 1};
        float intensity = 1.0f;
        float range = 100.0f;
        float innerAngle = 30.0f;  // degrees
        float outerAngle = 45.0f;  // degrees
        bool castShadows = false;
        float shadowBias = 0.005f;
        float shadowNormalBias = 0.0f;
    };

    struct CameraComponent {
        QUuid entityId;
        float fov = 60.0f;
        float aspect = 16.0f/9.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        bool orthographic = false;
        float orthoSize = 10.0f;
        int renderLayerMask = 0xFFFFFFFF;
    };

    struct EnvironmentComponent {
        QUuid entityId;
        QVector3D ambientColor = {0.02f, 0.02f, 0.03f};
        float ambientIntensity = 1.0f;
        QString skyboxTexture;
        QString irradianceMap;
        QString prefilterMap;
        QString brdfLut;
        float exposure = 1.0f;
    };

    explicit RenderSceneGraph(QObject* parent = nullptr);
    ~RenderSceneGraph();

    // Entity management
    QUuid createEntity(const QString& name = QString());
    void destroyEntity(QUuid id);
    Entity* getEntity(QUuid id);
    const Entity* getEntity(QUuid id) const;
    QVector<Entity*> getEntitiesByTag(const QString& tag);
    QVector<Entity*> getAllEntities() const;

    // Transform hierarchy
    void setParent(QUuid child, QUuid parent);
    void updateTransforms();
    QMatrix4x4 getWorldTransform(QUuid id) const;

    // Component access
    template<typename T>
    T* addComponent(QUuid entityId) {
        static_assert(std::is_same<T, TransformComponent>::value ||
                     std::is_same<T, MeshComponent>::value ||
                     std::is_same<T, LightComponent>::value ||
                     std::is_same<T, CameraComponent>::value ||
                     std::is_same<T, EnvironmentComponent>::value,
                     "Invalid component type");
        
        T* comp = new T();
        comp->entityId = entityId;
        QUuid compId = QUuid::createUuid();
        
        // Store in appropriate map
        if constexpr (std::is_same<T, TransformComponent>::value) {
            m_transforms[compId] = comp;
        } else if constexpr (std::is_same<T, MeshComponent>::value) {
            m_meshes[compId] = comp;
        } else if constexpr (std::is_same<T, LightComponent>::value) {
            m_lights[compId] = comp;
        } else if constexpr (std::is_same<T, CameraComponent>::value) {
            m_cameras[compId] = comp;
        } else if constexpr (std::is_same<T, EnvironmentComponent>::value) {
            m_environment = comp;
        }
        
        // Add to entity
        if (auto* entity = getEntity(entityId)) {
            entity->components.append(compId);
        }
        
        return comp;
    }

    template<typename T>
    T* getComponent(QUuid entityId) {
        if (auto* entity = getEntity(entityId)) {
            for (const auto& compId : entity->components) {
                if constexpr (std::is_same<T, TransformComponent>::value) {
                    if (m_transforms.contains(compId)) return m_transforms[compId];
                } else if constexpr (std::is_same<T, MeshComponent>::value) {
                    if (m_meshes.contains(compId)) return m_meshes[compId];
                } else if constexpr (std::is_same<T, LightComponent>::value) {
                    if (m_lights.contains(compId)) return m_lights[compId];
                } else if constexpr (std::is_same<T, CameraComponent>::value) {
                    if (m_cameras.contains(compId)) return m_cameras[compId];
                } else if constexpr (std::is_same<T, EnvironmentComponent>::value) {
                    return m_environment;
                }
            }
        }
        return nullptr;
    }

    template<typename T>
    const T* getComponent(QUuid entityId) const {
        if (auto* entity = getEntity(entityId)) {
            for (const auto& compId : entity->components) {
                if constexpr (std::is_same<T, TransformComponent>::value) {
                    if (m_transforms.contains(compId)) return m_transforms[compId];
                } else if constexpr (std::is_same<T, MeshComponent>::value) {
                    if (m_meshes.contains(compId)) return m_meshes[compId];
                } else if constexpr (std::is_same<T, LightComponent>::value) {
                    if (m_lights.contains(compId)) return m_lights[compId];
                } else if constexpr (std::is_same<T, CameraComponent>::value) {
                    if (m_cameras.contains(compId)) return m_cameras[compId];
                } else if constexpr (std::is_same<T, EnvironmentComponent>::value) {
                    return m_environment;
                }
            }
        }
        return nullptr;
    }

    template<typename T>
    void removeComponent(QUuid entityId) {
        if (auto* entity = getEntity(entityId)) {
            for (auto it = entity->components.begin(); it != entity->components.end(); ) {
                QUuid compId = *it;
                bool removed = false;
                if constexpr (std::is_same<T, TransformComponent>::value) {
                    if (m_transforms.contains(compId)) {
                        delete m_transforms.take(compId);
                        removed = true;
                    }
                } else if constexpr (std::is_same<T, MeshComponent>::value) {
                    if (m_meshes.contains(compId)) {
                        delete m_meshes.take(compId);
                        removed = true;
                    }
                } else if constexpr (std::is_same<T, LightComponent>::value) {
                    if (m_lights.contains(compId)) {
                        delete m_lights.take(compId);
                        removed = true;
                    }
                } else if constexpr (std::is_same<T, CameraComponent>::value) {
                    if (m_cameras.contains(compId)) {
                        delete m_cameras.take(compId);
                        removed = true;
                    }
                } else if constexpr (std::is_same<T, EnvironmentComponent>::value) {
                    if (m_environment) {
                        delete m_environment;
                        m_environment = nullptr;
                        removed = true;
                    }
                }
                if (removed) {
                    it = entity->components.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }

    // Clear all entities and components
    void clear();

    // Scene queries
    QVector<QUuid> getEntitiesWithMesh() const;
    QVector<QUuid> getEntitiesWithLight() const;
    CameraComponent* getMainCamera() const;
    EnvironmentComponent* getEnvironment() const { return m_environment; }

    // Serialization
    QJsonObject serialize() const;
    bool deserialize(const QJsonObject& data);

    // Recursive transform update (not a signal; called internally)
    void updateTransformRecursive(Entity* entity);

signals:
    void entityCreated(QUuid id);
    void entityDestroyed(QUuid id);
    void componentAdded(QUuid entityId, QUuid compId);
    void componentRemoved(QUuid entityId, QUuid compId);

private:
    QMap<QUuid, Entity*> m_entities;
    QMap<QUuid, TransformComponent*> m_transforms;
    QMap<QUuid, MeshComponent*> m_meshes;
    QMap<QUuid, LightComponent*> m_lights;
    QMap<QUuid, CameraComponent*> m_cameras;
    EnvironmentComponent* m_environment = nullptr;
};

} // namespace ks