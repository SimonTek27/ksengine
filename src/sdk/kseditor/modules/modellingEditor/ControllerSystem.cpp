#include "ControllerSystem.h"

#include "SceneParamAccess.h"

#include <QJsonArray>
#include <QJsonObject>
#include <cmath>

#include "sdk/kseditor/engine/Graphics/SceneGraph.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"

namespace ks {

QVariant ControllerDef::toVariant() const
{
    QJsonObject o;
    o["type"] = type;
    o["targetId"] = targetId;
    o["targetName"] = targetName;
    o["channel"] = channel;
    o["base"] = base;
    o["amplitude"] = amplitude;
    o["frequency"] = frequency;
    o["phase"] = phase;
    o["stiffness"] = stiffness;
    o["damping"] = damping;
    o["vertexIndex"] = vertexIndex;
    o["ox"] = offset.x();
    o["oy"] = offset.y();
    o["oz"] = offset.z();
    o["enabled"] = enabled;
    return o;
}

void ControllerDef::fromVariant(const QVariant& v)
{
    QJsonObject o = v.toJsonObject();
    type = o["type"].toInt();
    targetId = o["targetId"].toInt();
    targetName = o["targetName"].toString();
    channel = o["channel"].toString(QStringLiteral("position.y"));
    base = static_cast<float>(o["base"].toDouble(0.0));
    amplitude = static_cast<float>(o["amplitude"].toDouble(1.0));
    frequency = static_cast<float>(o["frequency"].toDouble(1.0));
    phase = static_cast<float>(o["phase"].toDouble(0.0));
    stiffness = static_cast<float>(o["stiffness"].toDouble(50.0));
    damping = static_cast<float>(o["damping"].toDouble(2.0));
    vertexIndex = o["vertexIndex"].toInt(0);
    offset = QVector3D(static_cast<float>(o["ox"].toDouble(0.0)),
                       static_cast<float>(o["oy"].toDouble(0.0)),
                       static_cast<float>(o["oz"].toDouble(0.0)));
    enabled = o["enabled"].toBool(true);
}

void ControllerSystem::add(int objectId, int type, int targetId, const QString& targetName,
                           const QString& channel, float base, float amplitude, float frequency,
                           float phase, float stiffness, float damping)
{
    if (targetId < 0 || targetId == objectId) return;
    ControllerDef c;
    c.type = type;
    c.targetId = targetId;
    c.targetName = targetName;
    c.channel = channel;
    c.base = base;
    c.amplitude = amplitude;
    c.frequency = frequency;
    c.phase = phase;
    c.stiffness = stiffness;
    c.damping = damping;
    m_controllers[objectId].append(c);
}

bool ControllerSystem::remove(int objectId, int index)
{
    auto it = m_controllers.find(objectId);
    if (it == m_controllers.end() || index < 0 || index >= it->size()) return false;
    it->removeAt(index);
    if (it->isEmpty()) m_controllers.erase(it);
    return true;
}

bool ControllerSystem::setEnabled(int objectId, int index, bool on)
{
    auto it = m_controllers.find(objectId);
    if (it == m_controllers.end() || index < 0 || index >= it->size()) return false;
    (*it)[index].enabled = on;
    return true;
}

bool ControllerSystem::setParams(int objectId, int index, float amplitude, float frequency,
                                 float phase, float stiffness, float damping)
{
    auto it = m_controllers.find(objectId);
    if (it == m_controllers.end() || index < 0 || index >= it->size()) return false;
    ControllerDef& c = (*it)[index];
    c.amplitude = amplitude;
    c.frequency = frequency;
    c.phase = phase;
    c.stiffness = stiffness;
    c.damping = damping;
    return true;
}

bool ControllerSystem::setAttachment(int objectId, int index, int vertexIndex,
                                     const QVector3D& offset)
{
    auto it = m_controllers.find(objectId);
    if (it == m_controllers.end() || index < 0 || index >= it->size()) return false;
    ControllerDef& c = (*it)[index];
    c.vertexIndex = vertexIndex;
    c.offset = offset;
    return true;
}

void ControllerSystem::clearObject(int objectId)
{
    m_controllers.remove(objectId);
    for (auto it = m_startTimes.begin(); it != m_startTimes.end(); ) {
        if (it.key().first == objectId) it = m_startTimes.erase(it);
        else ++it;
    }
}

void ControllerSystem::clearAll()
{
    m_controllers.clear();
    m_startTimes.clear();
}

float ControllerSystem::valueNoise(float x)
{
    // Value noise: hash the integer lattice, smoothstep-interpolate.
    auto hash = [](float n) {
        float s = std::sin(n) * 43758.5453f;
        return s - std::floor(s);
    };
    float xi = std::floor(x);
    float xf = x - xi;
    float u = xf * xf * (3.0f - 2.0f * xf);
    return hash(xi) * (1.0f - u) + hash(xi + 1.0f) * u;
}

namespace {

// Mirror of ConstraintSystem::decompose (reads the QMatrix4x4 as stored).
void decomposeMatrix(const QMatrix4x4& m, QVector3D& outPos, QVector3D& outEuler)
{
    outPos = QVector3D(m(0, 3), m(1, 3), m(2, 3));
    const float* d = m.constData();
    float sy = std::sqrt(d[0] * d[0] + d[4] * d[4]);
    if (sy > 1e-6f) {
        outEuler = QVector3D(std::atan2(d[6], d[10]),
                             std::atan2(-d[2], sy),
                             std::atan2(d[4], d[0]));
    } else {
        outEuler = QVector3D(std::atan2(-d[9], d[5]),
                             std::atan2(-d[2], sy),
                             0.0f);
    }
}

QMatrix4x4 eulerMatrix(const QVector3D& euler)
{
    float cx = std::cos(euler.x()), sx = std::sin(euler.x());
    float cy = std::cos(euler.y()), sy = std::sin(euler.y());
    float cz = std::cos(euler.z()), sz = std::sin(euler.z());
    QMatrix4x4 m;
    // ZYX (same order as SceneObject::setRotationEuler)
    m(0, 0) = cy * cz;            m(0, 1) = cz * sx * sy - cx * sz;  m(0, 2) = cx * cz * sy + sx * sz;
    m(1, 0) = cy * sz;            m(1, 1) = cx * cz + sx * sy * sz;  m(1, 2) = -cz * sx + cx * sy * sz;
    m(2, 0) = -sy;                m(2, 1) = cy * sx;                 m(2, 2) = cx * cy;
    return m;
}

QMatrix4x4 translationMatrix(const QVector3D& p)
{
    QMatrix4x4 m;
    m(0, 3) = p.x();
    m(1, 3) = p.y();
    m(2, 3) = p.z();
    return m;
}

// Aim-style: rotate the object so +Z points toward `target` (same math the
// Aim constraint uses, including the decompose/recompose round-trip).
void applyLookAt(SceneObject* obj, SceneObject* target, const QVector3D& offset)
{
    QMatrix4x4 targetWorld = target->worldTransform();
    QVector3D targetPos(targetWorld(0, 3), targetWorld(1, 3), targetWorld(2, 3));
    QVector3D objPos = obj->worldTransform() * QVector3D(0, 0, 0);

    QVector3D f = targetPos + offset - objPos;
    if (f.lengthSquared() < 1e-8f) return;
    f.normalize();
    QVector3D up(0, 1, 0);
    QVector3D r = QVector3D::crossProduct(up, f);
    if (r.lengthSquared() < 1e-8f) r = QVector3D(1, 0, 0);
    r.normalize();
    QVector3D u = QVector3D::crossProduct(f, r);
    u.normalize();

    QMatrix4x4 m;
    m(0, 0) = r.x(); m(1, 0) = r.y(); m(2, 0) = r.z();
    m(0, 1) = u.x(); m(1, 1) = u.y(); m(2, 1) = u.z();
    m(0, 2) = f.x(); m(1, 2) = f.y(); m(2, 2) = f.z();
    m(3, 3) = 1.0f;

    QMatrix4x4 desiredWorld = m;
    QVector3D p = obj->position();
    desiredWorld(0, 3) = p.x();
    desiredWorld(1, 3) = p.y();
    desiredWorld(2, 3) = p.z();

    QVector3D dummy, rotEuler;
    decomposeMatrix(desiredWorld, dummy, rotEuler);
    desiredWorld = translationMatrix(p) * eulerMatrix(rotEuler);

    SceneObject* parent = obj->parent();
    QMatrix4x4 parentWorld = parent ? parent->worldTransform() : QMatrix4x4();
    QMatrix4x4 local = parentWorld.inverted() * desiredWorld;
    QVector3D localPos, localEuler;
    decomposeMatrix(local, localPos, localEuler);
    obj->setPosition(localPos);
    obj->setRotationEuler(localEuler);
    obj->updateWorldTransform();
}

} // namespace

bool ControllerSystem::applyOne(SceneObject* obj, SceneObject* target, const ControllerDef& c,
                                int index, float time)
{
    switch (static_cast<ControllerType>(c.type)) {
    case ControllerType::Noise: {
        float v = c.base + c.amplitude * (valueNoise(time * c.frequency + c.phase) * 2.0f - 1.0f);
        sceneParamWrite(obj, c.channel, v);
        return true;
    }
    case ControllerType::Spring: {
        const auto key = qMakePair(obj->id(), index);
        if (!m_startTimes.contains(key))
            m_startTimes.insert(key, time);
        float t = time - m_startTimes.value(key);
        float v = c.base + c.amplitude * std::exp(-c.damping * t)
                              * std::sin(6.2831853f * c.frequency * t + c.phase);
        sceneParamWrite(obj, c.channel, v);
        return true;
    }
    case ControllerType::LookAt:
        if (target) {
            applyLookAt(obj, target, c.offset);
            return true;
        }
        return false;
    case ControllerType::Attachment: {
        if (!target || !target->mesh()) return false;
        const auto& verts = target->mesh()->geometry().vertices;
        if (c.vertexIndex < 0 || c.vertexIndex >= verts.size()) return false;
        const SceneVertex& sv = verts[c.vertexIndex];
        QMatrix4x4 targetWorld = target->worldTransform();
        QVector3D worldPos = targetWorld * QVector3D(sv.position.x(), sv.position.y(), sv.position.z());
        worldPos += c.offset;
        SceneObject* parent = obj->parent();
        QMatrix4x4 invParent = parent ? parent->worldTransform().inverted() : QMatrix4x4();
        obj->setPosition(invParent * worldPos);
        return true;
    }
    default:
        return false;
    }
}

int ControllerSystem::evaluate(SceneObject* obj, SceneGraph* graph, float time)
{
    if (!obj || !graph) return 0;
    auto it = m_controllers.constFind(obj->id());
    if (it == m_controllers.constEnd()) return 0;

    graph->updateAllTransforms();
    int applied = 0;
    int index = 0;
    for (const ControllerDef& c : *it) {
        if (c.enabled) {
            if (c.type == (int)ControllerType::Noise || c.type == (int)ControllerType::Spring) {
                if (applyOne(obj, nullptr, c, index, time)) ++applied;
            } else {
                SceneObject* target = graph->findObjectById(c.targetId);
                if (target) {
                    target->updateWorldTransform();
                    obj->updateWorldTransform();
                    if (applyOne(obj, target, c, index, time)) ++applied;
                }
            }
        }
        ++index;
    }
    return applied;
}

bool ControllerSystem::vertexWorldPos(const QVector<QVector3D>& localVerts, const QMatrix4x4& world,
                                      int vertexIndex, QVector3D& outWorldPos)
{
    if (vertexIndex < 0 || vertexIndex >= localVerts.size()) return false;
    outWorldPos = world * localVerts[vertexIndex];
    return true;
}

} // namespace ks