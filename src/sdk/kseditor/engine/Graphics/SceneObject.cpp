#include "SceneObject.h"
#include <QJsonArray>
#include <QtMath>

namespace ks {

SceneObject::SceneObject(int id, const QString& name, Type type)
    : m_id(id)
    , m_name(name)
    , m_type(type)
    , m_transform(Matrix4::Identity())
    , m_worldTransform(Matrix4::Identity())
{
}

SceneObject::SceneObject(SceneGraph* graph, const QString& name)
    : m_id(-1)
    , m_name(name)
    , m_type(Type::Node)
    , m_sceneGraph(graph)
    , m_transform(Matrix4::Identity())
    , m_worldTransform(Matrix4::Identity())
{
}

SceneObject::~SceneObject()
{
    qDeleteAll(m_children);
    m_children.clear();
}

void SceneObject::addChild(SceneObject* child)
{
    if (!child || child == this)
        return;

    if (child->m_parent == this)
        return;

    if (child->m_parent)
        child->m_parent->removeChild(child);

    child->m_parent = this;
    m_children.append(child);
    child->m_dirty = true;
}

void SceneObject::removeChild(SceneObject* child)
{
    if (!child)
        return;

    int idx = m_children.indexOf(child);
    if (idx >= 0) {
        m_children.removeAt(idx);
        child->m_parent = nullptr;
        child->m_dirty = true;
    }
}

QVector3D SceneObject::position() const {
    return QVector3D(m_transform.m[3][0], m_transform.m[3][1], m_transform.m[3][2]);
}

void SceneObject::setPosition(const QVector3D& pos) {
    m_transform.m[3][0] = pos.x();
    m_transform.m[3][1] = pos.y();
    m_transform.m[3][2] = pos.z();
    m_dirty = true;
    emit transformChanged();
}

QVector3D SceneObject::rotationEuler() const {
    // Extract Euler angles from rotation matrix (ZYX order)
    float sy = qSqrt(m_transform.m[0][0] * m_transform.m[0][0] + m_transform.m[0][1] * m_transform.m[0][1]);
    if (sy > 1e-6f) {
        return QVector3D(
            qAtan2(m_transform.m[1][2], m_transform.m[2][2]),
            qAtan2(-m_transform.m[0][2], sy),
            qAtan2(m_transform.m[0][1], m_transform.m[0][0])
        );
    }
    return QVector3D(
        qAtan2(-m_transform.m[2][1], m_transform.m[1][1]),
        qAtan2(-m_transform.m[0][2], sy),
        0.0f
    );
}

void SceneObject::setRotationEuler(const QVector3D& euler) {
    float cx = qCos(euler.x()), sx = qSin(euler.x());
    float cy = qCos(euler.y()), sy = qSin(euler.y());
    float cz = qCos(euler.z()), sz = qSin(euler.z());

    // ZYX rotation matrix
    m_transform.m[0][0] = cy * cz;
    m_transform.m[0][1] = cz * sx * sy - cx * sz;
    m_transform.m[0][2] = cx * cz * sy + sx * sz;
    m_transform.m[1][0] = cy * sz;
    m_transform.m[1][1] = cx * cz + sx * sy * sz;
    m_transform.m[1][2] = -cz * sx + cx * sy * sz;
    m_transform.m[2][0] = -sy;
    m_transform.m[2][1] = cy * sx;
    m_transform.m[2][2] = cx * cy;
    m_dirty = true;
    emit transformChanged();
}

QVector3D SceneObject::scale() const {
    return QVector3D(
        qSqrt(m_transform.m[0][0]*m_transform.m[0][0] + m_transform.m[0][1]*m_transform.m[0][1] + m_transform.m[0][2]*m_transform.m[0][2]),
        qSqrt(m_transform.m[1][0]*m_transform.m[1][0] + m_transform.m[1][1]*m_transform.m[1][1] + m_transform.m[1][2]*m_transform.m[1][2]),
        qSqrt(m_transform.m[2][0]*m_transform.m[2][0] + m_transform.m[2][1]*m_transform.m[2][1] + m_transform.m[2][2]*m_transform.m[2][2])
    );
}

void SceneObject::setScale(const QVector3D& s) {
    QVector3D rot = rotationEuler();
    // Rebuild rotation with scale
    float cx = qCos(rot.x()), sx = qSin(rot.x());
    float cy = qCos(rot.y()), sy = qSin(rot.y());
    float cz = qCos(rot.z()), sz = qSin(rot.z());

    m_transform.m[0][0] = cy * cz * s.x();
    m_transform.m[0][1] = (cz * sx * sy - cx * sz) * s.x();
    m_transform.m[0][2] = (cx * cz * sy + sx * sz) * s.x();
    m_transform.m[1][0] = cy * sz * s.y();
    m_transform.m[1][1] = (cx * cz + sx * sy * sz) * s.y();
    m_transform.m[1][2] = (-cz * sx + cx * sy * sz) * s.y();
    m_transform.m[2][0] = -sy * s.z();
    m_transform.m[2][1] = cy * sx * s.z();
    m_transform.m[2][2] = cx * cy * s.z();
    m_dirty = true;
    emit transformChanged();
}

QVector3D SceneObject::worldPosition() const {
    updateWorldTransform();
    return QVector3D(m_worldTransform.m[3][0], m_worldTransform.m[3][1], m_worldTransform.m[3][2]);
}

QVector3D SceneObject::worldScale() const {
    updateWorldTransform();
    const auto& m = m_worldTransform.m;
    return QVector3D(
        qSqrt(m[0][0]*m[0][0] + m[0][1]*m[0][1] + m[0][2]*m[0][2]),
        qSqrt(m[1][0]*m[1][0] + m[1][1]*m[1][1] + m[1][2]*m[1][2]),
        qSqrt(m[2][0]*m[2][0] + m[2][1]*m[2][1] + m[2][2]*m[2][2])
    );
}

QVector3D SceneObject::worldRotationEuler() const {
    updateWorldTransform();
    const auto& m = m_worldTransform.m;
    // Normalize the rotation columns by the world scale so the euler
    // extraction matches the local ZYX convention used by setRotationEuler.
    const QVector3D s = worldScale();
    if (s.x() > 1e-9f && s.y() > 1e-9f && s.z() > 1e-9f) {
        const float a00 = m[0][0] / s.x(), a01 = m[0][1] / s.x(), a02 = m[0][2] / s.x();
        const float a10 = m[1][0] / s.y(), a11 = m[1][1] / s.y(), a12 = m[1][2] / s.y();
        const float a20 = m[2][0] / s.z(), a21 = m[2][1] / s.z(), a22 = m[2][2] / s.z();
        float sy = qSqrt(a00 * a00 + a10 * a10);
        if (sy > 1e-6f) {
            return QVector3D(qAtan2(a21, a22), qAtan2(-a20, sy), qAtan2(a10, a00));
        }
        return QVector3D(qAtan2(-a21, a11), qAtan2(-a20, sy), 0.0f);
    }
    return rotationEuler();
}

void SceneObject::updateWorldTransform(bool force) const {
    if (!m_dirty && !force) return;

    if (m_parent) {
        m_parent->updateWorldTransform(force);
        m_worldTransform = m_parent->m_worldTransform * m_transform;
    } else {
        m_worldTransform = m_transform;
    }
    m_dirty = false;
}

SceneObject* SceneObject::findByName(const QString& name, bool recursive) const {
    for (SceneObject* child : m_children) {
        if (child->m_name == name)
            return child;
        if (recursive) {
            SceneObject* found = child->findByName(name, true);
            if (found) return found;
        }
    }
    return nullptr;
}

SceneObject* SceneObject::findById(int id) const {
    if (m_id == id) return const_cast<SceneObject*>(this);
    for (SceneObject* child : m_children) {
        SceneObject* found = child->findById(id);
        if (found) return found;
    }
    return nullptr;
}

QVector<SceneObject*> SceneObject::findByType(Type type, bool recursive) const {
    QVector<SceneObject*> result;
    for (SceneObject* child : m_children) {
        if (child->m_type == type)
            result.append(child);
        if (recursive)
            result.append(child->findByType(type, true));
    }
    return result;
}

void SceneObject::traverse(const std::function<void(SceneObject*)>& visitor) const {
    visitor(const_cast<SceneObject*>(this));
    for (SceneObject* child : m_children) {
        child->traverse(visitor);
    }
}

int SceneObject::descendantCount() const {
    int count = m_children.size();
    for (SceneObject* child : m_children) {
        count += child->descendantCount();
    }
    return count;
}

static const char* typeToString(SceneObject::Type t) {
    switch (t) {
        case SceneObject::Type::Node:    return "Node";
        case SceneObject::Type::Mesh:    return "Mesh";
        case SceneObject::Type::Spline:  return "Spline";
        case SceneObject::Type::Light:   return "Light";
        case SceneObject::Type::Camera:  return "Camera";
        case SceneObject::Type::Bone:    return "Bone";
        default: return "Unknown";
    }
}

static SceneObject::Type typeFromString(const QString& s) {
    if (s == "Node")    return SceneObject::Type::Node;
    if (s == "Mesh")    return SceneObject::Type::Mesh;
    if (s == "Spline")  return SceneObject::Type::Spline;
    if (s == "Light")   return SceneObject::Type::Light;
    if (s == "Camera")  return SceneObject::Type::Camera;
    if (s == "Bone")    return SceneObject::Type::Bone;
    return SceneObject::Type::Unknown;
}

QJsonObject SceneObject::serialize() const {
    QJsonObject json;
    json["id"] = m_id;
    json["name"] = m_name;
    json["type"] = typeToString(m_type);
    json["visible"] = m_visible;

    // Transform
    QJsonArray transformArr;
    for (int i = 0; i < 16; ++i)
        transformArr.append(static_cast<double>(m_transform.m[i / 4][i % 4]));
    json["transform"] = transformArr;

    // Material
    QJsonObject matJson;
    matJson["baseColor"] = QString("#%1").arg(m_baseColor.rgb(), 8, 16, QChar('0'));
    matJson["metallic"] = static_cast<double>(m_metallic);
    matJson["roughness"] = static_cast<double>(m_roughness);
    matJson["opacity"] = static_cast<double>(m_opacity);
    json["material"] = matJson;

    // Children
    QJsonArray childrenArr;
    for (SceneObject* child : m_children) {
        childrenArr.append(child->serialize());
    }
    json["children"] = childrenArr;

    return json;
}

void SceneObject::deserialize(const QJsonObject& json) {
    m_id = json["id"].toInt(0);
    m_name = json["name"].toString();
    m_type = typeFromString(json["type"].toString("Node"));
    m_visible = json["visible"].toBool(true);

    // Transform
    QJsonArray transformArr = json["transform"].toArray();
    if (transformArr.size() == 16) {
        for (int i = 0; i < 16; ++i)
            m_transform.m[i / 4][i % 4] = static_cast<float>(transformArr[i].toDouble());
    }

    // Material
    QJsonObject matJson = json["material"].toObject();
    if (!matJson.isEmpty()) {
        QString colorStr = matJson["baseColor"].toString();
        if (colorStr.startsWith("#")) {
            bool ok;
            m_baseColor = QColor::fromRgb(colorStr.toUInt(&ok, 16));
        }
        m_metallic = static_cast<float>(matJson["metallic"].toDouble(0.0));
        m_roughness = static_cast<float>(matJson["roughness"].toDouble(0.5));
        m_opacity = static_cast<float>(matJson["opacity"].toDouble(1.0));
    }

    m_dirty = true;
}

SceneObject* SceneObject::fromJson(const QJsonObject& json, int& nextId) {
    SceneObject* obj = new SceneObject(nextId++, json["name"].toString(),
                                        typeFromString(json["type"].toString("Node")));
    obj->deserialize(json);
    obj->m_id = json["id"].toInt(nextId - 1);

    QJsonArray childrenArr = json["children"].toArray();
    for (const auto& childJson : childrenArr) {
        SceneObject* child = fromJson(childJson.toObject(), nextId);
        obj->addChild(child);
    }

    return obj;
}

void SceneObject::setVisible(bool visible) {
    if (m_visible != visible) {
        m_visible = visible;
        emit visibilityChanged(visible);
    }
}

void SceneObject::setMesh(SceneMesh* mesh) {
    m_mesh = mesh;
    emit meshChanged();
}

void SceneObject::setMaterial(Material* material) {
    m_material = material;
}

SceneObject* SceneObject::findChild(const QString& name) {
    for (SceneObject* child : m_children) {
        if (child->m_name == name)
            return child;
    }
    return nullptr;
}

QVector<SceneObject*> SceneObject::findChildren(const QString& name, bool recursive) const {
    QVector<SceneObject*> result;
    for (SceneObject* child : m_children) {
        if (child->m_name == name)
            result.append(child);
        if (recursive)
            result.append(child->findChildren(name, true));
    }
    return result;
}

} // namespace ks
