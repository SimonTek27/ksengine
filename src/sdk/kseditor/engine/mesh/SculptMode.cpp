#include "SculptMode.h"
#include <QDebug>
#include <QtMath>

SculptMode::SculptMode(QObject* parent)
    : QObject(parent)
    , m_currentTool(ToolDraw)
    , m_isStroking(false)
    , m_retopoMode(false)
{
    m_brush.radius = 0.5f;
    m_brush.strength = 0.5f;
    m_brush.useTexture = false;
    m_brush.useNormalFalloff = false;
    m_brush.autoSmooth = 0.0f;
}

SculptMode::~SculptMode() = default;

void SculptMode::setMeshData(const QVector<QVector3D>& vertices, const QVector<int>& faces)
{
    m_vertices.clear();
    m_vertices.reserve(vertices.size());
    for (const auto& v : vertices) {
        Vertex vert;
        vert.position = v;
        vert.normal = QVector3D(0, 1, 0);
        vert.mask = 0.0f;
        m_vertices.append(vert);
    }
    m_faceIndices = faces;
    rebuildNormals();
}

void SculptMode::setTool(SculptTool tool)
{
    m_currentTool = tool;
}

void SculptMode::setBrushSize(float size)
{
    m_brush.radius = size;
}

void SculptMode::setBrushStrength(float strength)
{
    m_brush.strength = strength;
}

void SculptMode::beginStroke(const QVector3D& point)
{
    m_isStroking = true;
    m_currentStroke.clear();

    StrokePoint sp;
    sp.position = point;
    sp.pressure = 1.0f;
    m_currentStroke.append(sp);

    emit strokeStarted();
}

void SculptMode::addPoint(const QVector3D& point, const QVector3D& normal, float pressure)
{
    if (!m_isStroking || m_vertices.isEmpty()) return;

    StrokePoint sp;
    sp.position = point;
    sp.normal = normal;
    sp.pressure = pressure;
    m_currentStroke.append(sp);

    int centerIdx = findNearestVertex(point);
    if (centerIdx < 0) return;

    float radiusSq = m_brush.radius * m_brush.radius;

    for (int i = 0; i < m_vertices.size(); ++i) {
        QVector3D& vert = m_vertices[i].position;
        float distSq = (vert - point).lengthSquared();

        if (distSq < radiusSq) {
            float falloff = 1.0f - (sqrt(distSq) / m_brush.radius);
            float strength = m_brush.strength * falloff * sp.pressure;

            QVector3D vertNormal = m_vertices[i].normal;
            sculptPoint(vert, vertNormal, strength);
        }
    }

    emit strokeUpdated();
}

void SculptMode::quadDrawSplitEdge(int edgeIndex)
{
    if (edgeIndex < 0 || edgeIndex >= m_faces.size()) return;

    int v1 = m_faces[edgeIndex];
    int v2 = m_faces[(edgeIndex + 1) % m_faces.size()];

    if (v1 >= m_vertices.size() || v2 >= m_vertices.size()) return;

    if (m_retopoMode) {
        // Retopo-aware: split edge and preserve mask tags
        QVector3D midPos = (m_vertices[v1].position + m_vertices[v2].position) * 0.5f;
        Vertex newVert;
        newVert.position = midPos;
        newVert.normal = (m_vertices[v1].normal + m_vertices[v2].normal).normalized();
        newVert.mask = (m_vertices[v1].mask + m_vertices[v2].mask) * 0.5f;
        int newIdx = m_vertices.size();
        m_vertices.append(newVert);

        // Replace the edge in all faces containing it
        for (int fi = 0; fi + 2 < m_faces.size(); fi += 3) {
            int faceVerts[3] = {m_faces[fi], m_faces[fi+1], m_faces[fi+2]};
            for (int e = 0; e < 3; ++e) {
                int ev0 = faceVerts[e];
                int ev1 = faceVerts[(e+1)%3];
                if ((ev0 == v1 && ev1 == v2) || (ev0 == v2 && ev1 == v1)) {
                    int opp = faceVerts[(e+2)%3];
                    m_faces[fi] = v1; m_faces[fi+1] = newIdx; m_faces[fi+2] = opp;
                    m_faces.append(newIdx); m_faces.append(v2); m_faces.append(opp);
                    break;
                }
            }
        }
    } else {
        // Standard edge split: insert midpoint vertex
        QVector3D midPos = (m_vertices[v1].position + m_vertices[v2].position) * 0.5f;
        Vertex newVert;
        newVert.position = midPos;
        newVert.normal = (m_vertices[v1].normal + m_vertices[v2].normal).normalized();
        int newIdx = m_vertices.size();
        m_vertices.append(newVert);

        // Find and split faces containing this edge
        for (int fi = 0; fi + 2 < m_faces.size(); fi += 3) {
            int faceVerts[3] = {m_faces[fi], m_faces[fi+1], m_faces[fi+2]};
            for (int e = 0; e < 3; ++e) {
                int ev0 = faceVerts[e];
                int ev1 = faceVerts[(e+1)%3];
                if ((ev0 == v1 && ev1 == v2) || (ev0 == v2 && ev1 == v1)) {
                    int opp = faceVerts[(e+2)%3];
                    m_faces[fi] = v1; m_faces[fi+1] = newIdx; m_faces[fi+2] = opp;
                    m_faces.append(newIdx); m_faces.append(v2); m_faces.append(opp);
                    break;
                }
            }
        }
    }
}

