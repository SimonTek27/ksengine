#include "AssetManager.h"
#include "AssetSearchEngine.h"
#include "AssetFileWatcher.h"
#include "plugins/simulators/kunos/assettocorsa/FormatConverter.h"

#include <QDir>
#include <QFileInfo>
#include <QDirIterator>
#include <QMimeDatabase>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QPainter>
#include <QFont>
#include <QFile>
#include <QDebug>
#include <QStandardPaths>
#include <QImage>
#include <QSet>
#include <QProcess>
#include <QCoreApplication>
#include <algorithm>
#include <QSettings>

namespace ks {

AssetManager* AssetManager::s_instance = nullptr;

AssetManager* AssetManager::instance()
{
    if (!s_instance) s_instance = new AssetManager();
    return s_instance;
}

AssetManager::AssetManager(QObject* parent) : QObject(parent)
{
    m_dbPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
               + "/asset_db.json";
}

AssetManager::~AssetManager() { s_instance = nullptr; }

void AssetManager::setRootDirectory(const QString& dir)
{
    m_rootDirectory = dir;
}

void AssetManager::scan()
{
    if (m_rootDirectory.isEmpty()) return;
    m_assets.clear();

    QMimeDatabase mimeDb;
    QDirIterator it(m_rootDirectory, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString path = it.next();
        QFileInfo fi(path);

        Asset a;
        a.id           = QCryptographicHash::hash(path.toUtf8(),
                             QCryptographicHash::Md5).toHex();
        a.name         = fi.fileName();
        a.path         = path;
        a.extension    = fi.suffix().toLower();
        a.modifiedDate = fi.lastModified().toString(Qt::ISODate);
        a.type         = typeFromExtension(a.extension);
        a.fileSize     = fi.size();
        a.contentHash  = QString::fromLatin1(computeFileHash(path).toHex());

        m_assets.insert(a.id, a);
    }

    emit scanComplete(m_assets.size());

    // Check for duplicates after scan
    auto dupGroups = findDuplicateGroups();
    for (const auto& group : dupGroups) {
        for (int i = 1; i < group.size(); ++i) {
            auto* asset = getAsset(group[i].id);
            if (asset) {
                asset->isDuplicate = true;
                asset->originalAssetId = group[0].id;
            }
        }
    }

    AssetSearchEngine searchEngine;
    searchEngine.clearIndex();
    searchEngine.indexDirectory(m_rootDirectory, true);
    searchEngine.saveIndex(m_rootDirectory + "/.asset_index.json");
}

AssetType AssetManager::typeFromExtension(const QString& ext) const
{
    static const QMap<QString, AssetType> extMap = {
        {"kn5", AssetType::Model}, {"fbx", AssetType::Model}, {"obj", AssetType::Model},
        {"glb", AssetType::Model}, {"gltf", AssetType::Model},
        {"dds", AssetType::Texture}, {"png", AssetType::Texture},
        {"jpg", AssetType::Texture}, {"jpeg", AssetType::Texture},
        {"tga", AssetType::Texture}, {"bmp", AssetType::Texture},
        {"wav", AssetType::Audio}, {"ogg", AssetType::Audio},
        {"mp3", AssetType::Audio}, {"flac", AssetType::Audio},
        {"bnk", AssetType::Audio},
        {"ini", AssetType::Config}, {"json", AssetType::Config},
        {"acd", AssetType::Config},
        {"py", AssetType::Script}, {"lua", AssetType::Script},
        {"fnt", AssetType::Font}, {"ttf", AssetType::Font},
    };
    return extMap.value(ext, AssetType::Unknown);
}

QVector<Asset> AssetManager::getAssets(AssetType type) const
{
    if (type == AssetType::Unknown) return m_assets.values().toVector();
    QVector<Asset> result;
    for (const auto& a : m_assets)
        if (a.type == type) result << a;
    return result;
}

QVector<Asset> AssetManager::search(const QString& query) const
{
    QVector<Asset> result;
    const QString q = query.toLower();
    for (const auto& a : m_assets)
        if (a.name.toLower().contains(q) || a.tags.toLower().contains(q))
            result << a;
    return result;
}

bool AssetManager::importAsset(const QString& sourcePath, const QString& destDir)
{
    QFileInfo fi(sourcePath);
    if (!fi.exists()) { emit importFailed(sourcePath, "File not found"); return false; }

    QString targetDir = destDir.isEmpty() ? m_rootDirectory : destDir;
    if (targetDir.isEmpty()) { emit importFailed(sourcePath, "No target directory"); return false; }
    QDir().mkpath(targetDir);

    QString ext = fi.suffix().toLower();

    if (ext == "zip" || ext == "7z" || ext == "rar") {
        // Extract archive and import all recognized files
        QString extractDir = QDir(targetDir).filePath(fi.completeBaseName());
        QDir().mkpath(extractDir);

        QString sevenZip = QStandardPaths::findExecutable("7z",
            {QCoreApplication::applicationDirPath(), "C:/Program Files/7-Zip"});
        if (sevenZip.isEmpty()) sevenZip = "7z";

        QProcess proc;
        proc.start(sevenZip, {"x", sourcePath, "-o" + extractDir, "-y"});
        if (!proc.waitForFinished(60000) || proc.exitCode() != 0) {
            emit importFailed(sourcePath, "Archive extraction failed");
            return false;
        }

        scanDirectory(extractDir, true);
        return true;
    }

    // Simple file copy for non-archive assets
    QString dest = QDir(targetDir).filePath(fi.fileName());

    // Check for content hash duplicate before importing
    QByteArray srcHash = computeFileHash(sourcePath);
    if (!srcHash.isEmpty()) {
        QString existingId = findExistingByHash(QString::fromLatin1(srcHash.toHex()));
        if (!existingId.isEmpty()) {
            emit duplicateFound(existingId, sourcePath);
            const Asset* existing = getAsset(existingId);
            // Mark as duplicate reference without copying
            Asset a;
            a.id = QCryptographicHash::hash(sourcePath.toUtf8(), QCryptographicHash::Md5).toHex();
            a.name = fi.fileName();
            a.path = sourcePath;
            a.extension = ext;
            a.fileSize = fi.size();
            a.contentHash = QString::fromLatin1(srcHash.toHex());
            a.modifiedDate = QDateTime::currentDateTime().toString(Qt::ISODate);
            a.type = typeFromExtension(ext);
            a.isDuplicate = true;
            a.originalAssetId = existingId;
            if (existing) {
                a.metadata = existing->metadata;
                a.metadata["duplicateOf"] = existing->name;
                a.metadata["originalPath"] = existing->path;
            }
            enrichMetadata(a);
            m_assets.insert(a.id, a);
            generateThumbnail(a.id);
            emit assetImported(a);
            return true;
        }
    }

    if (QFile::exists(dest)) {
        dest = QDir(targetDir).filePath(fi.completeBaseName() + "_imported." + ext);
    }

    if (!QFile::copy(sourcePath, dest)) {
        emit importFailed(sourcePath, "Copy failed");
        return false;
    }

    Asset a;
    a.id           = QCryptographicHash::hash(dest.toUtf8(),
                         QCryptographicHash::Md5).toHex();
    a.name         = fi.fileName();
    a.path         = dest;
    a.extension    = ext;
    a.fileSize     = fi.size();
    a.contentHash  = QString::fromLatin1(srcHash.toHex());
    a.modifiedDate = QDateTime::currentDateTime().toString(Qt::ISODate);
    a.type         = typeFromExtension(ext);

    enrichMetadata(a);
    m_assets.insert(a.id, a);

    // Auto-generate thumbnail
    generateThumbnail(a.id);

    emit assetImported(a);
    return true;
}

bool AssetManager::removeAsset(const QString& id)
{
    if (!m_assets.contains(id)) return false;
    QString path = m_assets[id].path;
    m_assets.remove(id);
    emit assetRemoved(id);
    return true;
}

AssetManager::BatchImportResult AssetManager::importDirectory(const QString& dir, const ImportOptions& options)
{
    BatchImportResult result;
    QStringList files;
    QDirIterator::IteratorFlags flags = options.recursive ? QDirIterator::Subdirectories : QDirIterator::IteratorFlags();
    QDirIterator it(dir, QDir::Files, flags);
    QStringList supportedExts = { "kn5", "fbx", "glb", "gltf", "obj", "dae", "dds", "png", "jpg", "jpeg",
                                  "tga", "bmp", "wav", "ogg", "mp3", "flac", "bnk", "ini", "json", "py",
                                  "lua", "fnt", "ttf", "zip", "7z", "rar" };
    while (it.hasNext()) {
        QString path = it.next();
        QFileInfo fi(path);
        if (supportedExts.contains(fi.suffix().toLower()))
            files << path;
    }

    result.totalFound = files.size();
    return importFiles(files, options);
}

AssetManager::BatchImportResult AssetManager::importFiles(const QStringList& filePaths, const ImportOptions& options)
{
    BatchImportResult result;
    result.totalFound = filePaths.size();

    if (filePaths.isEmpty()) return result;

    emit batchImportStarted(filePaths.size());

    for (int i = 0; i < filePaths.size(); ++i) {
        const QString& filePath = filePaths[i];
        QString currentFile = QFileInfo(filePath).fileName();
        emit batchImportProgress(i + 1, filePaths.size(), currentFile);

        QFileInfo fi(filePath);
        if (!fi.exists()) {
            result.failed++;
            result.failedFiles << filePath;
            emit batchImportFileProcessed(i + 1, filePaths.size(), false, currentFile);
            continue;
        }

        QString destDir = m_rootDirectory;
        if (!options.targetSubdir.isEmpty())
            destDir = QDir(destDir).filePath(options.targetSubdir);

        // Check hash for duplicates
        QByteArray srcHash = computeFileHash(filePath);
        if (!srcHash.isEmpty()) {
            QString existingId = findExistingByHash(QString::fromLatin1(srcHash.toHex()));
            if (!existingId.isEmpty()) {
                if (options.onDuplicate == ImportOptions::Skip) {
                    result.skipped++;
                    result.skippedFiles << filePath;
                    emit batchImportFileProcessed(i + 1, filePaths.size(), true, currentFile);
                    continue;
                }
            }
        }

        // Auto-convert if requested
        QString sourcePath = filePath;
        QString ext = fi.suffix().toLower();
        bool didConvert = false;
        QString kn5Path;

        if (options.autoConvert && ext == "obj") {
            kn5Path = QDir(destDir).filePath(fi.completeBaseName() + ".kn5");
            FormatConverter converter;
            if (converter.convertOBJToKN5(filePath, kn5Path)) {
                sourcePath = kn5Path;
                ext = "kn5";
                didConvert = true;
                result.converted++;
                result.convertedFiles << kn5Path;
            }
        }

        if (importAsset(sourcePath, destDir)) {
            if (didConvert) {
                result.imported++;
                result.importedFiles << kn5Path;
            } else {
                result.imported++;
                result.importedFiles << sourcePath;
            }
            emit batchImportFileProcessed(i + 1, filePaths.size(), true, currentFile);
        } else {
            result.failed++;
            result.failedFiles << sourcePath;
            emit batchImportFileProcessed(i + 1, filePaths.size(), false, currentFile);
        }
    }

    QVariantMap summary;
    summary["totalFound"] = result.totalFound;
    summary["imported"] = result.imported;
    summary["skipped"] = result.skipped;
    summary["failed"] = result.failed;
    summary["converted"] = result.converted;
    summary["duplicates"] = result.duplicates;
    emit batchImportCompleted(summary);

    return result;
}

AssetManager::BatchImportResult AssetManager::importWithConversion(const QString& sourcePath, const QString& targetFormat,
                                                                    const ImportOptions& options)
{
    BatchImportResult result;
    QFileInfo fi(sourcePath);
    if (!fi.exists()) {
        result.failed++;
        result.failedFiles << sourcePath;
        return result;
    }

    emit batchImportStarted(1);
    emit batchImportProgress(1, 1, fi.fileName());

    QString destDir = m_rootDirectory;
    if (!options.targetSubdir.isEmpty())
        destDir = QDir(destDir).filePath(options.targetSubdir);

    QString destPath = QDir(destDir).filePath(fi.completeBaseName() + "." + targetFormat.toLower());

    FormatConverter converter;
    if (converter.convert(sourcePath, destPath)) {
        if (importAsset(destPath, destDir)) {
            result.totalFound = 1;
            result.imported = 1;
            result.converted = 1;
            result.importedFiles << destPath;
            result.convertedFiles << destPath;
            emit batchImportFileProcessed(1, 1, true, fi.fileName());
        } else {
            result.failed++;
            result.failedFiles << destPath;
            emit batchImportFileProcessed(1, 1, false, fi.fileName());
        }
    } else {
        result.failed++;
        result.failedFiles << sourcePath;
        emit batchImportFileProcessed(1, 1, false, fi.fileName());
    }

    QVariantMap summary;
    summary["totalFound"] = result.totalFound;
    summary["imported"] = result.imported;
    summary["converted"] = result.converted;
    emit batchImportCompleted(summary);

    return result;
}

void AssetManager::tagAsset(const QString& id, const QString& tag)
{
    if (!m_assets.contains(id)) return;
    if (!m_assets[id].tags.contains(tag)) {
        m_assets[id].tags += (m_assets[id].tags.isEmpty() ? "" : ",") + tag;
        emit assetTagged(id, tag);
    }
}

void AssetManager::save() const
{
    QJsonObject root;

    QJsonArray arr;
    for (const auto& a : m_assets) {
        QJsonObject obj;
        obj["id"]           = a.id;
        obj["name"]         = a.name;
        obj["path"]         = a.path;
        obj["extension"]    = a.extension;
        obj["tags"]         = a.tags;
        obj["modifiedDate"] = a.modifiedDate;
        obj["type"]         = static_cast<int>(a.type);
        obj["fileSize"]     = a.fileSize;
        obj["metadata"]     = QJsonObject::fromVariantMap(a.metadata);
        obj["isCore"]       = a.isCore;
        QJsonArray deps;
        for (const auto& d : a.dependencies) {
            QJsonObject dobj;
            dobj["assetId"] = d.assetId;
            dobj["path"]    = d.path;
            dobj["type"]    = static_cast<int>(d.type);
            deps.append(dobj);
        }
        obj["dependencies"] = deps;
        arr.append(obj);
    }
    root["assets"] = arr;
    root["favorites"] = QJsonArray::fromStringList(m_favorites);
    root["recent"] = QJsonArray::fromStringList(m_recent);

    QJsonArray bundlesArr;
    for (const auto& b : m_bundles) {
        QJsonObject bObj;
        bObj["id"] = b.id;
        bObj["name"] = b.name;
        bObj["description"] = b.description;
        bObj["author"] = b.author;
        bObj["version"] = b.version;
        bObj["createdDate"] = b.createdDate;
        bObj["modifiedDate"] = b.modifiedDate;

        QJsonArray bAssets;
        for (const auto& a : b.assets) {
            QJsonObject aObj;
            aObj["id"] = a.id;
            aObj["name"] = a.name;
            aObj["path"] = a.path;
            aObj["type"] = static_cast<int>(a.type);
            aObj["extension"] = a.extension;
            bAssets.append(aObj);
        }
        bObj["assets"] = bAssets;
        bundlesArr.append(bObj);
    }
    root["bundles"] = bundlesArr;

    QFile f(m_dbPath);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(root).toJson());
}

