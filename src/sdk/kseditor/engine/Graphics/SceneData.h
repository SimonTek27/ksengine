#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <QUuid>
#include <QDateTime>
#include <QVector3D>
#include <QJsonArray>

namespace ks {

class SceneGraph;
class SceneMesh;
class SceneObject;

class PBRMaterial;

// Project-level serialization format
struct SceneProjectData {
    QString formatVersion = "0.9.0";
    QString editorVersion = "0.9.0";
    QString name;
    QString version = "0.9.0";
    QString description;
    QString author;
    
    QDateTime created;
    QDateTime modified;
    QDateTime lastSaved;
    
    // Active state
    QString activeModule;
    int activeModuleIndex = 0;
    
    // Module states
    QMap<QString, QJsonObject> moduleStates;
    
    // Scene data
    QJsonObject scene;
    
    // Settings
    QMap<QString, QVariant> settings;
    
    // Window layout
    QJsonObject windowLayout;
    
    // Recent files
    QStringList recentFiles;
    
    // Asset database path
    QString assetDatabasePath;
};

struct AssetMetadata {
    QUuid id;
    QString name;
    QString type;           // "model", "texture", "audio", "material", "prefab", "scene"
    QString category;       // "car", "track", "character", "prop", "environment"
    QString filePath;
    QString sourcePath;     // Original import path
    qint64 fileSize = 0;
    QString hash;           // SHA256 of file content
    QString thumbnailPath;
    
    QDateTime created;
    QDateTime modified;
    QDateTime imported;
    
    QString author;
    QString license;
    QString version = "1.0.0";
    
    // Dependencies
    QVector<QUuid> dependencies;
    QVector<QUuid> dependents;
    
    // Tags for search
    QStringList tags;
    
    // Platform variants
    QMap<QString, QString> platformVariants;  // platform -> path
    
    // Import settings
    QJsonObject importSettings;
    
    // Status
    enum class Status { Missing, Valid, Outdated, Processing, Error };
    Status status = Status::Valid;
    QString errorMessage;
};

struct SceneAssetCollection {
    QString name;
    QString description;
    QVector<QUuid> assets;
    QStringList tags;
    QDateTime created;
    QDateTime modified;
};

struct WorkspaceState {
    // Window state
    QByteArray mainWindowGeometry;
    QByteArray mainWindowState;
    
    // Dock panels
    QMap<QString, QByteArray> dockPanelStates;
    
    // Editors
    QMap<QString, QJsonObject> editorStates;  // module -> state
    
    // UI preferences
    QString theme = "dark";
    int fontSize = 12;
    bool showGrid = true;
    bool showGizmos = true;
    QString cameraMode = "orbit";
    
    // Viewport
    QVector3D viewportCameraPos = {0, 5, 10};
    QVector3D viewportCameraTarget = {0, 0, 0};
    float viewportFOV = 60.0f;
    
    // Active document
    QString activeDocument;
    QStringList openDocuments;
};

struct UserPreferences {
    // General
    QString language = "en";
    bool autoSave = true;
    int autoSaveInterval = 300;  // seconds
    bool checkUpdates = true;
    bool telemetry = false;
    
    // Paths
    QString projectsPath;
    QString assetsPath;
    QString pluginsPath;
    QString tempPath;
    
    // Editor
    QString externalEditor;
    bool wordWrap = false;
    int tabSize = 4;
    bool useSpaces = true;
    QString codeTheme = "dark";
    
    // Viewport
    float gridSize = 1.0f;
    int gridDivisions = 10;
    bool snapToGrid = false;
    float snapIncrement = 0.1f;
    bool snapRotation = false;
    float rotationSnap = 15.0f;
    
    // Rendering
    bool vsync = true;
    int maxFPS = 144;
    bool hdr = false;
    float exposure = 1.0f;
    bool bloom = true;
    bool ssao = true;
    int shadowQuality = 2;
    int textureQuality = 2;
    
    // Audio
    int sampleRate = 48000;
    int bufferSize = 512;
    float masterVolume = 1.0f;
    
