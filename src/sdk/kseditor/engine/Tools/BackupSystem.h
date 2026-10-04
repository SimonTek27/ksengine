#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QDateTime>
#include <QJsonObject>
#include <QTimer>

namespace ks {

class BackupManager : public QObject
{
    Q_OBJECT

public:
    explicit BackupManager(QObject* parent = nullptr);
    ~BackupManager();
    Q_DISABLE_COPY(BackupManager)

    static BackupManager* instance();

    struct BackupEntry {
        QString id;
        QString projectPath;
        QString backupPath;
        QDateTime created;
        qint64 size;
        QString description;
        bool isAuto = false;
    };

    void setBackupDirectory(const QString& dir);
    QString getBackupDirectory() const { return m_backupDir; }

    void setAutoBackupEnabled(bool enabled);
    void setAutoBackupInterval(int seconds);
    void setMaxBackups(int max);
    void setCurrentProject(const QString& path);

    QString createBackup(const QString& projectPath, const QString& description = QString());
    QString createAutoBackup(const QString& projectPath, const QString& description);
    bool restoreBackup(const QString& backupId, const QString& targetPath);

    QVector<BackupEntry> getBackups(const QString& projectPath) const;
    BackupEntry getBackup(const QString& backupId) const;
    BackupEntry getLatestBackup(const QString& projectPath) const;

    bool deleteBackup(const QString& backupId);
    void clearOldBackups(int keepCount);

    void setAutoBackup(bool enabled, int intervalMinutes = 30);
    bool isAutoBackupEnabled() const { return m_autoEnabled; }

    bool hasBackup(const QString& projectPath) const;

    qint64 getTotalSize() const;

signals:
    void backupCreated(const BackupEntry& entry);
    void backupRestored(const QString& backupId, const QString& targetPath);
    void backupDeleted(const QString& backupId);
    void autoBackupStarted();
    void autoBackupCompleted();
    void error(const QString& message);

private slots:
    void performAutoBackup();

private:
    static BackupManager* s_instance;

    QString doCreateBackup(const QString& projectPath, const QString& description, bool isAuto);
    void pruneOldBackups(const QString& projectPath);
    void saveIndex();
    void loadIndex();

    QString m_backupDir;
    bool m_autoEnabled = true;
    int m_autoIntervalSeconds = 300;
    int m_maxBackups = 10;
    QString m_currentProjectPath;
    QTimer m_autoTimer;
    QVector<BackupEntry> m_entries;
};

class IncrementalBackup : public QObject
{
    Q_OBJECT

public:
    explicit IncrementalBackup(QObject* parent = nullptr);
    ~IncrementalBackup();
    Q_DISABLE_COPY(IncrementalBackup)

    static IncrementalBackup* instance();

    void setBaseBackup(const QString& backupId);
    void addChange(const QString& filePath, const QString& changeType);

    QString createIncremental(const QString& projectPath);
    bool restoreIncremental(const QString& baseBackupId, const QString& targetPath);

    struct Change {
        QString filePath;
        QString changeType;
        QByteArray data;
    };

    QVector<Change> getChanges() const { return m_changes; }

signals:
    void incrementalCreated();
    void incrementalRestored();

private:
    static IncrementalBackup* s_instance;

    QString m_baseBackupId;
    QVector<Change> m_changes;
};

class BackupScheduler : public QObject
{
    Q_OBJECT

public:
    explicit BackupScheduler(QObject* parent = nullptr);
    ~BackupScheduler();
    Q_DISABLE_COPY(BackupScheduler)

    static BackupScheduler* instance();

    void scheduleBackup(const QString& projectPath, const QString& cronExpression);
    void unscheduleBackup(const QString& projectPath);

    void pauseAll();
    void resumeAll();

    QVector<QString> getScheduledBackups() const { return m_scheduled.keys(); }

signals:
    void backupScheduled(const QString& projectPath);
    void backupTriggered(const QString& projectPath);

private slots:
    void onTimer();

private:
    static BackupScheduler* s_instance;

    struct ScheduledBackup {
        QString projectPath;
        QDateTime nextRun;
        QString cronExpression;
    };

    QMap<QString, ScheduledBackup> m_scheduled;
    QTimer m_timer;
};

class BackupRetention : public QObject
{
    Q_OBJECT

public:
    static BackupRetention* instance();

    struct RetentionPolicy {
        int maxDailyBackups = 1;
        int maxWeeklyBackups = 3;
        int maxMonthlyBackups = 6;
        int maxTotalBackups = 10;
    };

    void setPolicy(const RetentionPolicy& policy);
    RetentionPolicy getPolicy() const { return m_policy; }

    void applyPolicy(const QString& projectPath);
    QVector<QString> getBackupsToDelete(const QString& projectPath) const;

    void setMinBackupsToKeep(int count);
    int getMinBackupsToKeep() const { return m_minKeep; }

signals:
    void applied(const QVector<QString>& deleted);

private:
    BackupRetention(QObject* parent = nullptr);
    ~BackupRetention();
    Q_DISABLE_COPY(BackupRetention)

    static BackupRetention* s_instance;

    RetentionPolicy m_policy;
    int m_minKeep = 1;
};

} // namespace ks