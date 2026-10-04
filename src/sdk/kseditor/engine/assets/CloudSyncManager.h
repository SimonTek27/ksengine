#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QTimer>
#include <QDateTime>
#include <QMap>
#include <QNetworkAccessManager>
#include <functional>

namespace ks {

enum class CloudProviderType {
    None,
    GoogleDrive,
    Dropbox,
    OneDrive,
    Local,
    TrueNAS,
    AWS_S3,
    GoogleCloudStorage,
    AzureBlob,
    MinIO,
    CustomS3Compatible,
    WebDAV,
    Nextcloud
};

struct CloudSyncConfig {
    CloudProviderType provider = CloudProviderType::None;
    QString remotePath;
    QString localCachePath;
    QString accessToken;
    QString refreshToken;
    bool autoSync = false;
    int syncIntervalMinutes = 30;
    bool syncOnStartup = false;
};

struct CloudSyncFile {
    QString remoteId;
    QString remotePath;
    QString localPath;
    qint64 fileSize = 0;
    QString checksum;
    QDateTime lastModified;
    bool isDir = false;
    enum Status { Unknown, Synced, Modified, Conflict, NewLocal, NewRemote };
    Status status = Unknown;
};

class CloudSyncManager : public QObject {
    Q_OBJECT
public:
    explicit CloudSyncManager(QObject* parent = nullptr);

    bool configure(const CloudSyncConfig& config);
    CloudSyncConfig config() const { return m_config; }

    bool isAuthenticated() const { return m_authenticated; }
    bool isSyncing() const { return m_syncing; }

    void syncNow();
    void stopSync();

    // File listing and status
    QList<CloudSyncFile> getSyncStatus() const;
    QList<CloudSyncFile> getConflicts() const;
    int getPendingCount() const;

    // Conflict resolution
    enum ConflictResolution { KeepLocal, KeepRemote, Skip, UseNewest };
    bool resolveConflict(const QString& remotePath, ConflictResolution resolution);

    // Selective sync
    void setExcludedPaths(const QStringList& paths);
    QStringList excludedPaths() const { return m_excludedPaths; }

    // Sync history
    struct SyncEntry {
        QDateTime timestamp;
        QString path;
        QString action; // "upload", "download", "delete", "conflict"
        bool success;
        QString message;
    };
    QList<SyncEntry> getSyncHistory() const { return m_syncHistory; }

    QJsonObject serialize() const;
    void deserialize(const QJsonObject& data);

signals:
    void syncStarted();
    void syncProgress(int percent, const QString& currentFile);
    void syncCompleted(bool success, const QString& message);
    void authStatusChanged(bool authenticated);
    void conflictDetected(const QString& path);
    void fileSynced(const QString& path, bool uploaded);
    void syncError(const QString& path, const QString& error);

private slots:
    void onAutoSyncTimer();

private:
    void performSync();
    bool shouldExclude(const QString& path) const;
    void scanLocalChanges();
    void scanRemoteChanges();
    void resolveDifferences();
    bool uploadFile(const QString& localPath, const QString& remotePath);
    bool downloadFile(const QString& remotePath, const QString& localPath);
    bool deleteRemote(const QString& remotePath);
    QString computeChecksum(const QString& filePath) const;
    void logSync(const QString& path, const QString& action, bool success, const QString& msg = QString());
    void updateProgress(int percent, const QString& file);

    CloudSyncConfig m_config;
    bool m_authenticated = false;
    bool m_syncing = false;

    QList<CloudSyncFile> m_files;
    QStringList m_excludedPaths;
    QList<SyncEntry> m_syncHistory;

    QTimer* m_autoSyncTimer = nullptr;
    QMap<QString, QString> m_localChecksums;
    QMap<QString, QString> m_remoteChecksums;
    QMap<QString, QDateTime> m_localTimestamps;
    QMap<QString, QDateTime> m_remoteTimestamps;

    QNetworkAccessManager* m_nam = nullptr;

    static constexpr int MAX_HISTORY = 200;
};

} // namespace ks
