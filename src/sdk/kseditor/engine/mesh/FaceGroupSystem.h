#pragma once

#include <QString>
#include <QVector>
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>

namespace ks {

// Mudbox-style face groups: named, colored, visible groups of faces used to
// organize/mask/isolate regions while sculpting or painting. Groups are
// scene-level definitions; each object stores a per-face group index (-1 = not
// assigned). Serializable to/from JSON so the .ks3d aux metadata can persist it.
class FaceGroupSystem {
public:
    struct Group {
        QString name;
        int color = 0;       // packed RGB (0xRRGGBB)
        bool visible = true;
    };

    // ---- Scene-level group definitions ----
    int addGroup(const QString& name, int color = 0);
    bool removeGroup(int index);
    bool renameGroup(int index, const QString& name);
    bool setGroupColor(int index, int color);
    bool setGroupVisible(int index, bool visible);
    const Group* groupAt(int index) const;
    int groupCount() const;

    // ---- Per-object per-face assignment ----
    void assignFace(int objectId, int faceIndex, int groupIndex);
    void assignFaces(int objectId, const QVector<int>& faceIndices, int groupIndex);
    void clearFace(int objectId, int faceIndex);
    int groupForFace(int objectId, int faceIndex) const;
    QVector<int> facesInGroup(int objectId, int groupIndex) const;
    QVector<int> facesInAnyVisibleGroup(int objectId) const;
    int memberCount(int groupIndex) const;

    void removeObject(int objectId);
    void clearAll();

    // ---- Persistence (per-object keys resolved externally to names) ----
    QJsonObject groupsToJson() const;
    void groupsFromJson(const QJsonObject& o);
    QJsonObject assignToJson() const;
    void assignFromJson(const QJsonObject& o);

private:
    QVector<Group> m_groups;
    QHash<int, QVector<int>> m_assign;   // objectId -> per-face group index
};

} // namespace ks
