#include "BackupSystem.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QDebug>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonArray>
#include <algorithm>

namespace ks {

BackupManager* BackupManager::s_instance = nullptr;

BackupManager* BackupManager::instance()
{
    if (!s_instance)
        s_instance = new BackupManager();
    return s_instance;
}

BackupManager::BackupManager(QObject* parent)
    : QObject(parent)
{
    m_backupDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/backups";
    QDir().mkpath(m_backupDir);

    // Auto-backup timer: every 5 min
    connect(&m_autoTimer, &QTimer::timeout, this, [this]() {
        if (!m_currentProjectPath.isEmpty())
            createAutoBackup(m_currentProjectPath, "Automatic backup");
    });
}

BackupManager::~BackupManager()
{
    s_instance = nullptr;
}

IncrementalBackup* IncrementalBackup::s_instance = nullptr;
IncrementalBackup* IncrementalBackup::instance() { if (!s_instance) s_instance = new IncrementalBackup(); return s_instance; }
IncrementalBackup::IncrementalBackup(QObject* parent) : QObject(parent) {}
IncrementalBackup::~IncrementalBackup() { s_instance = nullptr; }

void IncrementalBackup::setBaseBackup(const QString& backupId) { m_baseBackupId = backupId; }

void IncrementalBackup::addChange(const QString& filePath, const QString& changeType)
{
    Change ch;
    ch.filePath = filePath;
    ch.changeType = changeType;
    QFile f(filePath);
    if (f.open(QIODevice::ReadOnly)) ch.data = f.readAll();
    m_changes.append(ch);
}

QString IncrementalBackup::createIncremental(const QString& projectPath)
{
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/incremental/" + id;
    QDir().mkpath(dir);

    QJsonArray arr;
    for (const auto& ch : m_changes) {
        QJsonObject obj;
        obj["filePath"] = ch.filePath;
        obj["changeType"] = ch.changeType;
        arr.append(obj);
        QDir().mkpath(QFileInfo(dir + "/" + ch.filePath).absolutePath());
        QFile of(dir + "/" + ch.filePath);
        if (of.open(QIODevice::WriteOnly)) of.write(ch.data);
    }

    QFile manifest(dir + "/manifest.json");
    if (manifest.open(QIODevice::WriteOnly))
        manifest.write(QJsonDocument(arr).toJson());

    return id;
}

bool IncrementalBackup::restoreIncremental(const QString& baseBackupId, const QString& targetPath)
{
    Q_UNUSED(baseBackupId);
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                  + "/incremental/" + m_baseBackupId;
    QFile manifest(dir + "/manifest.json");
    if (!manifest.open(QIODevice::ReadOnly)) return false;
    QJsonArray arr = QJsonDocument::fromJson(manifest.readAll()).array();
    bool ok = true;
    for (const auto& val : arr) {
        QJsonObject obj = val.toObject();
        QString srcPath = dir + "/" + obj["filePath"].toString();
        QString dstPath = targetPath + "/" + obj["filePath"].toString();
        QDir().mkpath(QFileInfo(dstPath).absolutePath());
        if (!QFile::copy(srcPath, dstPath)) ok = false;
    }
    return ok;
}

void BackupManager::setBackupDirectory(const QString& dir)
{
    m_backupDir = dir;
    QDir().mkpath(dir);
}

void BackupManager::setAutoBackupEnabled(bool enabled)
{
    m_autoEnabled = enabled;
    if (enabled) m_autoTimer.start(m_autoIntervalSeconds * 1000);
    else          m_autoTimer.stop();
}

void BackupManager::setAutoBackupInterval(int seconds)
{
    m_autoIntervalSeconds = qMax(1, seconds);
    m_autoTimer.setInterval(m_autoIntervalSeconds * 1000);
}

void BackupManager::setMaxBackups(int max)
{
    m_maxBackups = qMax(1, max);
}

void BackupManager::setCurrentProject(const QString& path)
{
    m_currentProjectPath = path;
    if (m_autoEnabled) m_autoTimer.start(m_autoIntervalSeconds * 1000);
}

QString BackupManager::createBackup(const QString& projectPath, const QString& description)
{
    return doCreateBackup(projectPath, description, false);
}

QString BackupManager::createAutoBackup(const QString& projectPath, const QString& description)
{
    return doCreateBackup(projectPath, description, true);
}

QString BackupManager::doCreateBackup(const QString& projectPath,
                                       const QString& description,
                                       bool isAuto)
{
    if (projectPath.isEmpty() || !QFile::exists(projectPath)) {
        emit error("Backup source not found: " + projectPath);
        return {};
    }

    QString id   = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QString ts   = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString ext  = QFileInfo(projectPath).suffix();
    QString dest = QDir(m_backupDir).filePath(
        QString("%1_%2.%3.bak").arg(ts, id, ext));

    if (!QFile::copy(projectPath, dest)) {
        emit error("Failed to create backup at: " + dest);
        return {};
    }

    BackupEntry entry;
    entry.id          = id;
    entry.projectPath = projectPath;
    entry.backupPath  = dest;
    entry.created     = QDateTime::currentDateTime();
    entry.size        = QFileInfo(dest).size();
    entry.description = description;
    entry.isAuto      = isAuto;
    m_entries.append(entry);

    saveIndex();
    pruneOldBackups(projectPath);

    emit backupCreated(entry);
    return id;
}

bool BackupManager::restoreBackup(const QString& backupId, const QString& targetPath)
{
    for (const auto& e : m_entries) {
        if (e.id != backupId) continue;
        QString dest = targetPath.isEmpty() ? e.projectPath : targetPath;
        // Backup current state before overwriting
        if (QFile::exists(dest))
            QFile::copy(dest, dest + ".pre-restore.bak");
        bool ok = QFile::remove(dest) && QFile::copy(e.backupPath, dest);
        if (ok) emit backupRestored(backupId, dest);
        else    emit error("Restore failed for backup " + backupId);
        return ok;
    }
    emit error("Backup not found: " + backupId);
    return false;
}

bool BackupManager::deleteBackup(const QString& backupId)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id != backupId) continue;
        if (!QFile::remove(m_entries[i].backupPath)) {
            qWarning() << "BackupManager: Failed to remove backup file:" << m_entries[i].backupPath;
        }
        m_entries.removeAt(i);
        saveIndex();
        emit backupDeleted(backupId);
        return true;
    }
    return false;
}

