#include "AssetSearchEngine.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QPainter>
#include <QImage>
#include <QColor>
#include <QCryptographicHash>
#include <QPolygonF>
#include <QProcess>
#include <QSet>
#include <QRegularExpression>

namespace ks {

AssetSearchEngine::AssetSearchEngine(QObject* parent)
    : QObject(parent)
{
}

AssetSearchEngine::~AssetSearchEngine()
{
}

void AssetSearchEngine::indexDirectory(const QString& path, bool recursive)
{
    QDir dir(path);
    if (!dir.exists()) return;

    QStringList filters;
    filters << "*.fbx" << "*.obj" << "*.glb" << "*.gltf" << "*.kn5"
            << "*.wav" << "*.mp3" << "*.ogg" << "*.flac"
            << "*.png" << "*.jpg" << "*.jpeg" << "*.tga" << "*.dds"
            << "*.ini" << "*.json" << "*.xml";

    QFileInfoList files = dir.entryInfoList(filters, QDir::Files);

    int totalFiles = files.size();
    int processed = 0;

    for (const QFileInfo& fileInfo : files) {
        indexFile(fileInfo.filePath());
        processed++;

        if (processed % 10 == 0) {
            emit indexingProgress((processed * 100.0f) / totalFiles);
        }
    }

    if (recursive) {
        QFileInfoList subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& subdir : subdirs) {
            indexDirectory(subdir.filePath(), true);
        }
    }

    emit indexingComplete(m_index.size());
}

void AssetSearchEngine::indexFile(const QString& filePath)
{
    QFileInfo info(filePath);
    if (!info.exists()) return;

    IndexedAsset asset;
    asset.path = filePath;
    asset.name = info.baseName();
    asset.extension = info.suffix().toLower();
    asset.size = info.size();
    asset.modifiedDate = info.lastModified();

    if (asset.extension == "fbx" || asset.extension == "obj" ||
        asset.extension == "glb" || asset.extension == "gltf" ||
        asset.extension == "kn5") {
        asset.category = "3D Models";
    } else if (asset.extension == "wav" || asset.extension == "mp3" ||
               asset.extension == "ogg" || asset.extension == "flac") {
        asset.category = "Audio";
    } else if (asset.extension == "png" || asset.extension == "jpg" ||
               asset.extension == "jpeg" || asset.extension == "tga" ||
               asset.extension == "dds") {
        asset.category = "Textures";
    } else if (asset.extension == "ini" || asset.extension == "json" ||
               asset.extension == "xml") {
        asset.category = "Config";
    } else {
        asset.category = "Other";
    }

    asset.contentPreview = generatePreview(filePath);

    m_index[filePath] = asset;
    updateInvertedIndex(filePath, asset);

    if (!m_categoryList.contains(asset.category)) {
        m_categoryList.append(asset.category);
    }
}

void AssetSearchEngine::removeFromIndex(const QString& path)
{
    auto it = m_index.constFind(path);
    if (it != m_index.constEnd()) {
        removeFromInvertedIndex(path, it.value());
    }
    m_index.remove(path);
}

void AssetSearchEngine::clearIndex()
{
    m_index.clear();
    m_tagList.clear();
    m_categoryList.clear();
    m_invertedIndex.clear();
    m_categoryIndex.clear();
    m_extensionIndex.clear();
    m_tagIndex.clear();
}

void AssetSearchEngine::tokenize(const QString& text, QSet<QString>& tokens) const
{
    QStringList words = text.toLower().split(QRegularExpression("[\\s_\\-\\.\\,\\;\\:\\!\\(\\)\\[\\]\\{\\}]+"),
        Qt::SkipEmptyParts);
    for (const QString& word : words) {
        if (word.length() >= 2) {
            tokens.insert(word);
        }
    }
}

