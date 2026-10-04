#include "AdvancedSculpt.h"
#include <QDebug>
#include <QtMath>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

namespace ks {
namespace graphics {

AdvancedSculptMode::AdvancedSculptMode(QObject* parent)
    : SculptMode(parent)
    , m_sculptMode(SculptModeType::Draw)
    , m_dyntopoEnabled(false)
    , m_detail(8.0f)
    , m_autoMaskEnabled(false)
{
}

AdvancedSculptMode::~AdvancedSculptMode() = default;

void AdvancedSculptMode::setSculptMode(SculptModeType mode)
{
    m_sculptMode = mode;
}

void AdvancedSculptMode::setDyntopo(bool enabled)
{
    m_dyntopoEnabled = enabled;
    if (enabled) {
        dyntopo.enabled = true;
    }
}

void AdvancedSculptMode::setRetopoMode(bool enabled)
{
    m_retopoMode = enabled;
    if (enabled) {
        // Activate retopo-aware edge splitting
        dyntopo.retopoMode = true;
    } else {
        dyntopo.retopoMode = false;
    }
}

void AdvancedSculptMode::setDetail(float detail)
{
    m_detail = detail;
    dyntopo.detail = detail;
}

void AdvancedSculptMode::setDetailRefine(float refine)
{
    dyntopo.detailRefine = refine;
}

void AdvancedSculptMode::setDetailScale(float scale)
{
    dyntopo.detailScale = scale;
}

void AdvancedSculptMode::setSymmetry(SymmetryMode mode)
{
    symmetry = mode;
}

void AdvancedSculptMode::setAutoMask(bool enabled)
{
    m_autoMaskEnabled = enabled;
}

void AdvancedSculptMode::addStroke(const QVector3D& position, const QVector3D& normal, float pressure, bool active)
{
    if (active) {
        beginStroke(position);
    }
    sculptPoint(const_cast<QVector3D&>(position), normal, pressure);
    SculptMode::addPoint(position, normal, pressure);
    if (!active) {
        endStroke();
    }
}

void AdvancedSculptMode::sculptPoint(QVector3D& vertex, const QVector3D& normal, float strength)
{
    switch (m_sculptMode) {
        case SculptModeType::Draw:
            vertex += normal * strength * 0.01f;
            break;
        case SculptModeType::DrawSharp:
            vertex += normal * strength * 0.02f;
            break;
        case SculptModeType::Flatten:
        case SculptModeType::Plane:
            vertex.setY(vertex.y() * (1.0f - strength));
            break;
        case SculptModeType::Smooth:
            vertex = vertex * (1.0f - strength * 0.5f);
            break;
        case SculptModeType::Grab:
        case SculptModeType::GrabElastic:
            vertex += normal * strength * 0.01f;
            break;
        case SculptModeType::Inflated:
        case SculptModeType::Inflator:
            vertex += normal * strength * 0.015f;
            break;
        case SculptModeType::Snake:
            vertex += normal * strength * 0.02f;
            break;
        case SculptModeType::ClayStrips:
        case SculptModeType::ClayThumb:
            vertex += normal * strength * 0.012f;
            break;
        case SculptModeType::DrawCrease:
            vertex += normal * strength * 0.005f;
            break;
        case SculptModeType::Pinch:
        case SculptModeType::PinchSlide:
            vertex -= normal * strength * 0.01f;
            break;
        case SculptModeType::Mask:
            break;
        case SculptModeType::Nudge:
            vertex += normal * strength * 0.015f;
            break;
        default:
            vertex += normal * strength * 0.01f;
            break;
    }
}

void AdvancedSculptMode::refineDynamicTopology(const QVector3D& point, const QVector3D& normal)
{
    if (!m_dyntopoEnabled || !m_mesh) return;

    float targetEdgeLength = m_detail * 0.5f;
    if (targetEdgeLength <= 0.0f) return;

    // Find faces near brush area and split their long edges
    QVector<QPair<int,int>> edgesToSplit;
    
    for (int fi = 0; fi + 2 < m_mesh->indices.size(); fi += 3) {
        int f0 = m_mesh->indices[fi], f1 = m_mesh->indices[fi + 1], f2 = m_mesh->indices[fi + 2];
        if (f0 >= m_mesh->vertices.size() || f1 >= m_mesh->vertices.size() || f2 >= m_mesh->vertices.size())
            continue;
        
        QVector3D center = (m_mesh->vertices[f0].position + m_mesh->vertices[f1].position + m_mesh->vertices[f2].position) / 3.0f;
        float dist = (center - point).length();
        if (dist > m_brushRadius * 2.0f) continue;
        
        // Check each edge of this triangle
        int faceVerts[3] = {f0, f1, f2};
        for (int e = 0; e < 3; ++e) {
            int v0 = faceVerts[e];
            int v1 = faceVerts[(e + 1) % 3];
            float edgeLen = (m_mesh->vertices[v0].position - m_mesh->vertices[v1].position).length();
            if (edgeLen > targetEdgeLength * 2.0f) {
                edgesToSplit.append({v0, v1});
            }
        }
    }
    
    // Split edges by inserting midpoint vertex
    for (const auto& edge : edgesToSplit) {
        int v0 = edge.first, v1 = edge.second;
        if (v0 >= m_mesh->vertices.size() || v1 >= m_mesh->vertices.size()) continue;
        
        QVector3D midPoint = (m_mesh->vertices[v0].position + m_mesh->vertices[v1].position) * 0.5f;
        ks::Vertex newVertex;
        newVertex.position = midPoint;
        newVertex.normal = (m_mesh->vertices[v0].normal + m_mesh->vertices[v1].normal).normalized();
        int newIdx = m_mesh->vertices.size();
        m_mesh->vertices.append(newVertex);
        
        // Find faces containing this edge and split them
        for (int fi = 0; fi + 2 < m_mesh->indices.size(); fi += 3) {
            int faceVerts[3] = {m_mesh->indices[fi], m_mesh->indices[fi+1], m_mesh->indices[fi+2]};
            for (int e = 0; e < 3; ++e) {
                int ev0 = faceVerts[e], ev1 = faceVerts[(e+1)%3];
                if ((ev0 == v0 && ev1 == v1) || (ev0 == v1 && ev1 == v0)) {
                    // Split this face: replace edge (v0,v1) with two faces
                    int opp = faceVerts[(e+2)%3];
                    m_mesh->indices[fi] = v0; m_mesh->indices[fi+1] = newIdx; m_mesh->indices[fi+2] = opp;
                    m_mesh->indices.append(newIdx); m_mesh->indices.append(v1); m_mesh->indices.append(opp);
                    break;
                }
            }
        }
    }
}

void AdvancedSculptMode::collapseLongEdges()
{
    if (!m_mesh) return;

    float maxLength = m_detail * 3.0f;
    if (maxLength <= 0.0f) return;
    
    QSet<int> verticesToRemove;
    
    // Find edges to collapse: merge shorter vertex into longer one
    for (int fi = 0; fi + 2 < m_mesh->indices.size(); fi += 3) {
        int f0 = m_mesh->indices[fi], f1 = m_mesh->indices[fi+1], f2 = m_mesh->indices[fi+2];
        if (f0 >= m_mesh->vertices.size() || f1 >= m_mesh->vertices.size() || f2 >= m_mesh->vertices.size())
            continue;
        
        int faceVerts[3] = {f0, f1, f2};
        for (int e = 0; e < 3; ++e) {
            int v0 = faceVerts[e];
            int v1 = faceVerts[(e+1)%3];
            if (v0 >= m_mesh->vertices.size() || v1 >= m_mesh->vertices.size()) continue;
            if (verticesToRemove.contains(v0) || verticesToRemove.contains(v1)) continue;
            
            float len = (m_mesh->vertices[v0].position - m_mesh->vertices[v1].position).length();
            if (len > maxLength) {
                // Collapse: remove the vertex with fewer incident edges (simpler)
                verticesToRemove.insert(v1);
                // Move v0 to midpoint (average) for smoother collapse
                m_mesh->vertices[v0].position = (m_mesh->vertices[v0].position + m_mesh->vertices[v1].position) * 0.5f;
            }
        }
    }
    
    // Remove vertices (from highest index first to preserve indices)
    QVector<int> sortedToRemove = verticesToRemove.values();
    std::sort(sortedToRemove.begin(), sortedToRemove.end(), std::greater<int>());
    
    for (int idx : sortedToRemove) {
        if (idx < m_mesh->vertices.size()) {
            m_mesh->vertices.removeAt(idx);
        }
    }
    
    // Remove faces that reference removed vertices
    QVector<int> facesToRemove;
    for (int fi = 0; fi + 2 < m_mesh->indices.size(); fi += 3) {
        int f0 = m_mesh->indices[fi], f1 = m_mesh->indices[fi+1], f2 = m_mesh->indices[fi+2];
        if (verticesToRemove.contains(f0) || verticesToRemove.contains(f1) || verticesToRemove.contains(f2)) {
            facesToRemove.append(fi);
        }
    }
    std::sort(facesToRemove.begin(), facesToRemove.end(), std::greater<int>());
    for (int fi : facesToRemove) {
        if (fi + 2 < m_mesh->indices.size()) {
            m_mesh->indices.removeAt(fi + 2);
            m_mesh->indices.removeAt(fi + 1);
            m_mesh->indices.removeAt(fi);
        }
    }
}

void AdvancedSculptMode::removeDoubles()
{
    if (!m_mesh) return;

    float threshold = 0.001f;
    QVector<int> toRemove;

    for (int i = 0; i < m_mesh->vertices.size(); ++i) {
        for (int j = i + 1; j < m_mesh->vertices.size(); ++j) {
            float dist = (m_mesh->vertices[i].position - m_mesh->vertices[j].position).length();
            if (dist < threshold) {
                toRemove.append(j);
            }
        }
    }

    QSet<int> uniqueToRemove(toRemove.begin(), toRemove.end());
    QVector<int> sortedToRemove = uniqueToRemove.values();
    std::sort(sortedToRemove.begin(), sortedToRemove.end(), std::greater<int>());

    for (int idx : sortedToRemove) {
        if (idx < m_mesh->vertices.size()) {
            m_mesh->vertices.removeAt(idx);
        }
    }
}

void AdvancedSculptMode::maskVertex(int index, float maskValue)
{
    if (!m_mesh || index < 0 || index >= m_mesh->vertices.size()) return;
    m_mesh->vertices[index].mask = qBound(0.0f, maskValue, 1.0f);
}

void AdvancedSculptMode::unmaskVertex(int index)
{
    if (!m_mesh || index < 0 || index >= m_mesh->vertices.size()) return;
    m_mesh->vertices[index].mask = 0.0f;
}

void AdvancedSculptMode::invertMask()
{
    if (!m_mesh) return;
    for (ks::Vertex& v : m_mesh->vertices) {
        v.mask = 1.0f - v.mask;
    }
}

void AdvancedSculptMode::clearMask()
{
    if (!m_mesh) return;
    for (ks::Vertex& v : m_mesh->vertices) {
        v.mask = 0.0f;
    }
    m_maskedVertices.clear();
}

void AdvancedSculptMode::smoothMask(int iterations)
{
    if (!m_mesh) return;

    for (int iter = 0; iter < iterations; ++iter) {
        QVector<float> newMasks = QVector<float>(m_mesh->vertices.size());
        for (int i = 0; i < m_mesh->vertices.size(); ++i) {
            float sum = m_mesh->vertices[i].mask;
            int count = 1;
            for (const Edge& e : m_mesh->edges) {
                if (e.v1 == i) { sum += m_mesh->vertices[e.v2].mask; count++; }
                else if (e.v2 == i) { sum += m_mesh->vertices[e.v1].mask; count++; }
            }
            newMasks[i] = sum / count;
        }
        for (int i = 0; i < m_mesh->vertices.size(); ++i) {
            m_mesh->vertices[i].mask = newMasks[i];
        }
    }
}

void AdvancedSculptMode::growMask()
{
    if (!m_mesh) return;
    QVector<int> toGrow;
    for (int i = 0; i < m_mesh->vertices.size(); ++i) {
        if (m_mesh->vertices[i].mask > 0.0f) {
            for (const Edge& e : m_mesh->edges) {
                if (e.v1 == i) toGrow.append(e.v2);
                else if (e.v2 == i) toGrow.append(e.v1);
            }
        }
    }
    for (int idx : toGrow) {
        if (idx < m_mesh->vertices.size()) m_mesh->vertices[idx].mask = 1.0f;
    }
}

void AdvancedSculptMode::shrinkMask()
{
    if (!m_mesh) return;
    QVector<int> toShrink;
    for (int i = 0; i < m_mesh->vertices.size(); ++i) {
        if (m_mesh->vertices[i].mask > 0.0f) {
            bool hasUnmasked = false;
            for (const Edge& e : m_mesh->edges) {
                if (e.v1 == i && m_mesh->vertices[e.v2].mask == 0.0f) hasUnmasked = true;
                else if (e.v2 == i && m_mesh->vertices[e.v1].mask == 0.0f) hasUnmasked = true;
            }
            if (hasUnmasked) toShrink.append(i);
        }
    }
    for (int idx : toShrink) {
        if (idx < m_mesh->vertices.size()) m_mesh->vertices[idx].mask = 0.0f;
    }
}

float AdvancedSculptMode::getAutoMaskValue(int vertex) const
{
    if (m_autoMaskEnabled && m_autoMask.contains(vertex)) {
        return m_autoMask.value(vertex);
    }
    return 1.0f;
}

SculptProject::SculptProject(QObject* parent)
    : QObject(parent)
    , m_activeLayer(0)
{
}

SculptProject::~SculptProject() = default;

void SculptProject::addLayer(const QString& name)
{
    Layer layer;
    layer.name = name;
    m_layers.append(layer);

    QVector<QVector<int>> strokes;
    m_layerStrokes.append(strokes);

    emit layerAdded(m_layers.size() - 1);
}

void SculptProject::removeLayer(int index)
{
    if (index >= 0 && index < m_layers.size()) {
        m_layers.removeAt(index);
        m_layerStrokes.removeAt(index);
        emit layerRemoved(index);
    }
}

void SculptProject::setActiveLayer(int index)
{
    if (index >= 0 && index < m_layers.size()) {
        m_activeLayer = index;
        emit activeLayerChanged(index);
    }
}

void SculptProject::addMaskStroke(int layerIndex, const QVector<int>& vertices, float strength)
{
    if (layerIndex < 0 || layerIndex >= m_layers.size()) return;
    if (layerIndex >= m_layerStrokes.size()) return;

    Layer& layer = m_layers[layerIndex];
    for (int idx : vertices) {
        if (idx >= 0 && idx < layer.maskValues.size()) {
            layer.maskValues[idx] = qBound(0.0f, layer.maskValues[idx] + strength, 1.0f);
        }
    }

    m_layerStrokes[layerIndex].append(vertices);

    emit layerModified(layerIndex);
}

void SculptProject::undoLayerStroke(int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= m_layerStrokes.size()) return;
    if (m_layerStrokes[layerIndex].isEmpty()) return;

