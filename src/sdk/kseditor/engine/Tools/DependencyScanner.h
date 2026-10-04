#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>
#include <QFile>
#include <QRegularExpression>

namespace ks {

struct Dependency {
    QString sourceFile;
    QString dependencyPath;
    bool exists;
    enum class Type { Texture, Mesh, Material, Audio, Config } type;
};

struct ScanResult {
    int totalFiles;
    int filesWithDependencies;
    int missingDependencies;
    QList<Dependency> allDependencies;
};

class DependencyScanner : public QObject {
    Q_OBJECT

public:
    static DependencyScanner* instance();

    ScanResult scanProject(const QString& projectPath, bool recursive = true);
    QStringList findMissingDependencies(const QString& projectPath);
    QList<Dependency> getDependenciesForFile(const QString& filePath);
    
    void addSearchPath(const QString& path);
    void clearSearchPaths();
    
    QList<QString> getTextureExtensions() const { return {"png", "jpg", "jpeg", "dds", "tga"}; }
    QList<QString> getMeshExtensions() const { return {"fbx", "obj", "gltf", "glb"}; }

signals:
    void scanProgress(int current, int total);
    void scanComplete(const ScanResult& result);

private:
    explicit DependencyScanner(QObject* parent = nullptr);
    static DependencyScanner* s_instance;
    
    QSet<QString> m_searchPaths;
    QMap<QString, QList<Dependency>> m_cache;
    
    QList<Dependency> scanFile(const QString& filePath);
    bool checkDependencyExists(const QString& path);
    Dependency::Type determineType(const QString& path);
};

}