void AssetSearchEngine::updateInvertedIndex(const QString& path, const IndexedAsset& asset)
{
    QSet<QString> tokens;
    tokenize(asset.name, tokens);

    // Index category
    m_categoryIndex[asset.category].insert(path);

    // Index extension
    m_extensionIndex[asset.extension].insert(path);

    // Index tags
    for (const QString& tag : asset.tags) {
        m_tagIndex[tag].insert(path);
    }

    // Index name tokens
    for (const QString& token : tokens) {
        m_invertedIndex[token].insert(path);
    }
}

void AssetSearchEngine::removeFromInvertedIndex(const QString& path, const IndexedAsset& asset)
{
    QSet<QString> tokens;
    tokenize(asset.name, tokens);

    for (const QString& token : tokens) {
        auto it = m_invertedIndex.find(token);
        if (it != m_invertedIndex.end()) {
            it.value().remove(path);
            if (it.value().isEmpty()) m_invertedIndex.erase(it);
        }
    }

    auto ci = m_categoryIndex.find(asset.category);
    if (ci != m_categoryIndex.end()) {
        ci.value().remove(path);
        if (ci.value().isEmpty()) m_categoryIndex.erase(ci);
    }

    auto ei = m_extensionIndex.find(asset.extension);
    if (ei != m_extensionIndex.end()) {
        ei.value().remove(path);
        if (ei.value().isEmpty()) m_extensionIndex.erase(ei);
    }

    for (const QString& tag : asset.tags) {
        auto ti = m_tagIndex.find(tag);
        if (ti != m_tagIndex.end()) {
            ti.value().remove(path);
            if (ti.value().isEmpty()) m_tagIndex.erase(ti);
        }
    }
}

QVector<AssetSearchEngine::SearchResult> AssetSearchEngine::search(const SearchQuery& query)
{
    QVector<SearchResult> results;

    // Build candidate set from fast lookup indices
    QSet<QString> candidates;

    // Use inverted index for text search
    if (!query.text.isEmpty()) {
        QSet<QString> tokens;
        tokenize(query.text, tokens);
        for (const QString& token : tokens) {
            auto it = m_invertedIndex.constFind(token.toLower());
            if (it != m_invertedIndex.constEnd()) {
                candidates.unite(it.value());
            }
        }
        // If no tokens matched anything, try prefix matching
        if (candidates.isEmpty()) {
            QString lower = query.text.toLower();
            for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
                if (it.value().name.toLower().contains(lower)) {
                    candidates.insert(it.key());
                }
            }
        }
    } else {
        // No text filter — start with all indexed paths
        candidates.reserve(m_index.size());
        for (auto it = m_index.constBegin(); it != m_index.constEnd(); ++it) {
            candidates.insert(it.key());
        }
    }

    // Intersect with category filter
    if (!query.categories.isEmpty()) {
        QSet<QString> catFiltered;
        for (const QString& cat : query.categories) {
            auto it = m_categoryIndex.constFind(cat);
            if (it != m_categoryIndex.constEnd()) {
                catFiltered.unite(it.value());
            }
        }
        candidates.intersect(catFiltered);
    }

    // Intersect with extension filter
    if (!query.extensions.isEmpty()) {
        QSet<QString> extFiltered;
        for (const QString& ext : query.extensions) {
            auto it = m_extensionIndex.constFind(ext);
            if (it != m_extensionIndex.constEnd()) {
                extFiltered.unite(it.value());
            }
        }
        candidates.intersect(extFiltered);
    }

    // Intersect with tag filter (requires ALL tags)
    if (!query.tags.isEmpty()) {
        for (const QString& tag : query.tags) {
            auto it = m_tagIndex.constFind(tag);
            if (it != m_tagIndex.constEnd()) {
                candidates.intersect(it.value());
            } else {
                candidates.clear();
                break;
            }
        }
    }

    // Evaluate candidates with full matchesQuery + relevance scoring
    for (const QString& path : candidates) {
        auto it = m_index.constFind(path);
        if (it == m_index.constEnd()) continue;
        const IndexedAsset& asset = it.value();
        if (!matchesQuery(query, asset)) continue;

        SearchResult result;
        result.path = asset.path;
        result.name = asset.name;
        result.category = asset.category;
        result.extension = asset.extension;
        result.size = asset.size;
        result.modifiedDate = asset.modifiedDate;
        result.tags = asset.tags;
        result.relevanceScore = calculateRelevance(query, asset);
        result.preview = asset.contentPreview;
        results.append(result);
    }

    std::sort(results.begin(), results.end(),
        [query](const SearchResult& a, const SearchResult& b) {
            if (query.sortBy == SearchQuery::Relevance) {
                return a.relevanceScore > b.relevanceScore;
            } else if (query.sortBy == SearchQuery::Name) {
                return a.name < b.name;
            } else if (query.sortBy == SearchQuery::Date) {
                return a.modifiedDate > b.modifiedDate;
            } else if (query.sortBy == SearchQuery::Size) {
                return a.size > b.size;
            }
            return false;
        });

    if (!query.ascending && results.size() > 1) {
        std::reverse(results.begin(), results.end());
    }

    emit searchComplete(results);
    return results;
}

