#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QVariantMap>
#include <QJsonObject>
#include <QByteArray>
#include <functional>

namespace ks {
class AssetFileWatcher;

enum class AssetType {
    Unknown,
    Model,
    Mesh,
    Texture,
    Audio,
    Material,
    Physics,
    Animation,
    Scene,
    Bundle,
    Config,
    Script,
    Font
};

struct AssetDependency {
    QString assetId;
    QString path;
    AssetType type = AssetType::Unknown;

    bool operator==(const AssetDependency& other) const {
        return assetId == other.assetId && path == other.path && type == other.type;
    }
};

struct Asset {
    QString id;
    QString name;
    AssetType type;
    QString path;
    QString extension;
    qint64 fileSize = 0;
    QString contentHash;
    QString modifiedDate;
    QVector<AssetDependency> dependencies;
    QVariantMap metadata;
    QString tags;
    bool isCore = false;
    bool isDuplicate = false;
    QString originalAssetId;
};

struct AssetBundle {
    QString id;
    QString name;
    QString description;
    QString author;
    QString version;
    QString gameVersion;
    QVector<Asset> assets;
    QVector<QString> includedFiles;
    QJsonObject customData;
    QString createdDate;
    QString modifiedDate;
};

class AssetManager : public QObject {
    Q_OBJECT

public:
    explicit AssetManager(QObject* parent = nullptr);
    ~AssetManager();

    static AssetManager* instance();

    void setRootDirectory(const QString& dir);
    QString getRootDirectory() const { return m_rootDirectory; }

    void scan();
    void scanDirectory(const QString& dir, bool recursive = true);

    // Import options
    struct ImportOptions {
        enum ConflictAction { Skip, Overwrite, Rename, Ask };
        ConflictAction onDuplicate = Rename;
        bool autoConvert = false;           // Convert formats (OBJ→KN5, etc.)
        bool extractArchives = true;
        bool generateThumbnails = true;
        bool recursive = true;
        QString targetSubdir;               // Subdirectory under root
    };

    struct BatchImportResult {
        int totalFound = 0;
        int imported = 0;
        int skipped = 0;
        int failed = 0;
        int converted = 0;
        int duplicates = 0;
        QStringList importedFiles;
        QStringList skippedFiles;
        QStringList failedFiles;
        QStringList convertedFiles;
    };

    BatchImportResult importDirectory(const QString& dir, const ImportOptions& options = ImportOptions());
    BatchImportResult importFiles(const QStringList& filePaths, const ImportOptions& options = ImportOptions());
    BatchImportResult importWithConversion(const QString& sourcePath, const QString& targetFormat,
                                            const ImportOptions& options = ImportOptions());

    void setImportOptions(const ImportOptions& options) { m_importOptions = options; }
    ImportOptions importOptions() const { return m_importOptions; }

    bool registerAsset(const Asset& asset);
    void unregisterAsset(const QString& assetId);
    void updateAsset(const Asset& asset);

    QVector<Asset> getAssets(AssetType type = AssetType::Unknown) const;
    QVector<Asset> searchAssets(const QString& query) const;
    QVector<Asset> search(const QString& query) const;
    QVector<Asset> getAssetsByTag(const QString& tag) const;
    QVector<Asset> getAssetsByPath(const QString& path) const;

    Asset* getAsset(const QString& assetId);
    const Asset* getAsset(const QString& assetId) const;

    QString getAssetPath(const QString& assetId) const;

    bool importAsset(const QString& sourcePath, const QString& destDir);
    bool removeAsset(const QString& id);
    void tagAsset(const QString& id, const QString& tag);

    // Content hash / deduplication
    static QByteArray computeFileHash(const QString& filePath);
    static QByteArray computeDataHash(const QByteArray& data);
    QVector<Asset> findByContentHash(const QString& hash) const;
    QVector<Asset> findDuplicates() const;
    QVector<QVector<Asset>> findDuplicateGroups() const;
    bool isDuplicateOf(const QString& assetId, const QString& otherAssetId) const;
    QString findExistingByHash(const QString& hash) const;
    void removeDuplicates(bool keepFirst = true);
    void scanAndDeduplicate();

    // File watcher
    void startWatching(const QString& directory);
    void stopWatching();
    bool isWatching() const;
    AssetFileWatcher* fileWatcher() const { return m_watcher; }