void SculptMode::endStroke()
{
    m_isStroking = false;
    rebuildNormals();
    emit strokeEnded();
}

void SculptMode::setRetopoMode(bool enabled)
{
    m_retopoMode = enabled;
}

void SculptMode::sculptPoint(QVector3D& vertex, const QVector3D& normal, float strength)
{
    switch (m_currentTool) {
        case ToolDraw:
            drawVertex(vertex, normal, strength);
            break;
        case ToolFlatten:
            flattenVertex(vertex, normal, strength);
            break;
        case ToolSmooth:
            smoothVertex(vertex, strength);
            break;
        case ToolCrease:
            creaseVertex(vertex, normal, strength);
            break;
        case ToolGrab:
            grabVertex(vertex, normal, strength);
            break;
        case ToolInflate:
            inflateVertex(vertex, strength);
            break;
        case ToolSnake:
            snakeHookVertex(vertex, normal, strength);
            break;
        case ToolEdgeSlide: {
            // Edge slide: move vertex along its connected edge loop
            QVector3D slideDir = QVector3D::crossProduct(normal, QVector3D(0, 1, 0)).normalized();
            if (slideDir.length() < 0.001f) {
                slideDir = QVector3D::crossProduct(normal, QVector3D(1, 0, 0)).normalized();
            }
            vertex += slideDir * strength * 0.01f;
            break;
        }
        default:
            break;
    }
}

void SculptMode::drawVertex(QVector3D& vertex, const QVector3D& direction, float strength)
{
    vertex += direction * strength * 0.01f;
}

void SculptMode::flattenVertex(QVector3D& vertex, const QVector3D& sculptNormal, float strength)
{
    QVector3D avgNormal = computeVertexNormal(0);
    float dot = QVector3D::dotProduct(vertex, avgNormal);
    QVector3D projected = vertex - avgNormal * dot;
    vertex = projected * (1.0f - strength) + projected * strength;
}

void SculptMode::smoothVertex(QVector3D& vertex, float strength)
{
    QVector<QVector3D> neighbors;
    if (m_vertices.size() <= 1) return;

    QVector3D avg;
    int count = 0;

    for (int i = 0; i < qMin(10, m_vertices.size()); ++i) {
        if (i < m_vertices.size()) {
            avg += m_vertices[i].position;
            count++;
        }
    }

    if (count > 0) {
        avg /= count;
        vertex = vertex * (1.0f - strength) + avg * strength;
    }
}

void SculptMode::creaseVertex(QVector3D& vertex, const QVector3D& normal, float strength)
{
    vertex += normal * strength * 0.005f;
}

void SculptMode::grabVertex(QVector3D& vertex, const QVector3D& delta, float strength)
{
    vertex += delta * strength;
}

void SculptMode::inflateVertex(QVector3D& vertex, float strength)
{
    QVector3D normal = computeVertexNormal(0);
    vertex += normal * strength * 0.01f;
}

void SculptMode::snakeHookVertex(QVector3D& vertex, const QVector3D& direction, float strength)
{
    vertex += direction * strength * 0.02f;
}

int SculptMode::findNearestVertex(const QVector3D& point) const
{
    if (m_vertices.isEmpty()) return -1;

    int nearest = -1;
    float minDist = 1e10f;

    for (int i = 0; i < m_vertices.size(); ++i) {
        float dist = (m_vertices[i].position - point).lengthSquared();
        if (dist < minDist) {
            minDist = dist;
            nearest = i;
        }
    }

    return nearest;
}

QVector3D SculptMode::computeVertexNormal(int index) const
{
    if (index >= 0 && index < m_normals.size()) {
        return m_normals[index];
    }
    return QVector3D(0, 1, 0);
}

void SculptMode::rebuildNormals()
{
    m_normals.clear();
    m_normals.resize(m_vertices.size());

    for (int i = 0; i < m_faceIndices.size(); i += 3) {
        int i0 = m_faceIndices[i];
        int i1 = m_faceIndices[i + 1];
        int i2 = m_faceIndices[i + 2];

        if (i0 >= m_vertices.size() || i1 >= m_vertices.size() || i2 >= m_vertices.size()) continue;

        QVector3D v0 = m_vertices[i0].position;
        QVector3D v1 = m_vertices[i1].position;
        QVector3D v2 = m_vertices[i2].position;

        QVector3D normal = QVector3D::crossProduct(v1 - v0, v2 - v0);

        m_normals[i0] += normal;
        m_normals[i1] += normal;
        m_normals[i2] += normal;
        m_vertices[i0].normal = m_normals[i0];
        m_vertices[i1].normal = m_normals[i1];
        m_vertices[i2].normal = m_normals[i2];
    }

    for (QVector3D& n : m_normals) {
        n.normalize();
    }
}

