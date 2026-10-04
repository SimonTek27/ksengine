#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QMutex>
#include <QFile>
#include <QDir>

namespace ks {

class CacheManager : public QObject
{
    Q_OBJECT

public:
    static CacheManager* instance();

    void setCacheDirectory(const QString& dir);
    QString getCacheDirectory() const { return m_cacheDir; }

    void setMaxDiskSize(qint64 bytes);
    qint64 diskSize() const;
    int entryCount() const;

    QString cacheKey(const QString& source) const;
    QString cachePath(const QString& key, const QString& ext = QString()) const;

    bool has(const QString& key) const;
    QVariant get(const QString& key) const;
    void set(const QString& key, const QVariant& value, int ttlSeconds = 0);

    void remove(const QString& key);
    void clear();

    void evictExpired();

signals:
    void entryAdded(const QString& key);
    void entryRemoved(const QString& key);
    void cacheCleared();

private:
    CacheManager(QObject* parent = nullptr);
    ~CacheManager();
    Q_DISABLE_COPY(CacheManager)

    static CacheManager* s_instance;

    struct MemEntry {
        QVariant data;
        QDateTime expiry;
    };

    QString m_cacheDir;
    qint64 m_maxDiskBytes = 500LL * 1024 * 1024;
    QMap<QString, MemEntry> m_memCache;
    mutable QMutex m_mutex;
};

class MemoryCache : public QObject
{
    Q_OBJECT

public:
    explicit MemoryCache(const QString& id, QObject* parent = nullptr);
    ~MemoryCache();

    QString getId() const { return m_id; }

    void setMaxSize(qint64 bytes);
    qint64 getMaxSize() const { return m_maxSize; }

    void put(const QString& key, const QVariant& value);
    QVariant get(const QString& key) const;
    bool contains(const QString& key) const;

    void remove(const QString& key);
    void clear();

    qint64 getSize() const { return m_currentSize; }

    void setPolicy(int maxCount, qint64 maxSize, int maxAge);
    void evict();

signals:
    void evicted();

private:
    struct CacheItem {
        QVariant value;
        qint64 size;
        qint64 timestamp;
    };

    QString m_id;
    qint64 m_maxSize = 50 * 1024 * 1024;
    int m_maxCount = 100;
    int m_maxAge = 3600;

    qint64 m_currentSize = 0;
    QMap<QString, CacheItem> m_cache;
    mutable QMutex m_mutex;
};

class DiskCache : public QObject
{
    Q_OBJECT

public:
    explicit DiskCache(const QString& id, QObject* parent = nullptr);
    ~DiskCache();

    QString getId() const { return m_id; }

    void setDirectory(const QString& dir);
    QString getDirectory() const { return m_directory; }

    void setMaxSize(qint64 bytes);
    qint64 getMaxSize() const { return m_maxSize; }

    void setCompression(bool enabled);
    bool isCompressionEnabled() const { return m_compression; }

    void put(const QString& key, const QByteArray& data);
    bool get(const QString& key, QByteArray& data) const;

    void remove(const QString& key);
    void clear();

    qint64 getSize() const;

signals:
    void error(const QString& error);

private:
    QString getFilePath(const QString& key) const;

    QString m_id;
    QString m_directory;
    qint64 m_maxSize = 500 * 1024 * 1024;
    bool m_compression = true;

    mutable QMutex m_mutex;
};

} // namespace ks