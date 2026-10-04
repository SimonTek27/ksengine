#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <functional>
#include "sdk/kseditor/engine/mesh/MeshOperations.h"
#include "sdk/kseditor/engine/mesh/BooleanOps.h"

namespace ks {

// Boolean operation kinds. Index matches the QML/UI operation picker
// (0 Union, 1 Difference, 2 Intersection, 3 Symmetric difference).
inline QString booleanOpTypeToString(int op) {
    switch (op) {
    case 0: return "Union";
    case 1: return "Difference";
    case 2: return "Intersection";
    case 3: return "Xor";
    }
    return "Union";
}

inline geometry::BooleanOperations::Operation booleanOpIndexToEnum(int op) {
    switch (qBound(0, op, 3)) {
    case 0: return geometry::BooleanOperations::Union;
    case 1: return geometry::BooleanOperations::Difference;
    case 2: return geometry::BooleanOperations::Intersection;
    case 3: return geometry::BooleanOperations::SymmetricDiff;
    }
    return geometry::BooleanOperations::Union;
}

// One non-destructive boolean operation in a BooleanStack.
// `operandId` references another scene object; `operation` selects the
// boolean kind; `enabled` bypasses the op without removing it.
struct BooleanOp {
    int operation = 0;      // BooleanOperations::Operation index
    int operandId = -1;     // scene object id of the operand mesh
    QString operandName;    // snapshot of operand name for display
    bool enabled = true;
};

// Non-destructive boolean stack attached to a SceneObject.
//
// The object keeps its raw (pre-boolean) mesh in `base()`. `evaluate()`
// replays the enabled operations (resolving operand meshes from the scene by
// id) on top of the base mesh and returns the result. Nothing is baked until
// `freeze`/apply, mirroring the Softimage boolean factory workflow.
class BooleanStack : public QObject {
    Q_OBJECT
public:
    explicit BooleanStack(QObject* parent = nullptr);

    // Base (raw, pre-boolean) mesh in local object coordinates.
    const MeshData& base() const { return m_base; }
    void setBase(const MeshData& md) { m_base = md; emit changed(); }
    bool hasBase() const { return !m_base.vertices.isEmpty(); }

    int count() const { return m_ops.size(); }
    const BooleanOp& at(int index) const { return m_ops.at(index); }
    bool hasOps() const { return !m_ops.isEmpty(); }

    bool add(int operation, int operandId, const QString& operandName);
    bool remove(int index);
    bool move(int fromIndex, int toIndex);
    bool setEnabled(int index, bool enabled);
    bool setOperation(int index, int operation);
    void clear();

    // Operand resolver: caller provides a function that maps operandId -> MeshData.
    using OperandResolver = std::function<MeshData(int operandId)>;
    void setOperandResolver(OperandResolver resolver) { m_resolver = resolver; }

    // Evaluate the boolean stack: applies all enabled operations sequentially
    // to the base mesh and returns the result. If no operations are enabled,
    // returns the base mesh unchanged. Returns empty MeshData on failure.
    MeshData evaluate() const;

signals:
    void changed();

private:
    MeshData m_base;
    QVector<BooleanOp> m_ops;
    OperandResolver m_resolver;
};

} // namespace ks
