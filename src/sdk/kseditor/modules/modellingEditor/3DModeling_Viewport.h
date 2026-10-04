#pragma once
// Common viewport types are now in core/mesh/Viewport3DSystem.h
// VulkanViewportRenderer remains here due to Vulkan dependencies.
#include "sdk/kseditor/engine/mesh/Viewport3DSystem.h"

#include <QVulkanWindow>
#include <QVulkanWindowRenderer>
#include "sdk/kseditor/engine/Graphics/SceneGraph.h"

namespace ks {

class VulkanViewportRenderer : public QVulkanWindowRenderer {
public:
    VulkanViewportRenderer(QVulkanWindow* w, ks::SceneGraph* scene);

    void initResources() override;
    void initSwapChainResources() override;
    void releaseSwapChainResources() override;
    void releaseResources() override;
    void startNextFrame() override;

    void setScene(ks::SceneGraph* scene) { m_scene = scene; }
    void updateViewportSize(int w, int h);

private:
    QVulkanWindow* m_window = nullptr;
    ks::SceneGraph* m_scene = nullptr;

    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkBuffer m_ubo = VK_NULL_HANDLE;
    VkDeviceMemory m_uboMemory = VK_NULL_HANDLE;
    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    struct PerMeshBuffer {
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexMemory = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
        QMatrix4x4 modelMatrix;
        QVector4D baseColor;
        float metallic = 0.0f;
        float roughness = 0.5f;
    };
    QVector<PerMeshBuffer> m_meshBuffers;
    int m_sceneObjectCount = 0;
    bool m_buffersDirty = true;

    struct CameraUBO {
        QMatrix4x4 view;
        QMatrix4x4 proj;
        QVector4D cameraPos;
    };

    struct MeshUBO {
        QMatrix4x4 model;
        QVector4D baseColor;
        float metallic;
        float roughness;
        float padding[2];
    };

    void createPipeline();
    void destroyPipeline();
    void updateCameraUBO(const QMatrix4x4& view, const QMatrix4x4& proj, const QVector3D& camPos);
    void createMeshBuffers();
    void destroyMeshBuffers();
    void markBuffersDirty();
};

} // namespace ks
