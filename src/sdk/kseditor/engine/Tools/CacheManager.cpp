#include "CacheManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QDebug>
#include <QTimer>

namespace ks {

CacheManager* CacheManager::s_instance = nullptr;

CacheManager* CacheManager::instance()
{
    if (!s_instance)
        s_instance = new CacheManager();
    return s_instance;
}

CacheManager::CacheManager(QObject* parent)
    : QObject(parent)
{
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/kseditor";
    QDir().mkpath(m_cacheDir);

    // Periodic eviction
    auto* evictTimer = new QTimer(this);
    connect(evictTimer, &QTimer::timeout, this, &CacheManager::evictExpired);
    evictTimer->start(60000); // every minute
}

CacheManager::~CacheManager()
{
    s_instance = nullptr;
}

MemoryCache::MemoryCache(const QString& id, QObject* parent)
    : QObject(parent), m_id(id) {}
MemoryCache::~MemoryCache() {}

void MemoryCache::put(const QString& key, const QVariant& value)
{
    QMutexLocker lock(&m_mutex);
    CacheItem item;
    item.value = value;
    item.size = static_cast<qint64>(value.toByteArray().size());
    item.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_cache.insert(key, item);
    m_currentSize += item.size;
    if ((m_maxSize > 0 && m_currentSize > m_maxSize) || (m_cache.size() > m_maxCount))
        evict();
}

QVariant MemoryCache::get(const QString& key) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_cache.find(key);
    if (it == m_cache.end()) return {};
    return it->value;
}

bool MemoryCache::contains(const QString& key) const
{
    QMutexLocker lock(&m_mutex);
    return m_cache.contains(key);
}

void MemoryCache::remove(const QString& key)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_cache.find(key);
    if (it != m_cache.end()) {
        m_currentSize -= it->size;
        m_cache.erase(it);
    }
}

void MemoryCache::clear()
{
    QMutexLocker lock(&m_mutex);
    m_cache.clear();
    m_currentSize = 0;
}

void MemoryCache::setPolicy(int maxCount, qint64 maxSize, int maxAge)
{
    QMutexLocker lock(&m_mutex);
    m_maxCount = maxCount;
    m_maxSize = maxSize;
    m_maxAge = maxAge;
    evict();
}

void MemoryCache::evict()
{
    QMutexLocker lock(&m_mutex);
    qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    for (auto it = m_cache.begin(); it != m_cache.end(); ) {
        qint64 age = it->timestamp ? (nowMs - it->timestamp) / 1000 : 0;
        if (m_maxAge > 0 && age > m_maxAge) {
            m_currentSize -= it->size;
            it = m_cache.erase(it);
        } else {
            ++it;
        }
    }
    while ((m_maxSize > 0 && m_currentSize > m_maxSize) || (m_cache.size() > m_maxCount)) {
        auto oldest = m_cache.begin();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
            if (it->timestamp < oldest->timestamp) oldest = it;
        if (oldest == m_cache.end()) break;
        m_currentSize -= oldest->size;
        m_cache.erase(oldest);
    }
    emit evicted();
}

DiskCache::DiskCache(const QString& id, QObject* parent)
    : QObject(parent), m_id(id) {}
DiskCache::~DiskCache() {}

QString CacheManager::cacheKey(const QString& source) const
{
    return QCryptographicHash::hash(source.toUtf8(), QCryptographicHash::Md5).toHex();
}

QString CacheManager::cachePath(const QString& key, const QString& ext) const
{
    return QDir(m_cacheDir).filePath(key + (ext.isEmpty() ? "" : "." + ext));
}

bool CacheManager::has(const QString& key) const
{
    if (m_memCache.contains(key)) {
        const auto& e = m_memCache[key];
        if (e.expiry.isNull() || e.expiry > QDateTime::currentDateTime()) return true;
    }
    return QFile::exists(cachePath(key));
}

QVariant CacheManager::get(const QString& key) const
{
    // Memory cache first
    if (m_memCache.contains(key)) {
        const auto& e = m_memCache[key];
        if (e.expiry.isNull() || e.expiry > QDateTime::currentDateTime())
            return e.data;
        const_cast<CacheManager*>(this)->m_memCache.remove(key);
    }
    // Disk cache
    QFile f(cachePath(key, "cache"));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QDataStream ds(&f);
    QVariant v;
    ds >> v;
    return v;
}

