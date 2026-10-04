#include "InstanceReference.h"

namespace ks {

bool InstanceReference::createInstance(int masterId, int instanceId) {
    if (masterId < 0 || instanceId < 0 || masterId == instanceId) return false;
    if (m_instances.contains(instanceId)) return false;
    m_instances.insert(instanceId, masterId);
    m_masters[masterId].append(instanceId);
    return true;
}

int InstanceReference::realizeInstance(int instanceId) {
    if (!m_instances.contains(instanceId)) return -1;
    const int masterId = m_instances.take(instanceId);
    auto& list = m_masters[masterId];
    list.removeAll(instanceId);
    if (list.isEmpty()) m_masters.remove(masterId);
    return masterId;
}

QVector<int> InstanceReference::instancesOf(int masterId) const {
    return m_masters.value(masterId);
}

int InstanceReference::masterOf(int instanceId) const {
    return m_instances.value(instanceId, -1);
}

} // namespace ks