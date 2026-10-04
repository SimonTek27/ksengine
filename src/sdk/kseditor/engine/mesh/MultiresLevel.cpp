#include "MultiresLevel.h"
#include <cmath>
#include <algorithm>
#include <QSet>

MultiresManager::MultiresManager(QObject* parent) : QObject(parent) {}

MultiresManager::~MultiresManager() = default;

void MultiresManager::createLevel(const QVector<QVector3D>& vertices, const QVector<int>& faces) {
    m_levels.clear();
    m_levels.append(SubdivisionLevel(vertices, faces));
    m_currentLevel = 0;
    emit levelsChanged(m_levels.size());
    emit levelChanged(0);
}

void MultiresManager::setCurrentLevel(int level) {
    if (level < 0) level = 0;
    if (level >= m_levels.size()) level = m_levels.size() - 1;
    if (m_currentLevel != level) {
        m_currentLevel = level;
        emit levelChanged(level);
    }
}

void MultiresManager::addLevel() {
    if (m_currentLevel < 0 || m_currentLevel >= m_levels.size()) return;
    
    // Create new level by subdividing current level
    const auto& current = m_levels[m_currentLevel];
    SubdivisionLevel newLevel;
    newLevel.vertices = catmullClarkSubdivide(current.vertices, current.faces);
    newLevel.faces = catmullClarkFaceIndices(current.faces);
    newLevel.edgeCreases.assign(newLevel.vertices.size(), 0.0f);
    newLevel.faceNormals = computeVertexNormals(newLevel.vertices, newLevel.faces);
    
    // Propagate edge creases from old to new level
    // ... (implementation would map old edges to new edges)
    
    m_levels.insert(m_currentLevel + 1, newLevel);
    setCurrentLevel(m_currentLevel + 1);
    emit levelsChanged(m_levels.size());
    emit levelChanged(m_currentLevel);
}

void MultiresManager::removeLevel() {
    if (m_levels.size() <= 1) return;
    if (m_currentLevel >= m_levels.size() - 1) m_currentLevel = m_levels.size() - 2;
    m_levels.removeAt(m_currentLevel + 1);
    emit levelsChanged(m_levels.size());
    emit levelChanged(m_currentLevel);
}

void MultiresManager::subdivideCurrentLevel() {
    if (m_levels.isEmpty() || m_currentLevel < 0 || m_currentLevel >= m_levels.size()) return;
    
    auto& current = m_levels[m_currentLevel];
    // Save pre-subdivision positions for limit surface preservation
    m_previousVertices = current.vertices;
    
    current.vertices = catmullClarkSubdivide(current.vertices, current.faces);
    current.faces = catmullClarkFaceIndices(current.faces);
    // Recompute normals and creases
    current.faceNormals = computeVertexNormals(current.vertices, current.faces);
    current.edgeCreases.assign(current.vertices.size(), 0.0f);
    
    emit levelChanged(m_currentLevel);
}

void MultiresManager::limitCurrentLevel() {
    if (m_levels.isEmpty() || m_currentLevel < 0 || m_currentLevel >= m_levels.size()) return;
    
    auto& current = m_levels[m_currentLevel];
    // Apply limit surface - preserve shape by constraining toward pre-subdivision positions
    if (!m_previousVertices.isEmpty()) {
        preserveShape(current.vertices, m_previousVertices);
    }
    
    emit levelChanged(m_currentLevel);
}

