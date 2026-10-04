#include "ConstraintSystem.h"

#include <QJsonArray>
#include <QJsonObject>
#include <cmath>

#include "sdk/kseditor/engine/Graphics/SceneGraph.h"

namespace ks {

QVariant ConstraintDef::toVariant() const
{
    QJsonObject o;
    o["type"] = type;
    o["targetId"] = targetId;
    o["targetName"] = targetName;
    o["ox"] = offset.x();
    o["oy"] = offset.y();
    o["oz"] = offset.z();
    o["rx"] = offsetRot.x();
    o["ry"] = offsetRot.y();
    o["rz"] = offsetRot.z();
    o["param"] = param;
    o["stiffness"] = stiffness;
    o["damping"] = damping;
    o["follow"] = follow;
    o["enabled"] = enabled;
    QJsonArray pathArr;
    for (const QVector3D& p : path)
        pathArr.append(QJsonArray{p.x(), p.y(), p.z()});
    o["path"] = pathArr;
    return o;
}

void ConstraintDef::fromVariant(const QVariant& v)
{
    QJsonObject o = v.toJsonObject();
    type = o["type"].toInt();
    targetId = o["targetId"].toInt();
    targetName = o["targetName"].toString();
    offset = QVector3D(o["ox"].toDouble(), o["oy"].toDouble(), o["oz"].toDouble());
    offsetRot = QVector3D(o["rx"].toDouble(), o["ry"].toDouble(), o["rz"].toDouble());
    param = static_cast<float>(o["param"].toDouble(0.0));
    stiffness = static_cast<float>(o["stiffness"].toDouble(50.0));
    damping = static_cast<float>(o["damping"].toDouble(2.0));
    follow = o["follow"].toBool(false);
    enabled = o["enabled"].toBool(true);
    path.clear();
    const QJsonArray pathArr = o["path"].toArray();
    for (const QJsonValue& pv : pathArr) {
        const QJsonArray q = pv.toArray();
        if (q.size() >= 3)
            path.append(QVector3D(static_cast<float>(q.at(0).toDouble()),
                                  static_cast<float>(q.at(1).toDouble()),
                                  static_cast<float>(q.at(2).toDouble())));
    }
}

void ConstraintSystem::add(int objectId, int type, int targetId, const QString& targetName,
                           const QVector3D& offset, const QVector3D& offsetRot)
{
    if (targetId < 0 || targetId == objectId) return;
    ConstraintDef c;
    c.type = type;
    c.targetId = targetId;
    c.targetName = targetName;
    c.offset = offset;
    c.offsetRot = offsetRot;
    m_constraints[objectId].append(c);
}

bool ConstraintSystem::remove(int objectId, int index)
{
    auto it = m_constraints.find(objectId);
    if (it == m_constraints.end() || index < 0 || index >= it->size()) return false;
    it->removeAt(index);
    if (it->isEmpty()) m_constraints.erase(it);
    return true;
}

bool ConstraintSystem::setEnabled(int objectId, int index, bool on)
{
    auto it = m_constraints.find(objectId);
    if (it == m_constraints.end() || index < 0 || index >= it->size()) return false;
    (*it)[index].enabled = on;
    return true;
}

bool ConstraintSystem::setOffset(int objectId, int index, const QVector3D& offset)
{
    auto it = m_constraints.find(objectId);
    if (it == m_constraints.end() || index < 0 || index >= it->size()) return false;
    (*it)[index].offset = offset;
    return true;
}

bool ConstraintSystem::setParam(int objectId, int index, float param)
{
    auto it = m_constraints.find(objectId);
    if (it == m_constraints.end() || index < 0 || index >= it->size()) return false;
    (*it)[index].param = param;
    return true;
}

bool ConstraintSystem::setFollow(int objectId, int index, bool follow)
{
    auto it = m_constraints.find(objectId);
    if (it == m_constraints.end() || index < 0 || index >= it->size()) return false;
    (*it)[index].follow = follow;
    return true;
}

bool ConstraintSystem::setSpringParams(int objectId, int index, float stiffness, float damping)
{
    auto it = m_constraints.find(objectId);
    if (it == m_constraints.end() || index < 0 || index >= it->size()) return false;
    (*it)[index].stiffness = stiffness;
    (*it)[index].damping = damping;
    return true;
}

bool ConstraintSystem::setPath(int objectId, int index, const QVector<QVector3D>& path)
{
    auto it = m_constraints.find(objectId);
    if (it == m_constraints.end() || index < 0 || index >= it->size()) return false;
    (*it)[index].path = path;
    return true;
}

void ConstraintSystem::clearObject(int objectId)
{
    m_constraints.remove(objectId);
}

void ConstraintSystem::clearAll()
{
    m_constraints.clear();
}

void ConstraintSystem::decompose(const QMatrix4x4& m, QVector3D& outPos, QVector3D& outEuler)
{
    outPos = QVector3D(m(0, 3), m(1, 3), m(2, 3));

    const float* d = m.constData();
    float sy = std::sqrt(d[0] * d[0] + d[4] * d[4]);
    if (sy > 1e-6f) {
        outEuler = QVector3D(
            std::atan2(d[6], d[10]),
            std::atan2(-d[2], sy),
            std::atan2(d[4], d[0]));
    } else {
        outEuler = QVector3D(
            std::atan2(-d[9], d[5]),
            std::atan2(-d[2], sy),
            0.0f);
    }
}

static QMatrix4x4 eulerMatrix(const QVector3D& euler)
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

static QMatrix4x4 translationMatrix(const QVector3D& p)
{
    QMatrix4x4 m;
    m(0, 3) = p.x();
    m(1, 3) = p.y();
    m(2, 3) = p.z();
    return m;
}

void ConstraintSystem::applyOne(SceneObject* obj, SceneObject* target, const ConstraintDef& c, int index)
{
    if (!obj || !target || obj == target) return;

    QMatrix4x4 targetWorld = target->worldTransform();
    QVector3D targetPos = QVector3D(targetWorld(0, 3), targetWorld(1, 3), targetWorld(2, 3));

    QMatrix4x4 desiredWorld;
    QVector3D rotEuler(0, 0, 0);

    switch (static_cast<ConstraintType>(c.type)) {
    case ConstraintType::Point: {
        QVector3D p = targetPos + c.offset;
        desiredWorld = translationMatrix(p);
        rotEuler = obj->rotationEuler();
        break;
    }
    case ConstraintType::Orientation: {
        QMatrix3x3 r = targetWorld.normalMatrix();
        QMatrix4x4 base(r);
        base(3, 3) = 1.0f;
        desiredWorld = base * eulerMatrix(c.offsetRot);
        desiredWorld(0, 3) = obj->position().x();
        desiredWorld(1, 3) = obj->position().y();
        desiredWorld(2, 3) = obj->position().z();
        QVector3D dummy;
        decompose(desiredWorld, dummy, rotEuler);
        QVector3D p = obj->position();
        desiredWorld = translationMatrix(p) * eulerMatrix(rotEuler);
        break;
    }
    case ConstraintType::Aim: {
        QVector3D objPos = obj->worldTransform() * QVector3D(0, 0, 0);
        QVector3D f = targetPos + c.offset - objPos;
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
        desiredWorld = m;
        QVector3D p = obj->position();
        desiredWorld(0, 3) = p.x();
        desiredWorld(1, 3) = p.y();
        desiredWorld(2, 3) = p.z();
        decompose(desiredWorld, p, rotEuler);
        desiredWorld = translationMatrix(p) * eulerMatrix(rotEuler);
        break;
    }
    case ConstraintType::Parent: {
        QMatrix4x4 offset = translationMatrix(c.offset) * eulerMatrix(c.offsetRot);
        desiredWorld = targetWorld * offset;
        QVector3D p;
        decompose(desiredWorld, p, rotEuler);
        desiredWorld = translationMatrix(p) * eulerMatrix(rotEuler);
        break;
    }
    case ConstraintType::Path: {
        if (c.path.size() < 2) return;
        const int n = c.path.size();
        QVector<QVector3D> worldSamples;
        worldSamples.reserve(n);
        for (const QVector3D& p : c.path) worldSamples.append(targetWorld * p);
        // Cumulative arc-length so `param` maps uniformly along the path.
        QVector<float> cum(n, 0.0f);
        for (int i = 1; i < n; ++i)
            cum[i] = cum[i - 1] + (worldSamples[i] - worldSamples[i - 1]).length();
        const float total = cum[n - 1];
        if (total < 1e-6f) return;
        float d = c.param * total;
        int seg = 0;
        for (int i = 1; i < n; ++i) {
            if (cum[i] >= d) { seg = i - 1; break; }
            seg = i - 1;
        }
        float s = (cum[seg + 1] > cum[seg]) ? (d - cum[seg]) / (cum[seg + 1] - cum[seg]) : 0.0f;
        QVector3D pos = worldSamples[seg] + (worldSamples[seg + 1] - worldSamples[seg]) * s;
        pos += c.offset;
        if (c.follow) {
            // Aim-style: orient the object along the segment tangent.
            QVector3D f = (worldSamples[seg + 1] - worldSamples[seg]).normalized();
            if (f.lengthSquared() < 1e-8f) f = QVector3D(0, 0, 1);
            QVector3D r = QVector3D::crossProduct(QVector3D(0, 1, 0), f);
            if (r.lengthSquared() < 1e-8f) r = QVector3D(1, 0, 0);
            r.normalize();
            QVector3D u = QVector3D::crossProduct(f, r);
            u.normalize();
            QMatrix4x4 m;
            m(0, 0) = r.x(); m(1, 0) = r.y(); m(2, 0) = r.z();
            m(0, 1) = u.x(); m(1, 1) = u.y(); m(2, 1) = u.z();
            m(0, 2) = f.x(); m(1, 2) = f.y(); m(2, 2) = f.z();
            m(3, 3) = 1.0f;
            desiredWorld = m;
            desiredWorld(0, 3) = pos.x();
            desiredWorld(1, 3) = pos.y();
            desiredWorld(2, 3) = pos.z();
            decompose(desiredWorld, pos, rotEuler);
            desiredWorld = translationMatrix(pos) * eulerMatrix(rotEuler);
            break;
        }
        desiredWorld = translationMatrix(pos);
        rotEuler = obj->rotationEuler();
        break;
    }
    case ConstraintType::Attachment: {
        const int vi = static_cast<int>(c.param);
        if (vi < 0 || vi >= c.path.size()) return;
        QVector3D p = targetWorld * c.path[vi] + c.offset;
        desiredWorld = translationMatrix(p);
        rotEuler = obj->rotationEuler();
        break;
    }
    case ConstraintType::Link: {
        const auto key = qMakePair(obj->id(), index);
        if (!m_lockedOffsets.contains(key))
            m_lockedOffsets.insert(key, obj->worldTransform() * targetWorld.inverted());
        desiredWorld = targetWorld * m_lockedOffsets.value(key);
        QVector3D p;
        decompose(desiredWorld, p, rotEuler);
        desiredWorld = translationMatrix(p) * eulerMatrix(rotEuler);
        break;
    }
    case ConstraintType::Spring: {
        QVector3D targetWorldPos(targetWorld(0, 3), targetWorld(1, 3), targetWorld(2, 3));
        QVector3D cur = obj->worldTransform() * QVector3D(0, 0, 0);
        const auto key = qMakePair(obj->id(), index);
        QVector3D vel = m_velocities.value(key);
        const float dt = 0.05f;                     // matches the 20 Hz constraint timer
        const QVector3D force = (targetWorldPos + c.offset - cur) * c.stiffness - vel * c.damping;
        vel += force * dt;
        m_velocities[key] = vel;
        const QVector3D p = cur + vel * dt;
        desiredWorld = translationMatrix(p);
        rotEuler = obj->rotationEuler();
        break;
    }
    default:
        return;
    }

    // Convert desired world transform into a local one (parent inverse), preserving scale.
    SceneObject* parent = obj->parent();
    QMatrix4x4 parentWorld = parent ? parent->worldTransform() : QMatrix4x4();
    QMatrix4x4 invParent = parentWorld.inverted();
    QMatrix4x4 local = invParent * desiredWorld;

    QVector3D localPos;
    QVector3D localEuler;
    decompose(local, localPos, localEuler);

    QVector3D s = obj->scale();
    obj->setPosition(localPos);
    obj->setRotationEuler(localEuler);
    if (s != QVector3D(1, 1, 1)) obj->setScale(s);
}

int ConstraintSystem::evaluate(SceneObject* obj, SceneGraph* graph)
{
    if (!obj || !graph) return 0;
    auto it = m_constraints.find(obj->id());
    if (it == m_constraints.end()) return 0;

    graph->updateAllTransforms();
    int applied = 0;
    int index = 0;
    for (const ConstraintDef& c : *it) {
        if (c.enabled) {
            SceneObject* target = graph->findObjectById(c.targetId);
            if (target) {
                target->updateWorldTransform();
                obj->updateWorldTransform();
                applyOne(obj, target, c, index);
                ++applied;
            }
        }
        ++index;
    }
    return applied;
}

} // namespace ks
