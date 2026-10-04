#pragma once

#include <functional>
#include <random>

#include <QObject>
#include <QUuid>
#include <QVector>
#include <QMap>
#include <QHash>
#include <QVariant>
#include <QMatrix4x4>

#include "sdk/kseditor/engine/physics/PhysicsEngine.h"
#include "../../../resources/ui/NodeGraphEditor.h"

namespace ks {

// ICE Particle System - Node-based particle simulation (XSI ICE style)

struct ICEParticleNodeType {
    enum Type {
        // Emitters
        EmitterPoint,
        EmitterCircle,
        EmitterSphere,
        EmitterMesh,
        
        // Forces
        ForceGravity,
        ForceWind,
        ForceTurbulence,
        ForceDrag,
        ForceVortex,
        ForceAttractor,
        
        // Collisions
        CollisionPlane,
        CollisionSphere,
        CollisionMesh,
        
        // Filters / Modifiers
        FilterAge,
        FilterVelocity,
        FilterPosition,
        FilterRandom,
        
        // Operators
        OpAdd,
        OpMultiply,
        OpLerp,
        OpCurve,
        OpVectorMath,
        
        // Properties
        PropPosition,
        PropVelocity,
        PropColor,
        PropSize,
        PropAge,
        PropMass,
        PropLifetime,
        
        // Output / Render
        OutputPoints,
        OutputMesh,
        OutputRibbons,
        OutputVolumetric,  // New: Volumetric rendering output
        
        // Flow control
        Branch,
        Switch,
        Loop,
        
        // Custom
        Custom
    };
};

struct ICEParticleGraph {
    // Nodes in the graph
    QMap<QUuid, ui::GraphNode> nodes;
    QVector<ui::GraphConnection> connections;
    
    // Graph metadata
    QString name;
    QUuid id;
    QMap<QString, QVariant> metadata;
    
    // Compilation cache (topological order)
    mutable QVector<QUuid> evalOrder;
    mutable bool evalOrderValid = false;
    
    void addNode(const ui::GraphNode& node);
    void removeNode(const QUuid& nodeId);
    void addConnection(const ui::GraphConnection& conn);
    void removeConnection(const QUuid& connId);
    void clear();
    
    // Get nodes in evaluation order (topological sort)
    QVector<QUuid> getEvalOrder() const;
    void invalidateEvalOrder() { evalOrderValid = false; }
    
    // Serialization
    QJsonObject toJson() const;
    bool fromJson(const QJsonObject& json);
};

struct ICEParticleState {
    // Per-particle data arrays (SoA for cache efficiency)
    QVector<QVector3D> positions;
    QVector<QVector3D> velocities;
    QVector<QVector3D> accelerations;
    QVector<float> ages;
    QVector<float> lifetimes;
    QVector<float> sizes;
    QVector<QVector4D> colors;
    QVector<float> masses;
    QVector<int> ids;  // unique IDs for tracking
    
    int maxCount = 10000;
    int aliveCount = 0;
    int nextId = 0;
    
    void ensureCapacity(int count);
    int allocate(int count);
    void kill(int index);
    void compact();
};

class ICEParticleEvaluator : public QObject {
    Q_OBJECT
public:
    explicit ICEParticleEvaluator(QObject* parent = nullptr);
    ~ICEParticleEvaluator();
    
    // Set the particle graph to evaluate
    void setGraph(const ICEParticleGraph& graph);
    
    // Set simulation parameters
    void setDeltaTime(float dt) { m_deltaTime = dt; }
    void setMaxParticles(int max) { m_state.maxCount = max; m_state.ensureCapacity(max); }
    void setTimeScale(float scale) { m_timeScale = scale; }
    // Seed the per-system RNG (default seeded in the constructor; each evaluator
    // instance has its own stream so systems do not share a global RNG).
    void setSeed(unsigned seed) { m_rng.seed(seed); }
    
    // Step the simulation
    void step();

    // Bake cache support: record current state snapshot / restore it
    ICEParticleState snapshot() const { return m_state; }
    void restoreState(const ICEParticleState& s) { m_state = s; }
    
    // Get current particle state (read-only)
    const ICEParticleState& state() const { return m_state; }
    QVector<QVector3D> getPositions() const;
    QVector<QVector4D> getColors() const;
    QVector<float> getSizes() const;
    int getAliveCount() const { return m_state.aliveCount; }
    