    // Ensure redo stack is ready
    while (m_redoStrokes.size() <= layerIndex)
        m_redoStrokes.append(QVector<QVector<int>>());

    m_redoStrokes[layerIndex].append(m_layerStrokes[layerIndex].last());
    m_layerStrokes[layerIndex].removeLast();
    emit layerModified(layerIndex);
}

void SculptProject::redoLayerStroke(int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= m_layerStrokes.size()) return;
    if (m_redoStrokes.size() <= layerIndex || m_redoStrokes[layerIndex].isEmpty()) return;

    m_layerStrokes[layerIndex].append(m_redoStrokes[layerIndex].last());
    m_redoStrokes[layerIndex].removeLast();
    emit layerModified(layerIndex);
}

QVector<int> SculptProject::getMaskedVertices(int layer) const
{
    if (layer < 0 || layer >= m_layerStrokes.size()) return {};
    
    // Collect unique masked vertices from all strokes on this layer
    QSet<int> masked;
    for (const auto& stroke : m_layerStrokes[layer]) {
        for (int vert : stroke) {
            if (vert >= 0) masked.insert(vert);
        }
    }
    return QVector<int>(masked.constBegin(), masked.constEnd());
}

DyntopoRefiner::DyntopoRefiner(QObject* parent)
    : QObject(parent)
    , m_detail(5.0f)
    , m_originalDetail(5.0f)
{
}