QVector<AssetSearchEngine::SearchResult> AssetSearchEngine::search(const QString& text)
{
    SearchQuery query;
    query.text = text;
    query.sortBy = SearchQuery::Relevance;
    return search(query);
}

bool AssetSearchEngine::matchesQuery(const SearchQuery& query, const IndexedAsset& asset) const
{
    if (!query.text.isEmpty()) {
        bool found = false;
        QString searchText = query.caseSensitive ? query.text : query.text.toLower();
        QString assetName = query.caseSensitive ? asset.name : asset.name.toLower();
        QString assetPath = query.caseSensitive ? asset.path : asset.path.toLower();

        if (assetName.contains(searchText) || assetPath.contains(searchText)) {
            found = true;
        }

        for (const QString& tag : asset.tags) {
            QString tagLower = query.caseSensitive ? tag : tag.toLower();
            if (tagLower.contains(searchText)) {
                found = true;
                break;
            }
        }

        if (!found) return false;
    }

    if (!query.categories.isEmpty() && !query.categories.contains(asset.category)) {
        return false;
    }

    if (!query.extensions.isEmpty() && !query.extensions.contains(asset.extension)) {
        return false;
    }

    if (!query.tags.isEmpty()) {
        bool hasAllTags = true;
        for (const QString& tag : query.tags) {
            if (!asset.tags.contains(tag)) {
                hasAllTags = false;
                break;
            }
        }
        if (!hasAllTags) return false;
    }

    if (query.minSize > 0 && asset.size < query.minSize) return false;
    if (query.maxSize > 0 && asset.size > query.maxSize) return false;

    if (query.dateFrom.isValid() && asset.modifiedDate < query.dateFrom) return false;
    if (query.dateTo.isValid() && asset.modifiedDate > query.dateTo) return false;

    return true;
}

float AssetSearchEngine::calculateRelevance(const SearchQuery& query, const IndexedAsset& asset) const
{
    float score = 0.0f;

    if (!query.text.isEmpty()) {
        QString searchText = query.caseSensitive ? query.text : query.text.toLower();
        QString assetName = query.caseSensitive ? asset.name : asset.name.toLower();

        if (assetName == searchText) {
            score += 100.0f;
        } else if (assetName.startsWith(searchText)) {
            score += 80.0f;
        } else if (assetName.contains(searchText)) {
            score += 50.0f;
        }

        for (const QString& tag : asset.tags) {
            QString tagLower = query.caseSensitive ? tag : tag.toLower();
            if (tagLower.contains(searchText)) {
                score += 30.0f;
            }
        }

        QString pathLower = query.caseSensitive ? asset.path : asset.path.toLower();
        if (pathLower.contains(searchText)) {
            score += 10.0f;
        }
    }

    for (const QString& tag : query.tags) {
        if (asset.tags.contains(tag)) {
            score += 20.0f;
        }
    }

    if (!query.categories.isEmpty() && query.categories.contains(asset.category)) {
        score += 15.0f;
    }

    return score;
}