    QString createBundle(const QString& name, const QString& description = QString());
    void addToBundle(const QString& bundleId, const QString& assetId);
    void removeFromBundle(const QString& bundleId, const QString& assetId);
    QString exportBundle(const QString& bundleId, const QString& outputPath);
    QString importBundle(const QString& bundlePath);

    QVector<AssetBundle> getBundles() const;
    AssetBundle* getBundle(const QString& bundleId);
    void deleteBundle(const QString& bundleId);

    void setFavorites(const QStringList& favorites);
    QStringList getFavorites() const { return m_favorites; }

    void addRecent(const QString& assetId);
    QStringList getRecent(int maxItems = 10) const { return m_recent.mid(0, maxItems); }

    QStringList getTags() const;
    void addTag(const QString& assetId, const QString& tag);
    void removeTag(const QString& assetId, const QString& tag);

    void save() const;
    void load();

signals:
    void assetAdded(const Asset& asset);
    void assetRemoved(const QString& assetId);
    void assetUpdated(const Asset& asset);
    void bundleCreated(const QString& bundleId);
    void bundleExported(const QString& path);
    void scanComplete(int assetCount);
    void importFailed(const QString& path, const QString& reason);
    void assetImported(const Asset& asset);
    void assetTagged(const QString& id, const QString& tag);
    void duplicateFound(const QString& existingId, const QString& newPath);
    void duplicatesRemoved(int count);
    void fileSystemChangeDetected(const QString& path);

    // Batch import signals
    void batchImportStarted(int fileCount);
    void batchImportProgress(int current, int total, const QString& currentFile);
    void batchImportCompleted(const QVariantMap& summary);
    void batchImportFileProcessed(int current, int total, bool success, const QString& file);

public:
    void scanFile(const QString& path);

    void enrichMetadata(Asset& asset) const;
    QString getThumbnail(const QString& assetId);
    QString generateThumbnail(const QString& assetId);

private:
    void loadIndex();
    void saveIndex();

    AssetType typeFromExtension(const QString& ext) const;
    QString detectAssetType(const QString& extension) const;
    Asset parseAssetFile(const QString& path) const;
    void updateDependencies(Asset& asset) const;
    void enrichFromIni(Asset& asset, const QString& iniPath) const;
    void enrichFromJson(Asset& asset, const QString& jsonPath) const;

    QString m_rootDirectory;
    QMap<QString, Asset> m_assets;
    QMap<QString, QByteArray> m_contentHashes;
    QMap<QString, AssetBundle> m_bundles;
    QStringList m_favorites;
    QStringList m_recent;
    QString m_dbPath;
    AssetFileWatcher* m_watcher = nullptr;
    ImportOptions m_importOptions;

    static AssetManager* s_instance;
    static constexpr int MAX_RECENT = 50;
    static constexpr qint64 kMaxHashSize = 500 * 1024 * 1024;
};

class AssetCollection : public QObject {
    Q_OBJECT

public:
    explicit AssetCollection(QObject* parent = nullptr);
    ~AssetCollection();

    void addAsset(const QString& assetId);
    void removeAsset(const QString& assetId);
    void clear();

    QVector<QString> getAssets() const { return m_assetIds; }
    bool contains(const QString& assetId) const { return m_assetIds.contains(assetId); }
    int count() const { return m_assetIds.size(); }

    void setName(const QString& name) { m_name = name; }
    QString getName() const { return m_name; }

    void setFilter(AssetType type, bool enabled);
    bool getFilter(AssetType type) const { return m_typeFilters.value(type, false); }

    void saveToFile(const QString& path);
    void loadFromFile(const QString& path);

signals:
    void collectionChanged();

private:
    QString m_name;
    QVector<QString> m_assetIds;
    QMap<AssetType, bool> m_typeFilters;
};

class AssetPreviewGenerator : public QObject {
    Q_OBJECT

public:
    explicit AssetPreviewGenerator(QObject* parent = nullptr);
    ~AssetPreviewGenerator();

    void setOutputDirectory(const QString& dir);
    QString getOutputDirectory() const { return m_outputDirectory; }

    void generatePreview(const QString& assetId);
    void generatePreviews(const QVector<QString>& assetIds);
    void clearCache();

    QString getPreviewPath(const QString& assetId) const;
    bool hasPreview(const QString& assetId) const;

signals:
    void previewGenerated(const QString& assetId, const QString& previewPath);
    void progressChanged(float progress);

private:
    QString m_outputDirectory;
    QMap<QString, QString> m_previewCache;
};

} // namespace ks