SelectionTools::SelectionTools(QObject* parent)
    : QObject(parent)
    , m_mode(SelectVertex)
{
}

SelectionTools::~SelectionTools() = default;

void SelectionTools::setSelectMode(SelectMode mode)
{
    m_mode = mode;
}

void SelectionTools::setMeshData(const QVector<QVector3D>& vertices, const QVector<int>& faces)
{
    m_vertices = vertices;
    m_faces = faces;
    buildAdjacency();
}

void SelectionTools::selectAll()
{
    switch (m_mode) {
        case SelectVertex:
            m_selectedVertices.clear();
            for (int i = 0; i < m_vertices.size(); ++i) {
                m_selectedVertices.append(i);
            }
            break;
        case SelectEdge:
            m_selectedEdges.clear();
            for (int i = 0; i < m_faces.size(); i += 3) {
                m_selectedEdges.append(i);
                m_selectedEdges.append(i + 1);
                m_selectedEdges.append(i + 2);
            }
            break;
        case SelectFace:
            m_selectedFaces.clear();
            for (int i = 0; i < m_faces.size() / 3; ++i) {
                m_selectedFaces.append(i);
            }
            break;
    }
    emit selectionChanged();
}

void SelectionTools::selectNone()
{
    m_selectedVertices.clear();
    m_selectedEdges.clear();
    m_selectedFaces.clear();
    emit selectionChanged();
}

void SelectionTools::selectInverse()
{
    QVector<int> inverted;

    switch (m_mode) {
        case SelectVertex: {
            for (int i = 0; i < m_vertices.size(); ++i) {
                if (!m_selectedVertices.contains(i)) {
                    inverted.append(i);
                }
            }
            m_selectedVertices = inverted;
            break;
        }
    }
    emit selectionChanged();
}

void SelectionTools::selectVertex(int index)
{
    if (!m_selectedVertices.contains(index)) {
        m_selectedVertices.append(index);
        emit selectionChanged();
    }
}

void SelectionTools::selectEdge(int v1, int v2)
{
    int edgeKey = v1 * 100000 + v2;
    if (!m_selectedEdges.contains(edgeKey)) {
        m_selectedEdges.append(edgeKey);
        emit selectionChanged();
    }
}

void SelectionTools::selectFace(int faceIndex)
{
    if (!m_selectedFaces.contains(faceIndex)) {
        m_selectedFaces.append(faceIndex);
        emit selectionChanged();
    }
}

void SelectionTools::selectVertexLoop(int startVertex)
{
    if (m_vertexToFaces.isEmpty()) buildAdjacency();
    if (!m_vertexToFaces.contains(startVertex)) return;

    QVector<int> visited;
    visited.append(startVertex);

    QVector<int> toProcess;
    toProcess.append(startVertex);

    while (!toProcess.isEmpty()) {
        int current = toProcess.takeFirst();

        const QVector<int>& faces = m_vertexToFaces[current];
        for (int faceIdx : faces) {
            const QVector<int>& faceVerts = getFaceVertices(faceIdx);
            for (int v : faceVerts) {
                if (!visited.contains(v)) {
                    visited.append(v);
                    toProcess.append(v);
                }
            }
        }
    }

    m_selectedVertices = visited;
    emit selectionChanged();
}

const QVector<int> SelectionTools::getFaceVertices(int faceIndex) const
{
    QVector<int> result;
    int start = faceIndex * 3;
    for (int i = 0; i < 3 && (start + i) < m_faces.size(); ++i) {
        result.append(m_faces[start + i]);
    }
    return result;
}

void SelectionTools::selectSimilar(SelectMode mode, const QString& property, float tolerance)
{
    if (mode == SelectVertex) {
        QVector3D avgNormal;
        for (int v : m_selectedVertices) {
            if (v < m_vertices.size()) {
                avgNormal += m_vertices[v];
            }
        }
        if (m_selectedVertices.size() > 0) {
            avgNormal /= m_selectedVertices.size();
        }
        avgNormal.normalize();

        for (int i = 0; i < m_vertices.size(); ++i) {
            if (!m_selectedVertices.contains(i) && i < m_vertices.size()) {
                QVector3D diff = m_vertices[i] - avgNormal;
                if (diff.length() < tolerance) {
                    m_selectedVertices.append(i);
                }
            }
        }
    }
    emit selectionChanged();
}

void SelectionTools::selectByNormal(const QVector3D& normal, float angleTolerance)
{
    float cosAngle = cos(angleTolerance);

    for (int i = 0; i < m_faces.size() / 3; ++i) {
        QVector3D faceNormal = computeFaceNormal(i);
        if (QVector3D::dotProduct(faceNormal, normal) > cosAngle) {
            if (!m_selectedFaces.contains(i)) {
                m_selectedFaces.append(i);
            }
        }
    }
    emit selectionChanged();
}

