#include "ProjectSerializer.h"
#include "sdk/kseditor/engine/Graphics/SceneGraph.h"
#include "sdk/kseditor/engine/Graphics/SceneObject.h"
#include "sdk/kseditor/engine/Graphics/SceneMesh.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QStandardPaths>

namespace ks {

ProjectSerializer& ProjectSerializer::instance() {
    static ProjectSerializer s;
    return s;
}

bool ProjectSerializer::save(const QString& path, const ProjectData& data) {
    QJsonObject root;

    root["formatVersion"] = FILE_FORMAT_VERSION;
    root["editorVersion"] = "1.16.0";
    root["name"] = data.name;
    root["version"] = data.version;
    root["created"] = data.created.toString(Qt::ISODate);
    root["modified"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["activeModule"] = data.activeModule;
    root["activeModuleIndex"] = data.activeModuleIndex;

    if (!data.modelerState.isEmpty())
        root["modeler"] = QJsonObject::fromVariantMap(data.modelerState);
    if (!data.audioState.isEmpty())
        root["audio"] = QJsonObject::fromVariantMap(data.audioState);
    if (!data.physicsState.isEmpty())
        root["physics"] = QJsonObject::fromVariantMap(data.physicsState);
    if (!data.showroomState.isEmpty())
        root["showroom"] = QJsonObject::fromVariantMap(data.showroomState);
    if (!data.trackState.isEmpty())
        root["track"] = QJsonObject::fromVariantMap(data.trackState);
    if (!data.characterState.isEmpty())
        root["character"] = QJsonObject::fromVariantMap(data.characterState);
    if (!data.settings.isEmpty())
        root["settings"] = QJsonObject::fromVariantMap(data.settings);
    if (!data.windowLayout.isEmpty())
        root["windowLayout"] = QJsonObject::fromVariantMap(data.windowLayout);

    QJsonDocument doc(root);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        emit error("Cannot write to: " + path);
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Compact));
    file.close();

    emit projectSaved(path);
    return true;
}

bool ProjectSerializer::load(const QString& path, ProjectData& data) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit error("Cannot read: " + path);
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isNull() || !doc.isObject()) {
        emit error("Invalid JSON in: " + path);
        return false;
    }

    QJsonObject root = doc.object();

    int formatVer = root["formatVersion"].toInt(1);
    if (formatVer > FILE_FORMAT_VERSION) {
        emit error("Unsupported project format version: " + QString::number(formatVer));
        return false;
    }

    data.name = root["name"].toString();
    data.version = root["version"].toString("1.0");
    data.filePath = path;
    data.created = QDateTime::fromString(root["created"].toString(), Qt::ISODate);
    data.modified = QDateTime::fromString(root["modified"].toString(), Qt::ISODate);
    data.activeModule = root["activeModule"].toString();
    data.activeModuleIndex = root["activeModuleIndex"].toInt(0);

    if (root.contains("modeler"))
        data.modelerState = root["modeler"].toObject().toVariantMap();
    if (root.contains("audio"))
        data.audioState = root["audio"].toObject().toVariantMap();
    if (root.contains("physics"))
        data.physicsState = root["physics"].toObject().toVariantMap();
    if (root.contains("showroom"))
        data.showroomState = root["showroom"].toObject().toVariantMap();
    if (root.contains("track"))
        data.trackState = root["track"].toObject().toVariantMap();
    if (root.contains("character"))
        data.characterState = root["character"].toObject().toVariantMap();
    if (root.contains("settings"))
        data.settings = root["settings"].toObject().toVariantMap();
    if (root.contains("windowLayout"))
        data.windowLayout = root["windowLayout"].toObject().toVariantMap();

    emit projectLoaded(path);
    return true;
}

bool ProjectSerializer::saveBackup(const QString& path, const ProjectData& data) {
    QFileInfo info(path);
    QString backupDir = info.absolutePath() + "/.kseditor_backup";
    QDir().mkpath(backupDir);

    QString backupName = QString("%1_%2.ksep")
        .arg(info.baseName())
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));

    return save(backupDir + "/" + backupName, data);
}

QJsonObject ProjectSerializer::serializeScene(SceneGraph* scene) {
    if (!scene) return QJsonObject();

    QJsonObject json;
    QJsonArray objectsArray;

    for (SceneObject* obj : scene->allObjects()) {
        objectsArray.append(serializeObject(obj));
    }

    json["objects"] = objectsArray;
    json["objectCount"] = objectsArray.size();
    return json;
}

bool ProjectSerializer::deserializeScene(SceneGraph* scene, const QJsonObject& json) {
    if (!scene) return false;

    scene->clear();

    QJsonArray objectsArray = json["objects"].toArray();
    for (const QJsonValue& val : objectsArray) {
        deserializeObject(val.toObject(), scene);
    }

    return true;
}

