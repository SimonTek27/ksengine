#include "SceneData.h"
#include "SceneObject.h"
#include "SceneGraph.h"
#include "SceneMesh.h"
#include "RenderGraph.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace ks {

// ─── SceneProjectSerializer Implementation ─────────────────────────────────────

SceneProjectSerializer* SceneProjectSerializer::s_instance = nullptr;

SceneProjectSerializer& SceneProjectSerializer::instance() {
    if (!s_instance) s_instance = new SceneProjectSerializer();
    return *s_instance;
}

SceneProjectSerializer::SceneProjectSerializer(QObject* parent) : QObject(parent) {}
SceneProjectSerializer::~SceneProjectSerializer() { s_instance = nullptr; }

bool SceneProjectSerializer::saveProject(const QString& path, const SceneProjectData& data) {
    QJsonObject obj;
    obj["formatVersion"] = data.formatVersion;
    obj["editorVersion"] = data.editorVersion;
    obj["name"] = data.name;
    obj["version"] = data.version;
    obj["description"] = data.description;
    obj["author"] = data.author;
    obj["activeModule"] = data.activeModule;
    obj["activeModuleIndex"] = data.activeModuleIndex;
    obj["scene"] = data.scene;
    obj["windowLayout"] = data.windowLayout;
    obj["recentFiles"] = QJsonArray::fromStringList(data.recentFiles);
    obj["assetDatabasePath"] = data.assetDatabasePath;

    QJsonObject settingsObj;
    for (auto it = data.settings.constBegin(); it != data.settings.constEnd(); ++it) {
        settingsObj[it.key()] = QJsonValue::fromVariant(it.value());
    }
    obj["settings"] = settingsObj;

    QJsonObject modulesObj;
    for (auto it = data.moduleStates.constBegin(); it != data.moduleStates.constEnd(); ++it) {
        modulesObj[it.key()] = it.value();
    }
    obj["moduleStates"] = modulesObj;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        emit projectError("Cannot save project: " + path);
        return false;
    }
    file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
    file.close();
    emit projectSaved(path);
    return true;
}

bool SceneProjectSerializer::loadProject(const QString& path, SceneProjectData& data) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit projectError("Cannot load project: " + path);
        return false;
    }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    QJsonObject obj = doc.object();

    data.formatVersion = obj["formatVersion"].toString();
    data.editorVersion = obj["editorVersion"].toString();
    data.name = obj["name"].toString();
    data.version = obj["version"].toString();
    data.description = obj["description"].toString();
    data.author = obj["author"].toString();
    data.activeModule = obj["activeModule"].toString();
    data.activeModuleIndex = obj["activeModuleIndex"].toInt();
    data.scene = obj["scene"].toObject();
    data.windowLayout = obj["windowLayout"].toObject();
    data.assetDatabasePath = obj["assetDatabasePath"].toString();

    QJsonArray recentArr = obj["recentFiles"].toArray();
    data.recentFiles.clear();
    for (const auto& v : recentArr) data.recentFiles.append(v.toString());

    QJsonObject settingsObj = obj["settings"].toObject();
    for (auto it = settingsObj.constBegin(); it != settingsObj.constEnd(); ++it) {
        data.settings[it.key()] = it.value().toVariant();
    }

    QJsonObject modulesObj = obj["moduleStates"].toObject();
    for (auto it = modulesObj.constBegin(); it != modulesObj.constEnd(); ++it) {
        data.moduleStates[it.key()] = it.value().toObject();
    }

    emit projectLoaded(path);
    return true;
}

bool SceneProjectSerializer::saveBackup(const QString& path, const SceneProjectData& data) {
    return saveProject(path, data);
}

QJsonObject SceneProjectSerializer::serializeScene(const SceneGraph* scene) {
    if (!scene) return QJsonObject();
    return scene->serialize();
}

bool SceneProjectSerializer::deserializeScene(SceneGraph* scene, const QJsonObject& json) {
    if (!scene) return false;
    scene->deserialize(json);
    return true;
}

QJsonObject SceneProjectSerializer::serializeObject(const SceneObject* object) {
    if (!object) return QJsonObject();
    return object->serialize();
}

SceneObject* SceneProjectSerializer::deserializeObject(SceneGraph* graph, const QJsonObject& json) {
    if (!graph) return nullptr;
    int nextId = 0;
    return SceneObject::fromJson(json, nextId);
}

QJsonObject SceneProjectSerializer::serializeMesh(const SceneMesh* mesh) {
    if (!mesh) return QJsonObject();
    return mesh->toJson();
}

SceneMesh* SceneProjectSerializer::deserializeMesh(const QJsonObject& json) {
    return SceneMesh::fromJson(json);
}

