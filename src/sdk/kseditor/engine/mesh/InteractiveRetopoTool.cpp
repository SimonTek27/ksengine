#include "InteractiveRetopoTool.h"
#include <QElapsedTimer>
#include <QtMath>
#include <algorithm>

namespace ks {

InteractiveRetopoTool::InteractiveRetopoTool(QObject* parent)
    : QObject(parent)
{
}

InteractiveRetopoTool::~InteractiveRetopoTool() {
}

void InteractiveRetopoTool::setActive(bool active) {
    if (m_isActive != active) {
        m_isActive = active;
        if (!active) {
            m_selectedVertices.clear();
            m_selectedVertex = -1;
            m_snapPreviewPoints.clear();
            m_snapPreviewIndices.clear();
        }
        emit activeChanged();
    }
}

void InteractiveRetopoTool::setSnapRadius(float radius) {
    if (!qFuzzyCompare(m_snapRadius, radius)) {
        m_snapRadius = radius;
        emit snapRadiusChanged();
    }
}

void InteractiveRetopoTool::setSelectedVertex(int index) {
    if (m_selectedVertex != index) {
        m_selectedVertex = index;
        emit selectedVertexChanged();
    }
}

void InteractiveRetopoTool::setHighPolyMesh(const MeshData& mesh) {
    m_highPolyMesh = mesh;
    m_cacheDirty = true;
}

void InteractiveRetopoTool::setLowPolyMesh(MeshData* mesh) {
    m_lowPolyMesh = mesh;
}

QVector3D InteractiveRetopoTool::snapToHighPoly(const QVector3D& point) const {
    QVector3D bestPoint = point;
    float bestDist = m_snapRadius * m_snapRadius;

    for (int i = 0; i < m_highPolyMesh.vertices.size(); ++i) {
        const Vertex& v = m_highPolyMesh.vertices[i];
        float dist = (v.position - point).lengthSquared();
        if (dist < bestDist) {
            bestDist = dist;
            bestPoint = v.position;
        }
    }

    return bestPoint;
}

QVector3D InteractiveRetopoTool::projectOnHighPoly(const QVector3D& rayOrigin, const QVector3D& rayDir) const {
    float closestT = 1e30f;
    QVector3D closestPoint = rayOrigin + rayDir * 1000.0f;

    for (int i = 0; i < m_highPolyMesh.faces.size(); ++i) {
        const Face& face = m_highPolyMesh.faces[i];
        if (face.indices.size() < 3) continue;

        for (int j = 1; j + 1 < face.indices.size(); ++j) {
            const QVector3D& v0 = m_highPolyMesh.vertices[face.indices[0]].position;
            const QVector3D& v1 = m_highPolyMesh.vertices[face.indices[j]].position;
            const QVector3D& v2 = m_highPolyMesh.vertices[face.indices[j + 1]].position;

            float t;
            QVector3D bary;
            if (rayTriangleIntersect(rayOrigin, rayDir, v0, v1, v2, t, bary)) {
                if (t > 0 && t < closestT) {
                    closestT = t;
                    closestPoint = rayOrigin + rayDir * t;
                }
            }
        }
    }

    return closestPoint;
}

bool InteractiveRetopoTool::rayTriangleIntersect(const QVector3D& rayOrigin, const QVector3D& rayDir,
                                                 const QVector3D& v0, const QVector3D& v1, const QVector3D& v2,
                                                 float& t, QVector3D& baryCoords) const {
    const float EPSILON = 1e-6f;
    QVector3D edge1 = v1 - v0;
    QVector3D edge2 = v2 - v0;
    QVector3D h = QVector3D::crossProduct(rayDir, edge2);
    float a = QVector3D::dotProduct(edge1, h);

    if (a > -EPSILON && a < EPSILON) return false;

    float f = 1.0f / a;
    QVector3D s = rayOrigin - v0;
    float u = f * QVector3D::dotProduct(s, h);

    if (u < 0.0f || u > 1.0f) return false;

    QVector3D q = QVector3D::crossProduct(s, edge1);
    float v = f * QVector3D::dotProduct(rayDir, q);

    if (v < 0.0f || u + v > 1.0f) return false;

    t = f * QVector3D::dotProduct(edge2, q);
    if (t > EPSILON) {
        baryCoords = QVector3D(1.0f - u - v, u, v);
        return true;
    }

    return false;
}

void InteractiveRetopoTool::updateSnapPreview() {
    m_snapPreviewPoints.clear();
    m_snapPreviewIndices.clear();

    if (!m_lowPolyMesh) return;

    for (int i = 0; i < m_lowPolyMesh->vertices.size(); ++i) {
        const Vertex& v = m_lowPolyMesh->vertices[i];
        QVector3D snapped = snapToHighPoly(v.position);
        if ((snapped - v.position).length() > 0.001f) {
            m_snapPreviewPoints.append(snapped);
            m_snapPreviewIndices.append(i);
        }
    }
}

bool InteractiveRetopoTool::handleMousePress(const QVector3D& rayOrigin, const QVector3D& rayDir,
                                             const QMatrix4x4& viewProj, const QSize& viewportSize) {
    if (!m_isActive || !m_lowPolyMesh) return false;

    m_lastMousePos = projectOnHighPoly(rayOrigin, rayDir);
    m_isDragging = false;

    float bestDist = m_snapRadius * m_snapRadius;
    int bestVertex = -1;

    for (int i = 0; i < m_lowPolyMesh->vertices.size(); ++i) {
        float dist = (m_lowPolyMesh->vertices[i].position - m_lastMousePos).lengthSquared();
        if (dist < bestDist) {
            bestDist = dist;
            bestVertex = i;
        }
    }

    if (bestVertex >= 0) {
        m_selectedVertex = bestVertex;
        if (!m_selectedVertices.contains(bestVertex)) {
            m_selectedVertices.append(bestVertex);
        }
        emit selectedVertexChanged();
        return true;
    }

    return false;
}

bool InteractiveRetopoTool::handleMouseMove(const QVector3D& rayOrigin, const QVector3D& rayDir,
                                            const QMatrix4x4& viewProj, const QSize& viewportSize) {
    if (!m_isActive || !m_lowPolyMesh || m_selectedVertex < 0) return false;

    m_isDragging = true;
    QVector3D newPos = projectOnHighPoly(rayOrigin, rayDir);
    QVector3D snapped = snapToHighPoly(newPos);

    m_lowPolyMesh->vertices[m_selectedVertex].position = snapped;
    updateSnapPreview();
    emit meshChanged();
    return true;
}

bool InteractiveRetopoTool::handleMouseRelease(const QVector3D& rayOrigin, const QVector3D& rayDir,
                                               const QMatrix4x4& viewProj, const QSize& viewportSize) {
    if (!m_isActive) return false;

    m_isDragging = false;
    return true;
}

bool InteractiveRetopoTool::addVertexAtCursor(const QVector3D& rayOrigin, const QVector3D& rayDir,
                                              const QMatrix4x4& viewProj, const QSize& viewportSize) {
    if (!m_lowPolyMesh) return false;

    QVector3D hitPoint = projectOnHighPoly(rayOrigin, rayDir);
    QVector3D snapped = snapToHighPoly(hitPoint);

    Vertex newVert;
    newVert.position = snapped;
    newVert.normal = QVector3D(0, 1, 0);
    newVert.uv = QVector2D(0, 0);
    m_lowPolyMesh->vertices.append(newVert);

    m_selectedVertex = m_lowPolyMesh->vertices.size() - 1;
    m_selectedVertices.clear();
    m_selectedVertices.append(m_selectedVertex);

    emit selectedVertexChanged();
    emit meshChanged();
    emit statusMessage(QString("Vertex added at (%1, %2, %3)")
                       .arg(snapped.x(), 0, 'f', 3)
                       .arg(snapped.y(), 0, 'f', 3)
                       .arg(snapped.z(), 0, 'f', 3));
    return true;
}

bool InteractiveRetopoTool::createQuadFromSelection() {
    if (!m_lowPolyMesh || m_selectedVertices.size() != 4) {
        emit statusMessage("Select exactly 4 vertices to create a quad");
        return false;
    }

    Face newFace;
    newFace.indices = m_selectedVertices;
    m_lowPolyMesh->faces.append(newFace);

    emit meshChanged();
    emit statusMessage("Quad created from selected vertices");
    return true;
}

bool InteractiveRetopoTool::createTriangleFromSelection() {
    if (!m_lowPolyMesh || m_selectedVertices.size() != 3) {
        emit statusMessage("Select exactly 3 vertices to create a triangle");
        return false;
    }

    Face newFace;
    newFace.indices = m_selectedVertices;
    m_lowPolyMesh->faces.append(newFace);

    emit meshChanged();
    emit statusMessage("Triangle created from selected vertices");
    return true;
}

bool InteractiveRetopoTool::deleteSelected() {
    if (!m_lowPolyMesh || m_selectedVertex < 0) return false;

    int deletedCount = 0;
    for (int i = m_lowPolyMesh->faces.size() - 1; i >= 0; --i) {
        const Face& face = m_lowPolyMesh->faces[i];
        if (face.indices.contains(m_selectedVertex)) {
            m_lowPolyMesh->faces.removeAt(i);
            deletedCount++;
        }
    }

    m_lowPolyMesh->vertices.removeAt(m_selectedVertex);
    m_selectedVertex = -1;
    m_selectedVertices.clear();

    emit meshChanged();
    emit statusMessage(QString("Deleted vertex and %1 faces").arg(deletedCount));
    return true;
}

bool InteractiveRetopoTool::mergeVertices(float threshold) {
    if (!m_lowPolyMesh) return false;

    float thresholdSq = threshold * threshold;
    int mergeCount = 0;
    QVector<bool> removed(m_lowPolyMesh->vertices.size(), false);

    for (int i = 0; i < m_lowPolyMesh->vertices.size(); ++i) {
        if (removed[i]) continue;

        for (int j = i + 1; j < m_lowPolyMesh->vertices.size(); ++j) {
            if (removed[j]) continue;

            if ((m_lowPolyMesh->vertices[i].position - m_lowPolyMesh->vertices[j].position).lengthSquared() < thresholdSq) {
                removed[j] = true;
                mergeCount++;

                for (Face& face : m_lowPolyMesh->faces) {
                    for (int& idx : face.indices) {
                        if (idx == j) idx = i;
                    }
                }
            }
        }
    }

    for (int i = m_lowPolyMesh->vertices.size() - 1; i >= 0; --i) {
        if (removed[i]) {
            m_lowPolyMesh->vertices.removeAt(i);

            for (Face& face : m_lowPolyMesh->faces) {
                for (int& idx : face.indices) {
                    if (idx > i) idx--;
                }
            }
        }
    }

    if (mergeCount > 0) {
        emit meshChanged();
        emit statusMessage(QString("Merged %1 vertex pairs").arg(mergeCount));
    }
    return mergeCount > 0;
}

bool InteractiveRetopoTool::relaxMesh(int iterations, float strength) {
    if (!m_lowPolyMesh || m_lowPolyMesh->vertices.isEmpty()) return false;

    for (int iter = 0; iter < iterations; ++iter) {
        QVector<QVector3D> newPositions;
        newPositions.reserve(m_lowPolyMesh->vertices.size());
        for (const auto& v : m_lowPolyMesh->vertices)
            newPositions.append(v.position);

        for (int i = 0; i < m_lowPolyMesh->vertices.size(); ++i) {
            QVector3D avgPos;
            int neighborCount = 0;

            for (const Face& face : m_lowPolyMesh->faces) {
                int idx = face.indices.indexOf(i);
                if (idx >= 0) {
                    for (int j = 0; j < face.indices.size(); ++j) {
                        if (face.indices[j] != i) {
                            avgPos += m_lowPolyMesh->vertices[face.indices[j]].position;
                            neighborCount++;
                        }
                    }
                }
            }

            if (neighborCount > 0) {
                avgPos /= neighborCount;
                QVector3D snapped = snapToHighPoly(m_lowPolyMesh->vertices[i].position +
                                                  (avgPos - m_lowPolyMesh->vertices[i].position) * strength);
                newPositions[i] = snapped;
            }
        }

        for (int i = 0; i < m_lowPolyMesh->vertices.size(); ++i) {
            m_lowPolyMesh->vertices[i].position = newPositions[i];
        }
    }

    updateSnapPreview();
    emit meshChanged();
    emit statusMessage(QString("Mesh relaxed (%1 iterations)").arg(iterations));
    return true;
}

} // namespace ks