DyntopoRefiner::~DyntopoRefiner() = default;

void DyntopoRefiner::setDetail(float detail)
{
    m_detail = detail;
}

void DyntopoRefiner::setOriginalDetail(float original)
{
    m_originalDetail = original;
}

MeshData DyntopoRefiner::refine(MeshData input, const QVector3D& brushPosition, float radius)
{
    QVector<EdgeRef> toCollapse = findCollapseCandidates(input, m_detail / 10.0f);
    MeshData subdivided = subdivideEdges(input, radius / 2.0f);

    MeshData result = subdivided;

    return result;
}

void DyntopoRefiner::addVertex(MeshData& mesh, const QVector3D& position)
{
    Vertex v;
    v.position = position;
    v.normal = QVector3D(0, 1, 0);
    v.uv = QVector2D(0, 0);
    v.color = QVector4D(1, 1, 1, 1);
    mesh.vertices.append(v);
}

void DyntopoRefiner::addFace(MeshData& mesh, int v1, int v2, int v3)
{
    Face face;
    face.indices.append(v1);
    face.indices.append(v2);
    face.indices.append(v3);
    mesh.faces.append(face);
}

QVector<float> DyntopoRefiner::getEdgeLengths(MeshData input)
{
    QVector<float> lengths;

    for (const Face& face : input.faces) {
        for (int i = 0; i < 3; ++i) {
            int j = (i + 1) % 3;
            QVector3D p1 = input.vertices[face[i]].position;
            QVector3D p2 = input.vertices[face[j]].position;
            lengths.append((p1 - p2).length());
        }
    }

    return lengths;
}

