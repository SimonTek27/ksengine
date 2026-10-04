#include "RenderOptimizer.h"
#include "sdk/kseditor/engine/Graphics/SceneObject.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"
#include <QtMath>
#include <algorithm>

namespace ks {

RenderOptimizer::RenderOptimizer()
	: m_config()
{
}

RenderOptimizer::~RenderOptimizer() {
}

void RenderOptimizer::setConfig(const RenderOptimizationConfig& config) {
	m_config = config;
}

void RenderOptimizer::updateFrame(
	const QVector3D& cameraPosition,
	const QVector3D& cameraDirection,
	float cameraFOV,
	float aspectRatio)
{
	m_frustum.position = cameraPosition;
	m_frustum.direction = cameraDirection;
	m_frustum.fov = cameraFOV;
	m_frustum.aspect = aspectRatio;

	m_stats.visibleObjectCount = 0;
	m_stats.culledObjectCount = 0;
	m_stats.batchCount = 0;
	m_stats.drawCallCount = 0;
}

bool RenderOptimizer::shouldRenderObject(
	int objectId,
	const QVector3D& objectPosition,
	float objectRadius) const
{
	if (!m_config.enableFrustumCulling) {
		return true;
	}

	return isSphereInFrustum(objectPosition, objectRadius);
}

int RenderOptimizer::calculateLODLevel(
	const QVector3D& objectPosition,
	const QVector3D& cameraPosition) const
{
	if (!m_config.enableLOD) {
		return 0; // Massima dettagli
	}

	float distance = (objectPosition - cameraPosition).length();

	if (distance < m_config.lodDistance0) return 0; // Massima dettagli
	if (distance < m_config.lodDistance1) return 1; // Medium
	if (distance < m_config.lodDistance2) return 2; // Low
	return 3; // Minimale/Hidden
}

QVector<MeshBatch> RenderOptimizer::optimizeBatches(const QVector<SceneObject*>& objects) {
	QVector<MeshBatch> batches;

	if (!m_config.enableBatching) {
		// Nessun batching: un batch per oggetto
		m_stats.drawCallCount = objects.size();
		return batches;
	}

	// Raggruppa per materiale
	QMap<QString, MeshBatch> batchMap;

	for (const auto& obj : objects) {
		if (!obj) continue;

		QString matId = obj->mesh() ? obj->name() : QString("default_%1").arg(obj->id());

		if (!batchMap.contains(matId)) {
			MeshBatch batch;
			batch.materialId = matId;
			batchMap[matId] = batch;
		}

		MeshBatch& batch = batchMap[matId];
		batch.objectIds.append(obj->id());
		if (obj->mesh()) {
			batch.vertexCount += obj->mesh()->geometry().vertices.size();
			batch.triangleCount += obj->mesh()->geometry().indices.size() / 3;
		}
	}

	// Converti map in vector
	for (const auto& batch : batchMap.values()) {
		batches.append(batch);
	}

	m_stats.batchCount = batches.size();
	m_stats.drawCallCount = batches.size();

	return batches;
}

void RenderOptimizer::invalidateCache(int objectId) {
	m_cache.remove(objectId);
}

void RenderOptimizer::invalidateAllCaches() {
	m_cache.clear();
}

bool RenderOptimizer::isSphereInFrustum(const QVector3D& center, float radius) const {
	QVector3D forward = m_frustum.direction.normalized();
	QVector3D right = QVector3D::crossProduct(forward, QVector3D(0, 1, 0)).normalized();
	if (right.lengthSquared() < 0.001f) right = QVector3D::crossProduct(forward, QVector3D(1, 0, 0)).normalized();
	QVector3D up = QVector3D::crossProduct(right, forward).normalized();

	float halfFovRad = qDegreesToRadians(m_frustum.fov * 0.5f);
	float tanHalfFov = qTan(halfFovRad);
	float halfHeightNear = m_frustum.nearPlane * tanHalfFov;
	float halfWidthNear = halfHeightNear * m_frustum.aspect;

	QVector3D nearCenter = m_frustum.position + forward * m_frustum.nearPlane;
	QVector3D farCenter = m_frustum.position + forward * m_frustum.farPlane;

	struct Plane { QVector3D normal; float d; };
	auto planeDist = [](const QVector3D& n, const QVector3D& p) {
		return QVector3D::dotProduct(n, p);
	};

	Plane planes[6];

	// Near plane
	planes[0].normal = forward;
	planes[0].d = planeDist(forward, nearCenter);

	// Far plane
	planes[1].normal = -forward;
	planes[1].d = planeDist(-forward, farCenter);

	// Left plane
	QVector3D leftEdge = nearCenter - right * halfWidthNear;
	QVector3D leftDir = (leftEdge - m_frustum.position).normalized();
	planes[2].normal = QVector3D::crossProduct(up, leftDir).normalized();
	planes[2].d = planeDist(planes[2].normal, m_frustum.position);

	// Right plane
	QVector3D rightEdge = nearCenter + right * halfWidthNear;
	QVector3D rightDir = (rightEdge - m_frustum.position).normalized();
	planes[3].normal = QVector3D::crossProduct(rightDir, up).normalized();
	planes[3].d = planeDist(planes[3].normal, m_frustum.position);

	// Top plane
	QVector3D topEdge = nearCenter + up * halfHeightNear;
	QVector3D topDir = (topEdge - m_frustum.position).normalized();
	planes[4].normal = QVector3D::crossProduct(right, topDir).normalized();
	planes[4].d = planeDist(planes[4].normal, m_frustum.position);

	// Bottom plane
	QVector3D bottomEdge = nearCenter - up * halfHeightNear;
	QVector3D bottomDir = (bottomEdge - m_frustum.position).normalized();
	planes[5].normal = QVector3D::crossProduct(bottomDir, right).normalized();
	planes[5].d = planeDist(planes[5].normal, m_frustum.position);

	for (const auto& plane : planes) {
		float dist = QVector3D::dotProduct(plane.normal, center) + plane.d;
		if (dist < -radius) return false;
	}

	return true;
}

// ============================================================================
// MeshGeometryCache Implementation
// ============================================================================

void MeshGeometryCache::setCacheEntry(int objectId, const CacheEntry& entry) {
	if (m_cache.contains(objectId)) {
		m_totalMemoryUsage -= m_cache[objectId].vertexData.size() * sizeof(float);
		m_totalMemoryUsage -= m_cache[objectId].indexData.size() * sizeof(uint32_t);
	}

	m_cache[objectId] = entry;
	m_totalMemoryUsage += entry.vertexData.size() * sizeof(float);
	m_totalMemoryUsage += entry.indexData.size() * sizeof(uint32_t);
}

bool MeshGeometryCache::getCacheEntry(int objectId, CacheEntry& outEntry) const {
	if (m_cache.contains(objectId) && m_cache[objectId].isValid) {
		outEntry = m_cache[objectId];
		return true;
	}
	return false;
}

bool MeshGeometryCache::isCacheValid(int objectId) const {
	if (m_cache.contains(objectId)) {
		return m_cache[objectId].isValid && !m_cache[objectId].isDirty;
	}
	return false;
}

void MeshGeometryCache::invalidateEntry(int objectId) {
	if (m_cache.contains(objectId)) {
		m_cache[objectId].isDirty = true;
	}
}

void MeshGeometryCache::invalidateAll() {
	for (auto& entry : m_cache) {
		entry.isDirty = true;
	}
}

int MeshGeometryCache::getCacheMemoryUsage() const {
	return m_totalMemoryUsage / (1024 * 1024); // In MB
}

void MeshGeometryCache::limitCacheSize(int maxMemoryMB) {
	int maxBytes = maxMemoryMB * 1024 * 1024;

	while (m_totalMemoryUsage > maxBytes && !m_cache.isEmpty()) {
		// Remove oldest entry (simple FIFO)
		auto it = m_cache.begin();
		m_totalMemoryUsage -= it->vertexData.size() * sizeof(float);
		m_totalMemoryUsage -= it->indexData.size() * sizeof(uint32_t);
		m_cache.erase(it);
	}
}

} // namespace ks