int MultiresManager::sculptBrush(const QVector3D& center, float radius, float strength, int mode,
                                 const QVector3D& drag, const QVector3D& previousCenter,
                                 float falloffPower, const QSet<int>* pinned) {
    if (m_levels.isEmpty()) return 0;
    
    auto& current = m_levels[m_currentLevel];
    QVector<QVector3D> newVerts = current.vertices;
    int affected = 0;

    // Pre-compute smooth averages if needed (mode 1)
    QVector<QVector3D> smoothPositions;
    if (mode == 1) {
        smoothPositions.resize(current.vertices.size());
        for (int vi = 0; vi < current.vertices.size(); ++vi) {
            QVector3D avgPos;
            int neighborCount = 0;
            // Find adjacent vertices from face data
            for (int fi = 0; fi + 2 < current.faces.size(); fi += 3) {
                int f0 = current.faces[fi], f1 = current.faces[fi + 1], f2 = current.faces[fi + 2];
                if (f0 == vi || f1 == vi || f2 == vi) {
                    if (f0 != vi && f0 < current.vertices.size()) { avgPos += current.vertices[f0]; neighborCount++; }
                    if (f1 != vi && f1 < current.vertices.size()) { avgPos += current.vertices[f1]; neighborCount++; }
                    if (f2 != vi && f2 < current.vertices.size()) { avgPos += current.vertices[f2]; neighborCount++; }
                }
            }
            smoothPositions[vi] = (neighborCount > 0) ? avgPos / float(neighborCount) : current.vertices[vi];
        }
    }
    
    for (int vi = 0; vi < current.vertices.size(); ++vi) {
        if (pinned && pinned->contains(vi)) continue;
        
        float d = (current.vertices[vi] - center).length();
        if (d > radius) continue;
        
        float t = 1.0f - (d / radius);
        float falloff;
        if (qAbs(falloffPower - 2.0f) < 1e-4f)
            falloff = t * t * (3.0f - 2.0f * t);
        else
            falloff = t > 0.0f ? qPow(t, qBound(0.25f, falloffPower, 8.0f)) : 0.0f;
        if (falloff <= 0.001f) continue;
        affected++;
        
        // Compute vertex normal for this vertex
        QVector3D vertNormal = current.vertices[vi].normalized();
        if (!current.faceNormals.isEmpty() && vi < current.faceNormals.size()) {
            vertNormal = current.faceNormals[vi];
        }
        
        switch (mode) {
            case 0: // draw
                newVerts[vi] += vertNormal * (strength * falloff);
                break;
            case 1: // smooth
                if (vi < smoothPositions.size()) {
                    newVerts[vi] = current.vertices[vi] * (1.0f - strength * falloff) +
                                   smoothPositions[vi] * (strength * falloff);
                }
                break;
            case 2: { // grab
                QVector3D grabDir = drag.normalized();
                newVerts[vi] += grabDir * (strength * falloff);
                break;
            }
            case 3: // flatten
                newVerts[vi].setY(newVerts[vi].y() * (1.0f - strength * falloff) +
                                  center.y() * (strength * falloff));
                break;
            case 4: // crease
                newVerts[vi] += vertNormal * (strength * falloff * 0.5f);
                break;
            case 5: // inflate
                newVerts[vi] += vertNormal * (strength * falloff * (1.0f + falloff));
                break;
            case 6: { // pinch - pull toward center
                QVector3D toCenter = center - current.vertices[vi];
                newVerts[vi] += toCenter * (strength * falloff * 0.3f);
                break;
            }
            case 7: // smear - follow drag
                newVerts[vi] += drag * (strength * falloff * 0.5f);
                break;
            case 8: // negate
                newVerts[vi] -= vertNormal * (strength * falloff * 1.5f);
                break;
            default:
                break;
        }
    }
    
    m_levels[m_currentLevel].vertices = newVerts;
    emit sculptUpdated();
    return affected;
}

void MultiresManager::bakeCurrentLevel() {
    if (m_levels.isEmpty() || m_currentLevel < 0 || m_currentLevel >= m_levels.size()) return;
    
    // Replace base mesh with current level geometry
    // and remove higher levels
    m_levels[m_currentLevel].vertices = m_levels.last().vertices;
    m_levels[m_currentLevel].faces = m_levels.last().faces;
    m_levels.remove(m_currentLevel + 1, m_levels.size() - m_currentLevel - 1);
    
    emit baked();
    emit levelsChanged(m_levels.size());
    emit levelChanged(m_currentLevel);
}