QVector<DyntopoRefiner::EdgeRef> DyntopoRefiner::findCollapseCandidates(MeshData input, float threshold)
{
    QVector<EdgeRef> candidates;

    for (int i = 0; i < input.edges.size(); ++i) {
        const Edge& edge = input.edges[i];
        float len = (input.vertices[edge.v1].position - input.vertices[edge.v2].position).length();
        if (len < threshold) {
            EdgeRef ref;
            ref.index = i;
            ref.length = len;
            candidates.append(ref);
        }
    }

    return candidates;
}

QVector<DyntopoRefiner::EdgeRef> DyntopoRefiner::findSubdivisionCandidates(MeshData input, float length)
{
    QVector<EdgeRef> candidates;

    for (int i = 0; i < input.edges.size(); ++i) {
        const Edge& edge = input.edges[i];
        float len = (input.vertices[edge.v1].position - input.vertices[edge.v2].position).length();
        if (len > length * 2.0f) {
            EdgeRef ref;
            ref.index = i;
            ref.length = len;
            candidates.append(ref);
        }
    }

    return candidates;
}

MeshData DyntopoRefiner::collapseEdges(MeshData input, float threshold)
{
    QVector<EdgeRef> candidates = findCollapseCandidates(input, threshold);

    QSet<int> verticesToRemove;
    for (const EdgeRef& edge : candidates) {
        verticesToRemove.insert(input.edges[edge.index].v2);
    }

    QVector<int> vertexMap(input.vertices.size());
    int newIndex = 0;
    for (int i = 0; i < input.vertices.size(); ++i) {
        if (!verticesToRemove.contains(i)) {
            vertexMap[i] = newIndex++;
        }
    }

    MeshData output;
    for (int i = 0; i < input.vertices.size(); ++i) {
        if (!verticesToRemove.contains(i)) {
            output.vertices.append(input.vertices[i]);
        }
    }

    return output;
}

