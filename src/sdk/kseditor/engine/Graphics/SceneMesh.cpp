#include "SceneMesh.h"
#include "VulkanFunctions.h"
#include "VulkanRenderer.h"
#include "../mesh/MultiresLevel.h"
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <limits>
#include <cstring>

namespace ks {

SceneMesh::SceneMesh(QObject* parent) : QObject(parent)
{
}

SceneMesh::SceneMesh(const SceneMeshGeometry& geometry, QObject* parent) : QObject(parent)
{
    m_geometry = geometry;
}

SceneMesh::~SceneMesh()
{
}

void SceneMesh::setGeometry(const SceneMeshGeometry& geometry)
{
    m_geometry = geometry;
    emit geometryChanged();
}

bool SceneMesh::createBuffers(VkDevice device, VkPhysicalDevice physDev, VkQueue queue, VkCommandPool pool)
{
    if (m_geometry.vertices.isEmpty() || m_geometry.indices.isEmpty()) return false;

    destroyBuffers(device);

    VkDeviceSize vertexSize = static_cast<VkDeviceSize>(m_geometry.vertices.size() * sizeof(SceneVertex));
    VkDeviceSize indexSize = static_cast<VkDeviceSize>(m_geometry.indices.size() * sizeof(uint32_t));

    if (vertexSize == 0 || indexSize == 0) return false;

    if (g_vk.createBuffer && g_vk.allocateMemory && g_vk.bindBufferMemory) {
        VkBuffer stagingVertexBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingVertexMem = VK_NULL_HANDLE;
        VkBuffer stagingIndexBuf = VK_NULL_HANDLE;
        VkDeviceMemory stagingIndexMem = VK_NULL_HANDLE;

        VkBufferCreateInfo sbCi{};
        sbCi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        sbCi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        sbCi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        auto createStaging = [&](VkDeviceSize size, VkBuffer& buf, VkDeviceMemory& mem) {
            sbCi.size = size;
            if (g_vk.createBuffer(device, &sbCi, nullptr, &buf) != VK_SUCCESS) return false;
            VkMemoryRequirements smr;
            g_vk.getBufferMemoryRequirements(device, buf, &smr);
            VkMemoryAllocateInfo sai{};
            sai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            sai.allocationSize = smr.size;
            sai.memoryTypeIndex = VulkanRenderer::findMemoryType(physDev, smr.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (g_vk.allocateMemory(device, &sai, nullptr, &mem) != VK_SUCCESS) {
                g_vk.destroyBuffer(device, buf, nullptr); buf = VK_NULL_HANDLE;
                return false;
            }
            g_vk.bindBufferMemory(device, buf, mem, 0);
            return true;
        };

        auto fillStaging = [&](VkDeviceMemory mem, VkDeviceSize size, const void* data) {
            void* mapped = nullptr;
            if (g_vk.mapMemory(device, mem, 0, size, 0, &mapped) == VK_SUCCESS) {
                memcpy(mapped, data, static_cast<size_t>(size));
                g_vk.unmapMemory(device, mem);
            }
        };

        bool haveVertexStaging = createStaging(vertexSize, stagingVertexBuf, stagingVertexMem);
        bool haveIndexStaging = createStaging(indexSize, stagingIndexBuf, stagingIndexMem);

        if (haveVertexStaging)
            fillStaging(stagingVertexMem, vertexSize, m_geometry.vertices.constData());
        if (haveIndexStaging)
            fillStaging(stagingIndexMem, indexSize, m_geometry.indices.constData());

        auto createDeviceBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage,
                                       VkBuffer& buf, VkDeviceMemory& mem) {
            VkBufferCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            ci.size = size;
            ci.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (g_vk.createBuffer(device, &ci, nullptr, &buf) != VK_SUCCESS) return false;
            VkMemoryRequirements mr;
            g_vk.getBufferMemoryRequirements(device, buf, &mr);
            VkMemoryAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize = mr.size;
            ai.memoryTypeIndex = VulkanRenderer::findMemoryType(physDev, mr.memoryTypeBits,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (g_vk.allocateMemory(device, &ai, nullptr, &mem) != VK_SUCCESS) {
                g_vk.destroyBuffer(device, buf, nullptr); buf = VK_NULL_HANDLE;
                return false;
            }
            g_vk.bindBufferMemory(device, buf, mem, 0);
            return true;
        };

        bool haveVBuf = createDeviceBuffer(vertexSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                            m_vertexBuffer, m_vertexMemory);
        bool haveIBuf = createDeviceBuffer(indexSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                                            m_indexBuffer, m_indexMemory);

        if (haveVBuf && haveIBuf && haveVertexStaging && haveIndexStaging) {
            VkCommandBufferAllocateInfo cmdAi{};
            cmdAi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdAi.commandPool = pool;
            cmdAi.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdAi.commandBufferCount = 1;

            VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
            if (g_vk.allocateCommandBuffers(device, &cmdAi, &cmdBuf) == VK_SUCCESS) {
                VkCommandBufferBeginInfo bi{};
                bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

                if (g_vk.beginCommandBuffer(cmdBuf, &bi) == VK_SUCCESS) {
                    VkBufferCopy region{};
                    region.size = vertexSize;
                    g_vk.cmdCopyBuffer(cmdBuf, stagingVertexBuf, m_vertexBuffer, 1, &region);
                    region.size = indexSize;
                    region.srcOffset = 0;
                    region.dstOffset = 0;
                    g_vk.cmdCopyBuffer(cmdBuf, stagingIndexBuf, m_indexBuffer, 1, &region);
                    g_vk.endCommandBuffer(cmdBuf);

                    VkSubmitInfo si{};
                    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                    si.commandBufferCount = 1;
                    si.pCommandBuffers = &cmdBuf;
                    g_vk.queueSubmit(queue, 1, &si, VK_NULL_HANDLE);
                    g_vk.queueWaitIdle(queue);
                    g_vk.freeCommandBuffers(device, pool, 1, &cmdBuf);
                }
            }
        }

        if (stagingVertexBuf && stagingVertexMem) {
            g_vk.destroyBuffer(device, stagingVertexBuf, nullptr);
            g_vk.freeMemory(device, stagingVertexMem, nullptr);
        }
        if (stagingIndexBuf && stagingIndexMem) {
            g_vk.destroyBuffer(device, stagingIndexBuf, nullptr);
            g_vk.freeMemory(device, stagingIndexMem, nullptr);
        }
    }

    m_buffersValid = (m_vertexBuffer != VK_NULL_HANDLE && m_indexBuffer != VK_NULL_HANDLE);
    emit buffersCreated();
    return m_buffersValid;
}

void SceneMesh::destroyBuffers(VkDevice device)
{
    if (!device) return;
    if (g_vk.destroyBuffer) {
        if (m_vertexBuffer) { g_vk.destroyBuffer(device, m_vertexBuffer, nullptr); m_vertexBuffer = VK_NULL_HANDLE; }
        if (m_indexBuffer)  { g_vk.destroyBuffer(device, m_indexBuffer, nullptr); m_indexBuffer = VK_NULL_HANDLE; }
    }
    if (g_vk.freeMemory) {
        if (m_vertexMemory) { g_vk.freeMemory(device, m_vertexMemory, nullptr); m_vertexMemory = VK_NULL_HANDLE; }
        if (m_indexMemory)  { g_vk.freeMemory(device, m_indexMemory, nullptr); m_indexMemory = VK_NULL_HANDLE; }
    }
    m_buffersValid = false;
}

void SceneMesh::setMorphWeight(int targetIndex, float weight)
{
    if (targetIndex < 0 || targetIndex >= m_geometry.morphPositionDeltas.size()) return;
    if (targetIndex >= m_morphWeights.size())
        m_morphWeights.resize(targetIndex + 1);
    m_morphWeights[targetIndex] = qBound(0.0f, weight, 1.0f);
    emit geometryChanged();
}

MorphTargetEditor* SceneMesh::morphTargetEditor() const { return m_morphTargetEditor; }
void SceneMesh::setMorphTargetEditor(MorphTargetEditor* editor) { m_morphTargetEditor = editor; }

MultiresManager* SceneMesh::multiresManager() const { return m_multiresManager; }
void SceneMesh::setMultiresManager(MultiresManager* manager) { m_multiresManager = manager; }
int SceneMesh::multiresCurrentLevel() const
{
    if (m_multiresManager) return m_multiresManager->currentLevel();
    return 0;
}
int SceneMesh::multiresLevelCount() const
{
    if (m_multiresManager) return m_multiresManager->allLevels().size();
    return 0;
}
void SceneMesh::multiresSetCurrentLevel(int level)
{
    if (m_multiresManager) m_multiresManager->setCurrentLevel(level);
}
void SceneMesh::multiresAddLevel()
{
    if (m_multiresManager) m_multiresManager->addLevel();
}
void SceneMesh::multiresRemoveLevel()
{
    if (m_multiresManager) m_multiresManager->removeLevel();
}
void SceneMesh::multiresSubdivideCurrent()
{
    if (m_multiresManager) m_multiresManager->subdivideCurrentLevel();
}
void SceneMesh::multiresBakeCurrent()
{
    if (m_multiresManager) m_multiresManager->bakeCurrentLevel();
}

SceneSubMesh* SceneMesh::getSubMesh(const QString& name)
{
    for (auto& sub : m_geometry.subMeshes) {
        if (sub.name == name) return &sub;
    }
    return nullptr;
}

QJsonObject SceneMesh::toJson() const
{
    QJsonObject obj;
    obj["name"] = m_geometry.name;
    obj["vertexCount"] = m_geometry.vertices.size();
    obj["indexCount"] = m_geometry.indices.size();
    return obj;
}

SceneMesh* SceneMesh::fromJson(const QJsonObject& obj)
{
    Q_UNUSED(obj);
    return new SceneMesh();
}

void SceneMeshGeometry::computeBounds()
{
    if (vertices.empty()) {
        boundsMin = boundsMax = QVector3D();
        boundsRadius = 0.0f;
        return;
    }
    
    boundsMin = boundsMax = vertices[0].position;
    for (const auto& v : vertices) {
        boundsMin.setX(qMin(boundsMin.x(), v.position.x()));
        boundsMin.setY(qMin(boundsMin.y(), v.position.y()));
        boundsMin.setZ(qMin(boundsMin.z(), v.position.z()));
        boundsMax.setX(qMax(boundsMax.x(), v.position.x()));
        boundsMax.setY(qMax(boundsMax.y(), v.position.y()));
        boundsMax.setZ(qMax(boundsMax.z(), v.position.z()));
    }
    boundsRadius = (boundsMax - boundsMin).length() * 0.5f;
}

void SceneMeshGeometry::computeTangents()
{
    if (vertices.empty() || indices.empty()) return;

    // Initialize tangent arrays
    QVector<QVector3D> tan1(vertices.size(), QVector3D(0, 0, 0));
    QVector<QVector3D> tan2(vertices.size(), QVector3D(0, 0, 0));

    // Iterate triangles
    for (int i = 0; i + 2 < indices.size(); i += 3) {
        int i0 = indices[i];
        int i1 = indices[i + 1];
        int i2 = indices[i + 2];

        if (i0 >= vertices.size() || i1 >= vertices.size() || i2 >= vertices.size()) continue;

        const QVector3D& p0 = vertices[i0].position;
        const QVector3D& p1 = vertices[i1].position;
        const QVector3D& p2 = vertices[i2].position;

        const QVector2D& uv0 = vertices[i0].uv;
        const QVector2D& uv1 = vertices[i1].uv;
        const QVector2D& uv2 = vertices[i2].uv;

        QVector3D edge1 = p1 - p0;
        QVector3D edge2 = p2 - p0;
        QVector2D duv1 = uv1 - uv0;
        QVector2D duv2 = uv2 - uv0;

        float denom = duv1.x() * duv2.y() - duv1.y() * duv2.x();
        if (std::abs(denom) < 1e-8f) continue;

        float f = 1.0f / denom;

        QVector3D t = (edge1 * duv2.y() - edge2 * duv1.y()) * f;
        QVector3D b = (edge2 * duv1.x() - edge1 * duv2.x()) * f;

        tan1[i0] += t; tan1[i1] += t; tan1[i2] += t;
        tan2[i0] += b; tan2[i1] += b; tan2[i2] += b;
    }

    // Orthogonalize and compute final tangent
    for (int i = 0; i < vertices.size(); ++i) {
        const QVector3D& n = vertices[i].normal;
        const QVector3D& t = tan1[i];

        if (t.lengthSquared() < 1e-8f) {
            vertices[i].tangent = QVector3D(1, 0, 0);
            continue;
        }

        // Gram-Schmidt orthogonalize: tangent = normalize(t - n * dot(n, t))
        QVector3D tangent = (t - n * QVector3D::dotProduct(n, t)).normalized();

        // Compute handedness for bitangent
        float w = (QVector3D::dotProduct(QVector3D::crossProduct(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;
        Q_UNUSED(w);

        vertices[i].tangent = tangent;
    }
}

void SceneMeshGeometry::optimizeVertexCache()
{
    if (indices.size() < 6) return;

    // Simple vertex cache optimization using Forsyth algorithm approximation
    // Group triangles into cache-friendly batches
    const int CACHE_SIZE = 24;

    QVector<uint32_t> optimizedIndices;
    optimizedIndices.reserve(indices.size());

    QVector<bool> usedTriangle(indices.size() / 3, false);
    QVector<int> vertexScore(vertices.size(), 0);

    // Score vertices based on cache proximity
    auto scoreVertex = [&](int vi) -> int {
        return vertexScore[vi];
    };

    // Find best starting triangle
    int bestTri = 0;
    int bestScore = -1;
    for (int t = 0; t + 2 < indices.size(); t += 3) {
        int score = scoreVertex(indices[t]) + scoreVertex(indices[t + 1]) + scoreVertex(indices[t + 2]);
        if (score > bestScore) {
            bestScore = score;
            bestTri = t;
        }
    }

    // Process triangles in cache-friendly order
    QVector<int> cache;
    int currentTri = bestTri;

    for (int pass = 0; pass < indices.size() / 3; ++pass) {
        if (currentTri >= 0 && currentTri + 2 < indices.size()) {
            if (!usedTriangle[currentTri / 3]) {
                usedTriangle[currentTri / 3] = true;

                for (int v = 0; v < 3; ++v) {
                    int vi = indices[currentTri + v];
                    optimizedIndices.append(vi);

                    // Update cache
                    cache.removeAll(vi);
                    cache.prepend(vi);
                    if (cache.size() > CACHE_SIZE) cache.removeLast();

                    // Update vertex scores
                    vertexScore[vi] = CACHE_SIZE - cache.indexOf(vi);
                }
            }
        }

        // Find next best triangle (adjacent to last added vertices)
        currentTri = -1;
        int bestNewScore = -1;
        for (int t = 0; t + 2 < indices.size(); t += 3) {
            if (usedTriangle[t / 3]) continue;

            int score = 0;
            for (int v = 0; v < 3; ++v) {
                score += vertexScore[indices[t + v]];
            }

            // Bonus for cache hits
            for (int v = 0; v < 3; ++v) {
                if (cache.contains(indices[t + v])) score += 10;
            }

            if (score > bestNewScore) {
                bestNewScore = score;
                currentTri = t;
            }
        }
    }

    indices = optimizedIndices;
}

} // namespace ks