QVector<BackupManager::BackupEntry> BackupManager::getBackups(const QString& projectPath) const
{
    if (projectPath.isEmpty()) return m_entries;
    QVector<BackupEntry> filtered;
    for (const auto& e : m_entries)
        if (e.projectPath == projectPath) filtered << e;
    return filtered;
}

BackupManager::BackupEntry BackupManager::getLatestBackup(const QString& projectPath) const
{
    BackupEntry latest;
    for (const auto& e : m_entries) {
        if (e.projectPath == projectPath &&
            (latest.id.isEmpty() || e.created > latest.created))
            latest = e;
    }
    return latest;
}

void BackupManager::pruneOldBackups(const QString& projectPath)
{
    // Collect entries for this project sorted by date desc
    QVector<BackupEntry*> proj;
    for (auto& e : m_entries)
        if (e.projectPath == projectPath) proj << &e;

    std::sort(proj.begin(), proj.end(),
              [](BackupEntry* a, BackupEntry* b){ return a->created > b->created; });

    // Keep m_maxBackups, delete the rest
    for (int i = m_maxBackups; i < proj.size(); ++i) {
        QFile::remove(proj[i]->backupPath);
        m_entries.removeIf([&](const BackupEntry& e){ return e.id == proj[i]->id; });
    }
    saveIndex();
}

qint64 BackupManager::getTotalSize() const
{
    qint64 total = 0;
    for (const auto& e : m_entries) total += e.size;
    return total;
}

void BackupManager::saveIndex()
{
    QJsonArray arr;
    for (const auto& e : m_entries) {
        QJsonObject obj;
        obj["id"]          = e.id;
        obj["projectPath"] = e.projectPath;
        obj["backupPath"]  = e.backupPath;
        obj["created"]     = e.created.toSecsSinceEpoch();
        obj["size"]        = e.size;
        obj["description"] = e.description;
        obj["isAuto"]      = e.isAuto;
        arr.append(obj);
    }
    QFile f(QDir(m_backupDir).filePath("index.json"));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(arr).toJson());
}

void BackupManager::loadIndex()
{
    QFile f(QDir(m_backupDir).filePath("index.json"));
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    m_entries.clear();
    for (const auto& v : arr) {
        QJsonObject obj = v.toObject();
        BackupEntry e;
        e.id          = obj["id"].toString();
        e.projectPath = obj["projectPath"].toString();
        e.backupPath  = obj["backupPath"].toString();
        e.created     = QDateTime::fromSecsSinceEpoch(
                           static_cast<qint64>(obj["created"].toDouble()));
        e.size        = static_cast<qint64>(obj["size"].toDouble());
        e.description = obj["description"].toString();
        e.isAuto      = obj["isAuto"].toBool();
        if (QFile::exists(e.backupPath)) m_entries << e;
    }
}

void BackupManager::performAutoBackup()
{
    if (!m_currentProjectPath.isEmpty())
        createAutoBackup(m_currentProjectPath, "Scheduled auto backup");
}

bool BackupManager::hasBackup(const QString& projectPath) const
{
    for (const auto& e : m_entries)
        if (e.projectPath == projectPath) return true;
    return false;
}

BackupManager::BackupEntry BackupManager::getBackup(const QString& backupId) const
{
    for (const auto& e : m_entries)
        if (e.id == backupId) return e;
    return BackupEntry();
}

void BackupManager::clearOldBackups(int keepCount)
{
    while (m_entries.size() > keepCount) {
        const auto& oldest = m_entries.first();
        QFile::remove(oldest.backupPath);
        m_entries.removeFirst();
    }
    saveIndex();
}