QString AssetSearchEngine::generatePreview(const QString& path) const
{
    QFileInfo info(path);
    QString ext = info.suffix().toLower();

    if (ext == "wav" || ext == "mp3" || ext == "ogg" || ext == "flac") {
        qint64 size = info.size();
        int seconds = size / 44100 / 2;
        return QString("Audio: %1 sec").arg(seconds);
    } else if (ext == "png" || ext == "jpg" || ext == "jpeg") {
        return QString("Image: %1 KB").arg(info.size() / 1024);
    } else if (ext == "fbx" || ext == "obj" || ext == "kn5") {
        return QString("3D Model");
    } else if (ext == "ini" || ext == "json") {
        return QString("Config File");
    }

    return QString("File: %1").arg(info.size());
}

void AssetSearchEngine::addTag(const QString& path, const QString& tag)
{
    if (m_index.contains(path)) {
        if (!m_index[path].tags.contains(tag)) {
            m_index[path].tags.append(tag);
            if (!m_tagList.contains(tag)) {
                m_tagList.append(tag);
            }
        }
    }
}

void AssetSearchEngine::removeTag(const QString& path, const QString& tag)
{
    if (m_index.contains(path)) {
        m_index[path].tags.removeAll(tag);
    }
}

QStringList AssetSearchEngine::getTags(const QString& path) const
{
    if (m_index.contains(path)) {
        return m_index[path].tags;
    }
    return QStringList();
}

void AssetSearchEngine::setCategory(const QString& path, const QString& category)
{
    if (m_index.contains(path)) {
        m_index[path].category = category;
        if (!m_categoryList.contains(category)) {
            m_categoryList.append(category);
        }
    }
}

QString AssetSearchEngine::getCategory(const QString& path) const
{
    if (m_index.contains(path)) {
        return m_index[path].category;
    }
    return QString();
}

void AssetSearchEngine::saveIndex(const QString& path)
{
    if (path.isEmpty()) return;
    QJsonObject root;

    QJsonArray assetsArray;
    for (const IndexedAsset& asset : m_index.values()) {
        QJsonObject assetObj;
        assetObj["path"] = asset.path;
        assetObj["name"] = asset.name;
        assetObj["category"] = asset.category;
        assetObj["extension"] = asset.extension;
        assetObj["size"] = static_cast<double>(asset.size);
        assetObj["modifiedDate"] = asset.modifiedDate.toString(Qt::ISODate);

        QJsonArray tagsArray;
        for (const QString& tag : asset.tags) {
            tagsArray.append(tag);
        }
        assetObj["tags"] = tagsArray;

        assetObj["preview"] = asset.contentPreview;
        assetsArray.append(assetObj);
    }
    root["assets"] = assetsArray;

    QJsonDocument doc(root);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(doc.toJson());
        file.close();
    }
}

void AssetSearchEngine::loadIndex(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    QJsonObject root = doc.object();
    QJsonArray assetsArray = root["assets"].toArray();

    m_index.clear();
    m_tagList.clear();
    m_categoryList.clear();

    for (const QJsonValue& val : assetsArray) {
        QJsonObject obj = val.toObject();
        IndexedAsset asset;
        asset.path = obj["path"].toString();
        asset.name = obj["name"].toString();
        asset.category = obj["category"].toString();
        asset.extension = obj["extension"].toString();
        asset.size = static_cast<qint64>(obj["size"].toDouble());
        asset.modifiedDate = QDateTime::fromString(obj["modifiedDate"].toString(), Qt::ISODate);
        asset.contentPreview = obj["preview"].toString();

        QJsonArray tagsArray = obj["tags"].toArray();
        for (const QJsonValue& tagVal : tagsArray) {
            asset.tags.append(tagVal.toString());
            if (!m_tagList.contains(tagVal.toString())) {
                m_tagList.append(tagVal.toString());
            }
        }

        m_index[asset.path] = asset;

        if (!m_categoryList.contains(asset.category)) {
            m_categoryList.append(asset.category);
        }
    }
}

QStringList AssetSearchEngine::getAllTags() const
{
    return m_tagList;
}