QVector3D SelectionTools::computeFaceNormal(int faceIndex) const
{
    int start = faceIndex * 3;
    if (start + 2 >= m_faces.size()) return QVector3D(0, 1, 0);

    int i0 = m_faces[start];
    int i1 = m_faces[start + 1];
    int i2 = m_faces[start + 2];

    if (i0 >= m_vertices.size() || i1 >= m_vertices.size() || i2 >= m_vertices.size()) {
        return QVector3D(0, 1, 0);
    }

    return QVector3D::crossProduct(m_vertices[i1] - m_vertices[i0], m_vertices[i2] - m_vertices[i0]);
}

void SelectionTools::setFaceMaterials(const QVector<int>& materials)
{
    m_faceMaterials = materials;
}

void SelectionTools::selectEdgeLoop(int startEdge)
{
    if (m_vertexToFaces.isEmpty()) buildAdjacency();
    m_selectedEdges.clear();

    QVector<int> visited;
    QVector<int> stack;
    stack.append(startEdge);
    visited.append(startEdge);

    while (!stack.isEmpty()) {
        int current = stack.takeFirst();
        m_selectedEdges.append(current);

        QVector<int> adjacent = findAdjacentEdges(current);
        for (int adj : adjacent) {
            if (!visited.contains(adj)) {
                visited.append(adj);
                stack.append(adj);
            }
        }
    }
    emit selectionChanged();
}

void SelectionTools::selectFaceRing(int startFace)
{
    if (m_vertexToFaces.isEmpty()) buildAdjacency();
    m_selectedFaces.clear();

    QVector<int> visited;
    QVector<int> stack;
    stack.append(startFace);
    visited.append(startFace);

    while (!stack.isEmpty()) {
        int current = stack.takeFirst();
        m_selectedFaces.append(current);

        const QVector<int>& faceVerts = getFaceVertices(current);
        for (int v : faceVerts) {
            if (m_vertexToFaces.contains(v)) {
                for (int adjFace : m_vertexToFaces[v]) {
                    if (!visited.contains(adjFace)) {
                        visited.append(adjFace);
                        stack.append(adjFace);
                    }
                }
            }
        }
    }
    emit selectionChanged();
}

void SelectionTools::selectVertexRing(int startVertex)
{
    if (!m_vertexToFaces.contains(startVertex)) return;
    if (m_vertexToFaces.isEmpty()) buildAdjacency();
    m_selectedVertices.clear();

    QVector<int> visited;
    QVector<int> stack;
    stack.append(startVertex);
    visited.append(startVertex);

    while (!stack.isEmpty()) {
        int current = stack.takeFirst();
        m_selectedVertices.append(current);

        const QVector<int>& faces = m_vertexToFaces[current];
        for (int faceIdx : faces) {
            const QVector<int>& faceVerts = getFaceVertices(faceIdx);
            for (int v : faceVerts) {
                if (!visited.contains(v)) {
                    visited.append(v);
                    stack.append(v);
                }
            }
        }
    }
    emit selectionChanged();
}

void SelectionTools::selectEdgeRing(int startEdge)
{
    if (m_vertexToFaces.isEmpty()) buildAdjacency();
    m_selectedEdges.clear();

    QVector<int> visited;
    QVector<int> stack;
    stack.append(startEdge);
    visited.append(startEdge);

    // Edge ring traverses edges that share a face but not a vertex
    while (!stack.isEmpty()) {
        int current = stack.takeFirst();
        m_selectedEdges.append(current);

        QVector<int> ev = getEdgeVertices(current);
        int v1 = ev.value(0, -1);
        if (v1 < 0) continue;

        // Find edges in the same face ring strip
        if (m_vertexToFaces.contains(v1)) {
            for (int faceIdx : m_vertexToFaces[v1]) {
                int base = faceIdx * 3;
                for (int i = 0; i < 3 && (base + i) < m_faces.size(); ++i) {
                    int edgeIdx = base + i;
                    if (!visited.contains(edgeIdx)) {
                        QVector<int> otherEv = getEdgeVertices(edgeIdx);
                        // Ring edges share one vertex with the current edge
                        if (otherEv.contains(v1) && edgeIdx != current) {
                            visited.append(edgeIdx);
                            stack.append(edgeIdx);
                        }
                    }
                }
            }
        }
    }
    emit selectionChanged();
}

void SelectionTools::selectBorder()
{
    if (m_vertexToFaces.isEmpty()) buildAdjacency();
    m_selectedEdges.clear();
    m_selectedVertices.clear();

    QMap<int, QVector<int>> edgeFaceCount;
    for (int i = 0; i < m_faces.size(); i += 3) {
        int i0 = m_faces[i], i1 = m_faces[i + 1], i2 = m_faces[i + 2];
        int e01 = findEdge(i0, i1);
        int e12 = findEdge(i1, i2);
        int e20 = findEdge(i2, i0);
        for (int e : {e01, e12, e20}) {
            if (!edgeFaceCount.contains(e)) edgeFaceCount[e] = QVector<int>();
            edgeFaceCount[e].append(i / 3);
        }
    }

    for (auto it = edgeFaceCount.constBegin(); it != edgeFaceCount.constEnd(); ++it) {
        if (it.value().size() == 1) {
            m_selectedEdges.append(it.key());
            QVector<int> ev = getEdgeVertices(it.key());
            for (int v : ev) {
                if (!m_selectedVertices.contains(v)) m_selectedVertices.append(v);
            }
        }
    }
    emit selectionChanged();
}

