#pragma once

#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QVariantMap>
#include <QDateTime>

namespace ks {
class SceneGraph;
class SceneObject;
class SceneMesh;

struct ProjectData {
    QString name;
    QString version = "1.0";
    QString filePath;
    QDateTime created;
    QDateTime modified;
    QString activeModule;
    int activeModuleIndex = 0;

    QVariantMap modelerState;
    QVariantMap audioState;
    QVariantMap physicsState;
    QVariantMap showroomState;
    QVariantMap trackState;
    QVariantMap characterState;
    QVariantMap settings;

    QVariantMap windowLayout;
};

class ProjectSerializer : public QObject {
    Q_OBJECT
public:
    static ProjectSerializer& instance();

    bool save(const QString& path, const ProjectData& data);
    bool load(const QString& path, ProjectData& data);
    bool saveBackup(const QString& path, const ProjectData& data);

    static QJsonObject serializeScene(SceneGraph* scene);
    static bool deserializeScene(SceneGraph* scene, const QJsonObject& json);

signals:
    void projectSaved(const QString& path);
    void projectLoaded(const QString& path);
    void error(const QString& message);

private:
    ProjectSerializer() = default;
    static QJsonObject serializeObject(SceneObject* obj);
    static SceneObject* deserializeObject(const QJsonObject& json, SceneGraph* scene);

    static QJsonObject serializeMesh(SceneMesh* mesh);
    static bool deserializeMesh(SceneMesh* mesh, const QJsonObject& json);

    static const int FILE_FORMAT_VERSION = 2;
};

} // namespace ks