QStringList AssetSearchEngine::getAllCategories() const
{
    return m_categoryList;
}

QVector<AssetSearchEngine::SearchResult> AssetSearchEngine::findSimilar(const QString& path, int maxResults)
{
    if (!m_index.contains(path)) return QVector<SearchResult>();

    const IndexedAsset& target = m_index[path];
    QVector<SearchResult> results;

    for (const IndexedAsset& asset : m_index.values()) {
        if (asset.path == path) continue;

        if (asset.extension != target.extension) continue;

        float score = 0.0f;

        if (asset.category == target.category) score += 30.0f;

        int commonTags = 0;
        for (const QString& tag : target.tags) {
            if (asset.tags.contains(tag)) commonTags++;
        }
        score += commonTags * 20.0f;

        qint64 sizeDiff = qAbs(asset.size - target.size);
        if (sizeDiff < target.size * 0.2f) score += 20.0f;

        if (score > 20.0f) {
            SearchResult result;
            result.path = asset.path;
            result.name = asset.name;
            result.category = asset.category;
            result.extension = asset.extension;
            result.size = asset.size;
            result.modifiedDate = asset.modifiedDate;
            result.tags = asset.tags;
            result.relevanceScore = score;
            results.append(result);
        }
    }

    std::sort(results.begin(), results.end(),
        [](const SearchResult& a, const SearchResult& b) {
            return a.relevanceScore > b.relevanceScore;
        });

    return results.mid(0, maxResults);
}

QVector<AssetSearchEngine::SearchResult> AssetSearchEngine::getRecentFiles(int count) const
{
    QVector<SearchResult> results;

    for (const IndexedAsset& asset : m_index.values()) {
        SearchResult result;
        result.path = asset.path;
        result.name = asset.name;
        result.category = asset.category;
        result.extension = asset.extension;
        result.size = asset.size;
        result.modifiedDate = asset.modifiedDate;
        result.tags = asset.tags;
        results.append(result);
    }

    std::sort(results.begin(), results.end(),
        [](const SearchResult& a, const SearchResult& b) {
            return a.modifiedDate > b.modifiedDate;
        });

    return results.mid(0, count);
}

QVector<AssetSearchEngine::SearchResult> AssetSearchEngine::getFilesByCategory(const QString& category) const
{
    QVector<SearchResult> results;

    for (const IndexedAsset& asset : m_index.values()) {
        if (asset.category == category) {
            SearchResult result;
            result.path = asset.path;
            result.name = asset.name;
            result.category = asset.category;
            result.extension = asset.extension;
            result.size = asset.size;
            result.modifiedDate = asset.modifiedDate;
            result.tags = asset.tags;
            results.append(result);
        }
    }

    return results;
}

QVector<AssetSearchEngine::SearchResult> AssetSearchEngine::getFilesByTag(const QString& tag) const
{
    QVector<SearchResult> results;

    for (const IndexedAsset& asset : m_index.values()) {
        if (asset.tags.contains(tag)) {
            SearchResult result;
            result.path = asset.path;
            result.name = asset.name;
            result.category = asset.category;
            result.extension = asset.extension;
            result.size = asset.size;
            result.modifiedDate = asset.modifiedDate;
            result.tags = asset.tags;
            results.append(result);
        }
    }

    return results;
}

// ============================================================================
// SmartAssetFilter
// ============================================================================

SmartAssetFilter::SmartAssetFilter(QObject* parent)
    : QObject(parent)
{
}

SmartAssetFilter::~SmartAssetFilter()
{
}

void SmartAssetFilter::addPreset(const FilterPreset& preset)
{
    m_presets[preset.name] = preset;
    emit presetAdded(preset.name);
}

void SmartAssetFilter::removePreset(const QString& name)
{
    m_presets.remove(name);
    emit presetRemoved(name);
}

SmartAssetFilter::FilterPreset SmartAssetFilter::getPreset(const QString& name) const
{
    return m_presets.value(name);
}

QStringList SmartAssetFilter::getPresetNames() const
{
    return m_presets.keys();
}

