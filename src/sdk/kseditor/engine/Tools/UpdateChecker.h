#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QVersionNumber>
#include <QTimer>

namespace ks {

enum class UpdateStatus {
    Unknown,
    Available,
    NotAvailable,
    Downloading,
    ReadyToInstall,
    Installed
};

struct UpdateChannel {
    QString id;
    QString name;
    QString description;
    bool isDefault = false;
};

struct UpdateInfo {
    QString version;
    QString releaseDate;
    QString downloadUrl;
    QString releaseNotes;
    qint64 fileSize;
    QString checksum;
    bool isSecurityUpdate;
    bool isMandatory;
    QStringList affectedVersions;
};

class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    static UpdateChecker* instance();

    void check();
    void startAutoCheck(int intervalHours = 24);
    void stopAutoCheck();

    bool isUpdateAvailable() const;
    QString currentVersion() const;
    QString latestVersion() const;
    QString downloadUrl() const;
    QString releaseNotes() const;

signals:
    void checkStarted();
    void checkComplete(bool available, const QString& version, const QString& url);
    void checkFailed(const QString& error);

private:
    UpdateChecker(QObject* parent = nullptr);
    ~UpdateChecker();
    Q_DISABLE_COPY(UpdateChecker)

    static UpdateChecker* s_instance;

    QNetworkAccessManager* m_nam;
    QTimer* m_timer = nullptr;
    bool m_checking = false;
    bool m_updateAvailable = false;
    QString m_latestVersion;
    QString m_downloadUrl;
    QString m_releaseNotes;
};

class UpdateManager : public QObject
{
    Q_OBJECT

public:
    explicit UpdateManager(QObject* parent = nullptr);
    ~UpdateManager();

    void setUpdateChecker(UpdateChecker* checker);

    bool installUpdateAndRestart();
    bool installUpdateAndQuit();

    void setUpdateInstallPolicy(const QString& policy);
    QString getUpdateInstallPolicy() const { return m_policy; }

    void postponeUpdate(int days);
    QDateTime getPostponedUntil() const { return m_postponedUntil; }

    void showChangelog();

signals:
    void updatePolicyChanged();

private:
    UpdateChecker* m_checker = nullptr;
    QString m_policy = "ask";
    QDateTime m_postponedUntil;
};

class UpdateUI : public QObject
{
    Q_OBJECT

public:
    explicit UpdateUI(QObject* parent = nullptr);
    ~UpdateUI();

    void setUpdateChecker(UpdateChecker* checker);

    void showUpdateDialog();
    void showChangelogDialog();
    void showSettingsDialog();

signals:
    void requested();

private:
    UpdateChecker* m_checker = nullptr;
};

} // namespace ks