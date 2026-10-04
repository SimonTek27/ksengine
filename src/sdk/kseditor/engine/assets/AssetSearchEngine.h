#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QSet>

namespace ks {

class AssetSearchEngine : public QObject
{
    Q_OBJECT

public:
    explicit AssetSearchEngine(QObject* parent = nullptr);
    ~AssetSearchEngine();

    struct SearchQuery {
        QString text;
        QStringList tags;
        QStringList categories;
        QStringList extensions;
        qint64 minSize = 0;
        qint64 maxSize = 0;
        QDateTime dateFrom;
        QDateTime dateTo;
        bool caseSensitive = false;
        enum SortBy { Name, Date, Size, Relevance } sortBy = Relevance;
        bool ascending = true;
    };

    struct SearchResult {
        QString path;
        QString name;
        QString category;
        QString extension;
        qint64 size = 0;
        QDateTime modifiedDate;
        QStringList tags;
        float relevanceScore = 0.0f;
        QString preview;
    };

    void indexDirectory(const QString& path, bool recursive = true);
    void indexFile(const QString& filePath);
    void removeFromIndex(const QString& path);
    void clearIndex();

    QVector<SearchResult> search(const SearchQuery& query);
    QVector<SearchResult> search(const QString& text);

    void addTag(const QString& path, const QString& tag);
    void removeTag(const QString& path, const QString& tag);
    QStringList getTags(const QString& path) const;

    void setCategory(const QString& path, const QString& category);
    QString getCategory(const QString& path) const;

    void saveIndex(const QString& path = QString());
    void loadIndex(const QString& path);

    QStringList getAllTags() const;
    QStringList getAllCategories() const;
    int indexSize() const { return m_index.size(); }

    QVector<SearchResult> findSimilar(const QString& path, int maxResults = 5);
    QVector<SearchResult> getRecentFiles(int count = 10) const;
    QVector<SearchResult> getFilesByCategory(const QString& category) const;
    QVector<SearchResult> getFilesByTag(const QString& tag) const;

signals:
    void indexingProgress(float percent);
    void indexingComplete(int indexedCount);
    void searchComplete(const QVector<SearchResult>& results);

private:
    struct IndexedAsset {
        QString path;
        QString name;
        QString category;
        QString extension;
        qint64 size = 0;
        QDateTime modifiedDate;
        QStringList tags;
        QString contentPreview;
    };

    void tokenize(const QString& text, QSet<QString>& tokens) const;
    void updateInvertedIndex(const QString& path, const IndexedAsset& asset);
    void removeFromInvertedIndex(const QString& path, const IndexedAsset& asset);
    float calculateRelevance(const SearchQuery& query, const IndexedAsset& asset) const;
    bool matchesQuery(const SearchQuery& query, const IndexedAsset& asset) const;
    QString generatePreview(const QString& path) const;

    QMap<QString, IndexedAsset> m_index;
    QStringList m_tagList;
    QStringList m_categoryList;

    // Fast lookup indices
    QMap<QString, QSet<QString>> m_invertedIndex;  // token → asset paths
    QMap<QString, QSet<QString>> m_categoryIndex;  // category → asset paths
    QMap<QString, QSet<QString>> m_extensionIndex; // extension → asset paths
    QMap<QString, QSet<QString>> m_tagIndex;       // tag → asset paths
};

class SmartAssetFilter : public QObject
{
    Q_OBJECT

public:
    explicit SmartAssetFilter(QObject* parent = nullptr);
    ~SmartAssetFilter();

    struct FilterPreset {
        QString name;
        QStringList requiredTags;
        QStringList excludedTags;
        QStringList requiredCategories;
        QStringList extensions;
        qint64 minSize = 0;
        qint64 maxSize = 0;
    };

    void addPreset(const FilterPreset& preset);
    void removePreset(const QString& name);
    FilterPreset getPreset(const QString& name) const;
    QStringList getPresetNames() const;

    QVector<AssetSearchEngine::SearchResult> applyFilter(
        const QVector<AssetSearchEngine::SearchResult>& results,
        const QString& presetName) const;

    QVector<AssetSearchEngine::SearchResult> applyFilter(
        const QVector<AssetSearchEngine::SearchResult>& results,
        const FilterPreset& preset) const;

    void savePresets(const QString& path);
    void loadPresets(const QString& path);

signals:
    void presetAdded(const QString& name);
    void presetRemoved(const QString& name);

private:
    QMap<QString, FilterPreset> m_presets;
};

class AssetThumbnailGenerator : public QObject
{
    Q_OBJECT

public:
    explicit AssetThumbnailGenerator(QObject* parent = nullptr);
    ~AssetThumbnailGenerator();

    enum ThumbnailSize { Small = 64, Medium = 128, Large = 256 };

    void generateThumbnail(const QString& assetPath, ThumbnailSize size = Medium);
    void generateThumbnails(const QStringList& paths, ThumbnailSize size = Medium);

    QString getThumbnailPath(const QString& assetPath, ThumbnailSize size = Medium) const;
    bool hasThumbnail(const QString& assetPath, ThumbnailSize size = Medium) const;

    void clearCache();
    void clearCacheForAsset(const QString& assetPath);

    void setCacheDirectory(const QString& dir);
    QString cacheDirectory() const { return m_cacheDir; }

signals:
    void thumbnailGenerated(const QString& assetPath, const QString& thumbnailPath);
    void progress(float percent);

private:
    QString generateImageThumbnail(const QString& imagePath, ThumbnailSize size);
    QString generate3DThumbnail(const QString& modelPath, ThumbnailSize size);
    QString generateAudioThumbnail(const QString& audioPath, ThumbnailSize size);
    QString generateVideoThumbnail(const QString& videoPath, ThumbnailSize size);
    QString generateFontThumbnail(const QString& fontPath, ThumbnailSize size);

    QString m_cacheDir;
    QMap<QString, QString> m_thumbnailCache;
};

} // namespace ks