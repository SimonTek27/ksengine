#pragma once

#include <QObject>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QSet>
#include <QStringList>

namespace ks {

class AssetFileWatcher : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool watching READ isWatching NOTIFY watchingChanged)
    Q_PROPERTY(int pendingChanges READ pendingChanges NOTIFY pendingChangesCountChanged)

public:
    explicit AssetFileWatcher(QObject* parent = nullptr);

    bool watchDirectory(const QString& path, bool recursive = true);
    bool watchFile(const QString& path);
    bool unwatchDirectory(const QString& path);
    bool unwatchFile(const QString& path);
    void unwatchAll();

    bool isWatching() const { return m_watcher.files().size() + m_watcher.directories().size() > 0; }
    int pendingChanges() const { return m_pendingChanges.size(); }
    QStringList watchedDirectories() const { return m_watcher.directories(); }
    QStringList watchedFiles() const { return m_watcher.files(); }

    void setDebounceInterval(int ms);
    int debounceInterval() const { return m_debounceTimer.interval(); }

    void setWatchNewDirectories(bool enabled) { m_watchNewDirs = enabled; }
    bool watchNewDirectories() const { return m_watchNewDirs; }

signals:
    void fileAdded(const QString& path);
    void fileModified(const QString& path);
    void fileRemoved(const QString& path);
    void directoryAdded(const QString& path);
    void directoryRemoved(const QString& path);
    void batchChanged(const QStringList& paths);
    void watchingChanged();
    void pendingChangesCountChanged(int count);
    void error(const QString& message);

public slots:
    void processChanges();

private slots:
    void onDirectoryChanged(const QString& path);
    void onFileChanged(const QString& path);
    void onDebounceTimeout();

private:
    void queueChange(const QString& path);
    void detectNewFiles(const QString& dirPath);

    QFileSystemWatcher m_watcher;
    QTimer m_debounceTimer;
    QSet<QString> m_pendingChanges;
    QSet<QString> m_knownFiles;
    bool m_watchNewDirs = true;
    static constexpr int kDefaultDebounceMs = 500;
};

} // namespace ks