QJsonObject SceneProjectSerializer::serializeMaterial(const PBRMaterial* material) {
    QJsonObject obj;
    if (!material) return obj;
    const auto& p = material->parameters();
    obj["baseColorFactor"] = QJsonArray{p.baseColorFactor.x(), p.baseColorFactor.y(), p.baseColorFactor.z(), p.baseColorFactor.w()};
    obj["metallicFactor"] = p.metallicFactor;
    obj["roughnessFactor"] = p.roughnessFactor;
    obj["emissiveFactor"] = QJsonArray{p.emissiveFactor.x(), p.emissiveFactor.y(), p.emissiveFactor.z()};
    obj["baseColorTexture"] = p.baseColorTexture.name;
    obj["normalTexture"] = p.normalTexture.name;
    obj["metallicRoughnessTexture"] = p.metallicRoughnessTexture.name;
    obj["occlusionTexture"] = p.occlusionTexture.name;
    obj["emissiveTexture"] = p.emissiveTexture.name;
    obj["alphaMode"] = static_cast<int>(p.alphaMode);
    obj["doubleSided"] = p.doubleSided;
    return obj;
}

PBRMaterial* SceneProjectSerializer::deserializeMaterial(const QJsonObject& json) {
    PBRMaterial* material = new PBRMaterial();
    auto params = material->parameters();
    QJsonArray bcf = json["baseColorFactor"].toArray();
    if (bcf.size() >= 4) params.baseColorFactor = {static_cast<float>(bcf[0].toDouble(1.0)), static_cast<float>(bcf[1].toDouble(1.0)), static_cast<float>(bcf[2].toDouble(1.0)), static_cast<float>(bcf[3].toDouble(1.0))};
    params.metallicFactor = static_cast<float>(json["metallicFactor"].toDouble(1.0));
    params.roughnessFactor = static_cast<float>(json["roughnessFactor"].toDouble(1.0));
    QJsonArray ef = json["emissiveFactor"].toArray();
    if (ef.size() >= 3) params.emissiveFactor = {static_cast<float>(ef[0].toDouble(0.0)), static_cast<float>(ef[1].toDouble(0.0)), static_cast<float>(ef[2].toDouble(0.0))};
    params.baseColorTexture.name = json["baseColorTexture"].toString();
    params.normalTexture.name = json["normalTexture"].toString();
    params.metallicRoughnessTexture.name = json["metallicRoughnessTexture"].toString();
    params.occlusionTexture.name = json["occlusionTexture"].toString();
    params.emissiveTexture.name = json["emissiveTexture"].toString();
    params.alphaMode = static_cast<PBRMaterial::AlphaMode>(json["alphaMode"].toInt(0));
    params.doubleSided = json["doubleSided"].toBool(false);
    material->setParameters(params);
    return material;
}

bool SceneProjectSerializer::migrateProject(SceneProjectData& data, const QString& fromVersion) {
    if (fromVersion.isEmpty() || fromVersion == data.formatVersion) return true;

    QStringList parts = fromVersion.split('.');
    int maj = parts.size() > 0 ? parts[0].toInt() : 0;
    int min = parts.size() > 1 ? parts[1].toInt() : 0;

    // Migration from v1.x to v2.x: wrap module states
    if (maj < 2) {
        QMap<QString, QJsonObject> migrated;
        QJsonObject legacy;
        for (auto it = data.moduleStates.constBegin(); it != data.moduleStates.constEnd(); ++it)
            legacy[it.key()] = it.value();
        migrated["legacy"] = legacy;
        data.moduleStates = migrated;
    }

    // Migration from v2.0 to v2.1+: add asset database path
    if (maj == 2 && min < 1) {
        if (data.assetDatabasePath.isEmpty()) {
            data.assetDatabasePath = "assets/";
        }
    }

    data.formatVersion = "0.9.0";
    return true;
}

QString SceneProjectSerializer::generateProjectPath(const QString& baseDir, const QString& name) {
    return QDir(baseDir).filePath(name + ".ksproject");
}

bool SceneProjectSerializer::validateProject(const SceneProjectData& data, QStringList& errors) {
    errors.clear();
    if (data.name.isEmpty()) errors.append("Project name is empty");
    return errors.isEmpty();
}

// ─── AssetDatabaseSerializer Implementation ───────────────────────────────

AssetDatabaseSerializer* AssetDatabaseSerializer::s_instance = nullptr;

AssetDatabaseSerializer& AssetDatabaseSerializer::instance() {
    if (!s_instance) s_instance = new AssetDatabaseSerializer();
    return *s_instance;
}

AssetDatabaseSerializer::AssetDatabaseSerializer(QObject* parent) : QObject(parent) {}
AssetDatabaseSerializer::~AssetDatabaseSerializer() { s_instance = nullptr; }

