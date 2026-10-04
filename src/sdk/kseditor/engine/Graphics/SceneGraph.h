#pragma once

#include <QVector>
#include <QString>
#include <QJsonObject>

#if !defined(Q_MOC_RUN)
#include "SceneObject.h"
#endif

namespace ks {

class SceneGraph
{
public:
    SceneGraph();
    ~SceneGraph();

    SceneObject* root() const { return m_root; }

    SceneObject* createObject(const QString& name,
                              SceneObject::Type type = SceneObject::Type::Node,
                              SceneObject* parent = nullptr);

    void deleteObject(SceneObject* obj);

    SceneObject* findObjectById(int id) const;
    SceneObject* findObjectByName(const QString& name, bool recursive = true) const;
    QVector<SceneObject*> findObjectsByType(SceneObject::Type type) const;
    QVector<SceneObject*> allObjects() const { QVector<SceneObject*> all; collectAll(m_root, all); return all; }

    // Transform propagation
    void updateAllTransforms();

    // Serialization
    QJsonObject serialize() const;
    void deserialize(const QJsonObject& json);
    bool saveToFile(const QString& filePath) const;
    bool loadFromFile(const QString& filePath);
    int objectCount() const { return allObjects().size() - 1; } // -1 for root

    void clear() { if (m_root) { deleteRecursive(m_root); m_root = new SceneObject(0, QStringLiteral("Root"), SceneObject::Type::Node); m_nextId = 1; } }

private:
    SceneObject* m_root = nullptr;
    int m_nextId = 1;

    void deleteRecursive(SceneObject* obj);
    void collectAll(SceneObject* obj, QVector<SceneObject*>& out) const;
};

} // namespace ks
