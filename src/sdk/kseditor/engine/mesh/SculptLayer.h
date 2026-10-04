#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QVector3D>
#include <QVector2D>

struct SculptLayer {
    QString name;
    bool visible = true;
    float opacity = 1.0f;
    bool locked = false;
    float blendMode = 0.0f; // 0=additive, 1=subtractive, 2=replace
    QVector<float> vertexWeights; // per-vertex weights for this layer
    
    SculptLayer() {}
    SculptLayer(const QString& n, bool vis = true, float opac = 1.0f, 
                bool lock = false, float blend = 0.0f)
        : name(n), visible(vis), opacity(opac), locked(lock), blendMode(blend) {}
    
    float effectiveWeight(float baseWeight) const {
        if (blendMode == 0) return baseWeight + vertexWeights.isEmpty() ? 0.0f : vertexWeights.isEmpty() ? baseWeight : vertexWeights.first();
        if (blendMode == 1) return baseWeight * (1.0f - (vertexWeights.isEmpty() ? 0.0f : vertexWeights.first()));
        if (blendMode == 2) return vertexWeights.isEmpty() ? 0.0f : vertexWeights.first();
        return baseWeight;
    }
    
    void applyToVertex(int vertexIndex, float basePosition, QVector3D& resultPos,
                       const QVector3D& sculptDelta) {
        float w = effectiveWeight(1.0f); // normalized weight
        if (locked) return;
        
        if (blendMode == 0) { // additive
            resultPos = sculptDelta * w * opacity;
        } else if (blendMode == 1) { // subtractive
            resultPos = -sculptDelta * w * opacity;
        } else { // replace
            resultPos = sculptDelta * w * opacity;
        }
    }
};

class SculptLayersManager : public QObject
{
    Q_OBJECT
public:
    explicit SculptLayersManager(QObject* parent = nullptr);
    ~SculptLayersManager();
    
    // Layer management
    int addLayer(const QString& name);
    bool removeLayer(int index);
    bool renameLayer(int index, const QString& newName);
    void setLayerVisible(int index, bool visible);
    void setLayerLocked(int index, bool locked);
    void setLayerBlendMode(int index, float mode); // 0=additive, 1=subtractive, 2=replace
    void setLayerOpacity(int index, float opacity);
    
    // Current layer
    int currentLayer() const { return m_currentLayer; }
    void setCurrentLayer(int index);
    
    // All layers
    const QVector<SculptLayer>& layers() const { return m_layers; }
    int layerCount() const { return m_layers.size(); }
    
    // Weight management per vertex
    void setVertexWeight(int layerIndex, int vertexIndex, float weight);
    float vertexWeight(int layerIndex, int vertexIndex) const;
    QVector<float> vertexWeights(int layerIndex) const;
    
    // Sculpting operations
    int sculptBrush(int layerIndex, const QVector3D& center, float radius, float strength, int mode,
                    const QVector3D& drag, const QVector3D& previousCenter,
                    float falloffPower, const QSet<int>* pinned);
    
    // Bake layers to base mesh
    void bakeCurrentLayer();

    // Set base mesh data for real calculations
    void setVertexPositions(const QVector<QVector3D>& positions);
    void setVertexNormals(const QVector<QVector3D>& normals);
    void setBasePositionDeltas(QVector<QVector3D>* deltas);
    
    // Signals
signals:
    void layerAdded(int index);
    void layerRemoved(int index);
    void layerRenamed(int index, const QString& newName);
    void layerVisibilityChanged(int index, bool visible);
    void layerLockedChanged(int index, bool locked);
    void layerBlendModeChanged(int index, float mode);
    void layerOpacityChanged(int index, float opacity);
    void currentLayerChanged(int index);
    void layersChanged();
    void sculptUpdated();
    void baked();
    
private:
    QVector<SculptLayer> m_layers;
    int m_currentLayer = -1;
    QVector<QVector3D> m_vertexPositions;
    QVector<QVector3D> m_vertexNormals;
    QVector<QVector3D>* m_baseDeltas = nullptr;
};