void SelectionTools::selectByMaterial(int materialId)
{
    if (m_faceMaterials.size() != m_faces.size() / 3) return;
    m_selectedFaces.clear();

    for (int i = 0; i < m_faceMaterials.size(); ++i) {
        if (m_faceMaterials[i] == materialId) {
            m_selectedFaces.append(i);
        }
    }
    emit selectionChanged();
}

QVector<int> SelectionTools::getEdgeVertices(int edgeIndex) const
{
    if (edgeIndex < 0 || edgeIndex >= m_faces.size()) return {};
    int faceStart = (edgeIndex / 3) * 3;
    int nextInFace = faceStart + ((edgeIndex - faceStart + 1) % 3);
    return { m_faces[edgeIndex], m_faces[nextInFace] };
}

QVector<int> SelectionTools::findAdjacentEdges(int edgeIndex) const
{
    QVector<int> result;
    int faceIdx = edgeIndex / 3;
    int base = faceIdx * 3;

    // Edges within the same face
    for (int i = 0; i < 3; ++i) {
        int e = base + i;
        if (e != edgeIndex && e < m_faces.size()) {
            result.append(e);
        }
    }

    // Edges from adjacent faces sharing the same vertices
    QVector<int> ev = getEdgeVertices(edgeIndex);
    if (ev.size() == 2) {
        for (int v : ev) {
            if (m_vertexToFaces.contains(v)) {
                for (int adjFace : m_vertexToFaces[v]) {
                    int adjBase = adjFace * 3;
                    for (int i = 0; i < 3 && (adjBase + i) < m_faces.size(); ++i) {
                        int e = adjBase + i;
                        if (e != edgeIndex && !result.contains(e)) {
                            result.append(e);
                        }
                    }
                }
            }
        }
    }

    return result;
}

int SelectionTools::oppositeVertex(int edgeIndex, int faceIndex) const
{
    int base = faceIndex * 3;
    if (base + 2 >= m_faces.size()) return -1;
    QVector<int> ev = getEdgeVertices(edgeIndex);
    if (ev.size() < 2) return -1;
    for (int i = 0; i < 3; ++i) {
        int v = m_faces[base + i];
        if (v != ev[0] && v != ev[1]) return v;
    }
    return -1;
}

void SelectionTools::growSelection()
{
    QVector<int> newSelection;

    for (int v : m_selectedVertices) {
        if (m_vertexToFaces.contains(v)) {
            for (int face : m_vertexToFaces[v]) {
                int start = face * 3;
                for (int i = 0; i < 3 && start + i < m_faces.size(); ++i) {
                    int vv = m_faces[start + i];
                    if (!m_selectedVertices.contains(vv)) {
                        newSelection.append(vv);
                    }
                }
            }
        }
    }

    m_selectedVertices.append(newSelection);
    emit selectionChanged();
}

void SelectionTools::shrinkSelection()
{
    QVector<int> kept;
    for (int v : m_selectedVertices) {
        bool hasAdjacentSelected = false;
        if (m_vertexToFaces.contains(v)) {
            for (int face : m_vertexToFaces[v]) {
                int start = face * 3;
                for (int i = 0; i < 3 && start + i < m_faces.size(); ++i) {
                    int vv = m_faces[start + i];
                    if (m_selectedVertices.contains(vv) && vv != v) {
                        hasAdjacentSelected = true;
                        break;
                    }
                }
            }
        }
        if (hasAdjacentSelected) {
            kept.append(v);
        }
    }
    m_selectedVertices = kept;
    emit selectionChanged();
}

void SelectionTools::buildAdjacency()
{
    m_vertexToFaces.clear();

    for (int i = 0; i < m_faces.size() / 3; ++i) {
        int i0 = m_faces[i * 3];
        int i1 = m_faces[i * 3 + 1];
        int i2 = m_faces[i * 3 + 2];

        if (!m_vertexToFaces.contains(i0)) m_vertexToFaces[i0] = QVector<int>();
        if (!m_vertexToFaces.contains(i1)) m_vertexToFaces[i1] = QVector<int>();
        if (!m_vertexToFaces.contains(i2)) m_vertexToFaces[i2] = QVector<int>();

        m_vertexToFaces[i0].append(i);
        m_vertexToFaces[i1].append(i);
        m_vertexToFaces[i2].append(i);
    }
}

int SelectionTools::findEdge(int v1, int v2) const
{
    return v1 * 100000 + v2;
}

QVector3D SelectionTools::computeEdgeDirection(int edgeIndex) const
{
    if (edgeIndex >= m_faces.size()) return QVector3D(0, 0, 1);

    int v1 = m_faces[edgeIndex];
    int v2 = m_faces[edgeIndex + 1];

    if (v1 >= m_vertices.size() || v2 >= m_vertices.size()) return QVector3D(0, 0, 1);

    return (m_vertices[v2] - m_vertices[v1]).normalized();
}