    // Network
    QString cloudSyncEndpoint;
    bool autoSync = false;
    
    // Advanced
    QJsonObject advanced;
};

class SceneProjectSerializer : public QObject
{
    Q_OBJECT

public:
    static SceneProjectSerializer& instance();

    // Project file operations
    bool saveProject(const QString& path, const SceneProjectData& data);
    bool loadProject(const QString& path, SceneProjectData& data);
    bool saveBackup(const QString& path, const SceneProjectData& data);
    
    // Scene serialization
    QJsonObject serializeScene(const SceneGraph* scene);
    bool deserializeScene(SceneGraph* scene, const QJsonObject& json);
    
    // Object serialization
    QJsonObject serializeObject(const SceneObject* object);
    SceneObject* deserializeObject(SceneGraph* graph, const QJsonObject& json);
    
    // Mesh serialization
    QJsonObject serializeMesh(const SceneMesh* mesh);
    SceneMesh* deserializeMesh(const QJsonObject& json);
    
    // Material serialization
    QJsonObject serializeMaterial(const PBRMaterial* material);
    PBRMaterial* deserializeMaterial(const QJsonObject& json);
    
    // Version migration
    bool migrateProject(SceneProjectData& data, const QString& fromVersion);
    
    // Utility
    static QString generateProjectPath(const QString& baseDir, const QString& name);
    static bool validateProject(const SceneProjectData& data, QStringList& errors);

signals:
    void projectSaved(const QString& path);
    void projectLoaded(const QString& path);
    void projectError(const QString& message);
    void migrationNeeded(const QString& fromVersion, const QString& toVersion);

private:
    SceneProjectSerializer(QObject* parent = nullptr);
    ~SceneProjectSerializer();
    Q_DISABLE_COPY(SceneProjectSerializer)

    static SceneProjectSerializer* s_instance;
    
    int m_currentFormatVersion = 2;
    int m_minSupportedVersion = 1;
};

// Asset database serialization
class AssetDatabaseSerializer : public QObject
{
    Q_OBJECT

public:
    static AssetDatabaseSerializer& instance();
    
    bool saveDatabase(const QString& path);
    bool loadDatabase(const QString& path);
    
    // Query serialization
    QJsonArray serializeAssets(const QVector<QUuid>& assetIds);
    QJsonArray serializeCollections(const QVector<QUuid>& collectionIds);
    
    // Import/Export
    bool exportPackage(const QString& path, const QVector<QUuid>& assetIds);
    bool importPackage(const QString& path, QVector<QUuid>& importedIds);
    
    // Validation
    bool validateAssets(QVector<QUuid>& valid, QVector<QUuid>& invalid);
    void repairMissingAssets(const QVector<QUuid>& assetIds);

signals:
    void databaseSaved(const QString& path);
    void databaseLoaded(const QString& path);
    void assetExported(const QUuid& id, const QString& path);
    void assetImported(const QUuid& id);

private:
    struct AssetRecord {
        QUuid id;
        QString name;
        QString type;
        QString path;
        QString collectionId;
    };
    struct CollectionRecord {
        QUuid id;
        QString name;
        QString description;
        QVector<QUuid> assetIds;
    };

    AssetDatabaseSerializer(QObject* parent = nullptr);
    ~AssetDatabaseSerializer();
    Q_DISABLE_COPY(AssetDatabaseSerializer)
    static AssetDatabaseSerializer* s_instance;

    QMap<QUuid, AssetRecord> m_assets;
    QMap<QUuid, CollectionRecord> m_collections;
};

// Version info
struct VersionInfo {
    int major = 2;
    int minor = 1;
    int patch = 0;
    QString build = "dev";
    QString channel = "stable";
    QDateTime buildDate;
    QString gitCommit;
    
    QString toString() const;
    static VersionInfo fromString(const QString& str);
    bool isCompatible(const VersionInfo& other) const;
    bool isNewerThan(const VersionInfo& other) const;
};

} // namespace ks