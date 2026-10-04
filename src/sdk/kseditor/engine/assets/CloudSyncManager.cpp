#include "CloudSyncManager.h"
#include "../sys/LogManager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QUrlQuery>
#include <QEventLoop>
#include <algorithm>

namespace ks {

// Forward declarations for provider helpers
static QString gdriveFileId(QNetworkAccessManager* nam, const QString& token, const QString& path);
static bool gdriveUploadImpl(QNetworkAccessManager* nam, const QString& token,
    const QString& localPath, const QString& remotePath);
static bool gdriveDownload(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& localPath);
static bool gdriveDelete(QNetworkAccessManager* nam, const QString& token, const QString& remotePath);
static bool dropboxUpload(QNetworkAccessManager* nam, const QString& token,
    const QString& localPath, const QString& remotePath);
static bool dropboxDownload(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& localPath);
static bool dropboxDelete(QNetworkAccessManager* nam, const QString& token, const QString& remotePath);
static bool onedriveUpload(QNetworkAccessManager* nam, const QString& token,
    const QString& localPath, const QString& remotePath);
static bool onedriveDownload(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& localPath);
static bool onedriveDelete(QNetworkAccessManager* nam, const QString& token, const QString& remotePath);
static bool truenasUpload(QNetworkAccessManager* nam, const QString& token,
    const QString& localPath, const QString& remotePath, const QString& endpoint);
static bool truenasDownload(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& localPath, const QString& endpoint);
static bool truenasDelete(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& endpoint);
static bool awsS3Upload(QNetworkAccessManager* nam, const QString& token,
    const QString& localPath, const QString& remotePath, const QString& endpoint);
static bool aws3Download(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& localPath, const QString& endpoint);
static bool aws3Delete(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& endpoint);
static bool gcsUpload(QNetworkAccessManager* nam, const QString& token,
    const QString& localPath, const QString& remotePath, const QString& endpoint);
static bool gcsDownload(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& localPath, const QString& endpoint);
static bool gcsDelete(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& endpoint);
static bool azureBlobUpload(QNetworkAccessManager* nam, const QString& token,
    const QString& localPath, const QString& remotePath, const QString& endpoint);
static bool azureBlobDownload(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& localPath, const QString& endpoint);
static bool azureBlobDelete(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& endpoint);
static bool minioUpload(QNetworkAccessManager* nam, const QString& token,
    const QString& localPath, const QString& remotePath, const QString& endpoint);
static bool minioDownload(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& localPath, const QString& endpoint);
static bool minioDelete(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& endpoint);
static bool customS3Upload(QNetworkAccessManager* nam, const QString& token,
    const QString& localPath, const QString& remotePath, const QString& endpoint);
static bool customS3Download(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& localPath, const QString& endpoint);
static bool customS3Delete(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& endpoint);
static bool webdavUpload(QNetworkAccessManager* nam, const QString& token,
    const QString& localPath, const QString& remotePath, const QString& endpoint);
static bool webdavDownload(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& localPath, const QString& endpoint);
static bool webdavDelete(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& endpoint);
static bool nextcloudUpload(QNetworkAccessManager* nam, const QString& token,
    const QString& localPath, const QString& remotePath, const QString& endpoint);
static bool nextcloudDownload(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& localPath, const QString& endpoint);
static bool nextcloudDelete(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& endpoint);
static bool truenasEnsurePath(QNetworkAccessManager* nam, const QString& token,
    const QString& endpoint, const QString& path);

// Stub implementations for forward-declared static functions
static bool minioUpload(QNetworkAccessManager*, const QString&, const QString&, const QString&, const QString&) { return false; }
static bool minioDownload(QNetworkAccessManager*, const QString&, const QString&, const QString&, const QString&) { return false; }
static bool minioDelete(QNetworkAccessManager*, const QString&, const QString&, const QString&) { return false; }
static bool customS3Upload(QNetworkAccessManager*, const QString&, const QString&, const QString&, const QString&) { return false; }
static bool customS3Download(QNetworkAccessManager*, const QString&, const QString&, const QString&, const QString&) { return false; }
static bool customS3Delete(QNetworkAccessManager*, const QString&, const QString&, const QString&) { return false; }
static bool webdavUpload(QNetworkAccessManager*, const QString&, const QString&, const QString&, const QString&) { return false; }
static bool webdavDownload(QNetworkAccessManager*, const QString&, const QString&, const QString&, const QString&) { return false; }
static bool webdavDelete(QNetworkAccessManager*, const QString&, const QString&, const QString&) { return false; }
static bool nextcloudUpload(QNetworkAccessManager*, const QString&, const QString&, const QString&, const QString&) { return false; }
static bool nextcloudDownload(QNetworkAccessManager*, const QString&, const QString&, const QString&, const QString&) { return false; }
static bool nextcloudDelete(QNetworkAccessManager*, const QString&, const QString&, const QString&) { return false; }
static bool truenasEnsurePath(QNetworkAccessManager*, const QString&, const QString&, const QString&) { return false; }
static bool truenasUpload(QNetworkAccessManager*, const QString&, const QString&, const QString&, const QString&) { return false; }
static bool truenasDownload(QNetworkAccessManager*, const QString&, const QString&, const QString&, const QString&) { return false; }
static bool truenasDelete(QNetworkAccessManager*, const QString&, const QString&, const QString&) { return false; }
static bool awsS3Upload(QNetworkAccessManager*, const QString&, const QString&, const QString&, const QString&) { return false; }
static bool aws3Download(QNetworkAccessManager*, const QString&, const QString&, const QString&, const QString&) { return false; }
static bool aws3Delete(QNetworkAccessManager*, const QString&, const QString&, const QString&) { return false; }
static bool gcsUpload(QNetworkAccessManager*, const QString&, const QString&, const QString&, const QString&) { return false; }
static bool gcsDownload(QNetworkAccessManager*, const QString&, const QString&, const QString&, const QString&) { return false; }
static bool gcsDelete(QNetworkAccessManager*, const QString&, const QString&, const QString&) { return false; }
static bool azureBlobUpload(QNetworkAccessManager*, const QString&, const QString&, const QString&, const QString&) { return false; }
static bool azureBlobDownload(QNetworkAccessManager*, const QString&, const QString&, const QString&, const QString&) { return false; }
static bool azureBlobDelete(QNetworkAccessManager*, const QString&, const QString&, const QString&) { return false; }

CloudSyncManager::CloudSyncManager(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    m_autoSyncTimer = new QTimer(this);
    m_autoSyncTimer->setSingleShot(false);
    connect(m_autoSyncTimer, &QTimer::timeout, this, &CloudSyncManager::onAutoSyncTimer);
}

bool CloudSyncManager::configure(const CloudSyncConfig& config)
{
    m_config = config;
    m_authenticated = !config.accessToken.isEmpty();
    emit authStatusChanged(m_authenticated);

    if (config.autoSync && m_authenticated && config.syncIntervalMinutes > 0) {
        m_autoSyncTimer->start(config.syncIntervalMinutes * 60 * 1000);
    } else {
        m_autoSyncTimer->stop();
    }

    LOG_INFO("CloudSyncManager", QString("Configured provider: %1, autoSync: %2")
        .arg(static_cast<int>(config.provider)).arg(config.autoSync));
    return true;
}

void CloudSyncManager::syncNow()
{
    if (m_syncing || m_config.provider == CloudProviderType::None) return;
    m_syncing = true;
    emit syncStarted();

    // Run sync asynchronously via timer to avoid blocking
    QTimer::singleShot(0, this, &CloudSyncManager::performSync);
}

void CloudSyncManager::stopSync()
{
    if (m_syncing) {
        m_syncing = false;
        m_autoSyncTimer->stop();
        LOG_INFO("CloudSyncManager", "Sync stopped by user");
        emit syncCompleted(false, "Sync cancelled by user");
    }
}

void CloudSyncManager::performSync()
{
    if (m_config.localCachePath.isEmpty()) {
        m_syncing = false;
        emit syncCompleted(false, "No local cache path configured");
        return;
    }

    QDir localDir(m_config.localCachePath);
    if (!localDir.exists()) {
        localDir.mkpath(".");
    }

    int totalSteps = 3; // scan local, scan remote, resolve
    int currentStep = 0;

    // Step 1: Scan local changes
    updateProgress(0, "Scanning local files...");
    scanLocalChanges();
    currentStep++;
    updateProgress(currentStep * 100 / totalSteps, "Local scan complete");

    // Step 2: Scan remote changes
    scanRemoteChanges();
    currentStep++;
    updateProgress(currentStep * 100 / totalSteps, "Remote scan complete");

    // Step 3: Resolve differences
    resolveDifferences();
    currentStep++;
    updateProgress(100, "Sync complete");

    m_syncing = false;
    int uploaded = 0, downloaded = 0, conflicts = 0;
    for (const auto& f : m_files) {
        if (f.status == CloudSyncFile::NewLocal) uploaded++;
        else if (f.status == CloudSyncFile::NewRemote) downloaded++;
        else if (f.status == CloudSyncFile::Conflict) conflicts++;
    }

    QString msg = QString("Synced: %1 uploaded, %2 downloaded, %3 conflicts")
        .arg(uploaded).arg(downloaded).arg(conflicts);
    emit syncCompleted(true, msg);
    LOG_INFO("CloudSyncManager", msg);
}

void CloudSyncManager::scanLocalChanges()
{
    m_localChecksums.clear();
    m_localTimestamps.clear();

    QDir localDir(m_config.localCachePath);
    QDirIterator it(localDir, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString filePath = it.next();
        QFileInfo fi(filePath);
        if (!fi.isFile()) continue;

        QString relativePath = localDir.relativeFilePath(filePath);
        if (shouldExclude(relativePath)) continue;

        m_localChecksums[relativePath] = computeChecksum(filePath);
        m_localTimestamps[relativePath] = fi.lastModified();
    }
}

void CloudSyncManager::scanRemoteChanges()
{
    m_remoteChecksums.clear();
    m_remoteTimestamps.clear();
    m_files.clear();

    // For local sync, remote is the remotePath directory
    if (m_config.provider == CloudProviderType::Local && !m_config.remotePath.isEmpty()) {
        QDir remoteDir(m_config.remotePath);
        if (remoteDir.exists()) {
            QDirIterator it(remoteDir, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                QString filePath = it.next();
                QFileInfo fi(filePath);
                if (!fi.isFile()) continue;

                QString relativePath = remoteDir.relativeFilePath(filePath);
                if (shouldExclude(relativePath)) continue;

                m_remoteChecksums[relativePath] = computeChecksum(filePath);
                m_remoteTimestamps[relativePath] = fi.lastModified();
            }
        }
    } else if (m_config.provider == CloudProviderType::Dropbox && m_nam && !m_config.accessToken.isEmpty()) {
        // List remote files via Dropbox API
        QJsonObject body;
        body["path"] = "";
        body["recursive"] = true;
        body["include_media_info"] = false;
        body["include_deleted"] = false;

        QNetworkRequest req{QUrl("https://api.dropboxapi.com/2/files/list_folder")};
        req.setRawHeader("Authorization", ("Bearer " + m_config.accessToken).toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply* reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject resp = QJsonDocument::fromJson(reply->readAll()).object();
            QJsonArray entries = resp["entries"].toArray();
            for (const auto& entry : entries) {
                QJsonObject e = entry.toObject();
                if (e[".tag"].toString() != "file") continue;

                QString path = e["path_lower"].toString();
                if (path.startsWith("/")) path = path.mid(1);
                if (shouldExclude(path)) continue;

                m_remoteChecksums[path] = e["content_hash"].toString();
                m_remoteTimestamps[path] = QDateTime::fromString(
                    e["server_modified"].toString(), Qt::ISODate);
            }

            // Handle pagination
            while (resp["has_more"].toBool()) {
                QJsonObject contBody;
                contBody["cursor"] = resp["cursor"].toString();

                QNetworkRequest contReq{QUrl("https://api.dropboxapi.com/2/files/list_folder/continue")};
                contReq.setRawHeader("Authorization", ("Bearer " + m_config.accessToken).toUtf8());
                contReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

                QNetworkReply* contReply = m_nam->post(contReq, QJsonDocument(contBody).toJson(QJsonDocument::Compact));
                QObject::connect(contReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
                loop.exec();

                if (contReply->error() != QNetworkReply::NoError) break;
                resp = QJsonDocument::fromJson(contReply->readAll()).object();
                entries = resp["entries"].toArray();
                for (const auto& entry : entries) {
                    QJsonObject e = entry.toObject();
                    if (e[".tag"].toString() != "file") continue;
                    QString path = e["path_lower"].toString();
                    if (path.startsWith("/")) path = path.mid(1);
                    if (shouldExclude(path)) continue;
                    m_remoteChecksums[path] = e["content_hash"].toString();
                    m_remoteTimestamps[path] = QDateTime::fromString(
                        e["server_modified"].toString(), Qt::ISODate);
                }
                contReply->deleteLater();
            }
        }
        reply->deleteLater();

    } else if (m_config.provider == CloudProviderType::OneDrive && m_nam && !m_config.accessToken.isEmpty()) {
        // List remote files via Microsoft Graph API
        QString url = "https://graph.microsoft.com/v1.0/me/drive/root/children?$select=name,file,lastModifiedDateTime,size,@microsoft.graph.downloadUrl&$top=200";

        while (!url.isEmpty()) {
            QNetworkRequest req{QUrl(url)};
            req.setRawHeader("Authorization", ("Bearer " + m_config.accessToken).toUtf8());

            QNetworkReply* reply = m_nam->get(req);
            QEventLoop loop;
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            loop.exec();

            if (reply->error() != QNetworkReply::NoError) { reply->deleteLater(); break; }

            QJsonObject resp = QJsonDocument::fromJson(reply->readAll()).object();
            QJsonArray entries = resp["value"].toArray();
            for (const auto& entry : entries) {
                QJsonObject e = entry.toObject();
                if (e["file"].isNull()) continue; // skip folders

                QString name = e["name"].toString();
                if (shouldExclude(name)) continue;

                m_remoteChecksums[name] = e["lastModifiedDateTime"].toString();
                m_remoteTimestamps[name] = QDateTime::fromString(
                    e["lastModifiedDateTime"].toString(), Qt::ISODate);
            }

            url = resp["@odata.nextLink"].toString();
            reply->deleteLater();
        }
    } else if (m_config.provider == CloudProviderType::TrueNAS && m_nam && !m_config.accessToken.isEmpty() && !m_config.remotePath.isEmpty()) {
        // List remote files via TrueNAS S3 API
        // TrueNAS S3 is AWS-compatible; use ListObjectsV2
        QString bucket = m_config.remotePath;
        
        QUrl url(QString("https://%1").arg(bucket));
        QUrlQuery query;
        query.addQueryItem("prefix", "");
        query.addQueryItem("max-keys", "1000");
        url.setQuery(query);
        
        QNetworkRequest req{QUrl(url.toString())};
        // Use AWS V4 auth header format for TrueNAS S3
        req.setRawHeader("Authorization", ("AWS4-HMAC-SHA256 Credential=" + m_config.accessToken + "/20240101/us-east-1/s3/aws4_request").toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml");
        
        QNetworkReply* reply = m_nam->get(req);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        
        if (reply->error() == QNetworkReply::NoError) {
            QString xml = reply->readAll();
            // Parse XML for object keys and metadata
            QRegularExpression keyRegex("<Key>([^<]+)</Key>");
            QRegularExpressionMatchIterator it = keyRegex.globalMatch(xml);
            while (it.hasNext()) {
                QRegularExpressionMatch match = it.next();
                QString key = match.captured(1);
                if (key.isEmpty()) continue;
                if (shouldExclude(key)) continue;
                
                // Extract LastModified and ETag similarly...
                m_remoteChecksums[key] = QString(); // placeholder checksum
            }
        }
        reply->deleteLater();
    } else if (m_config.provider == CloudProviderType::AWS_S3 && m_nam && !m_config.accessToken.isEmpty() && !m_config.remotePath.isEmpty()) {
        // List remote files via AWS S3 API
        // S3 API: GET /?prefix=&max-keys=1000
        QString bucket = m_config.remotePath;
        QString prefix = QString(); // optional path prefix
        
        QUrlQuery query;
        query.addQueryItem("prefix", prefix);
        query.addQueryItem("max-keys", "1000");
        query.addQueryItem("delimiter", "");
        
        QUrl listUrl(QString("https://%1").arg(bucket));
        listUrl.setQuery(query);
        
        QNetworkRequest req{QUrl(listUrl.toString())};
        // Use AWS V4 auth
        req.setRawHeader("Authorization", ("AWS4-HMAC-SHA256 Credential=" + m_config.accessToken + "/20240101/us-east-1/s3/aws4_request").toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml");
        
        QNetworkReply* reply = m_nam->get(req);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        
        if (reply->error() == QNetworkReply::NoError) {
            QString xml = reply->readAll();
            // Parse XML for object keys
            QRegularExpression keyRegex("<Key>([^<]+)</Key>");
            QRegularExpressionMatchIterator it = keyRegex.globalMatch(xml);
            while (it.hasNext()) {
                QRegularExpressionMatch match = it.next();
                QString key = match.captured(1);
                if (key.isEmpty()) continue;
                if (shouldExclude(key)) continue;
                
                m_remoteChecksums[key] = QString(); // placeholder
            }
        }
        reply->deleteLater();
    } else if (m_config.provider == CloudProviderType::GoogleCloudStorage && m_nam && !m_config.accessToken.isEmpty() && !m_config.remotePath.isEmpty()) {
        // Google Cloud Storage uses JSON API similar to Google Drive
        // List objects with prefix
        QString bucket = m_config.remotePath;
        QString prefix = QString(); // optional path prefix
        
        QUrlQuery query;
        query.addQueryItem("prefix", prefix);
        query.addQueryItem("max-keys", "1000");
        
        QUrl listUrl("https://storage.googleapis.com/" + bucket);
        listUrl.setQuery(query);
        
        QNetworkRequest req{QUrl(listUrl.toString())};
        req.setRawHeader("Authorization", ("Bearer " + m_config.accessToken).toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        
        QNetworkReply* reply = m_nam->get(req);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        
        if (reply->error() == QNetworkReply::NoError) {
            QJsonObject resp = QJsonDocument::fromJson(reply->readAll()).object();
            QJsonArray items = resp["items"].toArray();
            for (const auto& item : items) {
                QJsonObject obj = item.toObject();
                QString name = obj["name"].toString();
                if (name.isEmpty() || shouldExclude(name)) continue;
                
                m_remoteChecksums[name] = obj["id"].toString();
                m_remoteTimestamps[name] = QDateTime::fromString(obj["updated"].toString(), Qt::ISODate);
            }
        }
        reply->deleteLater();
    } else if (m_config.provider == CloudProviderType::AzureBlob && m_nam && !m_config.accessToken.isEmpty() && !m_config.remotePath.isEmpty()) {
        // Azure Blob Storage uses REST API
        // List blobs with prefix
        QString container = m_config.remotePath;
        QString prefix = QString(); // optional path prefix
        
        QString url = QString("https://%1.blob.core.windows.net/%2?restype=container&comp=list&prefix=%3&maxresults=5000")
            .arg(container, container, QUrl::toPercentEncoding(prefix));
        
        QNetworkRequest req{QUrl(url)};
        req.setRawHeader("Authorization", ("SharedAccessSignature " + m_config.accessToken).toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml");
        
        QNetworkReply* reply = m_nam->get(req);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        
        if (reply->error() == QNetworkReply::NoError) {
            QString xml = reply->readAll();
            // Parse XML for blob names
            QRegularExpression nameRegex("<Name>([^<]+)</Name>");
            QRegularExpressionMatchIterator it = nameRegex.globalMatch(xml);
            while (it.hasNext()) {
                QRegularExpressionMatch match = it.next();
                QString name = match.captured(1);
                if (name.isEmpty() || shouldExclude(name)) continue;
                
                m_remoteChecksums[name] = QString(); // placeholder
                m_remoteTimestamps[name] = QDateTime::currentDateTime();
            }
        }
        reply->deleteLater();
    } else if (m_config.provider == CloudProviderType::MinIO && m_nam && !m_config.accessToken.isEmpty() && !m_config.remotePath.isEmpty()) {
        // MinIO is S3-compatible; use S3 API
        // Similar to AWS S3 but with MinIO endpoint
        QString bucket = m_config.remotePath;
        QString prefix = QString(); // optional path prefix
        
        QUrlQuery query;
        query.addQueryItem("prefix", prefix);
        query.addQueryItem("max-keys", "1000");
        query.addQueryItem("delimiter", "");
        
        // MinIO typically runs on port 9000, but we use the remotePath as hostname:port
        QString endpoint = m_config.remotePath; // e.g., "minio:9000" or "localhost:9000"
        QStringList endpointParts = endpoint.split(':');
        QString host = endpointParts.first();
        QString port = endpointParts.size() > 1 ? endpointParts.at(1) : "9000";
        
        QUrl listUrl(QString("http://%1:%2/%3").arg(host, port, bucket));
        listUrl.setQuery(query);
        
        QNetworkRequest req{QUrl(listUrl.toString())};
        // MinIO uses S3 V4 auth with access key/secret
        req.setRawHeader("Authorization", ("AWS4-HMAC-SHA256 Credential=" + m_config.accessToken + "/20240101/" + host + "/s3/aws4_request").toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml");
        
        QNetworkReply* reply = m_nam->get(req);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        
        if (reply->error() == QNetworkReply::NoError) {
            QString xml = reply->readAll();
            // Parse XML for object keys
            QRegularExpression keyRegex("<Key>([^<]+)</Key>");
            QRegularExpressionMatchIterator it = keyRegex.globalMatch(xml);
            while (it.hasNext()) {
                QRegularExpressionMatch match = it.next();
                QString key = match.captured(1);
                if (key.isEmpty() || shouldExclude(key)) continue;
                
                m_remoteChecksums[key] = QString(); // placeholder
            }
        }
        reply->deleteLater();
    } else if (m_config.provider == CloudProviderType::CustomS3Compatible && m_nam && !m_config.accessToken.isEmpty() && !m_config.remotePath.isEmpty()) {
        // Custom S3-Compatible storage (e.g., Wasabi, DigitalOcean Spaces)
        // Similar to AWS S3 but with custom endpoint
        QString bucket = m_config.remotePath;
        QString prefix = QString(); // optional path prefix
        QString endpoint = m_config.accessToken; // endpoint URL stored in access token or remotePath
        
        QUrlQuery query;
        query.addQueryItem("prefix", prefix);
        query.addQueryItem("max-keys", "1000");
        
        // Use remotePath as endpoint if it contains host:port, otherwise use default
        QString listUrlStr = QString("https://%1/%2").arg(endpoint.isEmpty() ? bucket : endpoint, bucket);
        QUrl listUrl(listUrlStr);
        listUrl.setQuery(query);
        
        QNetworkRequest req{QUrl(listUrl.toString())};
        // Custom S3 auth
        req.setRawHeader("Authorization", ("AWS4-HMAC-SHA256 Credential=" + m_config.accessToken + "/20240101/s3/aws4_request").toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml");
        
        QNetworkReply* reply = m_nam->get(req);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        
        if (reply->error() == QNetworkReply::NoError) {
            QString xml = reply->readAll();
            // Parse XML for object keys
            QRegularExpression keyRegex("<Key>([^<]+)</Key>");
            QRegularExpressionMatchIterator it = keyRegex.globalMatch(xml);
            while (it.hasNext()) {
                QRegularExpressionMatch match = it.next();
                QString key = match.captured(1);
                if (key.isEmpty() || shouldExclude(key)) continue;
                
                m_remoteChecksums[key] = QString(); // placeholder
            }
        }
        reply->deleteLater();
    } else if (m_config.provider == CloudProviderType::WebDAV && m_nam && !m_config.accessToken.isEmpty() && !m_config.remotePath.isEmpty()) {
        // WebDAV protocol
        // List collections/items at the remote path
        QString path = m_config.remotePath;
        if (!path.startsWith("/")) path = "/" + path;
        
        QUrl url(QString("https://%1/%2").arg(m_config.accessToken, QUrl::toPercentEncoding(path)));
        QUrlQuery query;
        query.addQueryItem("depth", "1");
        url.setQuery(query);
        
        QNetworkRequest req{QUrl(url.toString())};
        req.setRawHeader("Authorization", ("Bearer " + m_config.accessToken).toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/xml");
        
        QNetworkReply* reply = m_nam->get(req);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        
        if (reply->error() == QNetworkReply::NoError) {
            QString xml = reply->readAll();
            // Parse XML for href elements
            QRegularExpression hrefRegex("<d:href>([^<]+)</d:href>");
            QRegularExpressionMatchIterator it = hrefRegex.globalMatch(xml);
            while (it.hasNext()) {
                QRegularExpressionMatch match = it.next();
                QString href = match.captured(1);
                if (href.isEmpty() || shouldExclude(href)) continue;
                
                m_remoteChecksums[href] = QString(); // placeholder
                m_remoteTimestamps[href] = QDateTime::currentDateTime();
            }
        }
        reply->deleteLater();
    } else if (m_config.provider == CloudProviderType::Nextcloud && m_nam && !m_config.accessToken.isEmpty() && !m_config.remotePath.isEmpty()) {
        // Nextcloud API (based on WebDAV + JSON)
        // List files at the path
        QString path = m_config.remotePath;
        if (!path.startsWith("/")) path = "/" + path;
        
        QUrl url(QString("https://%1/remote.php/dav/files/%2/%3")
            .arg(m_config.accessToken, m_config.remotePath, QUrl::toPercentEncoding(path)));
        
        QUrlQuery query;
        query.addQueryItem("format", "json");
        url.setQuery(query);
        
        QNetworkRequest req{url};
        req.setRawHeader("Authorization", ("Bearer " + m_config.accessToken).toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        
        QNetworkReply* reply = m_nam->get(req);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            QJsonObject resp = doc.object();
            QJsonArray entries = resp["ocs"].toObject()["data"].toArray();
            for (const auto& entry : entries) {
                QJsonObject obj = entry.toObject();
                QString name = obj["name"].toString();
                if (name.isEmpty() || shouldExclude(name)) continue;
                
                m_remoteChecksums[name] = obj["etag"].toString();
                m_remoteTimestamps[name] = QDateTime::fromString(obj["mtime"].toString(), Qt::ISODate);
            }
        }
        reply->deleteLater();
    }

    // Compare local vs remote
    QSet<QString> allPaths;
    for (auto it = m_localChecksums.begin(); it != m_localChecksums.end(); ++it)
        allPaths.insert(it.key());
    for (auto it = m_remoteChecksums.begin(); it != m_remoteChecksums.end(); ++it)
        allPaths.insert(it.key());

    for (const QString& path : allPaths) {
        CloudSyncFile file;
        file.remotePath = path;
        file.localPath = QDir(m_config.localCachePath).filePath(path);

        bool hasLocal = m_localChecksums.contains(path);
        bool hasRemote = m_remoteChecksums.contains(path);

        if (hasLocal && !hasRemote) {
            file.status = CloudSyncFile::NewLocal;
            file.checksum = m_localChecksums[path];
            file.lastModified = m_localTimestamps[path];
        } else if (!hasLocal && hasRemote) {
            file.status = CloudSyncFile::NewRemote;
            file.checksum = m_remoteChecksums[path];
            file.lastModified = m_remoteTimestamps[path];
        } else if (m_localChecksums[path] != m_remoteChecksums[path]) {
            file.status = CloudSyncFile::Conflict;
            file.checksum = m_localChecksums[path];
            file.lastModified = m_localTimestamps[path];
            if (m_remoteTimestamps[path] > m_localTimestamps[path]) {
                file.checksum = m_remoteChecksums[path];
                file.lastModified = m_remoteTimestamps[path];
            }
        } else {
            file.status = CloudSyncFile::Synced;
            file.checksum = m_localChecksums[path];
            file.lastModified = m_localTimestamps[path];
        }

        m_files.append(file);
    }
}

void CloudSyncManager::resolveDifferences()
{
    int total = m_files.size();
    int processed = 0;

    for (auto& file : m_files) {
        if (!m_syncing) break;

        processed++;
        updateProgress(processed * 100 / total, file.remotePath);

        if (file.status == CloudSyncFile::NewLocal) {
            if (uploadFile(file.localPath, file.remotePath)) {
                file.status = CloudSyncFile::Synced;
                logSync(file.remotePath, "upload", true);
                emit fileSynced(file.remotePath, true);
            } else {
                logSync(file.remotePath, "upload", false, "Upload failed");
                emit syncError(file.remotePath, "Upload failed");
            }
        } else if (file.status == CloudSyncFile::NewRemote) {
            if (downloadFile(file.remotePath, file.localPath)) {
                file.status = CloudSyncFile::Synced;
                logSync(file.remotePath, "download", true);
                emit fileSynced(file.remotePath, false);
            } else {
                logSync(file.remotePath, "download", false, "Download failed");
                emit syncError(file.remotePath, "Download failed");
            }
        } else if (file.status == CloudSyncFile::Conflict) {
            emit conflictDetected(file.remotePath);
            logSync(file.remotePath, "conflict", false, "Conflict detected");
        }
    }
}

// ── Provider dispatch ─────────────────────────────────────────────────

bool CloudSyncManager::uploadFile(const QString& localPath, const QString& remotePath)
{
    QFile file(localPath);
    if (!file.exists()) return false;

    if (m_config.provider == CloudProviderType::Local && !m_config.remotePath.isEmpty()) {
        QString destPath = QDir(m_config.remotePath).filePath(remotePath);
        QFileInfo fi(destPath);
        QDir().mkpath(fi.absolutePath());
        return QFile::copy(localPath, destPath);
    }

    if (!m_nam || m_config.accessToken.isEmpty()) {
        logSync(remotePath, "upload", false, "No access token configured");
        return false;
    }

    switch (m_config.provider) {
        case CloudProviderType::GoogleDrive:
            return gdriveUploadImpl(m_nam, m_config.accessToken, localPath, remotePath);
        case CloudProviderType::Dropbox:
            return dropboxUpload(m_nam, m_config.accessToken, localPath, remotePath);
        case CloudProviderType::OneDrive:
            return onedriveUpload(m_nam, m_config.accessToken, localPath, remotePath);
        case CloudProviderType::TrueNAS:
            return truenasUpload(m_nam, m_config.accessToken, localPath, remotePath,
                m_config.remotePath);
        case CloudProviderType::AWS_S3:
            return awsS3Upload(m_nam, m_config.accessToken, localPath, remotePath,
                m_config.remotePath);
        case CloudProviderType::GoogleCloudStorage:
            return gcsUpload(m_nam, m_config.accessToken, localPath, remotePath,
                m_config.remotePath);
        case CloudProviderType::AzureBlob:
            return azureBlobUpload(m_nam, m_config.accessToken, localPath, remotePath,
                m_config.remotePath);
        case CloudProviderType::MinIO:
            return minioUpload(m_nam, m_config.accessToken, localPath, remotePath,
                m_config.remotePath);
        case CloudProviderType::CustomS3Compatible:
            return customS3Upload(m_nam, m_config.accessToken, localPath, remotePath,
                m_config.remotePath);
        case CloudProviderType::WebDAV:
            return webdavUpload(m_nam, m_config.accessToken, localPath, remotePath,
                m_config.remotePath);
        case CloudProviderType::Nextcloud:
            return nextcloudUpload(m_nam, m_config.accessToken, localPath, remotePath,
                m_config.remotePath);
        default:
            return false;
    }
}

bool CloudSyncManager::downloadFile(const QString& remotePath, const QString& localPath)
{
    QFileInfo fi(localPath);
    QDir().mkpath(fi.absolutePath());

    if (m_config.provider == CloudProviderType::Local && !m_config.remotePath.isEmpty()) {
        QString sourcePath = QDir(m_config.remotePath).filePath(remotePath);
        if (!QFile::exists(sourcePath)) return false;
        if (QFile::exists(localPath))
            QFile::remove(localPath);
        return QFile::copy(sourcePath, localPath);
    }

    if (!m_nam) return false;

    switch (m_config.provider) {
        case CloudProviderType::GoogleDrive:
            return gdriveDownload(m_nam, m_config.accessToken, remotePath, localPath);
        case CloudProviderType::Dropbox:
            return dropboxDownload(m_nam, m_config.accessToken, remotePath, localPath);
        case CloudProviderType::OneDrive:
            return onedriveDownload(m_nam, m_config.accessToken, remotePath, localPath);
        case CloudProviderType::TrueNAS:
            return truenasDownload(m_nam, m_config.accessToken, remotePath, localPath,
                m_config.remotePath);
        case CloudProviderType::AWS_S3:
            return aws3Download(m_nam, m_config.accessToken, remotePath, localPath,
                m_config.remotePath);
        case CloudProviderType::GoogleCloudStorage:
            return gcsDownload(m_nam, m_config.accessToken, remotePath, localPath,
                m_config.remotePath);
        case CloudProviderType::AzureBlob:
            return azureBlobDownload(m_nam, m_config.accessToken, remotePath, localPath,
                m_config.remotePath);
        case CloudProviderType::MinIO:
            return minioDownload(m_nam, m_config.accessToken, remotePath, localPath,
                m_config.remotePath);
        case CloudProviderType::CustomS3Compatible:
            return customS3Download(m_nam, m_config.accessToken, remotePath, localPath,
                m_config.remotePath);
        case CloudProviderType::WebDAV:
            return webdavDownload(m_nam, m_config.accessToken, remotePath, localPath,
                m_config.remotePath);
        case CloudProviderType::Nextcloud:
            return nextcloudDownload(m_nam, m_config.accessToken, remotePath, localPath,
                m_config.remotePath);
        default:
            return false;
    }
}

bool CloudSyncManager::deleteRemote(const QString& remotePath)
{
    if (m_config.provider == CloudProviderType::Local && !m_config.remotePath.isEmpty()) {
        QString targetPath = QDir(m_config.remotePath).filePath(remotePath);
        return QFile::remove(targetPath);
    }

    if (!m_nam) return false;

    switch (m_config.provider) {
        case CloudProviderType::GoogleDrive:
            return gdriveDelete(m_nam, m_config.accessToken, remotePath);
        case CloudProviderType::Dropbox:
            return dropboxDelete(m_nam, m_config.accessToken, remotePath);
        case CloudProviderType::OneDrive:
            return onedriveDelete(m_nam, m_config.accessToken, remotePath);
        case CloudProviderType::TrueNAS:
            return truenasDelete(m_nam, m_config.accessToken, remotePath, m_config.remotePath);
        case CloudProviderType::AWS_S3:
            return aws3Delete(m_nam, m_config.accessToken, remotePath, m_config.remotePath);
        case CloudProviderType::GoogleCloudStorage:
            return gcsDelete(m_nam, m_config.accessToken, remotePath, m_config.remotePath);
        case CloudProviderType::AzureBlob:
            return azureBlobDelete(m_nam, m_config.accessToken, remotePath, m_config.remotePath);
        case CloudProviderType::MinIO:
            return minioDelete(m_nam, m_config.accessToken, remotePath, m_config.remotePath);
        case CloudProviderType::CustomS3Compatible:
            return customS3Delete(m_nam, m_config.accessToken, remotePath, m_config.remotePath);
        case CloudProviderType::WebDAV:
            return webdavDelete(m_nam, m_config.accessToken, remotePath, m_config.remotePath);
        case CloudProviderType::Nextcloud:
            return nextcloudDelete(m_nam, m_config.accessToken, remotePath, m_config.remotePath);
        default:
            return false;
    }
}

// ── Google Drive helpers ──────────────────────────────────────────────

static QString gdriveFileId(QNetworkAccessManager* nam, const QString& token, const QString& path)
{
    QStringList parts = path.split('/', Qt::SkipEmptyParts);
    QString parentId = "root";
    for (const QString& part : parts) {
        QUrlQuery query;
        query.addQueryItem("q", QString("'%1' in parents and name='%2' and trashed=false")
            .arg(parentId, part));
        query.addQueryItem("fields", "files(id)");
        query.addQueryItem("pageSize", "1");

        QNetworkRequest req{QUrl("https://www.googleapis.com/drive/v3/files?" + query.toString())};
        req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());

        QNetworkReply* reply = nam->get(req);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonArray files = doc.object()["files"].toArray();
        if (files.isEmpty()) return {};
        parentId = files[0].toObject()["id"].toString();
        reply->deleteLater();
    }
    return parentId;
}

static bool gdriveUploadImpl(QNetworkAccessManager* nam, const QString& token,
    const QString& localPath, const QString& remotePath)
{
    if (!QFile::exists(localPath)) return false;

    QFile* file = new QFile(localPath);
    if (!file->open(QIODevice::ReadOnly)) { delete file; return false; }

    QString parentId = gdriveFileId(nam, token, remotePath);
    QString fileName = QFileInfo(remotePath).fileName();

    QJsonObject metadata;
    metadata["name"] = fileName;
    if (!parentId.isEmpty() && parentId != "root")
        metadata["parents"] = QJsonArray{parentId};

    QHttpMultiPart* multi = new QHttpMultiPart(QHttpMultiPart::RelatedType);
    QHttpPart metaPart;
    metaPart.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=UTF-8");
    metaPart.setBody(QJsonDocument(metadata).toJson(QJsonDocument::Compact));
    multi->append(metaPart);

    QHttpPart dataPart;
    dataPart.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    dataPart.setBodyDevice(file);
    file->setParent(multi);
    multi->append(dataPart);

    QNetworkRequest req{QUrl("https://www.googleapis.com/upload/drive/v3/files?uploadType=multipart")};
    req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());

    QNetworkReply* reply = nam->post(req, multi);
    multi->setParent(reply);

    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    return ok;
}

static bool gdriveDownload(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& localPath)
{
    QString fileId = gdriveFileId(nam, token, remotePath);
    if (fileId.isEmpty()) return false;

    QNetworkRequest req{QUrl("https://www.googleapis.com/drive/v3/files/" + fileId + "?alt=media")};
    req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());

    QNetworkReply* reply = nam->get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) { reply->deleteLater(); return false; }

    QFile outFile(localPath);
    if (!outFile.open(QIODevice::WriteOnly)) { reply->deleteLater(); return false; }
    outFile.write(reply->readAll());
    outFile.close();
    reply->deleteLater();
    return true;
}

static bool gdriveDelete(QNetworkAccessManager* nam, const QString& token, const QString& remotePath)
{
    QString fileId = gdriveFileId(nam, token, remotePath);
    if (fileId.isEmpty()) return false;

    QNetworkRequest req{QUrl("https://www.googleapis.com/drive/v3/files/" + fileId)};
    req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());

    QNetworkReply* reply = nam->deleteResource(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    return ok;
}

// ── Dropbox helpers ───────────────────────────────────────────────────

static bool dropboxEnsurePath(QNetworkAccessManager* nam, const QString& token, const QString& path)
{
    QStringList parts = path.split('/', Qt::SkipEmptyParts);
    QString current;
    for (const QString& part : parts) {
        current += "/" + part;
        QJsonObject body;
        body["path"] = current;
        body["autorename"] = false;

        QNetworkRequest req{QUrl("https://api.dropboxapi.com/2/files/get_metadata")};
        req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply* reply = nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        bool exists = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();

        if (exists) continue;

        // Create folder
        QJsonObject createBody;
        createBody["path"] = current;
        createBody["autorename"] = false;

        QNetworkRequest createReq{QUrl("https://api.dropboxapi.com/2/files/create_folder_v2")};
        createReq.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
        createReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply* createReply = nam->post(createReq, QJsonDocument(createBody).toJson(QJsonDocument::Compact));
        QEventLoop createLoop;
        QObject::connect(createReply, &QNetworkReply::finished, &createLoop, &QEventLoop::quit);
        createLoop.exec();

        bool created = createReply->error() == QNetworkReply::NoError;
        createReply->deleteLater();
        if (!created) return false;
    }
    return true;
}

static bool dropboxUpload(QNetworkAccessManager* nam, const QString& token,
    const QString& localPath, const QString& remotePath)
{
    QString parentPath = QFileInfo(remotePath).path();
    if (parentPath.isEmpty() || parentPath == ".")
        parentPath = "";
    else if (!parentPath.startsWith("/"))
        parentPath = "/" + parentPath;

    if (!parentPath.isEmpty() && !dropboxEnsurePath(nam, token, parentPath))
        return false;

    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray data = file.readAll();
    file.close();

    QString dropboxPath = remotePath.startsWith("/") ? remotePath : "/" + remotePath;

    QJsonObject arg;
    arg["path"] = dropboxPath;
    arg["mode"] = "overwrite";
    arg["autorename"] = false;
    arg["mute"] = false;

    QNetworkRequest req{QUrl("https://content.dropboxapi.com/2/files/upload")};
    req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
    req.setRawHeader("Dropbox-API-Arg", QJsonDocument(arg).toJson(QJsonDocument::Compact));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");

    QNetworkReply* reply = nam->post(req, data);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    return ok;
}

static bool dropboxDownload(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& localPath)
{
    QString dropboxPath = remotePath.startsWith("/") ? remotePath : "/" + remotePath;

    QJsonObject arg;
    arg["path"] = dropboxPath;

    QNetworkRequest req{QUrl("https://content.dropboxapi.com/2/files/download")};
    req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
    req.setRawHeader("Dropbox-API-Arg", QJsonDocument(arg).toJson(QJsonDocument::Compact));

    QNetworkReply* reply = nam->post(req, QByteArray());
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) { reply->deleteLater(); return false; }

    QFile outFile(localPath);
    if (!outFile.open(QIODevice::WriteOnly)) { reply->deleteLater(); return false; }
    outFile.write(reply->readAll());
    outFile.close();
    reply->deleteLater();
    return true;
}

static bool dropboxDelete(QNetworkAccessManager* nam, const QString& token, const QString& remotePath)
{
    QString dropboxPath = remotePath.startsWith("/") ? remotePath : "/" + remotePath;

    QJsonObject body;
    body["path"] = dropboxPath;

    QNetworkRequest req{QUrl("https://api.dropboxapi.com/2/files/delete_v2")};
    req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply* reply = nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    return ok;
}

// ── OneDrive (Microsoft Graph) helpers ────────────────────────────────

static bool onedriveEnsurePath(QNetworkAccessManager* nam, const QString& token, const QString& path)
{
    QStringList parts = path.split('/', Qt::SkipEmptyParts);
    QString current;
    for (const QString& part : parts) {
        if (!current.isEmpty()) current += "/";
        current += part;

        QString escaped = QUrl::toPercentEncoding(current);
QNetworkRequest req{QUrl("https://graph.microsoft.com/v1.0/me/drive/root:/" + escaped)};
        req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());

        QNetworkReply* reply = nam->get(req);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        bool exists = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        if (exists) continue;

        // Create folder
        QJsonObject folder;
        folder["@microsoft.graph.conflictBehavior"] = "fail";
        folder["name"] = part;
        QJsonObject folderFacet;
        folder["folder"] = folderFacet;

        QString parentEscaped;
        QString parentPath = current.left(current.lastIndexOf('/'));
        if (parentPath.isEmpty())
            parentEscaped = "";
        else
            parentEscaped = QUrl::toPercentEncoding(parentPath);

        QString url = parentEscaped.isEmpty()
            ? "https://graph.microsoft.com/v1.0/me/drive/root/children"
            : "https://graph.microsoft.com/v1.0/me/drive/root:/" + parentEscaped + ":/children";

        QNetworkRequest createReq{QUrl(url)};
        createReq.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
        createReq.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply* createReply = nam->post(createReq, QJsonDocument(folder).toJson(QJsonDocument::Compact));
        QEventLoop createLoop;
        QObject::connect(createReply, &QNetworkReply::finished, &createLoop, &QEventLoop::quit);
        createLoop.exec();

        bool created = createReply->error() == QNetworkReply::NoError;
        createReply->deleteLater();
        if (!created) return false;
    }
    return true;
}

static bool onedriveUpload(QNetworkAccessManager* nam, const QString& token,
    const QString& localPath, const QString& remotePath)
{
    QString parentPath = QFileInfo(remotePath).path();
    if (!parentPath.isEmpty() && parentPath != "." && !onedriveEnsurePath(nam, token, parentPath))
        return false;

    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray data = file.readAll();
    file.close();

    QString escaped = QUrl::toPercentEncoding(remotePath);
    QNetworkRequest req{QUrl("https://graph.microsoft.com/v1.0/me/drive/root:/" + escaped + ":/content")};
    req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");

    QNetworkReply* reply = nam->put(req, data);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    return ok;
}

static bool onedriveDownload(QNetworkAccessManager* nam, const QString& token,
    const QString& remotePath, const QString& localPath)
{
    QString escaped = QUrl::toPercentEncoding(remotePath);
    QNetworkRequest req{QUrl("https://graph.microsoft.com/v1.0/me/drive/root:/" + escaped)};
    req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());

    QNetworkReply* reply = nam->get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) { reply->deleteLater(); return false; }

    QJsonObject meta = QJsonDocument::fromJson(reply->readAll()).object();
    QString downloadUrl = meta["@microsoft.graph.downloadUrl"].toString();
    reply->deleteLater();

    if (downloadUrl.isEmpty()) return false;

    QNetworkRequest dlReq{QUrl(downloadUrl)};
    QNetworkReply* dlReply = nam->get(dlReq);
    QObject::connect(dlReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    if (dlReply->error() != QNetworkReply::NoError) { dlReply->deleteLater(); return false; }

    QFile outFile(localPath);
    if (!outFile.open(QIODevice::WriteOnly)) { dlReply->deleteLater(); return false; }
    outFile.write(dlReply->readAll());
    outFile.close();
    dlReply->deleteLater();
    return true;
}

static bool onedriveDelete(QNetworkAccessManager* nam, const QString& token, const QString& remotePath)
{
    QString escaped = QUrl::toPercentEncoding(remotePath);
    QNetworkRequest req{QUrl("https://graph.microsoft.com/v1.0/me/drive/root:/" + escaped)};
    req.setRawHeader("Authorization", ("Bearer " + token).toUtf8());

    QNetworkReply* reply = nam->deleteResource(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    bool ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();
    return ok;
}

QString CloudSyncManager::computeChecksum(const QString& filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return QString();

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return QString();
    return hash.result().toHex();
}

bool CloudSyncManager::shouldExclude(const QString& path) const
{
    for (const auto& excluded : m_excludedPaths) {
        if (path.startsWith(excluded)) return true;
    }
    return false;
}

QList<CloudSyncFile> CloudSyncManager::getSyncStatus() const
{
    return m_files;
}

QList<CloudSyncFile> CloudSyncManager::getConflicts() const
{
    QList<CloudSyncFile> conflicts;
    for (const auto& f : m_files) {
        if (f.status == CloudSyncFile::Conflict) {
            conflicts.append(f);
        }
    }
    return conflicts;
}

int CloudSyncManager::getPendingCount() const
{
    int count = 0;
    for (const auto& f : m_files) {
        if (f.status != CloudSyncFile::Synced && f.status != CloudSyncFile::Unknown) {
            count++;
        }
    }
    return count;
}

bool CloudSyncManager::resolveConflict(const QString& remotePath, ConflictResolution resolution)
{
    for (auto& file : m_files) {
        if (file.remotePath == remotePath && file.status == CloudSyncFile::Conflict) {
            switch (resolution) {
                case KeepLocal:
                    if (uploadFile(file.localPath, file.remotePath)) {
                        file.status = CloudSyncFile::Synced;
                        logSync(remotePath, "upload", true, "Kept local version");
                        return true;
                    }
                    break;
                case KeepRemote:
                    if (downloadFile(file.remotePath, file.localPath)) {
                        file.status = CloudSyncFile::Synced;
                        logSync(remotePath, "download", true, "Kept remote version");
                        return true;
                    }
                    break;
                case Skip:
                    file.status = CloudSyncFile::Synced;
                    logSync(remotePath, "skip", true, "Skipped conflict");
                    return true;
                case UseNewest: {
                    QFileInfo localFi(file.localPath);
                    if (localFi.lastModified() > file.lastModified) {
                        if (uploadFile(file.localPath, file.remotePath)) {
                            file.status = CloudSyncFile::Synced;
                            logSync(remotePath, "upload", true, "Used newer local version");
                            return true;
                        }
                    } else {
                        if (downloadFile(file.remotePath, file.localPath)) {
                            file.status = CloudSyncFile::Synced;
                            logSync(remotePath, "download", true, "Used newer remote version");
                            return true;
                        }
                    }
                    break;
                }
            }
        }
    }
    return false;
}

void CloudSyncManager::setExcludedPaths(const QStringList& paths)
{
    m_excludedPaths = paths;
}

void CloudSyncManager::onAutoSyncTimer()
{
    if (m_authenticated && !m_syncing) {
        syncNow();
    }
}

void CloudSyncManager::updateProgress(int percent, const QString& file)
{
    emit syncProgress(qBound(0, percent, 100), file);
}

void CloudSyncManager::logSync(const QString& path, const QString& action, bool success, const QString& msg)
{
    SyncEntry entry;
    entry.timestamp = QDateTime::currentDateTime();
    entry.path = path;
    entry.action = action;
    entry.success = success;
    entry.message = msg;
    m_syncHistory.append(entry);

    while (m_syncHistory.size() > MAX_HISTORY) {
        m_syncHistory.removeFirst();
    }
}

QJsonObject CloudSyncManager::serialize() const
{
    QJsonObject data;
    data["provider"] = static_cast<int>(m_config.provider);
    data["remotePath"] = m_config.remotePath;
    data["localCachePath"] = m_config.localCachePath;
    data["accessToken"] = m_config.accessToken;
    data["refreshToken"] = m_config.refreshToken;
    data["autoSync"] = m_config.autoSync;
    data["syncIntervalMinutes"] = m_config.syncIntervalMinutes;
    data["syncOnStartup"] = m_config.syncOnStartup;
    data["excludedPaths"] = QJsonArray::fromStringList(m_excludedPaths);

    QJsonArray historyArr;
    for (const auto& entry : m_syncHistory) {
        QJsonObject hObj;
        hObj["timestamp"] = entry.timestamp.toString(Qt::ISODate);
        hObj["path"] = entry.path;
        hObj["action"] = entry.action;
        hObj["success"] = entry.success;
        hObj["message"] = entry.message;
        historyArr.append(hObj);
    }
    data["syncHistory"] = historyArr;
    return data;
}

void CloudSyncManager::deserialize(const QJsonObject& data)
{
    m_config.provider = static_cast<CloudProviderType>(data["provider"].toInt(0));
    m_config.remotePath = data["remotePath"].toString();
    m_config.localCachePath = data["localCachePath"].toString();
    m_config.accessToken = data["accessToken"].toString();
    m_config.refreshToken = data["refreshToken"].toString();
    m_config.autoSync = data["autoSync"].toBool(false);
    m_config.syncIntervalMinutes = data["syncIntervalMinutes"].toInt(30);
    m_config.syncOnStartup = data["syncOnStartup"].toBool(false);
    m_authenticated = !m_config.accessToken.isEmpty();

    m_excludedPaths.clear();
    for (const auto& v : data["excludedPaths"].toArray()) {
        m_excludedPaths.append(v.toString());
    }

    m_syncHistory.clear();
    for (const auto& v : data["syncHistory"].toArray()) {
        QJsonObject hObj = v.toObject();
        SyncEntry entry;
        entry.timestamp = QDateTime::fromString(hObj["timestamp"].toString(), Qt::ISODate);
        entry.path = hObj["path"].toString();
        entry.action = hObj["action"].toString();
        entry.success = hObj["success"].toBool();
        entry.message = hObj["message"].toString();
        m_syncHistory.append(entry);
    }
}

} // namespace ks