QJsonObject ProjectSerializer::serializeObject(SceneObject* obj) {
    QJsonObject json;

    json["id"] = obj->id();
    json["name"] = obj->name();
    json["type"] = static_cast<int>(obj->type());
    json["visible"] = obj->isVisible();

    auto pos = obj->position();
    auto rot = obj->rotationEuler();
    auto sc = obj->scale();

    QJsonObject transform;
    transform["x"] = static_cast<double>(pos.x());
    transform["y"] = static_cast<double>(pos.y());
    transform["z"] = static_cast<double>(pos.z());
    transform["rx"] = static_cast<double>(rot.x());
    transform["ry"] = static_cast<double>(rot.y());
    transform["rz"] = static_cast<double>(rot.z());
    transform["sx"] = static_cast<double>(sc.x());
    transform["sy"] = static_cast<double>(sc.y());
    transform["sz"] = static_cast<double>(sc.z());
    json["transform"] = transform;

    if (obj->mesh()) {
        json["mesh"] = serializeMesh(obj->mesh());
    }

    return json;
}

SceneObject* ProjectSerializer::deserializeObject(const QJsonObject& json, SceneGraph* scene) {
    if (!scene) return nullptr;

    QString name = json["name"].toString("Object");
    int type = json["type"].toInt(1);

    SceneObject::Type objType = static_cast<SceneObject::Type>(type);
    SceneObject* obj = scene->createObject(name, objType);
    if (!obj) return nullptr;

    obj->setVisible(json["visible"].toBool(true));

    if (json.contains("transform")) {
        QJsonObject t = json["transform"].toObject();
        obj->setPosition(QVector3D(
            t["x"].toDouble(0),
            t["y"].toDouble(0),
            t["z"].toDouble(0)
        ));
        obj->setRotationEuler(QVector3D(
            t["rx"].toDouble(0),
            t["ry"].toDouble(0),
            t["rz"].toDouble(0)
        ));
        obj->setScale(QVector3D(
            t["sx"].toDouble(1),
            t["sy"].toDouble(1),
            t["sz"].toDouble(1)
        ));
    }

    if (json.contains("mesh") && objType == SceneObject::Type::Mesh) {
        SceneMesh* mesh = new SceneMesh();
        deserializeMesh(mesh, json["mesh"].toObject());
        obj->setMesh(mesh);
    }

    return obj;
}

QJsonObject ProjectSerializer::serializeMesh(SceneMesh* mesh) {
    QJsonObject json;

    auto& verts = mesh->geometry().vertices;
    auto& idxs = mesh->geometry().indices;

    QJsonArray posArray, normArray, uvArray, idxArray, colorArray;

    for (const auto& v : verts) {
        QJsonArray pos = { v.position.x(), v.position.y(), v.position.z() };
        posArray.append(pos);

        QJsonArray norm = { v.normal.x(), v.normal.y(), v.normal.z() };
        normArray.append(norm);

        QJsonArray uv = { v.uv.x(), v.uv.y() };
        uvArray.append(uv);

        QJsonArray col = { v.color.x(), v.color.y(), v.color.z(), 1.0 };
        colorArray.append(col);
    }

    for (uint32_t idx : idxs) {
        idxArray.append(static_cast<int>(idx));
    }

    json["vertexCount"] = verts.size();
    json["indexCount"] = idxs.size();
    json["positions"] = posArray;
    json["normals"] = normArray;
    json["uvs"] = uvArray;
    json["colors"] = colorArray;
    json["indices"] = idxArray;

    return json;
}

bool ProjectSerializer::deserializeMesh(SceneMesh* mesh, const QJsonObject& json) {
    if (!mesh) return false;

    QJsonArray posArray = json["positions"].toArray();
    QJsonArray idxArray = json["indices"].toArray();

    if (posArray.isEmpty()) return false;

    auto& verts = mesh->geometry().vertices;
    auto& idxs = mesh->geometry().indices;

    verts.reserve(posArray.size());
    for (int i = 0; i < posArray.size(); ++i) {
        QJsonArray pos = posArray[i].toArray();
        SceneVertex v;
        v.position = QVector3D(pos[0].toDouble(), pos[1].toDouble(), pos[2].toDouble());

        if (i < json["normals"].toArray().size()) {
            QJsonArray norm = json["normals"].toArray()[i].toArray();
            v.normal = QVector3D(norm[0].toDouble(), norm[1].toDouble(), norm[2].toDouble());
        }

        if (i < json["uvs"].toArray().size()) {
            QJsonArray uv = json["uvs"].toArray()[i].toArray();
            v.uv = QVector2D(static_cast<float>(uv[0].toDouble()), static_cast<float>(uv[1].toDouble()));
        }

        if (i < json["colors"].toArray().size()) {
            QJsonArray col = json["colors"].toArray()[i].toArray();
            v.color = QVector4D(col[0].toDouble(), col[1].toDouble(), col[2].toDouble(), 1.0);
        }

        verts.append(v);
    }

    idxs.reserve(idxArray.size());
    for (int i = 0; i < idxArray.size(); ++i) {
        idxs.append(static_cast<uint32_t>(idxArray[i].toInt()));
    }

    return true;
}

} // namespace ks
