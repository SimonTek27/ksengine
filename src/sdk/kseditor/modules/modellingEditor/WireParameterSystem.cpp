#include "WireParameterSystem.h"

#include "ExpressionEvaluator.h"
#include "SceneParamAccess.h"

#include <QJsonArray>
#include <QJsonObject>

#include "sdk/kseditor/engine/Graphics/SceneGraph.h"

namespace ks {

QVariant WireBinding::toVariant() const
{
    QJsonObject o;
    o["driverId"] = driverId;
    o["driverName"] = driverName;
    o["driverProp"] = driverProp;
    o["drivenId"] = drivenId;
    o["drivenName"] = drivenName;
    o["drivenProp"] = drivenProp;
    o["scale"] = scale;
    o["offset"] = offset;
    o["enabled"] = enabled;
    o["expression"] = expression;
    return o;
}

void WireBinding::fromVariant(const QVariant& v)
{
    QJsonObject o = v.toJsonObject();
    driverId = o["driverId"].toInt();
    driverName = o["driverName"].toString();
    driverProp = o["driverProp"].toString(QStringLiteral("position.x"));
    drivenId = o["drivenId"].toInt();
    drivenName = o["drivenName"].toString();
    drivenProp = o["drivenProp"].toString(QStringLiteral("position.y"));
    scale = static_cast<float>(o["scale"].toDouble(1.0));
    offset = static_cast<float>(o["offset"].toDouble(0.0));
    enabled = o["enabled"].toBool(true);
    expression = o["expression"].toString();
}

bool WireParameterSystem::add(int driverId, const QString& driverName, const QString& driverProp,
                              int drivenId, const QString& drivenName, const QString& drivenProp,
                              float scale, float offset)
{
    if (driverId < 0 || drivenId < 0 || driverId == drivenId) return false;
    // Reject self-feeding cycles of length 1 (driver == driven already excluded).
    WireBinding b;
    b.driverId = driverId;
    b.driverName = driverName;
    b.driverProp = driverProp;
    b.drivenId = drivenId;
    b.drivenName = drivenName;
    b.drivenProp = drivenProp;
    b.scale = scale;
    b.offset = offset;
    m_bindings[drivenId].append(b);
    return true;
}

bool WireParameterSystem::remove(int drivenId, int index)
{
    auto it = m_bindings.find(drivenId);
    if (it == m_bindings.end() || index < 0 || index >= it->size()) return false;
    it->removeAt(index);
    if (it->isEmpty()) m_bindings.erase(it);
    return true;
}

bool WireParameterSystem::setEnabled(int drivenId, int index, bool on)
{
    auto it = m_bindings.find(drivenId);
    if (it == m_bindings.end() || index < 0 || index >= it->size()) return false;
    (*it)[index].enabled = on;
    return true;
}

bool WireParameterSystem::setParams(int drivenId, int index, float scale, float offset)
{
    auto it = m_bindings.find(drivenId);
    if (it == m_bindings.end() || index < 0 || index >= it->size()) return false;
    (*it)[index].scale = scale;
    (*it)[index].offset = offset;
    return true;
}

bool WireParameterSystem::setExpression(int drivenId, int index, const QString& expression)
{
    auto it = m_bindings.find(drivenId);
    if (it == m_bindings.end() || index < 0 || index >= it->size()) return false;
    (*it)[index].expression = expression;
    return true;
}

bool WireParameterSystem::setProperty(int drivenId, int index, const QString& drivenProp)
{
    auto it = m_bindings.find(drivenId);
    if (it == m_bindings.end() || index < 0 || index >= it->size()) return false;
    (*it)[index].drivenProp = drivenProp;
    return true;
}

void WireParameterSystem::clearDriven(int drivenId)
{
    m_bindings.remove(drivenId);
}

void WireParameterSystem::clearDriver(int driverId)
{
    for (auto it = m_bindings.begin(); it != m_bindings.end(); ) {
        bool erased = false;
        for (int i = it->size() - 1; i >= 0; --i) {
            if ((*it)[i].driverId == driverId) {
                it->removeAt(i);
                erased = true;
            }
        }
        if (erased && it->isEmpty()) it = m_bindings.erase(it);
        else ++it;
    }
}

void WireParameterSystem::clearAll()
{
    m_bindings.clear();
}

QVector<WireBinding> WireParameterSystem::forObject(int objectId) const
{
    return m_bindings.value(objectId);
}

QVector<WireBinding> WireParameterSystem::drivenBy(int objectId) const
{
    QVector<WireBinding> result;
    for (auto it = m_bindings.constBegin(); it != m_bindings.constEnd(); ++it)
        for (const WireBinding& b : it.value())
            if (b.driverId == objectId)
                result.append(b);
    return result;
}

void WireParameterSystem::applyOne(const WireBinding& b, SceneGraph* graph, bool& changed)
{
    SceneObject* driver = graph->findObjectById(b.driverId);
    SceneObject* driven = graph->findObjectById(b.drivenId);
    if (!driver || !driven) return;

    float src = 0.0f;
    if (!sceneParamRead(driver, b.driverProp, src)) return;

    const float value = [&]() {
        if (!b.expression.trimmed().isEmpty()) {
            bool ok = false;
            const double v = expr::ExpressionEvaluator::evaluate(b.expression, static_cast<double>(src), &ok);
            return ok ? static_cast<float>(v) : src; // fall back to driver value on error
        }
        return src * b.scale + b.offset;
    }();

    float old = 0.0f;
    if (sceneParamRead(driven, b.drivenProp, old) && qAbs(old - value) < 1e-6f)
        return; // no effective change
    if (sceneParamWrite(driven, b.drivenProp, value))
        changed = true;
}

int WireParameterSystem::evaluate(SceneGraph* graph)
{
    if (!graph) return 0;
    int applied = 0;
    bool changed = false;
    graph->updateAllTransforms();
    for (auto it = m_bindings.constBegin(); it != m_bindings.constEnd(); ++it)
        for (const WireBinding& b : it.value())
            if (b.enabled) {
                applyOne(b, graph, changed);
                ++applied;
            }
    if (changed) graph->updateAllTransforms();
    return applied;
}

} // namespace ks