void AssetManager::load()
{
    loadIndex();
}

void AssetManager::scanDirectory(const QString& dir, bool recursive)
{
    if (dir.isEmpty() || !QDir(dir).exists()) return;

    QDirIterator::IteratorFlags flags = QDirIterator::IteratorFlags();
    if (recursive) flags |= QDirIterator::Subdirectories;

    QDirIterator it(dir, QDir::Files, flags);
    while (it.hasNext()) {
        QString path = it.next();
        scanFile(path);
    }
    emit scanComplete(m_assets.size());
}

void AssetManager::scanFile(const QString& path)
{
    QFileInfo fi(path);
    if (!fi.exists()) return;

    QString ext = fi.suffix().toLower();
    AssetType type = typeFromExtension(ext);
    if (type == AssetType::Unknown) return;

    Asset a;
    a.id           = QCryptographicHash::hash(path.toUtf8(),
                         QCryptographicHash::Md5).toHex();
    a.name         = fi.fileName();
    a.path         = path;
    a.extension    = ext;
    a.fileSize     = fi.size();
    a.modifiedDate = fi.lastModified().toString(Qt::ISODate);
    a.type         = type;

    enrichMetadata(a);
    m_assets.insert(a.id, a);
}

bool AssetManager::registerAsset(const Asset& asset)
{
    m_assets.insert(asset.id, asset);
    emit assetAdded(asset);
    return true;
}

