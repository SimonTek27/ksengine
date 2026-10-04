#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QTimer>
#include <QJsonObject>
#include <QFileSystemWatcher>

namespace ks {

class Document;

struct RecoveryPoint {
    QString documentId;
    QString filePath;
    QString timestamp;
    QString backupPath;
    int version;
    bool isValid = true;
};

class AutoSave : public QObject
{
    Q_OBJECT

public:
    explicit AutoSave(QObject* parent = nullptr);
    ~AutoSave();

    void setDocument(Document* doc);
    void setBackupDirectory(const QString& dir);

    void setInterval(int seconds);
    int getInterval() const { return m_interval; }

    void setMaxBackups(int max);
    int getMaxBackups() const { return m_maxBackups; }

    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    void saveNow();
    void saveBackup();

    bool hasRecoveryPoint(const QString& documentId) const;
    QVector<RecoveryPoint> getRecoveryPoints(const QString& documentId) const;
    bool recover(const QString& documentId, int version);
    bool recoverLatest(const QString& documentId);
    bool hasAutoRecoveryPoint(const QString& documentId) const;
    QString recoverAutoRecoveryData() const;

    void cleanupOldBackups();

signals:
    void autoSaveTriggered();
    void backupCreated(const QString& path);
    void recoveryAvailable(const QString& documentId, int count);
    void recoveryComplete(const QString& documentId);
    void error(const QString& error);
    void autoRecoveryAvailable(const QString& data);

private:
    void performAutoSave();
    QString generateBackupPath() const;
    void setAutoRecoveryData(const QJsonObject& sessionData);
    bool recoverAutoRecoveryData(QString& outData) const;

    Document* m_document = nullptr;
    QString m_backupDir;
    QTimer m_timer;
    int m_interval = 300;
    int m_maxBackups = 5;
    bool m_enabled = true;
    mutable int m_version = 0;
};

class Document : public QObject
{
    Q_OBJECT

public:
    explicit Document(QObject* parent = nullptr);
    virtual ~Document();

    QString getId() const { return m_id; }
    void setId(const QString& id) { m_id = id; }

    QString getPath() const { return m_path; }
    void setPath(const QString& path) { m_path = path; }

    QString getName() const { return m_name; }

    bool isModified() const { return m_modified; }
    void setModified(bool modified) { m_modified = modified; }

    QJsonObject getState() const { return m_state; }
    void setState(const QJsonObject& state) { m_state = state; }

    virtual bool load(const QString& path);
    virtual bool save(const QString& path = QString());
    virtual bool canSave() const { return true; }

signals:
    void contentChanged();
    void saved();
    void pathChanged(const QString& path);

protected:
    QString m_id;
    QString m_path;
    QString m_name;
    bool m_modified = false;
    QJsonObject m_state;
};

class DocumentManager : public QObject
{
    Q_OBJECT

public:
    explicit DocumentManager(QObject* parent = nullptr);
    ~DocumentManager();

    void addDocument(Document* doc);
    void removeDocument(const QString& docId);

    Document* getDocument(const QString& docId) const;
    QVector<Document*> getDocuments() const { return m_documents.values(); }

    Document* getActiveDocument() const { return m_activeDocument; }
    void setActiveDocument(Document* doc);

    bool hasUnsavedChanges() const;
    bool saveAll();
    bool closeAll();

    QVector<Document*> getUnsavedDocuments() const;

signals:
    void documentAdded(Document* doc);
    void documentRemoved(const QString& docId);
    void activeDocumentChanged(Document* doc);
    void unsavedChangesChanged(bool hasUnsaved);

private:
    QMap<QString, Document*> m_documents;
    Document* m_activeDocument = nullptr;
};

} // namespace ks
