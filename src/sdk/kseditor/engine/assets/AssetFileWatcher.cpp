#include "AssetFileWatcher.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace ks {

AssetFileWatcher::AssetFileWatcher(QObject* parent)
    : QObject(parent)
{
    m_debounceTimer.setSingleShot(true);
    m_debounceTimer.setInterval(kDefaultDebounceMs);
    connect(&m_debounceTimer, &QTimer::timeout, this, &AssetFileWatcher::onDebounceTimeout);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &AssetFileWatcher::onDirectoryChanged);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, &AssetFileWatcher::onFileChanged);
}

bool AssetFileWatcher::watchDirectory(const QString& path, bool recursive)
{
    QDir dir(path);
    if (!dir.exists()) return false;

    m_watcher.addPath(path);

    if (recursive) {
        QDirIterator it(path, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            QString subDir = it.next();
            m_watcher.addPath(subDir);
        }
    }

    QDirIterator::IteratorFlags flags = recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags;
    QDirIterator fileIt(path, QDir::Files, flags);
    while (fileIt.hasNext()) {
        m_knownFiles.insert(fileIt.next());
    }

    emit watchingChanged();
    return true;
}

bool AssetFileWatcher::watchFile(const QString& path)
{
    if (!QFileInfo::exists(path)) return false;
    m_watcher.addPath(path);
    m_knownFiles.insert(path);
    emit watchingChanged();
    return true;
}

bool AssetFileWatcher::unwatchDirectory(const QString& path)
{
    QStringList dirs = m_watcher.directories();
    for (const auto& d : dirs) {
        if (d.startsWith(path)) {
            m_watcher.removePath(d);
        }
    }

    QStringList files = m_watcher.files();
    for (const auto& f : files) {
        if (f.startsWith(path)) {
            m_watcher.removePath(f);
            m_knownFiles.remove(f);
        }
    }

    emit watchingChanged();
    return true;
}

bool AssetFileWatcher::unwatchFile(const QString& path)
{
    m_watcher.removePath(path);
    m_knownFiles.remove(path);
    emit watchingChanged();
    return true;
}

void AssetFileWatcher::unwatchAll()
{
    QStringList dirs = m_watcher.directories();
    for (const auto& d : dirs) m_watcher.removePath(d);

    QStringList files = m_watcher.files();
    for (const auto& f : files) m_watcher.removePath(f);

    m_knownFiles.clear();
    m_pendingChanges.clear();
    m_debounceTimer.stop();
    emit watchingChanged();
}

void AssetFileWatcher::setDebounceInterval(int ms)
{
    m_debounceTimer.setInterval(qMax(50, ms));
}

void AssetFileWatcher::processChanges()
{
    if (m_pendingChanges.isEmpty()) return;
    QStringList changes = m_pendingChanges.values();
    m_pendingChanges.clear();
    emit batchChanged(changes);
}

void AssetFileWatcher::onDirectoryChanged(const QString& path)
{
    queueChange(path);
    detectNewFiles(path);

    QDir dir(path);
    if (!dir.exists()) {
        emit directoryRemoved(path);
        m_watcher.removePath(path);
        return;
    }

    QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto& e : entries) {
        QString fullPath = dir.absoluteFilePath(e);
        if (!m_watcher.directories().contains(fullPath) && m_watchNewDirs) {
            m_watcher.addPath(fullPath);
            detectNewFiles(fullPath);
            emit directoryAdded(fullPath);
        }
    }
}

void AssetFileWatcher::onFileChanged(const QString& path)
{
    queueChange(path);
    if (!QFileInfo::exists(path)) {
        m_knownFiles.remove(path);
        emit fileRemoved(path);
    } else {
        emit fileModified(path);
    }
}

void AssetFileWatcher::onDebounceTimeout()
{
    if (m_pendingChanges.isEmpty()) return;

    QStringList changes = m_pendingChanges.values();
    m_pendingChanges.clear();

    for (const auto& path : changes) {
        QString absPath = QFileInfo(path).absoluteFilePath();
        if (!m_knownFiles.contains(absPath) && QFileInfo::exists(absPath)) {
            m_knownFiles.insert(absPath);
            emit fileAdded(absPath);
        } else if (QFileInfo::exists(absPath)) {
            emit fileModified(absPath);
        }
    }

    emit batchChanged(changes);
}

void AssetFileWatcher::queueChange(const QString& path)
{
    m_pendingChanges.insert(path);
    emit pendingChangesCountChanged(m_pendingChanges.size());
    m_debounceTimer.start();
}

void AssetFileWatcher::detectNewFiles(const QString& dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists()) return;

    QStringList files = dir.entryList(QDir::Files, QDir::Name);
    for (const auto& f : files) {
        QString fullPath = dir.absoluteFilePath(f);
        if (!m_knownFiles.contains(fullPath)) {
            queueChange(fullPath);
        }
    }
}

} // namespace ks