void AssetManager::unregisterAsset(const QString& assetId)
{
    if (m_assets.remove(assetId) > 0) {
        emit assetRemoved(assetId);
    }
}

void AssetManager::updateAsset(const Asset& asset)
{
    if (m_assets.contains(asset.id)) {
        m_assets[asset.id] = asset;
        emit assetUpdated(asset);
    }
}

QVector<Asset> AssetManager::searchAssets(const QString& query) const
{
    return search(query);
}

QVector<Asset> AssetManager::getAssetsByTag(const QString& tag) const
{
    QVector<Asset> result;
    const QString t = tag.toLower();
    for (const auto& a : m_assets)
        if (a.tags.toLower().contains(t))
            result << a;
    return result;
}

QVector<Asset> AssetManager::getAssetsByPath(const QString& path) const
{
    QVector<Asset> result;
    const QString p = path.toLower();
    for (const auto& a : m_assets)
        if (a.path.toLower().contains(p))
            result << a;
    return result;
}

Asset* AssetManager::getAsset(const QString& assetId)
{
    return m_assets.contains(assetId) ? &m_assets[assetId] : nullptr;
}

const Asset* AssetManager::getAsset(const QString& assetId) const
{
    auto it = m_assets.find(assetId);
    return (it != m_assets.end()) ? &(*it) : nullptr;
}

