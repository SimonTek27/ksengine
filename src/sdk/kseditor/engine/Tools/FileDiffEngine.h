#pragma once
#include <QObject>
#include <QString>
#include <QVector>
#include <QByteArray>
#include <functional>
#include <QJsonObject>
#include <QDateTime>
#include <QMap>
#include <QHash>

namespace ks {

class FileDiffResult {
public:
    QString filePath;
    QString type;
    enum ChangeType {
        None,
        Added,
        Modified,
        Deleted,
        Renamed,
        ModifiedContent
    };
    ChangeType change;
    
    // For content changes
    QVector<QString> addedLines;
    QVector<QString> removedLines;
    QVector<QString> contextLines;
    int lineCount;
    
    // For structure changes (JSON, INI, etc.)
    QJsonObject oldData;
    QJsonObject newData;
    QVector<QString> changedKeys;
    
    // Metadata
    QDateTime timestamp;
    QString createdBy;
    
    FileDiffResult() : change(None), lineCount(0) {}
    FileDiffResult(const QString& path, ChangeType c) 
        : filePath(path), change(c), lineCount(0) {}
};

class FileComparisonEngine : public QObject {
    Q_OBJECT

public:
    static FileComparisonEngine* instance();
    virtual ~FileComparisonEngine() {}
    
    virtual void initialize() = 0;
    virtual bool isInitialized() const = 0;
    
    virtual FileDiffResult compareFiles(const QString& oldPath, const QString& newPath) = 0;
    virtual FileDiffResult compareFileContent(const QString& path, const QByteArray& oldContent, const QByteArray& newContent) = 0;
    
    virtual QVector<FileDiffResult> compareDirectories(const QString& oldDir, const QString& newDir) = 0;
    virtual QVector<FileDiffResult> detectFileChanges(const QString& dir1, const QString& dir2) = 0;
    
    virtual QJsonObject generateReport(const QVector<FileDiffResult>& diffs) const = 0;
    virtual QString generateHumanReadableReport(const QVector<FileDiffResult>& diffs) const = 0;
    
    virtual void setReportCallback(std::function<void(const QJsonObject&)> callback) = 0;
    
    virtual bool isBinaryFile(const QString& path) const = 0;
    virtual QByteArray readFileContent(const QString& path) const = 0;
    virtual QString detectFileType(const QString& path) const = 0;
    
    virtual void setMaxFileSize(int maxSize) = 0;
    virtual int getMaxFileSize() const = 0;
    
    virtual void setTextDiffCallback(std::function<QVector<QString>(const QString&, const QString&)> callback) = 0;
    virtual void setBinaryFileCallback(std::function<bool(const QString&)> callback) = 0;
    
    virtual bool exportDiffReport(const QVector<FileDiffResult>& diffs, const QString& outputPath) = 0;
    virtual bool importDiffReport(const QString& inputPath, QVector<FileDiffResult>& diffs) = 0;
    
    virtual QMap<QString, QVector<FileDiffResult>> groupDiffsByType(const QVector<FileDiffResult>& diffs) = 0;
    virtual QString getDiffSummary(const QVector<FileDiffResult>& diffs) const = 0;

private:
    static FileComparisonEngine* s_instance;
};

} // namespace ks
