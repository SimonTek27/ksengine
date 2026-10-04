#include "SculptLayer.h"
#include <cmath>
#include <algorithm>
#include <QSet>

SculptLayersManager::SculptLayersManager(QObject* parent) : QObject(parent) {}

SculptLayersManager::~SculptLayersManager() = default;

int SculptLayersManager::addLayer(const QString& name) {
    SculptLayer layer(name);
    m_layers.append(layer);
    if (m_layers.size() == 1) {
        setCurrentLayer(0);
    }
    emit layerAdded(m_layers.size() - 1);
    emit layersChanged();
    return m_layers.size() - 1;
}

bool SculptLayersManager::removeLayer(int index) {
    if (index < 0 || index >= m_layers.size()) return false;
    if (m_layers.size() <= 1) return false; // Keep at least one layer
    
    m_layers.removeAt(index);
    
    // Adjust current layer index if needed
    if (m_currentLayer >= m_layers.size()) {
        m_currentLayer = m_layers.size() - 1;
    }
    if (m_currentLayer < 0) m_currentLayer = 0;
    
    emit layerRemoved(index);
    emit layersChanged();
    emit currentLayerChanged(m_currentLayer);
    return true;
}

bool SculptLayersManager::renameLayer(int index, const QString& newName) {
    if (index < 0 || index >= m_layers.size()) return false;
    m_layers[index].name = newName;
    emit layerRenamed(index, newName);
    return true;
}

void SculptLayersManager::setLayerVisible(int index, bool visible) {
    if (index < 0 || index >= m_layers.size()) return;
    m_layers[index].visible = visible;
    emit layerVisibilityChanged(index, visible);
}

void SculptLayersManager::setLayerLocked(int index, bool locked) {
    if (index < 0 || index >= m_layers.size()) return;
    m_layers[index].locked = locked;
    emit layerLockedChanged(index, locked);
}

void SculptLayersManager::setLayerBlendMode(int index, float mode) {
    if (index < 0 || index >= m_layers.size()) return;
    m_layers[index].blendMode = mode;
    emit layerBlendModeChanged(index, mode);
}

void SculptLayersManager::setLayerOpacity(int index, float opacity) {
    if (index < 0 || index >= m_layers.size()) return;
    m_layers[index].opacity = opacity;
    emit layerOpacityChanged(index, opacity);
}

void SculptLayersManager::setCurrentLayer(int index) {
    if (index < 0 || index >= m_layers.size()) return;
    if (m_currentLayer != index) {
        m_currentLayer = index;
        emit currentLayerChanged(index);
    }
}

void SculptLayersManager::setVertexWeight(int layerIndex, int vertexIndex, float weight) {
    if (layerIndex < 0 || layerIndex >= m_layers.size()) return;
    if (vertexIndex < 0 || vertexIndex >= m_layers[layerIndex].vertexWeights.size()) {
        // Extend weights vector if needed
        int needed = vertexIndex + 1 - m_layers[layerIndex].vertexWeights.size();
        m_layers[layerIndex].vertexWeights.resize(m_layers[layerIndex].vertexWeights.size() + needed, 0.0f);
    }
    m_layers[layerIndex].vertexWeights[vertexIndex] = weight;
    // Clamp to 0-1
    m_layers[layerIndex].vertexWeights[vertexIndex] = qBound(0.0f, m_layers[layerIndex].vertexWeights[vertexIndex], 1.0f);
}

float SculptLayersManager::vertexWeight(int layerIndex, int vertexIndex) const {
    if (layerIndex < 0 || layerIndex >= m_layers.size()) return 0.0f;
    if (vertexIndex < 0 || vertexIndex >= m_layers[layerIndex].vertexWeights.size()) return 0.0f;
    return m_layers[layerIndex].vertexWeights[vertexIndex];
}

QVector<float> SculptLayersManager::vertexWeights(int layerIndex) const {
    if (layerIndex < 0 || layerIndex >= m_layers.size()) return QVector<float>();
    return m_layers[layerIndex].vertexWeights;
}