void BackupManager::setAutoBackup(bool enabled, int intervalMinutes)
{
    m_autoEnabled = enabled;
    if (enabled) {
        m_autoTimer.start(intervalMinutes * 60000);
    } else {
        m_autoTimer.stop();
    }
}

// ─── BackupScheduler ─────────────────────────────────────────────────────

static BackupScheduler* g_backupSchedulerInstance = nullptr;

BackupScheduler* BackupScheduler::instance()
{
    if (!g_backupSchedulerInstance)
        g_backupSchedulerInstance = new BackupScheduler();
    return g_backupSchedulerInstance;
}

BackupScheduler::BackupScheduler(QObject* parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &BackupScheduler::onTimer);
    m_timer.start(60000);
}

BackupScheduler::~BackupScheduler()
{
    g_backupSchedulerInstance = nullptr;
}

void BackupScheduler::scheduleBackup(const QString& projectPath, const QString& cronExpression)
{
    ScheduledBackup sb;
    sb.projectPath = projectPath;
    sb.nextRun = QDateTime::currentDateTime().addSecs(3600);
    sb.cronExpression = cronExpression;
    m_scheduled.insert(projectPath, sb);
}

void BackupScheduler::unscheduleBackup(const QString& projectPath)
{
    m_scheduled.remove(projectPath);
}

void BackupScheduler::pauseAll()
{
    m_timer.stop();
}

void BackupScheduler::resumeAll()
{
    m_timer.start(60000);
}

void BackupScheduler::onTimer()
{
    QDateTime now = QDateTime::currentDateTime();
    for (auto it = m_scheduled.begin(); it != m_scheduled.end(); ++it) {
        if (it.value().nextRun <= now) {
            emit backupTriggered(it.value().projectPath);
            it.value().nextRun = now.addSecs(3600);
        }
    }
}

// ─── BackupRetention ─────────────────────────────────────────────────────

static BackupRetention* g_retentionInstance = nullptr;

BackupRetention* BackupRetention::instance()
{
    if (!g_retentionInstance)
        g_retentionInstance = new BackupRetention();
    return g_retentionInstance;
}

BackupRetention::BackupRetention(QObject* parent)
    : QObject(parent) {}

BackupRetention::~BackupRetention()
{
    g_retentionInstance = nullptr;
}

void BackupRetention::setPolicy(const RetentionPolicy& policy)
{
    m_policy = policy;
}

void BackupRetention::applyPolicy(const QString& projectPath)
{
    QVector<QString> toDelete = getBackupsToDelete(projectPath);
    BackupManager* mgr = BackupManager::instance();
    for (const auto& backupId : toDelete) {
        mgr->deleteBackup(backupId);
    }
    if (!toDelete.isEmpty()) {
        qInfo() << "BackupRetention: Pruned" << toDelete.size() << "old backups for" << projectPath;
    }
}

QVector<QString> BackupRetention::getBackupsToDelete(const QString& projectPath) const
{
    QVector<QString> toDelete;
    BackupManager* mgr = BackupManager::instance();
    QVector<BackupManager::BackupEntry> backups = mgr->getBackups(projectPath);

    if (backups.size() <= m_minKeep) return toDelete;

    // Sort by creation time, newest first
    std::sort(backups.begin(), backups.end(), [](const auto& a, const auto& b) {
        return a.created > b.created;
    });

    // Daily backups: keep maxDailyBackups
    QMap<QDate, int> dailyCount;
    // Weekly backups: keep maxWeeklyBackups per ISO week
    QMap<QString, int> weeklyCount;
    // Monthly backups: keep maxMonthlyBackups per year-month
    QMap<QString, int> monthlyCount;

    int totalKept = 0;

    for (const auto& backup : backups) {
        QDate date = backup.created.date();
        QString weekKey = QString("%1-W%2").arg(date.year()).arg(date.weekNumber());
        QString monthKey = QString("%1-%2").arg(date.year()).arg(date.month(), 2, 10, QChar('0'));

        bool keep = false;

        // Check daily limit
        dailyCount[date]++;
        if (dailyCount[date] <= m_policy.maxDailyBackups) keep = true;

        // Check weekly limit
        weeklyCount[weekKey]++;
        if (weeklyCount[weekKey] <= m_policy.maxWeeklyBackups) keep = true;

        // Check monthly limit
        monthlyCount[monthKey]++;
        if (monthlyCount[monthKey] <= m_policy.maxMonthlyBackups) keep = true;

        // Always keep the most recent backup
        if (totalKept == 0) keep = true;

        // Check total limit
        if (totalKept >= m_policy.maxTotalBackups) keep = false;

        // Enforce minimum keep
        if (totalKept < m_minKeep) keep = true;

        if (keep) {
            totalKept++;
        } else {
            toDelete.append(backup.id);
        }
    }

    return toDelete;
}

void BackupRetention::setMinBackupsToKeep(int count)
{
    m_minKeep = count;
}

} // namespace ks