MeshData DyntopoRefiner::subdivideEdges(MeshData input, float length)
{
    QVector<EdgeRef> candidates = findSubdivisionCandidates(input, length);

    MeshData output = input;
    for (const EdgeRef& edge : candidates) {
        const Edge& e = input.edges[edge.index];
        QVector3D midPoint = (input.vertices[e.v1].position + input.vertices[e.v2].position) * 0.5f;
        addVertex(output, midPoint);
    }

    return output;
}

SculptBrushPreset::SculptBrushPreset(QObject* parent)
    : QObject(parent)
{
    buildDefaultPresets();
}

SculptBrushPreset::~SculptBrushPreset() = default;

void SculptBrushPreset::addPreset(const PresetData& preset)
{
    m_presets.append(preset);
}

void SculptBrushPreset::removePreset(int index)
{
    if (index >= 0 && index < m_presets.size()) {
        m_presets.removeAt(index);
    }
}

SculptBrushPreset::PresetData* SculptBrushPreset::getPreset(int index)
{
    if (index >= 0 && index < m_presets.size()) {
        return &m_presets[index];
    }
    return nullptr;
}

void SculptBrushPreset::buildDefaultPresets()
{
    {
        PresetData p;
        p.name = "Standard";
        p.mode = AdvancedSculptMode::SculptModeType::Draw;
        p.size = 1.0f;
        p.strength = 1.0f;
        m_presets.append(p);
    }

    {
        PresetData p;
        p.name = "Smooth";
        p.mode = AdvancedSculptMode::SculptModeType::Smooth;
        p.size = 1.0f;
        p.strength = 0.5f;
        m_presets.append(p);
    }

    {
        PresetData p;
        p.name = "Clay";
        p.mode = AdvancedSculptMode::SculptModeType::ClayThumb;
        p.size = 1.5f;
        p.strength = 0.8f;
        m_presets.append(p);
    }

    {
        PresetData p;
        p.name = "Flatten";
        p.mode = AdvancedSculptMode::SculptModeType::Flatten;
        p.size = 1.0f;
        p.strength = 0.7f;
        m_presets.append(p);
    }

    {
        PresetData p;
        p.name = "Grab";
        p.mode = AdvancedSculptMode::SculptModeType::Grab;
        p.size = 1.5f;
        p.strength = 1.0f;
        m_presets.append(p);
    }
}