int SculptLayersManager::sculptBrush(int layerIndex, const QVector3D& center, float radius, float strength, int mode,
                                     const QVector3D& drag, const QVector3D& previousCenter,
                                     float falloffPower, const QSet<int>* pinned) {
    if (layerIndex < 0 || layerIndex >= m_layers.size()) return 0;
    if (m_layers[layerIndex].locked) return 0;
    
    SculptLayer& layer = m_layers[layerIndex];
    int affected = 0;
    
    int vertCount = m_vertexPositions.isEmpty() ? layer.vertexWeights.size() : m_vertexPositions.size();
    
    for (int vi = 0; vi < vertCount; ++vi) {
        // Skip pinned vertices
        if (pinned && pinned->contains(vi)) continue;
        
        // Compute real distance from brush center
        float d;
        if (!m_vertexPositions.isEmpty() && vi < m_vertexPositions.size()) {
            d = (m_vertexPositions[vi] - center).length();
        } else {
            d = qSqrt(float(vi * vi % 100 + vi / 10)) * 0.1f;
            if (d > radius) continue;
        }
        if (d > radius) continue;
        
        float t = 1.0f - (d / radius);
        float falloff;
        if (qAbs(falloffPower - 2.0f) < 1e-4f)
            falloff = t * t * (3.0f - 2.0f * t);
        else
            falloff = t > 0.0f ? qPow(t, qBound(0.25f, falloffPower, 8.0f)) : 0.0f;
        if (falloff <= 0.001f) continue;
        affected++;
        
        // Ensure weights vector is large enough
        if (vi >= layer.vertexWeights.size()) {
            layer.vertexWeights.resize(vi + 1, 0.0f);
        }
        
        // Update weight based on brush mode
        switch (mode) {
            case 0: // draw - add weight
                layer.vertexWeights[vi] = qMin(1.0f, layer.vertexWeights[vi] + strength * falloff);
                break;
            case 1: { // smooth - average with neighbors
                float avgWeight = layer.vertexWeights[vi];
                int count = 1;
                // Simple: smooth with adjacent vertex weights
                if (vi > 0) { avgWeight += layer.vertexWeights[vi - 1]; count++; }
                if (vi < layer.vertexWeights.size() - 1) { avgWeight += layer.vertexWeights[vi + 1]; count++; }
                avgWeight /= float(count);
                layer.vertexWeights[vi] = layer.vertexWeights[vi] * (1.0f - strength * falloff) +
                                          avgWeight * (strength * falloff);
                break;
            }
            case 2: // grab - pull weight toward center's weight
                if (vi > 0 && vi < layer.vertexWeights.size()) {
                    float centerWeight = layer.vertexWeights[vi];
                    layer.vertexWeights[vi] = centerWeight * (1.0f - strength * falloff * 0.5f);
                }
                break;
            default:
                break;
        }
        
        // Clamp weight
        layer.vertexWeights[vi] = qBound(0.0f, layer.vertexWeights[vi], 1.0f);
    }
    
    emit sculptUpdated();
    return affected;
}

void SculptLayersManager::bakeCurrentLayer() {
    if (m_currentLayer < 0 || m_currentLayer >= m_layers.size()) return;
    if (m_layers[m_currentLayer].vertexWeights.isEmpty()) return;
    
    SculptLayer& layer = m_layers[m_currentLayer];
    
    // Apply layer weights to base mesh position deltas
    if (m_baseDeltas) {
        for (int vi = 0; vi < layer.vertexWeights.size() && vi < m_baseDeltas->size(); ++vi) {
            float w = layer.vertexWeights[vi] * layer.opacity;
            if (w <= 0.0f) continue;
            
            QVector3D delta;
            if (layer.blendMode == 0) { // additive
                delta = QVector3D(0, w * 0.01f, 0);
            } else if (layer.blendMode == 1) { // subtractive
                delta = QVector3D(0, -w * 0.01f, 0);
            } else { // replace
                delta = QVector3D(0, w * 0.01f, 0);
            }
            
            (*m_baseDeltas)[vi] += delta;
        }
    }
    
    // Clear layer weights after baking
    layer.vertexWeights.clear();
    
    emit baked();
}

void SculptLayersManager::setVertexPositions(const QVector<QVector3D>& positions) {
    m_vertexPositions = positions;
}

void SculptLayersManager::setVertexNormals(const QVector<QVector3D>& normals) {
    m_vertexNormals = normals;
}

void SculptLayersManager::setBasePositionDeltas(QVector<QVector3D>* deltas) {
    m_baseDeltas = deltas;
}