#include "AutoSave.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>
#include <QDebug>
#include <QStandardPaths>

namespace ks {

// ============================================================================
// Document
// ============================================================================

Document::Document(QObject* parent)
    : QObject(parent)
    , m_id(QUuid::createUuid().toString(QUuid::WithoutBraces))
{}

Document::~Document() {}

bool Document::load(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Document::load – cannot open" << path;
        return false;
    }
    m_path = path;
    m_name = QFileInfo(path).baseName();
    m_modified = false;
    emit pathChanged(path);
    return true;
}

bool Document::save(const QString& path)
{
    QString savePath = path.isEmpty() ? m_path : path;
    if (savePath.isEmpty()) return false;
    m_path = savePath;
    m_modified = false;
    emit saved();
    return true;
}

// ============================================================================
// DocumentManager
// ============================================================================

DocumentManager::DocumentManager(QObject* parent)
    : QObject(parent)
{}

DocumentManager::~DocumentManager() {}

void DocumentManager::addDocument(Document* doc)
{
    if (!doc) return;
    m_documents.insert(doc->getId(), doc);
    emit documentAdded(doc);
    emit unsavedChangesChanged(hasUnsavedChanges());
}

void DocumentManager::removeDocument(const QString& docId)
{
    if (!m_documents.contains(docId)) return;
    if (m_activeDocument && m_activeDocument->getId() == docId)
        m_activeDocument = nullptr;
    m_documents.remove(docId);
    emit documentRemoved(docId);
    emit unsavedChangesChanged(hasUnsavedChanges());
}

Document* DocumentManager::getDocument(const QString& docId) const
{
    return m_documents.value(docId, nullptr);
}

void DocumentManager::setActiveDocument(Document* doc)
{
    if (m_activeDocument == doc) return;
    m_activeDocument = doc;
    emit activeDocumentChanged(doc);
}

bool DocumentManager::hasUnsavedChanges() const
{
    for (auto* doc : m_documents)
        if (doc->isModified()) return true;
    return false;
}

bool DocumentManager::saveAll()
{
    bool ok = true;
    for (auto* doc : m_documents)
        if (doc->isModified()) ok &= doc->save();
    return ok;
}

bool DocumentManager::closeAll()
{
    m_documents.clear();
    m_activeDocument = nullptr;
    return true;
}

QVector<Document*> DocumentManager::getUnsavedDocuments() const
{
    QVector<Document*> result;
    for (auto* doc : m_documents)
        if (doc->isModified()) result << doc;
    return result;
}

// ============================================================================
// AutoSave
// ============================================================================

AutoSave::AutoSave(QObject* parent)
    : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &AutoSave::performAutoSave);
    m_timer.setInterval(m_interval * 1000);
}

AutoSave::~AutoSave() {}

void AutoSave::setDocument(Document* doc)
{
    m_document = doc;
    if (m_document && m_enabled) m_timer.start();
}

void AutoSave::setBackupDirectory(const QString& dir)
{
    m_backupDir = dir;
    QDir().mkpath(dir);
}

void AutoSave::setInterval(int seconds)
{
    m_interval = qMax(10, seconds);
    m_timer.setInterval(m_interval * 1000);
}

void AutoSave::setMaxBackups(int max)
{
    m_maxBackups = qMax(1, max);
}

void AutoSave::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (enabled) m_timer.start();
    else          m_timer.stop();
}

void AutoSave::saveNow()
{
    if (m_document) m_document->save();
    emit autoSaveTriggered();
}

void AutoSave::saveBackup()
{
    if (!m_document || m_backupDir.isEmpty()) return;
    QString path = generateBackupPath();
    QFile::copy(m_document->getPath(), path);
    emit backupCreated(path);
}

bool AutoSave::hasRecoveryPoint(const QString& documentId) const
{
    QDir dir(m_backupDir);
    return !dir.entryList(QStringList() << documentId + "_*.bak").isEmpty();
}

QVector<RecoveryPoint> AutoSave::getRecoveryPoints(const QString& documentId) const
{
    QVector<RecoveryPoint> points;
    QDir dir(m_backupDir);
    const auto files = dir.entryInfoList(QStringList() << documentId + "_*.bak",
                                         QDir::Files, QDir::Time);
    int version = files.size();
    for (const auto& fi : files) {
        RecoveryPoint rp;
        rp.documentId  = documentId;
        rp.backupPath  = fi.absoluteFilePath();
        rp.timestamp   = fi.lastModified().toString(Qt::ISODate);
        rp.version     = version--;
        rp.isValid     = fi.size() > 0;
        points << rp;
    }
    return points;
}

bool AutoSave::recover(const QString& documentId, int version)
{
    const auto points = getRecoveryPoints(documentId);
    for (const auto& rp : points) {
        if (rp.version == version) {
            bool ok = QFile::copy(rp.backupPath, rp.filePath);
            if (ok) emit recoveryComplete(documentId);
            return ok;
        }
    }
    return false;
}

bool AutoSave::recoverLatest(const QString& documentId)
{
    return recover(documentId, 1);
}

void AutoSave::cleanupOldBackups()
{
    if (!m_document) return;
    auto points = getRecoveryPoints(m_document->getId());
    while (points.size() > m_maxBackups) {
        QFile::remove(points.last().backupPath);
        points.removeLast();
    }
}

void AutoSave::performAutoSave()
{
    if (!m_document || !m_document->isModified()) return;
    saveBackup();
    cleanupOldBackups();
    emit autoSaveTriggered();
}

void AutoSave::setAutoRecoveryData(const QJsonObject& sessionData)
{
    if (!m_document) return;
    
    QString docId = m_document->getId();
    QString docPath = m_document->getPath();
    
    if (!m_backupDir.isEmpty() && !docPath.isEmpty()) {
        QDir().mkpath(m_backupDir);
        
        QString autoRecoveryPath = m_backupDir + "/recover_" + docId + ".dat";
        QFile file(autoRecoveryPath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(QJsonDocument(sessionData).toJson());
            file.close();
            emit backupCreated(autoRecoveryPath);
        }
    }
}

bool AutoSave::recoverAutoRecoveryData(QString& outData) const
{
    if (!m_document) return false;
    
    QString docId = m_document->getId();
    QString docPath = m_document->getPath();
    
    if (m_backupDir.isEmpty() || docPath.isEmpty()) return false;
    
    QDir dir(m_backupDir);
    QStringList recoverFiles = dir.entryList(QStringList() << "recover_" + docId + "_*.dat", QDir::Files, QDir::Time);
    
    if (recoverFiles.isEmpty()) return false;
    
    QString latestFile = dir.filePath(recoverFiles.first());
    QFile file(latestFile);
    if (!file.open(QIODevice::ReadOnly)) return false;
    
    QByteArray jsonData = file.readAll();
    file.close();
    
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(jsonData, &error);
    if (error.error != QJsonParseError::NoError) return false;
    
    outData = doc.toJson(QJsonDocument::Compact);
    return true;
}

QString AutoSave::generateBackupPath() const
{
    QString docId = m_document ? m_document->getId() : "unknown";
    QString ts    = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    return QDir(m_backupDir).filePath(
        QString("%1_%2_v%3.bak").arg(docId, ts).arg(++m_version)
    );
}

} // namespace ks
