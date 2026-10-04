#include "FaceGroupSystem.h"

namespace ks {

int FaceGroupSystem::addGroup(const QString& name, int color) {
    Group g;
    g.name = name.isEmpty() ? QString("Group %1").arg(m_groups.size() + 1) : name;
    g.color = color;
    g.visible = true;
    m_groups.append(g);
    return m_groups.size() - 1;
}

bool FaceGroupSystem::removeGroup(int index) {
    if (index < 0 || index >= m_groups.size()) return false;
    m_groups.removeAt(index);
    // Shift references above the removed index down; -1 stays -1.
    for (auto it = m_assign.begin(); it != m_assign.end(); ++it) {
        QVector<int>& v = it.value();
        for (int& f : v) {
            if (f == index) f = -1;
            else if (f > index) --f;
        }
    }
    return true;
}

bool FaceGroupSystem::renameGroup(int index, const QString& name) {
    if (index < 0 || index >= m_groups.size() || name.isEmpty()) return false;
    m_groups[index].name = name;
    return true;
}

bool FaceGroupSystem::setGroupColor(int index, int color) {
    if (index < 0 || index >= m_groups.size()) return false;
    m_groups[index].color = color;
    return true;
}

bool FaceGroupSystem::setGroupVisible(int index, bool visible) {
    if (index < 0 || index >= m_groups.size()) return false;
    m_groups[index].visible = visible;
    return true;
}

const FaceGroupSystem::Group* FaceGroupSystem::groupAt(int index) const {
    if (index < 0 || index >= m_groups.size()) return nullptr;
    return &m_groups.at(index);
}

int FaceGroupSystem::groupCount() const {
    return m_groups.size();
}

void FaceGroupSystem::assignFace(int objectId, int faceIndex, int groupIndex) {
    if (faceIndex < 0) return;
    if (groupIndex < -1 || groupIndex >= m_groups.size()) return;
    QVector<int>& v = m_assign[objectId];
    if (faceIndex >= v.size()) v.resize(faceIndex + 1, -1);
    v[faceIndex] = groupIndex;
}

void FaceGroupSystem::assignFaces(int objectId, const QVector<int>& faceIndices, int groupIndex) {
    if (groupIndex < -1 || groupIndex >= m_groups.size()) return;
    QVector<int>& v = m_assign[objectId];
    for (int fi : faceIndices) {
        if (fi < 0) continue;
        if (fi >= v.size()) v.resize(fi + 1, -1);
        v[fi] = groupIndex;
    }
}

void FaceGroupSystem::clearFace(int objectId, int faceIndex) {
    if (faceIndex < 0) return;
    QVector<int>& v = m_assign[objectId];
    if (faceIndex < v.size()) v[faceIndex] = -1;
}

int FaceGroupSystem::groupForFace(int objectId, int faceIndex) const {
    const QVector<int>& v = m_assign.value(objectId);
    if (faceIndex < 0 || faceIndex >= v.size()) return -1;
    return v[faceIndex];
}

QVector<int> FaceGroupSystem::facesInGroup(int objectId, int groupIndex) const {
    QVector<int> out;
    const QVector<int>& v = m_assign.value(objectId);
    for (int i = 0; i < v.size(); ++i)
        if (v[i] == groupIndex) out.append(i);
    return out;
}

QVector<int> FaceGroupSystem::facesInAnyVisibleGroup(int objectId) const {
    QVector<int> out;
    const QVector<int>& v = m_assign.value(objectId);
    for (int i = 0; i < v.size(); ++i) {
        if (v[i] < 0) continue;
        if (v[i] < m_groups.size() && m_groups[v[i]].visible)
            out.append(i);
    }
    return out;
}

int FaceGroupSystem::memberCount(int groupIndex) const {
    int count = 0;
    for (auto it = m_assign.constBegin(); it != m_assign.constEnd(); ++it)
        for (int f : it.value())
            if (f == groupIndex) ++count;
    return count;
}

void FaceGroupSystem::removeObject(int objectId) {
    m_assign.remove(objectId);
}

void FaceGroupSystem::clearAll() {
    m_groups.clear();
    m_assign.clear();
}

QJsonObject FaceGroupSystem::groupsToJson() const {
    QJsonObject o;
    QJsonArray arr;
    for (const Group& g : m_groups) {
        QJsonObject go;
        go["name"] = g.name;
        go["color"] = g.color;
        go["visible"] = g.visible;
        arr.append(go);
    }
    o["defs"] = arr;
    return o;
}

void FaceGroupSystem::groupsFromJson(const QJsonObject& o) {
    m_groups.clear();
    const QJsonArray arr = o["defs"].toArray();
    for (const auto& v : arr) {
        const QJsonObject go = v.toObject();
        Group g;
        g.name = go["name"].toString(QString("Group %1").arg(m_groups.size() + 1));
        g.color = go["color"].toInt(0);
        g.visible = go["visible"].toBool(true);
        m_groups.append(g);
    }
}

QJsonObject FaceGroupSystem::assignToJson() const {
    QJsonObject o;
    for (auto it = m_assign.constBegin(); it != m_assign.constEnd(); ++it) {
        QJsonArray arr;
        for (int f : it.value()) arr.append(f);
        o[QString::number(it.key())] = arr;
    }
    return o;
}

void FaceGroupSystem::assignFromJson(const QJsonObject& o) {
    m_assign.clear();
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        bool ok = false;
        const int objectId = it.key().toInt(&ok);
        if (!ok) continue;
        QVector<int> v;
        const QJsonArray arr = it.value().toArray();
        for (const auto& fv : arr) v.append(fv.toInt(-1));
        m_assign[objectId] = v;
    }
}

} // namespace ks