KnifeTool::KnifeTool(QObject* parent)
    : QObject(parent)
    , m_mode(ModeCut)
    , m_cutThrough(false)
    , m_poisonThreshold(0.001f)
    , m_maxSubdivisions(1)
{
}

KnifeTool::~KnifeTool() = default;

void KnifeTool::setMeshData(const QVector<QVector3D>& vertices, const QVector<int>& faces)
{
    m_vertices = vertices;
    m_faces = faces;
}

void KnifeTool::setMode(Mode mode)
{
    m_mode = mode;
}

void KnifeTool::setCutThrough(bool cutThrough)
{
    m_cutThrough = cutThrough;
}

void KnifeTool::beginCut(const QVector3D& start)
{
    m_cutPoints.clear();
    m_cutPoints.append(start);
}

void KnifeTool::continueCut(const QVector3D& current)
{
    m_cutPoints.append(current);
}

QVector<int> KnifeTool::completeCut(const QVector3D& end)
{
    m_cutPoints.append(end);
    QVector<CutEdge> intersections;
    for (int i = 1; i < m_cutPoints.size(); ++i) {
        findEdgeIntersections(m_cutPoints[i - 1], m_cutPoints[i], intersections);
    }
    connectCuts();
    emit cutCompleted();
    QVector<int> result;
    for (int i = 0; i < m_newVertices.size(); ++i)
        result.append(m_vertices.size() + i);
    return result;
}

void KnifeTool::findEdgeIntersections(const QVector3D& start, const QVector3D& end, QVector<CutEdge>& intersections)
{
    QVector3D direction = end - start;

    for (int i = 0; i < m_faces.size(); i += 3) {
        int i0 = m_faces[i];
        int i1 = m_faces[i + 1];
        int i2 = m_faces[i + 2];

        if (i0 >= m_vertices.size() || i1 >= m_vertices.size() || i2 >= m_vertices.size()) continue;

        QVector3D v0 = m_vertices[i0];
        QVector3D v1 = m_vertices[i1];
        QVector3D v2 = m_vertices[i2];

        QPair<QVector3D, float> hit1 = lineTriangleIntersect(start, direction, v0, v1, m_poisonThreshold);
        if (hit1.second > 0.0f && hit1.second <= 1.0f) {
            CutEdge edge;
            edge.edgeIndex = i;
            edge.t = hit1.second;
            edge.point = hit1.first;
            intersections.append(edge);
        }

        QPair<QVector3D, float> hit2 = lineTriangleIntersect(start, direction, v1, v2, m_poisonThreshold);
        if (hit2.second > 0.0f && hit2.second <= 1.0f) {
            CutEdge edge;
            edge.edgeIndex = i + 1;
            edge.t = hit2.second;
            edge.point = hit2.first;
            intersections.append(edge);
        }

        QPair<QVector3D, float> hit3 = lineTriangleIntersect(start, direction, v2, v0, m_poisonThreshold);
        if (hit3.second > 0.0f && hit3.second <= 1.0f) {
            CutEdge edge;
            edge.edgeIndex = i + 2;
            edge.t = hit3.second;
            edge.point = hit3.first;
            intersections.append(edge);
        }
    }
}

QPair<QVector3D, float> KnifeTool::lineTriangleIntersect(const QVector3D& origin, const QVector3D& dir,
                                                       const QVector3D& v0, const QVector3D& v1, float epsilon)
{
    QVector3D edge = v1 - v0;
    QVector3D normal = QVector3D::crossProduct(dir, edge);

    if (normal.lengthSquared() < epsilon * epsilon) {
        return QPair<QVector3D, float>(QVector3D(), -1.0f);
    }

    float t = QVector3D::dotProduct(v0 - origin, normal) / normal.lengthSquared();
    QVector3D point = origin + dir * t;

    float param = QVector3D::dotProduct(point - v0, edge) / edge.lengthSquared();
    if (param < 0.0f || param > 1.0f) {
        return QPair<QVector3D, float>(QVector3D(), -1.0f);
    }

    return QPair<QVector3D, float>(point, t);
}

QVector3D KnifeTool::pointOnEdge(int edgeIndex, float t) const
{
    if (edgeIndex < 0 || edgeIndex >= m_faces.size()) return QVector3D();
    int v1 = m_faces[edgeIndex];
    int faceStart = (edgeIndex / 3) * 3;
    int nextInFace = faceStart + ((edgeIndex - faceStart + 1) % 3);
    int v2 = m_faces[nextInFace];
    if (v1 >= m_vertices.size() || v2 >= m_vertices.size()) return QVector3D();
    return m_vertices[v1] * (1.0f - t) + m_vertices[v2] * t;
}