void CacheManager::set(const QString& key, const QVariant& value, int ttlSeconds)
{
    MemEntry e;
    e.data   = value;
    e.expiry = ttlSeconds > 0
               ? QDateTime::currentDateTime().addSecs(ttlSeconds)
               : QDateTime();
    m_memCache.insert(key, e);

    // Persist to disk
    QFile f(cachePath(key, "cache"));
    if (f.open(QIODevice::WriteOnly)) {
        QDataStream ds(&f);
        ds << value;
    }
    emit entryAdded(key);
}

void CacheManager::remove(const QString& key)
{
    m_memCache.remove(key);
    QFile::remove(cachePath(key, "cache"));
    emit entryRemoved(key);
}

void CacheManager::clear()
{
    m_memCache.clear();
    QDir dir(m_cacheDir);
    for (const auto& fi : dir.entryInfoList({"*.cache"}, QDir::Files))
        QFile::remove(fi.absoluteFilePath());
    emit cacheCleared();
}

void CacheManager::evictExpired()
{
    QDateTime now = QDateTime::currentDateTime();
    QStringList toRemove;
    for (auto it = m_memCache.begin(); it != m_memCache.end(); ++it)
        if (!it->expiry.isNull() && it->expiry <= now)
            toRemove << it.key();
    for (const auto& k : toRemove) remove(k);
}

qint64 CacheManager::diskSize() const
{
    qint64 total = 0;
    for (const auto& fi : QDir(m_cacheDir).entryInfoList({"*.cache"}, QDir::Files))
        total += fi.size();
    return total;
}

int CacheManager::entryCount() const
{
    return m_memCache.size();
}

void CacheManager::setMaxDiskSize(qint64 bytes)
{
    m_maxDiskBytes = bytes;
}

void CacheManager::setCacheDirectory(const QString& dir)
{
    m_cacheDir = dir;
    QDir().mkpath(dir);
}

QString DiskCache::getFilePath(const QString& key) const
{
    QString safe = QString(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Md5).toHex());
    return QDir(m_directory).filePath(safe + ".cache");
}

void DiskCache::setDirectory(const QString& dir)
{
    QMutexLocker lock(&m_mutex);
    m_directory = dir;
    QDir().mkpath(dir);
}

void DiskCache::setMaxSize(qint64 bytes)
{
    QMutexLocker lock(&m_mutex);
    m_maxSize = bytes;
}

void DiskCache::setCompression(bool enabled)
{
    QMutexLocker lock(&m_mutex);
    m_compression = enabled;
}

void DiskCache::put(const QString& key, const QByteArray& data)
{
    QMutexLocker lock(&m_mutex);
    if (m_directory.isEmpty()) return;
    QFile f(getFilePath(key));
    if (!f.open(QIODevice::WriteOnly)) {
        emit error(f.errorString());
        return;
    }
    QByteArray payload = m_compression ? qCompress(data) : data;
    char flag = m_compression ? 1 : 0;
    f.putChar(flag);
    f.write(payload);
}

bool DiskCache::get(const QString& key, QByteArray& data) const
{
    QMutexLocker lock(&m_mutex);
    data.clear();
    if (m_directory.isEmpty()) return false;
    QFile f(getFilePath(key));
    if (!f.open(QIODevice::ReadOnly)) return false;
    char flag = 0;
    if (!f.getChar(&flag)) return false;
    QByteArray payload = f.readAll();
    data = (flag == 1) ? qUncompress(payload) : payload;
    return true;
}

void DiskCache::remove(const QString& key)
{
    QMutexLocker lock(&m_mutex);
    if (m_directory.isEmpty()) return;
    QFile::remove(getFilePath(key));
}

void DiskCache::clear()
{
    QMutexLocker lock(&m_mutex);
    if (m_directory.isEmpty()) return;
    QDir dir(m_directory);
    for (const auto& fi : dir.entryInfoList(QDir::Files))
        QFile::remove(fi.absoluteFilePath());
}

qint64 DiskCache::getSize() const
{
    QMutexLocker lock(&m_mutex);
    if (m_directory.isEmpty()) return 0;
    qint64 total = 0;
    QDir dir(m_directory);
    for (const auto& fi : dir.entryInfoList(QDir::Files))
        total += fi.size();
    return total;
}

} // namespace ks