QVector<AssetSearchEngine::SearchResult> SmartAssetFilter::applyFilter(
    const QVector<AssetSearchEngine::SearchResult>& results,
    const QString& presetName) const
{
    if (!m_presets.contains(presetName)) return results;
    return applyFilter(results, m_presets[presetName]);
}

QVector<AssetSearchEngine::SearchResult> SmartAssetFilter::applyFilter(
    const QVector<AssetSearchEngine::SearchResult>& results,
    const FilterPreset& preset) const
{
    QVector<AssetSearchEngine::SearchResult> filtered;
    for (const auto& r : results) {
        if (!preset.extensions.isEmpty() && !preset.extensions.contains(r.extension))
            continue;
        if (preset.minSize > 0 && r.size < preset.minSize)
            continue;
        if (preset.maxSize > 0 && r.size > preset.maxSize)
            continue;
        if (!preset.requiredCategories.isEmpty()) {
            bool match = false;
            for (const auto& cat : preset.requiredCategories) {
                if (r.category.contains(cat, Qt::CaseInsensitive)) { match = true; break; }
            }
            if (!match) continue;
        }
        if (!preset.requiredTags.isEmpty()) {
            bool match = false;
            for (const auto& tag : preset.requiredTags) {
                if (r.tags.contains(tag, Qt::CaseInsensitive)) { match = true; break; }
            }
            if (!match) continue;
        }
        if (!preset.excludedTags.isEmpty()) {
            bool excluded = false;
            for (const auto& tag : preset.excludedTags) {
                if (r.tags.contains(tag, Qt::CaseInsensitive)) { excluded = true; break; }
            }
            if (excluded) continue;
        }
        filtered.append(r);
    }
    return filtered;
}

void SmartAssetFilter::savePresets(const QString& path)
{
    QJsonObject root;
    QJsonArray arr;
    for (auto it = m_presets.begin(); it != m_presets.end(); ++it) {
        QJsonObject obj;
        obj["name"] = it.key();
        obj["extensions"] = QJsonArray::fromStringList(it.value().extensions);
        arr.append(obj);
    }
    root["presets"] = arr;
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(root).toJson());
        file.close();
    }
}

void SmartAssetFilter::loadPresets(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonArray arr = doc.object()["presets"].toArray();
    for (const auto& val : arr) {
        QJsonObject obj = val.toObject();
        FilterPreset preset;
        preset.name = obj["name"].toString();
        QJsonArray exts = obj["extensions"].toArray();
        for (const auto& e : exts) preset.extensions.append(e.toString());
        m_presets[preset.name] = preset;
    }
}

// ============================================================================
// AssetThumbnailGenerator
// ============================================================================

AssetThumbnailGenerator::AssetThumbnailGenerator(QObject* parent)
    : QObject(parent)
    , m_cacheDir(QDir::homePath() + "/.kseditor/thumbnails")
{
}

AssetThumbnailGenerator::~AssetThumbnailGenerator()
{
}

void AssetThumbnailGenerator::generateThumbnail(const QString& assetPath, ThumbnailSize size)
{
    if (assetPath.isEmpty()) return;

    QString ext = QFileInfo(assetPath).suffix().toLower();
    if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "bmp" || ext == "tga" || ext == "dds") {
        generateImageThumbnail(assetPath, size);
    } else if (ext == "kn5" || ext == "fbx" || ext == "obj" || ext == "dae" || ext == "glb" || ext == "gltf") {
        generate3DThumbnail(assetPath, size);
    } else if (ext == "wav" || ext == "ogg" || ext == "mp3" || ext == "flac") {
        generateAudioThumbnail(assetPath, size);
    } else if (ext == "mp4" || ext == "avi" || ext == "mkv" || ext == "mov" || ext == "wmv" || ext == "flv" || ext == "webm") {
        generateVideoThumbnail(assetPath, size);
    } else if (ext == "ttf" || ext == "otf" || ext == "fon") {
        generateFontThumbnail(assetPath, size);
    }
}