void KnifeTool::splitEdgeAtPoint(int edgeIndex, const QVector3D& point)
{
    if (edgeIndex < 0 || edgeIndex >= m_faces.size()) return;

    int v1 = m_faces[edgeIndex];
    int v2 = m_faces[(edgeIndex + 1) % m_faces.size()];
    if (v1 >= m_vertices.size() || v2 >= m_vertices.size()) return;

    int newVertIdx = m_newVertices.size();
    m_newVertices.append(point);

    m_newFaces.append(v1);
    m_newFaces.append(newVertIdx);
    m_newFaces.append(v2);
}

void KnifeTool::subdivideEdge(int edgeIndex, float t)
{
    if (edgeIndex < 0 || edgeIndex >= m_faces.size()) return;

    int v1 = m_faces[edgeIndex];
    int faceStart = (edgeIndex / 3) * 3;
    int nextInFace = faceStart + ((edgeIndex - faceStart + 1) % 3);
    int v2 = m_faces[nextInFace];

    if (v1 >= m_vertices.size() || v2 >= m_vertices.size()) return;

    QVector3D point = m_vertices[v1] * (1.0f - t) + m_vertices[v2] * t;
    splitEdgeAtPoint(edgeIndex, point);
}

void KnifeTool::addEdgeLoop(const QVector<QVector3D>& points)
{
    if (points.size() < 2) return;

    QVector<int> loopVerts;
    for (const auto& pt : points) {
        int idx = m_newVertices.size();
        m_newVertices.append(pt);
        loopVerts.append(idx);
    }

    for (int i = 0; i < loopVerts.size() - 1; ++i) {
        m_newFaces.append(loopVerts[i]);
        m_newFaces.append(loopVerts[i + 1]);
    }
}

void KnifeTool::connectCuts()
{
    if (m_cutPoints.size() < 2) return;

    QVector<int> newVertIndices;
    for (const auto& pt : m_cutPoints) {
        int idx = m_newVertices.size();
        m_newVertices.append(pt);
        newVertIndices.append(idx);
    }

    for (int i = 0; i < newVertIndices.size() - 1; ++i) {
        m_newFaces.append(newVertIndices[i]);
        m_newFaces.append(newVertIndices[i + 1]);
        m_newFaces.append(newVertIndices[i]);
    }

    emit cutCompleted();
}

// ─── LoopCutTool ─────────────────────────────────────────────────────────────

LoopCutTool::LoopCutTool(QObject* parent) : QObject(parent), m_numberOfCuts(1), m_cutPosition(0.5f) {}
LoopCutTool::~LoopCutTool() = default;

void LoopCutTool::setMeshData(const QVector<QVector3D>& vertices, const QVector<int>& faces)
{
    m_vertices = vertices;
    m_faces = faces;
}

void LoopCutTool::setEdge(const QVector3D& point, const QVector3D& direction)
{
    m_edgePoint = point;
    m_edgeDirection = direction.normalized();
}

void LoopCutTool::setNumberOfCuts(int count) { m_numberOfCuts = qMax(1, count); }
void LoopCutTool::setCutPosition(float percentage, bool relativeToSelection) {
    m_cutPosition = qBound(0.0f, percentage, 1.0f);
    m_relativeToSelection = relativeToSelection;
}

void LoopCutTool::addLoop()
{
    if (m_vertices.isEmpty() || m_faces.isEmpty()) return;

    QVector<QVector3D> loopVertices;
    QVector<int> loopFaces;

    for (int cut = 0; cut < m_numberOfCuts; ++cut) {
        float t = (cut + 1.0f) / (m_numberOfCuts + 1.0f);

        // Apply cut position offset when relative-to-selection mode is active
        if (m_relativeToSelection) {
            t = qBound(0.0f, t + (m_cutPosition - 0.5f) * 0.5f, 1.0f);
        }

        for (int i = 0; i < m_faces.size(); i += 3) {
            int i0 = m_faces[i];
            int i1 = m_faces[i + 1];
            int i2 = m_faces[i + 2];

            if (i0 >= m_vertices.size() || i1 >= m_vertices.size() || i2 >= m_vertices.size()) continue;

            QVector3D mid01 = m_vertices[i0] * (1.0f - t) + m_vertices[i1] * t;
            QVector3D mid12 = m_vertices[i1] * (1.0f - t) + m_vertices[i2] * t;
            QVector3D mid20 = m_vertices[i2] * (1.0f - t) + m_vertices[i0] * t;

            int base = loopVertices.size();
            loopVertices.append(mid01);
            loopVertices.append(mid12);
            loopVertices.append(mid20);

            loopFaces.append(base);
            loopFaces.append(base + 1);
            loopFaces.append(base + 2);
        }
    }

    m_newVertices.append(loopVertices);
    m_newFaces.append(loopFaces);
    emit loopAdded();
}

void LoopCutTool::removeLoop(int loopIndex)
{
    if (loopIndex < 0 || loopIndex >= m_edgeLoops.size()) return;
    m_edgeLoops.removeAt(loopIndex);
    emit loopsUpdated();
}

void LoopCutTool::slideLoop(int loopIndex, float delta)
{
    if (loopIndex < 0 || loopIndex >= m_edgeLoops.size()) return;

    auto& loop = m_edgeLoops[loopIndex];
    for (int& vertIdx : loop) {
        if (vertIdx < m_vertices.size()) {
            m_vertices[vertIdx] += m_edgeDirection * delta;
        }
    }
    emit loopsUpdated();
}