bool AssetDatabaseSerializer::saveDatabase(const QString& path) {
    if (path.isEmpty() || m_assets.isEmpty()) return false;
    QJsonObject root;
    root["version"] = 1;
    root["assetCount"] = m_assets.size();

    QJsonArray assetArray = serializeAssets(m_assets.keys().toVector());
    root["assets"] = assetArray;

    QJsonArray collectionArray = serializeCollections(m_collections.keys().toVector());
    root["collections"] = collectionArray;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool AssetDatabaseSerializer::loadDatabase(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) return false;

    QJsonObject root = doc.object();
    m_assets.clear();
    m_collections.clear();

    QJsonArray assetArray = root["assets"].toArray();
    for (const auto& val : assetArray) {
        QJsonObject obj = val.toObject();
        QString id = obj["id"].toString();
        if (!id.isEmpty()) {
            AssetRecord rec;
            rec.id = QUuid(id);
            rec.name = obj["name"].toString();
            rec.type = obj["type"].toString();
            rec.path = obj["path"].toString();
            rec.collectionId = obj["collectionId"].toString();
            m_assets.insert(rec.id, rec);
        }
    }

    QJsonArray collectionArray = root["collections"].toArray();
    for (const auto& val : collectionArray) {
        QJsonObject obj = val.toObject();
        QString id = obj["id"].toString();
        if (!id.isEmpty()) {
            CollectionRecord coll;
            coll.id = QUuid(id);
            coll.name = obj["name"].toString();
            coll.description = obj["description"].toString();
            QJsonArray assetIds = obj["assetIds"].toArray();
            for (const auto& aid : assetIds)
                coll.assetIds.append(QUuid(aid.toString()));
            m_collections.insert(coll.id, coll);
        }
    }
    return true;
}

QJsonArray AssetDatabaseSerializer::serializeAssets(const QVector<QUuid>& assetIds) {
    QJsonArray result;
    for (const auto& id : assetIds) {
        if (!m_assets.contains(id)) continue;
        const auto& rec = m_assets[id];
        QJsonObject obj;
        obj["id"] = rec.id.toString(QUuid::WithoutBraces);
        obj["name"] = rec.name;
        obj["type"] = rec.type;
        obj["path"] = rec.path;
        obj["collectionId"] = rec.collectionId;
        result.append(obj);
    }
    return result;
}

QJsonArray AssetDatabaseSerializer::serializeCollections(const QVector<QUuid>& collectionIds) {
    QJsonArray result;
    for (const auto& id : collectionIds) {
        if (!m_collections.contains(id)) continue;
        const auto& coll = m_collections[id];
        QJsonObject obj;
        obj["id"] = coll.id.toString(QUuid::WithoutBraces);
        obj["name"] = coll.name;
        obj["description"] = coll.description;
        QJsonArray assetIds;
        for (const auto& aid : coll.assetIds)
            assetIds.append(aid.toString(QUuid::WithoutBraces));
        obj["assetIds"] = assetIds;
        result.append(obj);
    }
    return result;
}

bool AssetDatabaseSerializer::exportPackage(const QString& path, const QVector<QUuid>& assetIds) {
    QJsonDocument doc(serializeAssets(assetIds));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool AssetDatabaseSerializer::importPackage(const QString& path, QVector<QUuid>& importedIds) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isArray()) return false;

    QJsonArray arr = doc.array();
    for (const auto& val : arr) {
        QJsonObject obj = val.toObject();
        AssetRecord rec;
        rec.id = QUuid::createUuid();
        rec.name = obj["name"].toString();
        rec.type = obj["type"].toString();
        rec.path = obj["path"].toString();
        rec.collectionId = obj["collectionId"].toString();
        m_assets.insert(rec.id, rec);
        importedIds.append(rec.id);
    }
    return true;
}

bool AssetDatabaseSerializer::validateAssets(QVector<QUuid>& valid, QVector<QUuid>& invalid) {
    valid.clear();
    invalid.clear();
    for (auto it = m_assets.begin(); it != m_assets.end(); ++it) {
        if (QFile::exists(it.value().path)) {
            valid.append(it.key());
        } else {
            invalid.append(it.key());
        }
    }
    return invalid.isEmpty();
}

void AssetDatabaseSerializer::repairMissingAssets(const QVector<QUuid>& assetIds) {
    for (const auto& id : assetIds) {
        if (m_assets.contains(id)) {
            AssetRecord& rec = m_assets[id];
            QStringList searchDirs = { "assets/", "textures/", "models/", "meshes/" };
            for (const auto& dir : searchDirs) {
                QDir searchDir(dir);
                if (searchDir.exists()) {
                    QStringList candidates = searchDir.entryList(QStringList{rec.name + ".*"}, QDir::Files, QDir::Name);
                    if (!candidates.isEmpty()) {
                        rec.path = searchDir.absoluteFilePath(candidates.first());
                        break;
                    }
                }
            }
        }
    }
}



// ─── VersionInfo Implementation ──────────────────────────────────────────

QString VersionInfo::toString() const {
    return QString("%1.%2.%3").arg(major).arg(minor).arg(patch);
}

VersionInfo VersionInfo::fromString(const QString& str) {
    VersionInfo v;
    QStringList parts = str.split('.');
    if (parts.size() >= 1) v.major = parts[0].toInt();
    if (parts.size() >= 2) v.minor = parts[1].toInt();
    if (parts.size() >= 3) v.patch = parts[2].toInt();
    if (parts.size() >= 4) v.build = parts[3];
    return v;
}

bool VersionInfo::isCompatible(const VersionInfo& other) const {
    return major == other.major && minor == other.minor;
}

bool VersionInfo::isNewerThan(const VersionInfo& other) const {
    if (major != other.major) return major > other.major;
    if (minor != other.minor) return minor > other.minor;
    return patch > other.patch;
}

} // namespace ks