QVector<QVector3D> MultiresManager::catmullClarkSubdivide(const QVector<QVector3D>& verts, const QVector<int>& faces) {
    QVector<QVector3D> newVerts;

    int numVerts = verts.size();
    int numFaces = faces.size() / 3;

    QVector<QVector3D> facePoints(numFaces);
    for (int i = 0; i < numFaces; ++i) {
        int fi = i * 3;
        facePoints[i] = (verts[faces[fi]] + verts[faces[fi + 1]] + verts[faces[fi + 2]]) / 3.0f;
    }

    QMap<QPair<int,int>, int> edgeMidpoints;
    QVector<QVector3D> edgePoints;
    auto getEdgeIndex = [&](int a, int b) -> int {
        if (a > b) qSwap(a, b);
        QPair<int,int> key(a, b);
        if (edgeMidpoints.contains(key)) return edgeMidpoints[key];
        int idx = edgePoints.size();
        edgePoints.append((verts[a] + verts[b]) / 2.0f);
        edgeMidpoints[key] = idx;
        return idx;
    };

    QVector<int> vertFaceCount(numVerts, 0);
    QVector<QVector3D> vertFaceAvg(numVerts, QVector3D(0,0,0));
    QVector<int> vertEdgeCount(numVerts, 0);
    QVector<QVector3D> vertEdgeAvg(numVerts, QVector3D(0,0,0));

    for (int i = 0; i < numFaces; ++i) {
        int fi = i * 3;
        int v0 = faces[fi], v1 = faces[fi+1], v2 = faces[fi+2];

        vertFaceCount[v0]++; vertFaceAvg[v0] += facePoints[i];
        vertFaceCount[v1]++; vertFaceAvg[v1] += facePoints[i];
        vertFaceCount[v2]++; vertFaceAvg[v2] += facePoints[i];

        int e0 = getEdgeIndex(v0, v1);
        int e1 = getEdgeIndex(v1, v2);
        int e2 = getEdgeIndex(v2, v0);

        vertEdgeCount[v0]++; vertEdgeAvg[v0] += edgePoints[e0] + edgePoints[e2];
        vertEdgeCount[v1]++; vertEdgeAvg[v1] += edgePoints[e0] + edgePoints[e1];
        vertEdgeCount[v2]++; vertEdgeAvg[v2] += edgePoints[e1] + edgePoints[e2];
    }

    for (int i = 0; i < numVerts; ++i) {
        if (vertFaceCount[i] > 0 && vertEdgeCount[i] > 0) {
            QVector3D F = vertFaceAvg[i] / vertFaceCount[i];
            QVector3D E = vertEdgeAvg[i] / vertEdgeCount[i];
            int n = vertFaceCount[i];
            newVerts.append((F + 2.0f * E + (n - 3.0f) * verts[i]) / n);
        } else {
            newVerts.append(verts[i]);
        }
    }

    for (auto& ep : edgePoints)
        newVerts.append(ep);

    for (const auto& fp : facePoints)
        newVerts.append(fp);

    return newVerts;
}

QVector<int> MultiresManager::catmullClarkFaceIndices(const QVector<int>& faces) {
    QVector<int> newFaces;
    int numFaces = faces.size() / 3;
    int baseVerts = 0;
    int baseEdges = 0;

    QMap<QPair<int,int>, int> edgeMap;
    QVector<QPair<int,int>> edgeList;
    for (int i = 0; i < numFaces; ++i) {
        int fi = i * 3;
        for (int j = 0; j < 3; ++j) {
            int a = faces[fi + j];
            int b = faces[fi + (j + 1) % 3];
            if (a > b) qSwap(a, b);
            QPair<int,int> key(a, b);
            if (!edgeMap.contains(key)) {
                edgeMap[key] = edgeList.size();
                edgeList.append(key);
            }
        }
    }

    int totalOriginal = faces.size() / 3;
    int vertCount = 0;
    for (int i = 0; i < numFaces; ++i) {
        int fi = i * 3;
        int v0 = faces[fi], v1 = faces[fi+1], v2 = faces[fi+2];

        int e01 = edgeMap.value(QPair<int,int>(qMin(v0,v1), qMax(v0,v1))) + vertCount;
        int e12 = edgeMap.value(QPair<int,int>(qMin(v1,v2), qMax(v1,v2))) + vertCount;
        int e20 = edgeMap.value(QPair<int,int>(qMin(v2,v0), qMax(v2,v0))) + vertCount;
        int fCenter = edgeList.size() + i + vertCount;

        newFaces.append(v0); newFaces.append(e01); newFaces.append(fCenter);
        newFaces.append(v1); newFaces.append(e12); newFaces.append(fCenter);
        newFaces.append(v2); newFaces.append(e20); newFaces.append(fCenter);
    }

    return newFaces;
}

QVector<QVector3D> MultiresManager::computeVertexNormals(const QVector<QVector3D>& verts, const QVector<int>& faces) {
    QVector<QVector3D> norms(verts.size(), QVector3D(0, 0, 0));
    
    for (int i = 0; i + 2 < faces.size(); i += 3) {
        int i0 = faces[i];
        int i1 = faces[i + 1];
        int i2 = faces[i + 2];
        
        if (i0 >= verts.size() || i1 >= verts.size() || i2 >= verts.size()) continue;
        
        QVector3D n = QVector3D::crossProduct(verts[i1] - verts[i0], verts[i2] - verts[i0]);
        norms[i0] += n;
        norms[i1] += n;
        norms[i2] += n;
    }
    
    for (auto& n : norms) {
        n.normalize();
    }
    
    return norms;
}

void MultiresManager::preserveShape(QVector<QVector3D>& newVerts, const QVector<QVector3D>& oldVerts) {
    // Simple shape preservation - move new vertices toward old positions
    for (int i = 0; i < qMin(newVerts.size(), oldVerts.size()); ++i) {
        newVerts[i] = newVerts[i] * 0.5f + oldVerts[i] * 0.5f;
    }
}