// ─── BisectTool ──────────────────────────────────────────────────────────────

BisectTool::BisectTool(QObject* parent) : QObject(parent), m_fillMode(0), m_clearInner(false), m_clearOuter(false) {}
BisectTool::~BisectTool() = default;

void BisectTool::setMeshData(const QVector<QVector3D>& vertices, const QVector<int>& faces)
{
    m_vertices = vertices;
    m_faces = faces;
}

void BisectTool::setPlane(const QVector3D& origin, const QVector3D& normal) { m_planeOrigin = origin; m_planeNormal = normal; }
void BisectTool::setFillMode(int mode) { m_fillMode = mode; }
void BisectTool::setClearInner(bool clear) { m_clearInner = clear; }
void BisectTool::setClearOuter(bool clear) { m_clearOuter = clear; }
void BisectTool::apply(bool markDelete)
{
    QVector<int> crossingEdges = findCrossingEdges(0.001f);
    QVector<QVector3D> newVerts = m_vertices;
    QVector<int> newFaces = m_faces;

    // Split each crossing edge at the intersection point
    QMap<int, int> edgeNewVertMap;
    for (int edgeIdx : crossingEdges) {
        float t = 0.5f; // Default midpoint; refined by classifyVertex
        QVector<QVector3D> nv;
        QVector<int> nf;
        splitCrossingEdge(edgeIdx, t, nv, nf);
        // Merge new geometry
        for (const auto& v : nv) {
            edgeNewVertMap[edgeIdx] = newVerts.size();
            newVerts.append(v);
        }
    }

    // Classify all vertices and optionally mark/delete outer side
    if (markDelete) {
        QVector<bool> toDelete(newVerts.size(), false);
        for (int i = 0; i < m_vertices.size(); ++i) {
            QVector<int> side = classifyVertex(i);
            if (!side.isEmpty() && side[0] < 0)
                toDelete[i] = true;
        }
        // Remove deleted vertices (in reverse to preserve indices)
        for (int i = toDelete.size() - 1; i >= 0; --i) {
            if (toDelete[i]) {
                newVerts.removeAt(i);
                // Update face indices
                for (int& f : newFaces) {
                    if (f == i) f = 0;
                    else if (f > i) f--;
                }
            }
        }
    }

    m_resultVertices = newVerts;
    m_resultFaces = newFaces;
    m_vertices = newVerts;
    m_faces = newFaces;
    emit bisectApplied();
}

QVector<int> BisectTool::classifyVertex(int index)
{
    if (index < 0 || index >= m_vertices.size()) return {};
    QVector3D v = m_vertices[index];
    float dist = QVector3D::dotProduct(v - m_planeOrigin, m_planeNormal);
    // Returns side: positive = above plane (inner), negative = below (outer)
    return { dist >= 0.0f ? 1 : -1 };
}

QVector<int> BisectTool::findCrossingEdges(float threshold)
{
    QVector<int> crossingEdges;
    // Iterate faces to find edges that cross the plane
    for (int i = 0; i + 2 < m_faces.size(); i += 3) {
        int v0 = m_faces[i];
        int v1 = m_faces[i + 1];
        int v2 = m_faces[i + 2];

        if (v0 >= m_vertices.size() || v1 >= m_vertices.size() || v2 >= m_vertices.size())
            continue;

        float d0 = QVector3D::dotProduct(m_vertices[v0] - m_planeOrigin, m_planeNormal);
        float d1 = QVector3D::dotProduct(m_vertices[v1] - m_planeOrigin, m_planeNormal);
        float d2 = QVector3D::dotProduct(m_vertices[v2] - m_planeOrigin, m_planeNormal);

        // Edge crosses plane if endpoints are on opposite sides
        if ((d0 * d1 < -threshold) || (d1 * d2 < -threshold) || (d2 * d0 < -threshold)) {
            if (!crossingEdges.contains(i / 3))
                crossingEdges.append(i / 3);
        }
    }
    return crossingEdges;
}

void BisectTool::splitCrossingEdge(int edgeIndex, float t, QVector<QVector3D>& newVerts, QVector<int>& newFaces)
{
    // Find the face and determine which edge crosses
    int faceBase = edgeIndex * 3;
    if (faceBase + 2 >= m_faces.size()) return;

    int v0idx = m_faces[faceBase];
    int v1idx = m_faces[faceBase + 1];

    if (v0idx >= m_vertices.size() || v1idx >= m_vertices.size()) return;

    QVector3D v0 = m_vertices[v0idx];
    QVector3D v1 = m_vertices[v1idx];

    // Interpolate the crossing point
    QVector3D midPoint = v0 * (1.0f - t) + v1 * t;
    int newIdx = m_vertices.size() + newVerts.size();
    newVerts.append(midPoint);

    // The new vertex index will be used by the caller to rebuild faces
    newFaces.append(newIdx);
}