QString AssetManager::getAssetPath(const QString& assetId) const
{
    return m_assets.value(assetId).path;
}

// ─── Bundle Management ───────────────────────────────────────────────────────

QString AssetManager::createBundle(const QString& name, const QString& description)
{
    AssetBundle bundle;
    bundle.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    bundle.name = name;
    bundle.description = description;
    bundle.createdDate = QDateTime::currentDateTime().toString(Qt::ISODate);
    bundle.modifiedDate = bundle.createdDate;
    m_bundles[bundle.id] = bundle;
    emit bundleCreated(bundle.id);
    return bundle.id;
}

void AssetManager::addToBundle(const QString& bundleId, const QString& assetId)
{
    if (!m_bundles.contains(bundleId) || !m_assets.contains(assetId)) return;
    AssetBundle& b = m_bundles[bundleId];
    for (const auto& a : b.assets)
        if (a.id == assetId) return;
    b.assets.append(m_assets[assetId]);
    b.modifiedDate = QDateTime::currentDateTime().toString(Qt::ISODate);
}

void AssetManager::removeFromBundle(const QString& bundleId, const QString& assetId)
{
    if (!m_bundles.contains(bundleId)) return;
    AssetBundle& b = m_bundles[bundleId];
    for (int i = 0; i < b.assets.size(); ++i) {
        if (b.assets[i].id == assetId) {
            b.assets.removeAt(i);
            b.modifiedDate = QDateTime::currentDateTime().toString(Qt::ISODate);
            return;
        }
    }
}

QString AssetManager::exportBundle(const QString& bundleId, const QString& outputPath)
{
    if (!m_bundles.contains(bundleId)) return QString();

    const AssetBundle& b = m_bundles[bundleId];
    QDir().mkpath(QFileInfo(outputPath).absolutePath());

    QJsonObject root;
    root["name"] = b.name;
    root["description"] = b.description;
    root["author"] = b.author;
    root["version"] = b.version;
    root["gameVersion"] = b.gameVersion;
    root["createdDate"] = b.createdDate;
    root["modifiedDate"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QJsonArray assetsArr;
    for (const auto& a : b.assets) {
        QJsonObject obj;
        obj["id"] = a.id;
        obj["name"] = a.name;
        obj["path"] = a.path;
        obj["type"] = static_cast<int>(a.type);
        obj["extension"] = a.extension;
        assetsArr.append(obj);
    }
    root["assets"] = assetsArr;

    QFile f(outputPath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson());
        f.close();
        emit bundleExported(outputPath);
        return outputPath;
    }
    return QString();
}