    // Emitter interface (for non-graph emitters)
    void addParticles(const QVector<QVector3D>& positions, const QVector<QVector3D>& velocities, int count);
    void clearParticles();

    // Collision mesh (world-space triangle soup, 3 verts per triangle)
    void setCollisionMesh(const QVector<QVector3D>& triangleVerts);
    const QVector<QVector3D>& collisionMesh() const { return m_collisionTriangles; }
    void setCollisionObjectId(int objectId) { m_collisionObjectId = objectId; }
    int collisionObjectId() const { return m_collisionObjectId; }
    void setCollisionGridCell(float cellSize) { m_collisionCellSize = qMax(0.05f, cellSize); rebuildCollisionGrid(); }
    float collisionGridCell() const { return m_collisionCellSize; }

    // Emitter mesh (world-space triangle soup, 3 verts per triangle)
    void setEmitterMesh(const QVector<QVector3D>& triangleVerts);
    const QVector<QVector3D>& emitterMesh() const { return m_emitterTriangles; }
    void setEmitterObjectId(int objectId) { m_emitterObjectId = objectId; }
    int emitterObjectId() const { return m_emitterObjectId; }
    
    // Node registration
    using NodeEvalFunc = std::function<void(const QUuid& nodeId, const ui::GraphNode& node, 
                                           ICEParticleState& state, float dt,
                                           const QMap<QUuid, QVariant>& portValues)>;
    void registerNodeType(const QString& typeName, NodeEvalFunc func);
    
    // Port value cache during evaluation
    QMap<QUuid, QVariant> m_portValues;
    QVector<QVector3D> m_collisionTriangles;
    int m_collisionObjectId = -1;
    QVector<QVector3D> m_emitterTriangles;
    int m_emitterObjectId = -1;

    // Uniform grid over the collision triangles (spatial hash for broadphase)
    void rebuildCollisionGrid();
    QHash<qint64, QVector<int>> m_collisionGrid;
    float m_collisionCellSize = 1.0f;
    QVector<QVector3D> m_collisionTriBounds; // per-triangle AABB min (0..2n-1 min, then max)
    
signals:
    void particlesUpdated(int count);
    void evaluationError(const QString& message);

private:
    ICEParticleState m_state;
    ICEParticleGraph m_graph;
    float m_deltaTime = 1.0f / 60.0f;
    float m_timeScale = 1.0f;
    float m_accumulator = 0.0f;
    std::mt19937 m_rng{ 0x5EED7u };
    
    QMap<QString, NodeEvalFunc> m_nodeEvaluators;
    
    // Evaluation
    void evaluateGraph();
    void evaluateNode(const QUuid& nodeId);
    QVariant getInputValue(const QUuid& nodeId, const QString& portName);
    void setOutputValue(const QUuid& nodeId, const QString& portName, const QVariant& value);
    
    // Built-in node evaluators
    static void evalEmitterPoint(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    void evalEmitterSphere(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    void evalEmitterMesh(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    void evalEmitterCircle(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalForceGravity(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalForceWind(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    void evalForceTurbulence(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalForceDrag(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalForceVortex(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalForceAttractor(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalCollisionPlane(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalCollisionSphere(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    void evalCollisionMesh(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalFilterAge(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalFilterVelocity(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    void evalFilterRandom(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalFilterPosition(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalOpAdd(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalOpMultiply(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalOpLerp(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalOpVectorMath(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalOpCurve(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalFCurve(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalPropColor(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalPropSize(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalPropLifetime(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalPropMass(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalOutputPoints(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalOutputRibbons(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalOutputMesh(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    static void evalOutputVolumetric(const QUuid&, const ui::GraphNode&, ICEParticleState&, float, const QMap<QUuid, QVariant>&);
    
    // Age-based size curve evaluation
    static float evaluateAgeSizeCurve(float age, float lifetime, const QVector<QPair<float, float>>& curve);
    
    // Depth sorting for volumetric rendering
    static void sortByDepth(QVector<QVector3D>& positions, QVector<float>& sizes, QVector<QVector4D>& colors, const QVector3D& cameraPos);
};

} // namespace ks