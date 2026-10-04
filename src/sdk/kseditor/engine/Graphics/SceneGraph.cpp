#include "SceneGraph.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>

namespace ks {

SceneGraph::SceneGraph()
{
    m_root = new SceneObject(0, QStringLiteral("Root"), SceneObject::Type::Node);
}

SceneGraph::~SceneGraph()
{
    if (m_root) {
        deleteRecursive(m_root);
        m_root = nullptr;
    }
}

SceneObject* SceneGraph::createObject(const QString& name,
                                      SceneObject::Type type,
                                      SceneObject* parent)
{
    if (!parent)
        parent = m_root;

    SceneObject* obj = new SceneObject(m_nextId++, name, type);
    obj->setSceneGraph(this);
    parent->addChild(obj);
    return obj;
}

void SceneGraph::deleteObject(SceneObject* obj)
{
    if (!obj || obj == m_root)
        return;

    if (SceneObject* p = obj->parent())
        p->removeChild(obj);

    deleteRecursive(obj);
}

SceneObject* SceneGraph::findObjectById(int id) const
{
    if (!m_root)
        return nullptr;

    return m_root->findById(id);
}

SceneObject* SceneGraph::findObjectByName(const QString& name, bool recursive) const
{
    if (!m_root)
        return nullptr;

    return m_root->findByName(name, recursive);
}

QVector<SceneObject*> SceneGraph::findObjectsByType(SceneObject::Type type) const
{
    if (!m_root)
        return {};

    return m_root->findByType(type, true);
}

void SceneGraph::updateAllTransforms()
{
    if (m_root) {
        m_root->updateWorldTransform(true);
    }
}

QJsonObject SceneGraph::serialize() const
{
    QJsonObject json;
    json["version"] = 1;
    json["objectCount"] = m_nextId - 1;
    json["root"] = m_root->serialize();
    return json;
}

void SceneGraph::deserialize(const QJsonObject& json)
{
    clear();
    int nextId = 1;
    QJsonObject rootJson = json["root"].toObject();
    if (m_root) {
        deleteRecursive(m_root);
    }
    m_root = SceneObject::fromJson(rootJson, nextId);
    m_nextId = nextId;
    m_root->setSceneGraph(this);
    m_root->traverse([this](SceneObject* o) { o->setSceneGraph(this); });
}

bool SceneGraph::saveToFile(const QString& filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QJsonDocument doc(serialize());
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool SceneGraph::loadFromFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        return false;
    }

    deserialize(doc.object());
    return true;
}

void SceneGraph::deleteRecursive(SceneObject* obj)
{
    if (!obj)
        return;

    auto children = obj->children();
    for (SceneObject* c : children) {
        deleteRecursive(c);
    }

    delete obj;
}

void SceneGraph::collectAll(SceneObject* obj, QVector<SceneObject*>& out) const
{
    if (!obj)
        return;

    out.append(obj);
    for (SceneObject* c : obj->children()) {
        collectAll(c, out);
    }
}

} // namespace ks