QString AssetManager::importBundle(const QString& bundlePath)
{
    QFile f(bundlePath);
    if (!f.open(QIODevice::ReadOnly)) return QString();

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return QString();

    QJsonObject root = doc.object();
    AssetBundle bundle;
    bundle.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    bundle.name = root["name"].toString();
    bundle.description = root["description"].toString();
    bundle.author = root["author"].toString();
    bundle.version = root["version"].toString();
    bundle.gameVersion = root["gameVersion"].toString();
    bundle.createdDate = root["createdDate"].toString();
    bundle.modifiedDate = QDateTime::currentDateTime().toString(Qt::ISODate);

    for (const auto& v : root["assets"].toArray()) {
        QJsonObject obj = v.toObject();
        Asset a;
        a.id = obj["id"].toString();
        a.name = obj["name"].toString();
        a.path = obj["path"].toString();
        a.type = static_cast<AssetType>(obj["type"].toInt());
        a.extension = obj["extension"].toString();
        bundle.assets.append(a);
    }

    m_bundles[bundle.id] = bundle;
    emit bundleCreated(bundle.id);
    return bundle.id;
}

QVector<AssetBundle> AssetManager::getBundles() const
{
    return m_bundles.values().toVector();
}

AssetBundle* AssetManager::getBundle(const QString& bundleId)
{
    return m_bundles.contains(bundleId) ? &m_bundles[bundleId] : nullptr;
}

void AssetManager::deleteBundle(const QString& bundleId)
{
    m_bundles.remove(bundleId);
}

// ─── Favorites & Recent ──────────────────────────────────────────────────────

void AssetManager::setFavorites(const QStringList& favorites)
{
    m_favorites = favorites;
}

void AssetManager::addRecent(const QString& assetId)
{
    m_recent.removeAll(assetId);
    m_recent.prepend(assetId);
    while (m_recent.size() > MAX_RECENT)
        m_recent.removeLast();
}

// ─── Tags ────────────────────────────────────────────────────────────────────

QStringList AssetManager::getTags() const
{
    QSet<QString> tags;
    for (const auto& a : m_assets) {
        for (const QString& t : a.tags.split(',', Qt::SkipEmptyParts))
            tags.insert(t.trimmed());
    }
    return tags.values();
}

void AssetManager::addTag(const QString& assetId, const QString& tag)
{
    tagAsset(assetId, tag);
}

void AssetManager::removeTag(const QString& assetId, const QString& tag)
{
    if (!m_assets.contains(assetId)) return;
    Asset& a = m_assets[assetId];
    QStringList tags = a.tags.split(',', Qt::SkipEmptyParts);
    tags.removeAll(tag.trimmed());
    a.tags = tags.join(',');
    emit assetUpdated(a);
}

// ─── Index Persistence ───────────────────────────────────────────────────────

void AssetManager::saveIndex()
{
    save();
}

void AssetManager::loadIndex()
{
    QFile f(m_dbPath);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();

    if (doc.isArray()) {
        QJsonArray arr = doc.array();
        m_assets.clear();
        for (const auto& v : arr) {
            QJsonObject obj = v.toObject();
            Asset a;
            a.id           = obj["id"].toString();
            a.name         = obj["name"].toString();
            a.path         = obj["path"].toString();
            a.extension    = obj["extension"].toString();
            a.tags         = obj["tags"].toString();
            a.modifiedDate = obj["modifiedDate"].toString();
            a.type         = static_cast<AssetType>(obj["type"].toInt());
            a.fileSize     = obj["fileSize"].toInteger(0);
            a.metadata     = obj["metadata"].toObject().toVariantMap();
            a.isCore       = obj["isCore"].toBool(false);
            for (const auto& dv : obj["dependencies"].toArray()) {
                QJsonObject dObj = dv.toObject();
                AssetDependency d;
                d.assetId  = dObj["assetId"].toString();
                d.path     = dObj["path"].toString();
                d.type     = static_cast<AssetType>(dObj["type"].toInt());
                a.dependencies.append(d);
            }
            if (QFile::exists(a.path)) m_assets.insert(a.id, a);
        }
    } else if (doc.isObject()) {
        QJsonObject root = doc.object();
        m_assets.clear();
        QJsonArray arr = root["assets"].toArray();
        for (const auto& v : arr) {
            QJsonObject obj = v.toObject();
            Asset a;
            a.id           = obj["id"].toString();
            a.name         = obj["name"].toString();
            a.path         = obj["path"].toString();
            a.extension    = obj["extension"].toString();
            a.tags         = obj["tags"].toString();
            a.modifiedDate = obj["modifiedDate"].toString();
            a.type         = static_cast<AssetType>(obj["type"].toInt());
            a.fileSize     = obj["fileSize"].toInteger(0);
            a.metadata     = obj["metadata"].toObject().toVariantMap();
            a.isCore       = obj["isCore"].toBool(false);
            for (const auto& dv : obj["dependencies"].toArray()) {
                QJsonObject dObj = dv.toObject();
                AssetDependency d;
                d.assetId  = dObj["assetId"].toString();
                d.path     = dObj["path"].toString();
                d.type     = static_cast<AssetType>(dObj["type"].toInt());
                a.dependencies.append(d);
            }
            if (QFile::exists(a.path)) m_assets.insert(a.id, a);
        }

        m_favorites = root["favorites"].toVariant().toStringList();
        m_recent = root["recent"].toVariant().toStringList();

        QJsonArray bundlesArr = root["bundles"].toArray();
        for (const auto& bv : bundlesArr) {
            QJsonObject bObj = bv.toObject();
            AssetBundle bundle;
            bundle.id = bObj["id"].toString();
            bundle.name = bObj["name"].toString();
            bundle.description = bObj["description"].toString();
            bundle.author = bObj["author"].toString();
            bundle.version = bObj["version"].toString();
            bundle.createdDate = bObj["createdDate"].toString();
            bundle.modifiedDate = bObj["modifiedDate"].toString();
            for (const auto& av : bObj["assets"].toArray()) {
                QJsonObject aObj = av.toObject();
                Asset a;
                a.id = aObj["id"].toString();
                a.name = aObj["name"].toString();
                a.path = aObj["path"].toString();
                a.type = static_cast<AssetType>(aObj["type"].toInt());
                a.extension = aObj["extension"].toString();
                bundle.assets.append(a);
            }
            m_bundles[bundle.id] = bundle;
        }
    }
}