void AssetThumbnailGenerator::generateThumbnails(const QStringList& paths, ThumbnailSize size)
{
    for (const auto& path : paths) {
        generateThumbnail(path, size);
    }
}

QString AssetThumbnailGenerator::getThumbnailPath(const QString& assetPath, ThumbnailSize size) const
{
    QFileInfo fi(assetPath);
    return m_cacheDir + "/" + fi.baseName() + "_" + QString::number(size) + ".png";
}

bool AssetThumbnailGenerator::hasThumbnail(const QString& assetPath, ThumbnailSize size) const
{
    return QFile::exists(getThumbnailPath(assetPath, size));
}

void AssetThumbnailGenerator::clearCache()
{
    m_thumbnailCache.clear();
}

void AssetThumbnailGenerator::clearCacheForAsset(const QString& assetPath)
{
    m_thumbnailCache.remove(assetPath);
}

void AssetThumbnailGenerator::setCacheDirectory(const QString& dir)
{
    m_cacheDir = dir;
    QDir().mkpath(m_cacheDir);
}

static int thumbSizeForEnum(AssetThumbnailGenerator::ThumbnailSize size) {
    switch (size) {
        case AssetThumbnailGenerator::Small: return 64;
        case AssetThumbnailGenerator::Medium: return 128;
        case AssetThumbnailGenerator::Large: return 256;
    }
    return 128;
}

static QString cachePathFor(const QString& cacheDir, const QString& filePath, int size) {
    QByteArray hash = QCryptographicHash::hash(filePath.toUtf8(), QCryptographicHash::Md5).toHex();
    return QDir(cacheDir).filePath(QString("%1_%2.png").arg(QString::fromLatin1(hash)).arg(size));
}

QString AssetThumbnailGenerator::generateImageThumbnail(const QString& imagePath, ThumbnailSize size)
{
    int s = thumbSizeForEnum(size);
    QString out = cachePathFor(m_cacheDir, imagePath, s);
    if (QFile::exists(out)) return out;

    QImage img(imagePath);
    if (img.isNull()) return QString();

    QImage thumb = img.scaled(s, s, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QDir().mkpath(m_cacheDir);
    thumb.save(out);
    return out;
}

QString AssetThumbnailGenerator::generate3DThumbnail(const QString& modelPath, ThumbnailSize size)
{
    int s = thumbSizeForEnum(size);
    QString out = cachePathFor(m_cacheDir, modelPath, s);
    if (QFile::exists(out)) return out;

    QImage thumb(s, s, QImage::Format_ARGB32);
    thumb.fill(QColor(35, 35, 45));
    QPainter p(&thumb);
    p.setRenderHint(QPainter::Antialiasing);
    // Draw a generic 3D model icon
    p.setBrush(QColor(70, 120, 180));
    p.setPen(Qt::NoPen);
    QPolygonF cube;
    cube << QPointF(s*0.3, s*0.7) << QPointF(s*0.5, s*0.8) << QPointF(s*0.7, s*0.7)
         << QPointF(s*0.7, s*0.3) << QPointF(s*0.5, s*0.2) << QPointF(s*0.3, s*0.3);
    p.drawPolygon(cube);
    p.setBrush(QColor(50, 90, 140));
    QPolygonF top;
    top << QPointF(s*0.3, s*0.3) << QPointF(s*0.5, s*0.2) << QPointF(s*0.5, s*0.4) << QPointF(s*0.3, s*0.5);
    p.drawPolygon(top);
    QPolygonF side;
    side << QPointF(s*0.7, s*0.3) << QPointF(s*0.7, s*0.7) << QPointF(s*0.5, s*0.8) << QPointF(s*0.5, s*0.4);
    p.drawPolygon(side);
    p.end();

    QDir().mkpath(m_cacheDir);
    thumb.save(out);
    return out;
}

QString AssetThumbnailGenerator::generateAudioThumbnail(const QString& audioPath, ThumbnailSize size)
{
    int s = thumbSizeForEnum(size);
    QString out = cachePathFor(m_cacheDir, audioPath, s);
    if (QFile::exists(out)) return out;

    // Try ffmpeg to generate waveform thumbnail
    QStringList ffmpegCandidates = {"ffmpeg", "ffmpeg.exe"};
    for (const QString& ff : ffmpegCandidates) {
        QProcess proc;
        proc.start(ff, {"-i", audioPath,
                        "-filter_complex",
                        QString("showwavespic=s=%1x%2:colors=#64c864").arg(s).arg(s),
                        "-frames:v", "1", "-y", out});
        if (proc.waitForFinished(15000) && proc.exitCode() == 0 && QFile::exists(out)) {
            return out;
        }
        if (QFile::exists(out)) QFile::remove(out);
    }

    // Fallback: drawn waveform
    QImage thumb(s, s, QImage::Format_ARGB32);
    thumb.fill(QColor(25, 25, 35));
    {   QPainter p(&thumb);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(100, 200, 100), 2));
        int cx = s / 2, cy = s / 2;
        for (int x = 4; x < s - 4; ++x) {
            float t = (float)(x - 4) / (s - 8);
            float amp = sinf(t * 12.0f) * 0.3f + sinf(t * 7.0f) * 0.2f + sinf(t * 23.0f) * 0.1f;
            int y = cy + (int)(amp * (s / 2 - 8));
            p.drawPoint(x, y);
        }
    }

    QDir().mkpath(m_cacheDir);
    thumb.save(out);
    return out;
}

