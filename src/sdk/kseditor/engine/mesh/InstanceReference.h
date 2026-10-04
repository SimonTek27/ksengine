#pragma once

#include <QMap>
#include <QVector>

namespace ks {

class SceneObject;

// Live instance references (Plasticity-style "Make Instances").
// An instance object shares the master's SceneMesh (single VBO); when the
// master is edited the bridge pushes the updated mesh to every instance.
// `realizeInstance` detaches an instance into an independent mesh.
class InstanceReference {
public:
    static InstanceReference& instance() {
        static InstanceReference inst;
        return inst;
    }

    // Registers `instanceId` as a live reference to `masterId`. Returns true
    // if master exists in the registry (any mesh object can be a master).
    bool createInstance(int masterId, int instanceId);

    // Detaches the instance from its master. Returns the master id or -1.
    int realizeInstance(int instanceId);

    // All live instance ids of a master.
    QVector<int> instancesOf(int masterId) const;

    // Master of an instance, or -1 if not an instance.
    int masterOf(int instanceId) const;

    bool isInstance(int objectId) const { return m_instances.contains(objectId); }
    bool hasInstances(int masterId) const { return m_masters.contains(masterId); }
    int instanceCount(int masterId) const { return m_masters.value(masterId).size(); }

    void clear() { m_instances.clear(); m_masters.clear(); }

private:
    InstanceReference() = default;

    QMap<int, int> m_instances;               // instanceId -> masterId
    QMap<int, QVector<int>> m_masters;        // masterId -> instanceIds
};

} // namespace ks