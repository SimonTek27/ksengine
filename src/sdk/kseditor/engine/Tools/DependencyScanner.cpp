#include "DependencyScanner.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDebug>

namespace ks {

DependencyScanner* DependencyScanner::s_instance = nullptr;

DependencyScanner::DependencyScanner(QObject* parent)
    : QObject(parent)
{
    m_searchPaths.insert(".");
    m_searchPaths.insert("textures");
    m_searchPaths.insert("models");
    m_searchPaths.insert("materials");
}

DependencyScanner* DependencyScanner::instance() {
    if (!s_instance) {
        s_instance = new DependencyScanner();
    }
    return s_instance;
}

void DependencyScanner::addSearchPath(const QString& path) {
    m_searchPaths.insert(path);
}

void DependencyScanner::clearSearchPaths() {
    m_searchPaths.clear();
}

ScanResult DependencyScanner::scanProject(const QString& projectPath, bool recursive) {
    ScanResult result;
    result.totalFiles = 0;
    result.filesWithDependencies = 0;
    result.missingDependencies = 0;
    
    QDir dir(projectPath);
    QStringList filters;
    filters << "*.kn5" << "*.fbx" << "*.obj" << "*.gltf" << "*.glb" << "*.ini" << "*.json";
    
    QFileInfoList files;
    if (recursive) {
        QDirIterator it(dir.absolutePath(), filters, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) files.append(it.nextFileInfo());
    } else {
        files = dir.entryInfoList(filters, QDir::Files);
    }

    int total = files.size();
    for (int i = 0; i < files.size(); ++i) {
        const QFileInfo& info = files.at(i);
        emit scanProgress(i + 1, total);
        
        QList<Dependency> deps = scanFile(info.absoluteFilePath());
        
        if (!deps.isEmpty()) {
            result.filesWithDependencies++;
            
            for (const Dependency& dep : deps) {
                result.allDependencies.append(dep);
                if (!dep.exists) {
                    result.missingDependencies++;
                }
            }
        }
    }
    
    result.totalFiles = total;
    emit scanComplete(result);
    return result;
}

QStringList DependencyScanner::findMissingDependencies(const QString& projectPath)
{
    QStringList missing;
    ScanResult result = scanProject(projectPath);
    for (const Dependency& dep : result.allDependencies) {
        if (!dep.exists) {
            missing.append(dep.dependencyPath);
        }
    }
    return missing;
}

QList<Dependency> DependencyScanner::getDependenciesForFile(const QString& filePath)
{
    return scanFile(filePath);
}

QList<Dependency> DependencyScanner::scanFile(const QString& filePath) {
    if (m_cache.contains(filePath)) {
        return m_cache.value(filePath);
    }
    
    QList<Dependency> dependencies;
    QFile file(filePath);
    
    if (!file.open(QIODevice::ReadOnly)) {
        return dependencies;
    }
    
    QByteArray content = file.readAll();
    file.close();
    
    QRegularExpression textureRegex(R"((?:textures?|tex|diffuse|albedo)[\\/]?([^\s,\)]+\.(?:png|jpg|jpeg|dds|tga)))", QRegularExpression::CaseInsensitiveOption);
    QRegularExpression meshRegex(R"((?:models?|meshes|geometry)[\\/]?([^\s,\)]+\.(?:fbx|obj|gltf|glb|kn5)))", QRegularExpression::CaseInsensitiveOption);
    
    QRegularExpressionMatchIterator it = textureRegex.globalMatch(QString::fromUtf8(content));
    while (it.hasNext()) {
        Dependency dep;
        dep.sourceFile = filePath;
        dep.dependencyPath = it.next().captured(1);
        dep.type = Dependency::Type::Texture;
        dep.exists = checkDependencyExists(dep.dependencyPath);
        dependencies.append(dep);
    }
    
    it = meshRegex.globalMatch(QString::fromUtf8(content));
    while (it.hasNext()) {
        Dependency dep;
        dep.sourceFile = filePath;
        dep.dependencyPath = it.next().captured(1);
        dep.type = Dependency::Type::Mesh;
        dep.exists = checkDependencyExists(dep.dependencyPath);
        dependencies.append(dep);
    }
    
    m_cache.insert(filePath, dependencies);
    return dependencies;
}

bool DependencyScanner::checkDependencyExists(const QString& path) {
    QFileInfo info(path);
    if (info.exists()) return true;
    
    for (const QString& searchPath : m_searchPaths) {
        QString fullPath = searchPath + "/" + path;
        if (QFileInfo::exists(fullPath)) {
            return true;
        }
    }
    return false;
}

Dependency::Type DependencyScanner::determineType(const QString& path) {
    QString ext = path.split('.').last().toLower();
    if (getTextureExtensions().contains(ext)) return Dependency::Type::Texture;
    if (getMeshExtensions().contains(ext)) return Dependency::Type::Mesh;
    return Dependency::Type::Config;
}

}