QString AssetManager::detectAssetType(const QString& extension) const
{
    AssetType t = typeFromExtension(extension);
    switch (t) {
        case AssetType::Model:   return "Model";
        case AssetType::Texture: return "Texture";
        case AssetType::Audio:   return "Audio";
        case AssetType::Config:  return "Config";
        case AssetType::Script:  return "Script";
        case AssetType::Font:    return "Font";
        default:                 return "Unknown";
    }
}

Asset AssetManager::parseAssetFile(const QString& path) const
{
    QFileInfo fi(path);
    Asset a;
    a.id = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Md5).toHex();
    a.name = fi.fileName();
    a.path = path;
    a.extension = fi.suffix().toLower();
    a.fileSize = fi.size();
    a.modifiedDate = fi.lastModified().toString(Qt::ISODate);
    a.type = typeFromExtension(a.extension);
    return a;
}

void AssetManager::updateDependencies(Asset& asset) const
{
    if (asset.type == AssetType::Unknown || asset.path.isEmpty()) return;
    QFileInfo fi(asset.path);
    QDir dir = fi.dir();
    // Check for common companion files (textures, metadata, configs)
    QStringList extensions = {".dds", ".png", ".jpg", ".tga", ".ini", ".json", ".lut"};
    for (const auto& ext : extensions) {
        QString companion = dir.filePath(fi.completeBaseName() + ext);
        if (QFile::exists(companion) && companion != asset.path) {
            if (!asset.dependencies.contains(AssetDependency{{}, companion, typeFromExtension(QFileInfo(companion).suffix())})) {
                AssetDependency dep;
                dep.path = companion;
                dep.type = typeFromExtension(QFileInfo(companion).suffix());
                asset.dependencies.append(dep);
            }
        }
    }
}

// ─── Metadata Enrichment ─────────────────────────────────────────────────────

void AssetManager::enrichMetadata(Asset& asset) const
{
    QFileInfo fi(asset.path);
    QDir dir = fi.absoluteDir();

    if (asset.type == AssetType::Model || asset.type == AssetType::Texture) {
        QString carIni = dir.filePath("car.ini");
        if (QFile::exists(carIni)) { enrichFromIni(asset, carIni); return; }

        QString uiCar = dir.filePath("ui_car.json");
        if (QFile::exists(uiCar)) { enrichFromJson(asset, uiCar); return; }

        QString trackIni = dir.filePath("track.ini");
        if (QFile::exists(trackIni)) { enrichFromIni(asset, trackIni); return; }

        QString skinIni = dir.filePath("skin.ini");
        if (QFile::exists(skinIni)) { enrichFromIni(asset, skinIni); return; }
    }

    if (asset.extension == "ini") {
        enrichFromIni(asset, asset.path);
    }

    if (asset.extension == "json") {
        enrichFromJson(asset, asset.path);
    }
}

void AssetManager::enrichFromIni(Asset& asset, const QString& iniPath) const
{
    QSettings ini(iniPath, QSettings::IniFormat);
    QString fn = QFileInfo(iniPath).completeBaseName().toLower();

    QString name  = ini.value("HEADER/NAME").toString();
    QString brand = ini.value("HEADER/BRAND").toString();
    QString author = ini.value("HEADER/AUTHOR").toString();
    QString year  = ini.value("HEADER/YEAR").toString();
    QString country = ini.value("HEADER/COUNTRY").toString();

    if (!name.isEmpty())  asset.metadata["displayName"] = name;
    if (!brand.isEmpty()) asset.metadata["brand"] = brand;
    if (!author.isEmpty()) asset.metadata["author"] = author;
    if (!year.isEmpty())  asset.metadata["year"] = year;
    if (!country.isEmpty()) asset.metadata["country"] = country;

    if (fn == "car" || fn == "engine" || fn == "tyres" || fn == "drivetrain")
        asset.metadata["category"] = "Car";
    else if (fn == "track" || fn == "layout" || fn == "surfaces" || fn == "track_model")
        asset.metadata["category"] = "Track";
    else if (fn == "skin")
        asset.metadata["category"] = "Skin";
    else if (fn == "lights" || fn == "shaders")
        asset.metadata["category"] = "Visuals";
    else if (fn == "ai" || fn == "fast_lane")
        asset.metadata["category"] = "AI";
    else if (fn == "ui" || fn == "ui_car")
        asset.metadata["category"] = "UI";

    QStringList sections = ini.childGroups();
    for (const QString& section : sections) {
        if (section != "HEADER") {
            QVariantMap sectionData;
            ini.beginGroup(section);
            for (const QString& key : ini.childKeys())
                sectionData[key] = ini.value(key);
            ini.endGroup();
            asset.metadata[section.toLower()] = QVariant(sectionData);
        }
    }
}

