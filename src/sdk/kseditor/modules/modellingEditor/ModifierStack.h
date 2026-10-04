#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QVariant>
#include <QJsonObject>
#include "sdk/kseditor/engine/mesh/MeshOperations.h"

namespace ks {

// One entry in a non-destructive modifier stack.
// `type` selects the concrete Modifier implementation; `params` carries its
// editable parameters; `enabled` allows bypassing the modifier without removing it.
struct StackModifier {
    QString type;
    QJsonObject params;
    bool enabled = true;

    bool operator==(const StackModifier& other) const {
        return type == other.type && params == other.params && enabled == other.enabled;
    }
};

// Non-destructive modifier stack attached to a SceneObject.
//
// The object stores its raw (base) mesh in `base()`. `evaluate()` replays the
// enabled modifiers in stack order on top of the base mesh and returns the
// result for display. Nothing is permanently baked until `freeze` is called,
// mirroring the Softimage/Blender modifier-stack workflow.
class ModifierStack : public QObject {
    Q_OBJECT
public:
    explicit ModifierStack(QObject* parent = nullptr);

    // Base (raw, pre-modifier) mesh.
    const MeshData& base() const { return m_base; }
    void setBase(const MeshData& md) { m_base = md; emit changed(); }
    bool hasBase() const { return !m_base.vertices.isEmpty(); }

    // Stack management.
    int count() const { return m_mods.size(); }
    const StackModifier& at(int index) const { return m_mods.at(index); }
    bool hasModifiers() const { return !m_mods.isEmpty(); }

    bool add(const QString& type);
    bool remove(int index);
    bool move(int fromIndex, int toIndex);
    bool setEnabled(int index, bool enabled);
    bool setParam(int index, const QString& name, const QVariant& value);
    void clear();

    // Replays enabled modifiers over the base mesh.
    MeshData evaluate() const;

signals:
    void changed();

private:
    MeshData m_base;
    QVector<StackModifier> m_mods;
};

} // namespace ks
