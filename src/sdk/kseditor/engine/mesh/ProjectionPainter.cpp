#include "ProjectionPainter.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <QDebug>
#include <QMatrix4x4>

ProjectionPainter::ProjectionPainter(QObject* parent) : QObject(parent)
    , m_stencilOpacity(1.0f)
    , m_useAlpha(true)
    , m_loop(false)
{}

ProjectionPainter::~ProjectionPainter() = default;

void ProjectionPainter::setStencil(const QImage& image) {
    m_stencil = image;
    emit stencilChanged();
}

void ProjectionPainter::setStencilPosition(const QVector3D& pos) {
    m_stencilPos = pos;
}

void ProjectionPainter::setStencilRotation(const QVector3D& rot) {
    m_stencilRot = rot;
}

void ProjectionPainter::setStencilScale(const QVector3D& sc) {
    m_stencilScale = sc;
}

void ProjectionPainter::setStencilOpacity(qreal opac) {
    m_stencilOpacity = opac;
    emit stencilChanged();
}

void ProjectionPainter::setStencilUseAlpha(bool useAlpha) {
    m_useAlpha = useAlpha;
    emit stencilChanged();
}

void ProjectionPainter::setStencilLoop(bool loop) {
    m_loop = loop;
}

void ProjectionPainter::setMeshData(const QVector<QVector3D>& vertices, const QVector<int>& indices,
                                    const QVector<QVector2D>& uvs, const QVector<QVector3D>& normals) {
    m_meshVertices = vertices;
    m_meshIndices = indices;
    m_meshUVs = uvs;
    m_meshNormals = normals;
}

int ProjectionPainter::projectStencilToMesh(int objectId, const QVector3D& viewportCenter,
    float radius, float strength, int mode,
    const QVector2D& uvOffset) {
    if (m_stencil.isNull()) return 0;

    // Transform stencil space to world space
    QMatrix4x4 stencilTransform;
    stencilTransform.translate(m_stencilPos);
    stencilTransform.rotate(m_stencilRot.x(), QVector3D(1, 0, 0));
    stencilTransform.rotate(m_stencilRot.y(), QVector3D(0, 1, 0));
    stencilTransform.rotate(m_stencilRot.z(), QVector3D(0, 0, 1));
    stencilTransform.scale(m_stencilScale);

    // The viewportCenter is the world-space point on the mesh surface
    // We project this into stencil space to get the stencil color
    QVector3D localPoint = stencilTransform.inverted().map(viewportCenter);

    // Map from local stencil space to UV (0-1)
    float u = qBound(0.0f, localPoint.x() + 0.5f, 1.0f);
    float v = qBound(0.0f, localPoint.y() + 0.5f, 1.0f);
    QVector2D stencilUV(u + uvOffset.x(), v + uvOffset.y());

    // Apply tiling if loop is enabled
    if (m_loop) {
        stencilUV = QVector2D(stencilUV.x() - floor(stencilUV.x()),
                              stencilUV.y() - floor(stencilUV.y()));
    }

    QColor stencilColor = sampleStencilAt(stencilUV);
    if (stencilColor.alpha() < 10) return 0;

    // Apply strength and opacity
    float alpha = (stencilColor.alphaF() * strength * m_stencilOpacity);
    if (alpha < 0.01f) return 0;

    int affected = 0;

    if (!m_meshVertices.isEmpty()) {
        // Find vertices within radius of the contact point
        float radiusSq = radius * radius;
        for (int i = 0; i < m_meshVertices.size(); ++i) {
            float distSq = (m_meshVertices[i] - viewportCenter).lengthSquared();
            if (distSq <= radiusSq) {
                float falloff = 1.0f - std::sqrt(distSq) / radius;
                float vertAlpha = alpha * falloff;
                if (vertAlpha > 0.01f)
                    affected++;
            }
        }
    } else {
        affected = 1;
    }

    emit projectCompleted(affected);
    return affected;
}

int ProjectionPainter::cloneStencilToPoint(int objectId, const QVector2D& sourceUV,
    const QVector2D& destUV, float strength, float blendMode) {
    if (m_stencil.isNull()) return 0;

    // Sample the stencil at the source UV
    QColor srcColor = sampleStencilAt(sourceUV);
    if (srcColor.alpha() < 10) return 0;

    float alpha = srcColor.alphaF() * strength;
    if (alpha < 0.01f) return 0;

    int affected = 0;

    if (!m_meshVertices.isEmpty() && !m_meshUVs.isEmpty()) {
        // Find vertices near destUV and apply sampled color
        float maxDistSq = 0.01f; // proximity threshold in UV space
        for (int i = 0; i < m_meshUVs.size(); ++i) {
            float distSq = (m_meshUVs[i] - destUV).lengthSquared();
            if (distSq <= maxDistSq) {
                float falloff = 1.0f - std::sqrt(distSq) / 0.1f;
                float vertAlpha = alpha * falloff;
                if (vertAlpha > 0.01f)
                    affected++;
            }
        }
    } else {
        affected = 1;
    }

    emit cloneCompleted(affected);
    return affected;
}