void AssetManager::enrichFromJson(Asset& asset, const QString& jsonPath) const
{
    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) return;

    QJsonObject obj = doc.object();

    QString name = obj["name"].toString();
    QString brand = obj["brand"].toString();
    QString author = obj["author"].toString();
    int year = obj["year"].toInt();
    QString country = obj["country"].toString();
    QString type = obj["type"].toString();

    if (!name.isEmpty())  asset.metadata["displayName"] = name;
    if (!brand.isEmpty()) asset.metadata["brand"] = brand;
    if (!author.isEmpty()) asset.metadata["author"] = author;
    if (year > 0)         asset.metadata["year"] = year;
    if (!country.isEmpty()) asset.metadata["country"] = country;
    if (!type.isEmpty())  asset.metadata["category"] = type;

    if (jsonPath.contains("ui_car", Qt::CaseInsensitive))
        asset.metadata["category"] = "Car";
}

QString AssetManager::getThumbnail(const QString& assetId)
{
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                       + "/thumbnails";
    QDir().mkpath(cacheDir);

    Asset* a = getAsset(assetId);
    if (!a) return {};

    QByteArray hash = QCryptographicHash::hash(a->path.toUtf8(), QCryptographicHash::Md5).toHex();
    QString thumbPath = QDir(cacheDir).filePath(QString::fromLatin1(hash) + ".png");
    if (QFile::exists(thumbPath)) return thumbPath;

    // Generate on-demand if not cached
    return generateThumbnail(assetId);
}

QString AssetManager::generateThumbnail(const QString& assetId)
{
    Asset* a = getAsset(assetId);
    if (!a) return {};

    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                       + "/thumbnails";
    QDir().mkpath(cacheDir);

    QByteArray hash = QCryptographicHash::hash(a->path.toUtf8(), QCryptographicHash::Md5).toHex();
    QString thumbPath = QDir(cacheDir).filePath(QString::fromLatin1(hash) + ".png");

    QImage thumb(128, 128, QImage::Format_ARGB32);
    thumb.fill(QColor(35, 35, 45));

    if (a->type == AssetType::Texture) {
        QImage src(a->path);
        if (!src.isNull())
            thumb = src.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else {
        QPainter p(&thumb);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QColor(180, 180, 200));
        p.setFont(QFont("Arial", 14, QFont::Bold));
        p.drawText(thumb.rect(), Qt::AlignCenter, QFileInfo(a->path).suffix().toUpper());
        p.end();
    }

    thumb.save(thumbPath);
    return thumbPath;
}

// ─── AssetCollection ─────────────────────────────────────────────────────────

AssetCollection::AssetCollection(QObject* parent) : QObject(parent) {}
AssetCollection::~AssetCollection() = default;

void AssetCollection::addAsset(const QString& assetId)
{
    if (!m_assetIds.contains(assetId)) { m_assetIds.append(assetId); emit collectionChanged(); }
}

void AssetCollection::removeAsset(const QString& assetId)
{
    m_assetIds.removeAll(assetId);
    emit collectionChanged();
}

void AssetCollection::clear()
{
    m_assetIds.clear();
    emit collectionChanged();
}

void AssetCollection::setFilter(AssetType type, bool enabled) { m_typeFilters[type] = enabled; }

void AssetCollection::saveToFile(const QString& path)
{
    QJsonObject obj;
    obj["name"] = m_name;
    QJsonArray arr;
    for (const auto& id : m_assetIds) arr.append(id);
    obj["assets"] = arr;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(obj).toJson());
}

void AssetCollection::loadFromFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    m_name = obj["name"].toString();
    m_assetIds.clear();
    for (const auto& v : obj["assets"].toArray())
        m_assetIds.append(v.toString());
}

// ─── AssetPreviewGenerator ───────────────────────────────────────────────────

AssetPreviewGenerator::AssetPreviewGenerator(QObject* parent) : QObject(parent) {}
AssetPreviewGenerator::~AssetPreviewGenerator() = default;

void AssetPreviewGenerator::setOutputDirectory(const QString& dir) { m_outputDirectory = dir; }

void AssetPreviewGenerator::generatePreview(const QString& assetId)
{
    auto* mgr = AssetManager::instance();
    Asset* asset = mgr->getAsset(assetId);
    if (!asset) return;

    QString outputPath = getPreviewPath(assetId);
    if (QFile::exists(outputPath)) return;

    QImage preview(256, 256, QImage::Format_ARGB32);
    preview.fill(QColor(45, 45, 55));

    if (asset->type == AssetType::Texture) {
        QImage src(asset->path);
        if (!src.isNull())
            preview = src.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    } else {
        QPainter p(&preview);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QColor(180, 180, 200));
        p.setFont(QFont("Arial", 14, QFont::Bold));
        p.drawText(preview.rect(), Qt::AlignCenter, QFileInfo(asset->path).suffix().toUpper());
        p.end();
    }

    QDir().mkpath(QFileInfo(outputPath).absolutePath());
    preview.save(outputPath);
    m_previewCache[assetId] = outputPath;
    emit previewGenerated(assetId, outputPath);
}

void AssetPreviewGenerator::generatePreviews(const QVector<QString>& assetIds)
{
    for (const auto& id : assetIds)
        generatePreview(id);
}

void AssetPreviewGenerator::clearCache() { m_previewCache.clear(); }

QString AssetPreviewGenerator::getPreviewPath(const QString& assetId) const
{
    return m_previewCache.value(assetId);
}

