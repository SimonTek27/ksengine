#include "UpdateChecker.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVersionNumber>
#include <QDebug>
#include <QTimer>
#include <QDesktopServices>
#include <QUrl>

namespace ks {

static const QString kCurrentVersion = "2.0.0";
static const QString kUpdateUrl =
    "https://api.github.com/repos/kseditor/kseditor/releases/latest";

UpdateChecker* UpdateChecker::s_instance = nullptr;

UpdateChecker* UpdateChecker::instance()
{
    if (!s_instance)
        s_instance = new UpdateChecker();
    return s_instance;
}

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    // Periodic check every 24 h
    m_timer = new QTimer(this);
    m_timer->setInterval(24 * 60 * 60 * 1000);
    connect(m_timer, &QTimer::timeout, this, &UpdateChecker::check);
}

UpdateChecker::~UpdateChecker()
{
    s_instance = nullptr;
}

void UpdateChecker::check()
{
    if (m_checking) return;
    m_checking = true;
    emit checkStarted();

    QUrl url(kUpdateUrl);
    QNetworkRequest req{url};
    req.setRawHeader("User-Agent", "ksEditor/2.0");
    req.setRawHeader("Accept", "application/vnd.github.v3+json");

    QNetworkReply* reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        m_checking = false;
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            emit checkFailed(reply->errorString());
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        if (!doc.isObject()) {
            emit checkFailed("Invalid JSON response from GitHub");
            return;
        }
        QJsonObject root = doc.object();
        QString tag = root["tag_name"].toString().remove('v');
        QString url = root["html_url"].toString();
        QString notes = root["body"].toString();

        QVersionNumber latest  = QVersionNumber::fromString(tag);
        QVersionNumber current = QVersionNumber::fromString(kCurrentVersion);

        m_latestVersion  = tag;
        m_downloadUrl    = url;
        m_releaseNotes   = notes;
        m_updateAvailable = (latest > current);

        emit checkComplete(m_updateAvailable, tag, url);
        if (m_updateAvailable) {
            qInfo() << "[UpdateChecker] New version available:" << tag;
        } else {
            qDebug() << "[UpdateChecker] Up to date (" << kCurrentVersion << ")";
        }
    });
}

void UpdateChecker::startAutoCheck(int intervalHours)
{
    m_timer->setInterval(intervalHours * 3600 * 1000);
    m_timer->start();
    check(); // immediate first check
}

void UpdateChecker::stopAutoCheck()
{
    m_timer->stop();
}

QString UpdateChecker::currentVersion() const { return kCurrentVersion; }
QString UpdateChecker::latestVersion()  const { return m_latestVersion; }
QString UpdateChecker::downloadUrl()    const { return m_downloadUrl; }
QString UpdateChecker::releaseNotes()   const { return m_releaseNotes; }
bool    UpdateChecker::isUpdateAvailable() const { return m_updateAvailable; }

// ─── UpdateManager ───────────────────────────────────────────────────────────

UpdateManager::UpdateManager(QObject* parent) : QObject(parent) {}
UpdateManager::~UpdateManager() = default;

void UpdateManager::setUpdateChecker(UpdateChecker* checker) { m_checker = checker; }

bool UpdateManager::installUpdateAndRestart() {
    if (!m_checker || !m_checker->isUpdateAvailable()) return false;
    QString url = m_checker->downloadUrl();
    if (url.isEmpty()) return false;
    QDesktopServices::openUrl(QUrl(url));
    return true;
}

bool UpdateManager::installUpdateAndQuit() {
    if (!m_checker || !m_checker->isUpdateAvailable()) return false;
    QString url = m_checker->downloadUrl();
    if (url.isEmpty()) return false;
    QDesktopServices::openUrl(QUrl(url));
    return true;
}

void UpdateManager::setUpdateInstallPolicy(const QString& policy) { m_policy = policy; emit updatePolicyChanged(); }
void UpdateManager::postponeUpdate(int days) { m_postponedUntil = QDateTime::currentDateTime().addDays(days); }

void UpdateManager::showChangelog() {
    if (!m_checker) return;
    QString notes = m_checker->releaseNotes();
    if (notes.isEmpty()) return;
    qDebug() << "Update changelog:" << notes;
}

// ─── UpdateUI ────────────────────────────────────────────────────────────────

UpdateUI::UpdateUI(QObject* parent) : QObject(parent) {}
UpdateUI::~UpdateUI() = default;

void UpdateUI::setUpdateChecker(UpdateChecker* checker) { m_checker = checker; }

void UpdateUI::showUpdateDialog() {
    if (!m_checker || !m_checker->isUpdateAvailable()) return;
    QString msg = QString("Update available: %1\nCurrent: %2\n\nDownload from:\n%3")
        .arg(m_checker->latestVersion(), m_checker->currentVersion(), m_checker->downloadUrl());
    qDebug() << msg;
}

void UpdateUI::showChangelogDialog() {
    if (!m_checker) return;
    QString notes = m_checker->releaseNotes();
    qDebug() << "Changelog:" << notes;
}

void UpdateUI::showSettingsDialog() {
    emit requested();
}

} // namespace ks