QString AssetThumbnailGenerator::generateVideoThumbnail(const QString& videoPath, ThumbnailSize size)
{
    int s = thumbSizeForEnum(size);
    QString out = cachePathFor(m_cacheDir, videoPath, s);
    if (QFile::exists(out)) return out;

    // Try ffmpeg to extract first frame at given timestamp
    QStringList ffmpegCandidates = {"ffmpeg", "ffmpeg.exe"};
    for (const QString& ff : ffmpegCandidates) {
        QProcess proc;
        proc.start(ff, {"-ss", "0.5", "-i", videoPath,
                        "-vframes", "1", "-q:v", "2",
                        "-s", QString("%1x%1").arg(s),
                        "-y", out});
        if (proc.waitForFinished(15000) && proc.exitCode() == 0 && QFile::exists(out)) {
            return out;
        }
        // Clean partial output if ffmpeg failed
        if (QFile::exists(out)) QFile::remove(out);
    }

    // Fallback: drawn placeholder
    QImage thumb(s, s, QImage::Format_ARGB32);
    thumb.fill(QColor(20, 20, 30));
    {   QPainter p(&thumb);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(QColor(200, 180, 60), 2));
        p.setBrush(QColor(200, 180, 60, 40));
        p.drawRoundedRect(s*0.15, s*0.1, s*0.7, s*0.8, 4, 4);
        QPolygonF play;
        play << QPointF(s*0.38, s*0.3) << QPointF(s*0.38, s*0.7) << QPointF(s*0.68, s*0.5);
        p.setBrush(QColor(200, 180, 60));
        p.setPen(Qt::NoPen);
        p.drawPolygon(play);
    }
    QDir().mkpath(m_cacheDir);
    thumb.save(out);
    return out;
}

QString AssetThumbnailGenerator::generateFontThumbnail(const QString& fontPath, ThumbnailSize size)
{
    int s = thumbSizeForEnum(size);
    QString out = cachePathFor(m_cacheDir, fontPath, s);
    if (QFile::exists(out)) return out;

    QImage thumb(s, s, QImage::Format_ARGB32);
    thumb.fill(QColor(30, 25, 35));
    QPainter p(&thumb);
    p.setRenderHint(QPainter::Antialiasing);
    QFont font("Arial", s * 0.35, QFont::Bold);
    p.setFont(font);
    p.setPen(QColor(180, 120, 220));
    p.drawText(QRect(0, 0, s, s), Qt::AlignCenter, "Aa");
    p.end();

    QDir().mkpath(m_cacheDir);
    thumb.save(out);
    return out;
}

} // namespace ks