bool AssetPreviewGenerator::hasPreview(const QString& assetId) const
{
    return m_previewCache.contains(assetId);
}

// --- Content hash / deduplication ---

QByteArray AssetManager::computeFileHash(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return {};

    qint64 fileSize = file.size();
    if (fileSize > kMaxHashSize) {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        constexpr qint64 kChunkSize = 64 * 1024;
        qint64 totalRead = 0;
        while (!file.atEnd() && totalRead < kMaxHashSize) {
            QByteArray chunk = file.read(kChunkSize);
            totalRead += chunk.size();
            hash.addData(chunk);
            if (totalRead >= kMaxHashSize) {
                hash.addData(QByteArray::number(fileSize));
                break;
            }
        }
        return hash.result();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&file);
    file.close();
    return hash.result();
}

QByteArray AssetManager::computeDataHash(const QByteArray& data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
}

QVector<Asset> AssetManager::findByContentHash(const QString& hash) const
{
    QVector<Asset> results;
    for (auto it = m_assets.begin(); it != m_assets.end(); ++it) {
        if (it.value().contentHash == hash) {
            results.append(it.value());
        }
    }
    return results;
}

QVector<Asset> AssetManager::findDuplicates() const
{
    QMap<QByteArray, int> hashCounts;
    for (auto it = m_assets.begin(); it != m_assets.end(); ++it) {
        if (!it.value().contentHash.isEmpty()) {
            hashCounts[QByteArray::fromHex(it.value().contentHash.toLatin1())]++;
        }
    }

    QVector<Asset> duplicates;
    for (auto it = m_assets.begin(); it != m_assets.end(); ++it) {
        if (it.value().contentHash.isEmpty()) continue;
        if (hashCounts.value(QByteArray::fromHex(it.value().contentHash.toLatin1()), 0) > 1) {
            duplicates.append(it.value());
        }
    }
    return duplicates;
}

QVector<QVector<Asset>> AssetManager::findDuplicateGroups() const
{
    QMap<QString, QVector<Asset>> groups;
    for (auto it = m_assets.begin(); it != m_assets.end(); ++it) {
        if (!it.value().contentHash.isEmpty()) {
            groups[it.value().contentHash].append(it.value());
        }
    }

    QVector<QVector<Asset>> result;
    for (auto it = groups.begin(); it != groups.end(); ++it) {
        if (it.value().size() > 1) {
            result.append(it.value());
        }
    }
    return result;
}

bool AssetManager::isDuplicateOf(const QString& assetId, const QString& otherAssetId) const
{
    auto* a = getAsset(assetId);
    auto* b = getAsset(otherAssetId);
    if (!a || !b) return false;
    if (a->contentHash.isEmpty() || b->contentHash.isEmpty()) return false;
    return a->contentHash == b->contentHash;
}

QString AssetManager::findExistingByHash(const QString& hash) const
{
    for (auto it = m_assets.begin(); it != m_assets.end(); ++it) {
        if (it.value().contentHash == hash && !it.value().isDuplicate) {
            return it.key();
        }
    }
    return {};
}

void AssetManager::removeDuplicates(bool keepFirst)
{
    auto groups = findDuplicateGroups();
    int removed = 0;

    for (const auto& group : groups) {
        if (group.size() < 2) continue;

        for (int i = keepFirst ? 1 : 0; i < group.size(); ++i) {
            const auto& dup = group[i];
            auto* asset = getAsset(dup.id);
            if (asset) {
                asset->isDuplicate = true;
                asset->originalAssetId = group[0].id;
                removed++;
            }
        }
    }

    if (removed > 0) {
        emit duplicatesRemoved(removed);
        saveIndex();
    }
}

void AssetManager::scanAndDeduplicate()
{
    scan();

    for (auto& asset : m_assets) {
        if (asset.contentHash.isEmpty()) {
            asset.contentHash = QString::fromLatin1(computeFileHash(asset.path).toHex());
        }
    }

    removeDuplicates(true);
}

// --- File watcher ---

void AssetManager::startWatching(const QString& directory)
{
    if (!m_watcher) {
        m_watcher = new AssetFileWatcher(this);

        connect(m_watcher, &AssetFileWatcher::fileAdded, this, [this](const QString& path) {
            if (!m_assets.contains(path)) {
                scanFile(path);
                emit fileSystemChangeDetected(path);
            }
        });

        connect(m_watcher, &AssetFileWatcher::fileModified, this, [this](const QString& path) {
            auto assets = getAssetsByPath(path);
            for (auto& a : assets) {
                a.contentHash = QString::fromLatin1(computeFileHash(path).toHex());
                updateAsset(a);
            }
            emit fileSystemChangeDetected(path);
        });

        connect(m_watcher, &AssetFileWatcher::fileRemoved, this, [this](const QString& path) {
            auto assets = getAssetsByPath(path);
            for (const auto& a : assets) {
                unregisterAsset(a.id);
            }
            emit fileSystemChangeDetected(path);
        });

        connect(m_watcher, &AssetFileWatcher::batchChanged, [this](const QStringList& paths) {
            Q_UNUSED(paths);
            saveIndex();
        });
    }

    m_watcher->watchDirectory(directory, true);
}

void AssetManager::stopWatching()
{
    if (m_watcher) {
        m_watcher->unwatchAll();
    }
}

bool AssetManager::isWatching() const
{
    return m_watcher && m_watcher->isWatching();
}

} // namespace ks
