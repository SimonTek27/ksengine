#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QVector2D>
#include <QVector3D>
#include <QVector4D>
#include <QMatrix4x4>
#include <QUuid>
#include <vulkan/vulkan.h>

class MorphTargetEditor;
class MultiresManager;

namespace ks {

struct SceneVertex {
    QVector3D position;
    QVector3D normal;
    QVector2D uv;
    QVector4D color = {1, 1, 1, 1};
    QVector3D tangent;
    float weight = 1.0f;
    float mask = 0.0f;
    int boneIndex = -1;
    
    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription binding{};
        binding.binding = 0;
        binding.stride = sizeof(SceneVertex);
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return binding;
    }
    
    static QVector<VkVertexInputAttributeDescription> getAttributeDescriptions() {
        QVector<VkVertexInputAttributeDescription> attrs(8);
        attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SceneVertex, position)};
        attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SceneVertex, normal)};
        attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(SceneVertex, uv)};
        attrs[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(SceneVertex, color)};
        attrs[4] = {4, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(SceneVertex, tangent)};
        attrs[5] = {5, 0, VK_FORMAT_R32_SFLOAT, offsetof(SceneVertex, weight)};
        attrs[6] = {6, 0, VK_FORMAT_R32_SFLOAT, offsetof(SceneVertex, mask)};
        attrs[7] = {7, 0, VK_FORMAT_R32_SINT, offsetof(SceneVertex, boneIndex)};
        return attrs;
    }
};

struct SceneSubMesh {
    QString name;
    QString materialName;
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    uint32_t vertexOffset = 0;
    uint32_t vertexCount = 0;
    QVector3D boundsMin;
    QVector3D boundsMax;
};

struct SceneMeshGeometry {
    QString name;
    QVector<SceneVertex> vertices;
    QVector<uint32_t> indices;
    QVector<SceneSubMesh> subMeshes;
    QVector3D boundsMin;
    QVector3D boundsMax;
    float boundsRadius = 0.0f;
    
    // Skinning
    QVector<QMatrix4x4> inverseBindMatrices;
    QVector<QString> boneNames;
    
    // Morph targets
    QStringList morphTargetNames;
    QVector<QVector<QVector3D>> morphPositionDeltas;
    QVector<QVector<QVector3D>> morphNormalDeltas;
    
    // Multiresolution levels
    QVector<QVector<QVector3D>> multiresVertices; // vertices per level
    QVector<QVector<int>> multiresFaces; // face indices per level
    QVector<int> multiresCurrentLevel; // current level index
    
    void computeBounds();
    void computeTangents();
    void optimizeVertexCache();
};

class SceneMesh : public QObject
{
    Q_OBJECT

public:
    explicit SceneMesh(QObject* parent = nullptr);
    explicit SceneMesh(const SceneMeshGeometry& geometry, QObject* parent = nullptr);
    ~SceneMesh() override;

    // Geometry
    const SceneMeshGeometry& geometry() const { return m_geometry; }
    SceneMeshGeometry& geometry() { return m_geometry; }
    void setGeometry(const SceneMeshGeometry& geometry);

    // Vulkan buffers
    VkBuffer vertexBuffer() const { return m_vertexBuffer; }
    VkDeviceMemory vertexMemory() const { return m_vertexMemory; }
    VkBuffer indexBuffer() const { return m_indexBuffer; }
    VkDeviceMemory indexMemory() const { return m_indexMemory; }
    uint32_t indexCount() const { return static_cast<uint32_t>(m_geometry.indices.size()); }
    uint32_t vertexCount() const { return static_cast<uint32_t>(m_geometry.vertices.size()); }

    // Buffer creation
    bool createBuffers(VkDevice device, VkPhysicalDevice physDev, VkQueue queue, VkCommandPool pool);
    void destroyBuffers(VkDevice device);

    // Skinning
    const QVector<QMatrix4x4>& inverseBindMatrices() const { return m_geometry.inverseBindMatrices; }
    const QVector<QString>& boneNames() const { return m_geometry.boneNames; }
    bool hasSkinning() const { return !m_geometry.inverseBindMatrices.isEmpty(); }

    // Morph targets
    const QStringList& morphTargetNames() const { return m_geometry.morphTargetNames; }
    const QVector<QVector<QVector3D>>& morphPositionDeltas() const { return m_geometry.morphPositionDeltas; }
    const QVector<QVector<QVector3D>>& morphNormalDeltas() const { return m_geometry.morphNormalDeltas; }
    void setMorphWeight(int targetIndex, float weight);

    // Morph target editor
    MorphTargetEditor* morphTargetEditor() const;
    void setMorphTargetEditor(MorphTargetEditor* editor);

    // Multiresolution
    MultiresManager* multiresManager() const;
    void setMultiresManager(MultiresManager* manager);
    int multiresCurrentLevel() const;
    int multiresLevelCount() const;
    void multiresSetCurrentLevel(int level);
    void multiresAddLevel();
    void multiresRemoveLevel();
    void multiresSubdivideCurrent();
    void multiresBakeCurrent();

    // Bounds
    QVector3D boundsMin() const { return m_geometry.boundsMin; }
    QVector3D boundsMax() const { return m_geometry.boundsMax; }
    float boundsRadius() const { return m_geometry.boundsRadius; }

    // Sub-meshes
    const QVector<SceneSubMesh>& subMeshes() const { return m_geometry.subMeshes; }
    SceneSubMesh* getSubMesh(const QString& name);

    // Serialization
    QJsonObject toJson() const;
    static SceneMesh* fromJson(const QJsonObject& obj);

signals:
    void geometryChanged();
    void buffersCreated();
    void boundsChanged();

private:
    SceneMeshGeometry m_geometry;
    
    // Multiresolution
    MultiresManager* m_multiresManager = nullptr;
    
    // Morph target editor
    MorphTargetEditor* m_morphTargetEditor = nullptr;
    
    // Vulkan resources
    VkBuffer m_vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_vertexMemory = VK_NULL_HANDLE;
    VkBuffer m_indexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_indexMemory = VK_NULL_HANDLE;
    
    // Morph weights
    QVector<float> m_morphWeights;
    
    // Buffer state
    bool m_buffersValid = false;
};

} // namespace ks
