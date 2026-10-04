#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QVector3D>
#include <QVector2D>
#include <QMap>

struct MorphTarget {
    QString name;
    QVector<QVector3D> positionDeltas; // per-vertex position deltas
    QVector<QVector3D> normalDeltas;   // per-vertex normal deltas
    float weight = 0.0f;
    bool enabled = true;
    
    MorphTarget() {}
    MorphTarget(const QString& n, const QVector<QVector3D>& posDeltas,
                const QVector<QVector3D>& normDeltas = QVector<QVector3D>(),
                float w = 0.0f, bool en = true)
        : name(n), positionDeltas(posDeltas), normalDeltas(normDeltas), weight(w), enabled(en) {}
};

class MorphTargetEditor : public QObject
{
    Q_OBJECT
public:
    explicit MorphTargetEditor(QObject* parent = nullptr);
    ~MorphTargetEditor();
    
    // Morph target management
    int addMorphTarget(const QString& name, int vertexCount);
    bool removeMorphTarget(int index);
    bool renameMorphTarget(int index, const QString& newName);
    void setMorphTargetWeight(int index, float weight);
    float morphTargetWeight(int index) const;
    
    // Current target
    int currentTarget() const { return m_currentTarget; }
    void setCurrentTarget(int index);

    // Target names for UI
    QString currentTargetName() const;
    QStringList targetNames() const;
    
    // All targets
    const QVector<MorphTarget>& targets() const { return m_targets; }
    int targetCount() const { return m_targets.size(); }
    
    // Brush operations on morph targets
    int sculptBrushToTarget(int targetIndex, const QVector3D& center, float radius, float strength, int mode,
                            const QVector3D& drag, const QVector3D& previousCenter,
                            float falloffPower, const QSet<int>* pinned);

    // Set base mesh data for real vertex distance calculations
    void setBaseMeshData(const QVector<QVector3D>& positions, const QVector<QVector3D>& normals,
                         const QVector<QVector<int>>& adjacency);

    // Get vertex position (base + delta) for external queries
    QVector3D getVertexPosition(int targetIndex, int vertexIndex) const;
    
    // Signals
signals:
    void targetAdded(int index);
    void targetRemoved(int index);
    void targetRenamed(int index, const QString& newName);
    void targetWeightChanged(int index, float weight);
    void currentTargetChanged(int index);
    void targetsChanged();
    void sculptUpdated();
    
private:
    QVector<MorphTarget> m_targets;
    int m_currentTarget = -1;
    QVector<QVector3D> m_basePositions;
    QVector<QVector3D> m_baseNormals;
    QVector<QVector<int>> m_adjacency;
};