void SculptBrushPreset::loadPresets()
{
    QString presetPath = QDir::homePath() + "/.kseditor/sculpt_presets.json";
    QFile file(presetPath);
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isArray()) return;

    m_presets.clear();
    QJsonArray arr = doc.array();
    for (const QJsonValue& val : arr) {
        QJsonObject obj = val.toObject();
        PresetData p;
        p.name = obj["name"].toString();
        p.mode = static_cast<AdvancedSculptMode::SculptModeType>(obj["mode"].toInt());
        p.size = obj["size"].toDouble();
        p.strength = obj["strength"].toDouble();
        p.autoSmoothFactor = obj["autoSmoothFactor"].toDouble();
        p.autoSmoothSteps = obj["autoSmoothSteps"].toInt();
        p.useAccumulate = obj["useAccumulate"].toBool();
        p.useDirection = obj["useDirection"].toDouble();
        p.useInvert = obj["useInvert"].toBool();
        p.useFrontface = obj["useFrontface"].toBool();
        p.shape = obj["shape"].toInt();
        p.textureId = obj["textureId"].toInt();
        p.curve.curve = QVector<float>(); // curve data not serialized in flat JSON
        p.curve.useFalloff = obj["useFalloff"].toBool(true);
        p.curve.curveMapping = obj["curveMapping"].toDouble();
        p.normalWeight.normalWeight = obj["normalWeight"].toDouble();
        p.normalWeight.useNormalWeight = obj["useNormalWeight"].toBool();
        p.restrictUpperlip = obj["restrictUpperlip"].toBool();
        p.restrictLowermouth = obj["restrictLowermouth"].toBool();
        p.useLockedMask = obj["useLockedMask"].toBool();
        m_presets.append(p);
    }
    emit presetsLoaded();
}

void SculptBrushPreset::savePresets()
{
    QString presetPath = QDir::homePath() + "/.kseditor/sculpt_presets.json";
    QDir().mkpath(QDir::homePath() + "/.kseditor");

    QJsonArray arr;
    for (const PresetData& p : m_presets) {
        QJsonObject obj;
        obj["name"] = p.name;
        obj["mode"] = static_cast<int>(p.mode);
        obj["size"] = p.size;
        obj["strength"] = p.strength;
        obj["autoSmoothFactor"] = p.autoSmoothFactor;
        obj["autoSmoothSteps"] = p.autoSmoothSteps;
        obj["useAccumulate"] = p.useAccumulate;
        obj["useDirection"] = p.useDirection;
        obj["useInvert"] = p.useInvert;
        obj["useFrontface"] = p.useFrontface;
        obj["shape"] = p.shape;
        obj["textureId"] = p.textureId;
        obj["useFalloff"] = p.curve.useFalloff;
        obj["curveMapping"] = p.curve.curveMapping;
        obj["normalWeight"] = p.normalWeight.normalWeight;
        obj["useNormalWeight"] = p.normalWeight.useNormalWeight;
        obj["restrictUpperlip"] = p.restrictUpperlip;
        obj["restrictLowermouth"] = p.restrictLowermouth;
        obj["useLockedMask"] = p.useLockedMask;
        arr.append(obj);
    }

    QFile file(presetPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(arr).toJson());
        file.close();
    }
    emit presetsSaved();
}

} // namespace graphics
} // namespace ks