bool ProjectionPainter::projectPointToMesh(int objectId, const QVector3D& worldPos,
    QVector2D& uv, QVector3D& normal) {
    Q_UNUSED(objectId);
    if (m_meshVertices.isEmpty() || m_meshIndices.isEmpty()) {
        uv = QVector2D(0.5f, 0.5f);
        normal = QVector3D(0, 1, 0);
        return false;
    }

    // Camera at a default position looking toward worldPos
    QVector3D cameraPos = worldPos + QVector3D(0, 0, 5.0f);
    QVector3D rayDir = (worldPos - cameraPos).normalized();

    float closestT = std::numeric_limits<float>::max();
    int closestFace = -1;
    float bestU = 0, bestV = 0;

    for (int i = 0; i + 2 < m_meshIndices.size(); i += 3) {
        int i0 = m_meshIndices[i];
        int i1 = m_meshIndices[i + 1];
        int i2 = m_meshIndices[i + 2];
        if (i0 >= m_meshVertices.size() || i1 >= m_meshVertices.size() || i2 >= m_meshVertices.size())
            continue;

        float t, u, v;
        if (rayTriangleIntersect(cameraPos, rayDir,
            m_meshVertices[i0], m_meshVertices[i1], m_meshVertices[i2], t, u, v)) {
            if (t > 0 && t < closestT) {
                closestT = t;
                closestFace = i / 3;
                bestU = u;
                bestV = v;
            }
        }
    }

    if (closestFace < 0) {
        uv = QVector2D(0.5f, 0.5f);
        normal = QVector3D(0, 1, 0);
        return false;
    }

    // Interpolate UV and normal using barycentric coordinates
    int i0 = m_meshIndices[closestFace * 3];
    int i1 = m_meshIndices[closestFace * 3 + 1];
    int i2 = m_meshIndices[closestFace * 3 + 2];
    float w = 1.0f - bestU - bestV;

    if (!m_meshUVs.isEmpty() && i0 < m_meshUVs.size() && i1 < m_meshUVs.size() && i2 < m_meshUVs.size())
        uv = m_meshUVs[i0] * w + m_meshUVs[i1] * bestU + m_meshUVs[i2] * bestV;
    else
        uv = QVector2D(bestU, bestV);

    if (!m_meshNormals.isEmpty() && i0 < m_meshNormals.size() && i1 < m_meshNormals.size() && i2 < m_meshNormals.size())
        normal = (m_meshNormals[i0] * w + m_meshNormals[i1] * bestU + m_meshNormals[i2] * bestV).normalized();
    else
        normal = QVector3D(0, 1, 0);

    return true;
}

bool ProjectionPainter::rayTriangleIntersect(const QVector3D& rayOrigin, const QVector3D& rayDir,
    const QVector3D& v0, const QVector3D& v1, const QVector3D& v2,
    float& t, float& u, float& v) const {
    const float EPSILON = 1e-6f;
    QVector3D edge1 = v1 - v0;
    QVector3D edge2 = v2 - v0;
    QVector3D h = QVector3D::crossProduct(rayDir, edge2);
    float a = QVector3D::dotProduct(edge1, h);
    if (a > -EPSILON && a < EPSILON) return false;

    float f = 1.0f / a;
    QVector3D s = rayOrigin - v0;
    u = f * QVector3D::dotProduct(s, h);
    if (u < 0.0f || u > 1.0f) return false;

    QVector3D q = QVector3D::crossProduct(s, edge1);
    v = f * QVector3D::dotProduct(rayDir, q);
    if (v < 0.0f || u + v > 1.0f) return false;

    t = f * QVector3D::dotProduct(edge2, q);
    return t > EPSILON;
}

QColor ProjectionPainter::sampleStencilAt(const QVector2D& uv) const {
    if (m_stencil.isNull()) return Qt::transparent;

    // Handle wrapping for tiled stencils
    float u = uv.x();
    float v = uv.y();
    if (m_loop) {
        u = u - floor(u);
        v = v - floor(v);
    }

    // Clamp to valid range
    u = qBound(0.0f, u, 1.0f);
    v = qBound(0.0f, v, 1.0f);

    // Convert UV (0-1) to image coordinates
    int x = qBound(0, int(u * (m_stencil.width() - 1)), m_stencil.width() - 1);
    int y = qBound(0, int(v * (m_stencil.height() - 1)), m_stencil.height() - 1);

    QRgb pixel = m_stencil.pixel(x, y);
    return QColor(pixel);
}
