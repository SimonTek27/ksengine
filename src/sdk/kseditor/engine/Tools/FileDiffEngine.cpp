#include "FileDiffEngine.h"
#include <QFile>
#include <QDirIterator>
#include <QTextStream>
#include <QMimeDatabase>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSet>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <algorithm>
#include <functional>
#include <unordered_map>

namespace ks {

FileComparisonEngine* FileComparisonEngine::s_instance = nullptr;

class DefaultFileComparisonEngine : public FileComparisonEngine {
public:
    DefaultFileComparisonEngine() : m_maxFileSize(10 * 1024 * 1024) {} // 10MB limit
    virtual ~DefaultFileComparisonEngine() {}
    
    virtual void initialize() override {
        // Initialize the comparison engine
        qDebug() << "[FileComparisonEngine] Initialized";
    }
    
    virtual bool isInitialized() const override {
        return true; // Simple implementation
    }
    
    virtual FileDiffResult compareFiles(const QString& oldPath, const QString& newPath) override {
        FileDiffResult result;
        
        if (!QFile::exists(oldPath) && !QFile::exists(newPath)) {
            result.change = FileDiffResult::None;
            return result;
        }
        
        if (!QFile::exists(oldPath)) {
            result.filePath = newPath;
            result.change = FileDiffResult::Added;
            return result;
        }
        
        if (!QFile::exists(newPath)) {
            result.filePath = oldPath;
            result.change = FileDiffResult::Deleted;
            return result;
        }
        
        result.filePath = newPath;
        
        // Detect file type
        QString fileType = detectFileType(oldPath);
        
        if (fileType == "binary") {
            // Compare file sizes first
            QFileInfo oldInfo(oldPath);
            QFileInfo newInfo(newPath);
            
            if (oldInfo.size() != newInfo.size()) {
                result.change = FileDiffResult::Modified;
            } else {
                result.change = FileDiffResult::None;
            }
            
            result.addedLines << tr("File size changed from %1 to %2 bytes").arg(oldInfo.size()).arg(newInfo.size());
        } else if (fileType == "ini" || fileType == "json" || fileType == "xml") {
            // Parse and compare structured content
            result = compareStructuredFiles(oldPath, newPath);
        } else {
            // Text file comparison
            result = compareTextFiles(oldPath, newPath);
        }
        
        result.timestamp = QDateTime::currentDateTime();
        return result;
    }
    
    virtual FileDiffResult compareFileContent(const QString& path, const QByteArray& oldContent, const QByteArray& newContent) override {
        FileDiffResult result;
        result.filePath = path;
        
        if (oldContent == newContent) {
            result.change = FileDiffResult::None;
            return result;
        }
        
        result.change = FileDiffResult::ModifiedContent;
        
        // Calculate diff using simple line-based algorithm
        QVector<QString> oldLines;
        QVector<QString> newLines;
        
        for (const QByteArray& line : oldContent.split('\n')) {
            oldLines << QString::fromUtf8(line).trimmed();
        }
        
        for (const QByteArray& line : newContent.split('\n')) {
            newLines << QString::fromUtf8(line).trimmed();
        }
        
        // Simple diff algorithm
        QVector<QString> commonLines;
        QVector<QString> addedLines;
        QVector<QString> removedLines;
        
        for (const QString& line : newLines) {
            if (std::find(oldLines.begin(), oldLines.end(), line) != oldLines.end()) {
                commonLines << line;
            } else {
                addedLines << line;
            }
        }
        
        for (const QString& line : oldLines) {
            if (std::find(newLines.begin(), newLines.end(), line) == newLines.end()) {
                removedLines << line;
            }
        }
        
        result.addedLines = addedLines;
        result.removedLines = removedLines;
        result.contextLines = commonLines;
        result.lineCount = addedLines.size() + removedLines.size();
        
        return result;
    }
    
    virtual QVector<FileDiffResult> compareDirectories(const QString& oldDir, const QString& newDir) override {
        QVector<FileDiffResult> results;
        
        // Ensure both paths exist
        if (!QFile::exists(oldDir) || !QFile::exists(newDir)) {
            return results;
        }
        
        // Compare files in oldDir
        QDirIterator oldIt(oldDir, QDir::Files, QDirIterator::Subdirectories);
        while (oldIt.hasNext()) {
            QString oldPath = oldIt.next();
            QString relativePath = QDir(oldDir).relativeFilePath(oldPath);
            
            // Check if file exists in newDir
            QString newPath = newDir + "/" + relativePath;
            if (QFile::exists(newPath)) {
                // File exists in both directories - compare content
                FileDiffResult diff = compareFiles(oldPath, newPath);
                results << diff;
            } else {
                // File only in old directory
                FileDiffResult diff;
                diff.filePath = oldPath;
                diff.change = FileDiffResult::Deleted;
                diff.timestamp = QDateTime::currentDateTime();
                results << diff;
            }
        }
        
        // Check for files only in newDir
        QDirIterator newIt(newDir, QDir::Files, QDirIterator::Subdirectories);
        QSet<QString> processedPaths;
        
        for (int i = 0; i < results.size(); ++i) {
            processedPaths.insert(results[i].filePath);
        }
        
        while (newIt.hasNext()) {
            QString newPath = newIt.next();
            QString relativePath = QDir(newDir).relativeFilePath(newPath);
            
            if (!processedPaths.contains(newPath)) {
                FileDiffResult diff;
                diff.filePath = newPath;
                diff.change = FileDiffResult::Added;
                diff.timestamp = QDateTime::currentDateTime();
                results << diff;
            }
        }
        
        return results;
    }
    
    virtual QVector<FileDiffResult> detectFileChanges(const QString& dir1, const QString& dir2) override {
        return compareDirectories(dir1, dir2);
    }
    
    virtual QJsonObject generateReport(const QVector<FileDiffResult>& diffs) const override {
        QJsonObject report;
        QJsonArray diffArray;
        
        int addedCount = 0;
        int modifiedCount = 0;
        int deletedCount = 0;
        int unchangedCount = 0;
        
        for (const FileDiffResult& diff : diffs) {
            QJsonObject diffObj;
            diffObj["file"] = diff.filePath;
            diffObj["change"] = changeToString(diff.change);
            
            if (diff.change == FileDiffResult::Added) {
                addedCount++;
                diffObj["newContent"] = formatContentVector(diff.addedLines);
            } else if (diff.change == FileDiffResult::Modified) {
                modifiedCount++;
                diffObj["oldContent"] = formatContentVector(diff.removedLines);
                diffObj["newContent"] = formatContentVector(diff.addedLines);
            } else if (diff.change == FileDiffResult::Deleted) {
                deletedCount++;
            } else if (diff.change == FileDiffResult::ModifiedContent) {
                modifiedCount++;
                diffObj["oldLines"] = formatContentVector(diff.removedLines);
                diffObj["newLines"] = formatContentVector(diff.addedLines);
            } else {
                unchangedCount++;
            }
            
            diffObj["timestamp"] = diff.timestamp.toString(Qt::ISODate);
            diffArray.append(diffObj);
        }
        
        report["diffs"] = diffArray;
        report["added"] = addedCount;
        report["modified"] = modifiedCount;
        report["deleted"] = deletedCount;
        report["unchanged"] = unchangedCount;
        report["total"] = diffs.size();
        
        return report;
    }
    
    virtual QString generateHumanReadableReport(const QVector<FileDiffResult>& diffs) const override {
        QString report;
        
        int addedCount = 0, modifiedCount = 0, deletedCount = 0, unchangedCount = 0;
        
        report += tr("File Comparison Report\n");
        report += tr("=======================\n\n");
        
        for (const FileDiffResult& diff : diffs) {
            QString icon;
            QString changeStr;
            
            switch (diff.change) {
                case FileDiffResult::Added:
                    icon = "+ ";
                    changeStr = tr("ADDED");
                    addedCount++;
                    break;
                case FileDiffResult::Modified:
                    icon = "~ ";
                    changeStr = tr("MODIFIED");
                    modifiedCount++;
                    break;
                case FileDiffResult::Deleted:
                    icon = "- ";
                    changeStr = tr("DELETED");
                    deletedCount++;
                    break;
                case FileDiffResult::ModifiedContent:
                    icon = "~ ";
                    changeStr = tr("CONTENT CHANGED");
                    modifiedCount++;
                    break;
                default:
                    icon = "  ";
                    changeStr = tr("UNCHANGED");
                    unchangedCount++;
            }
            
            report += QString("%1%2 (%3)").arg(icon).arg(QFileInfo(diff.filePath).fileName()).arg(changeStr);
            
            if (diff.change == FileDiffResult::ModifiedContent) {
                if (diff.lineCount > 0) {
                    report += tr(" (%1 line(s) changed)").arg(diff.lineCount);
                }
            }
            
            report += "\n";
            
            if (diff.change == FileDiffResult::Added && !diff.addedLines.isEmpty()) {
                report += "  Added lines:\n";
                for (const QString& line : diff.addedLines) {
                    report += "    " + line + "\n";
                }
            } else if (diff.change == FileDiffResult::Modified && !diff.addedLines.isEmpty()) {
                report += "  Changes:\n";
                for (const QString& line : diff.addedLines) {
                    report += "    + " + line + "\n";
                }
                for (const QString& line : diff.removedLines) {
                    report += "    - " + line + "\n";
                }
            }
            
            report += "\n";
        }
        
        report += tr("\nSummary:\n");
        report += tr("  Added: %1 files\n").arg(addedCount);
        report += tr("  Modified: %1 files\n").arg(modifiedCount);
        report += tr("  Deleted: %1 files\n").arg(deletedCount);
        report += tr("  Unchanged: %1 files\n").arg(unchangedCount);
        
        return report;
    }
    
    virtual void setReportCallback(std::function<void(const QJsonObject&)> callback) override {
        m_reportCallback = callback;
        if (callback) {
            QJsonObject emptyReport;
            emptyReport["status"] = "initialized";
            callback(emptyReport);
        }
    }
    
    virtual bool isBinaryFile(const QString& path) const override {
        // Check file extension against known binary extensions
        QString ext = QFileInfo(path).suffix().toLower();
        QStringList binaryExtensions = {"exe", "dll", "so", "dylib", "bin", "dat", "pak", "wad", "assets", "png", "jpg", "jpeg", "gif", "bmp", "ico", "avi", "mp4", "mov", "wav", "mp3", "flac", "ogg", "pdf"};
        
        return binaryExtensions.contains(ext);
    }
    
    virtual QByteArray readFileContent(const QString& path) const override {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            return QByteArray();
        }
        
        return file.readAll();
    }
    
    virtual QString detectFileType(const QString& path) const override {
        // Simple file type detection based on extension and content
        QString ext = QFileInfo(path).suffix().toLower();
        
        QStringList textExtensions = {"txt", "ini", "conf", "cfg", "json", "xml", "html", "css", "js", "py", "cpp", "h", "c", "java", "sh", "bat", "md", "yml", "yaml", "csv", "tsv", "log", "readme", "license"};
        QStringList binaryExtensions = {"exe", "dll", "so", "dylib", "bin", "dat", "pak", "wad", "assets", "png", "jpg", "jpeg", "gif", "bmp", "ico", "avi", "mp4", "mov", "wav", "mp3", "flac", "ogg", "pdf"};
        
        if (textExtensions.contains(ext)) {
            return "text";
        } else if (binaryExtensions.contains(ext)) {
            return "binary";
        }
        
        // Check file magic number for known types
        QByteArray content = readFileContent(path);
        if (content.size() < 1024) { // Limit check size
            return "text";
        }
        
        // Check for JSON
        if (content.startsWith("{") || content.startsWith("[")) {
            return "json";
        }
        
        // Check for XML
        if (content.contains("<") && content.contains(">")) {
            return "xml";
        }
        
        // Check for INI
        if (content.contains("[") && content.contains("=")) {
            return "ini";
        }
        
        // Default to text
        return "text";
    }
    
    virtual void setMaxFileSize(int maxSize) override {
        m_maxFileSize = maxSize;
    }
    
    virtual int getMaxFileSize() const override {
        return m_maxFileSize;
    }
    
    virtual void setTextDiffCallback(std::function<QVector<QString>(const QString&, const QString&)> callback) override {
        m_textDiffCallback = callback;
    }
    
    virtual void setBinaryFileCallback(std::function<bool(const QString&)> callback) override {
        m_binaryFileCallback = callback;
    }
    
    virtual bool exportDiffReport(const QVector<FileDiffResult>& diffs, const QString& outputPath) override {
        QJsonObject report = generateReport(diffs);
        
        QFile file(outputPath);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        
        QJsonDocument doc(report);
        file.write(doc.toJson());
        file.close();
        
        return true;
    }
    
    virtual bool importDiffReport(const QString& inputPath, QVector<FileDiffResult>& diffs) override {
        QFile file(inputPath);
        if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
            return false;
        }
        
        QByteArray data = file.readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        
        if (!doc.isObject()) {
            return false;
        }
        
        QJsonObject obj = doc.object();
        
        if (!obj.contains("diffs")) {
            return false;
        }
        
        // Load diffs from report
        QJsonArray diffArray = obj["diffs"].toArray();
        
        // Parse diff objects (simplified)
        for (const auto& diffRef : diffArray) {
            QJsonObject diffObj = diffRef.toObject();
            FileDiffResult result;
            result.filePath = diffObj["file"].toString();
            result.change = stringToChange(diffObj["change"].toString());
            
            if (diffObj.contains("newContent") && diffObj["newContent"].isString()) {
                result.addedLines << diffObj["newContent"].toString().split('\n');
            }
            
            diffs << result;
        }
        
        return true;
    }
    
    virtual QMap<QString, QVector<FileDiffResult>> groupDiffsByType(const QVector<FileDiffResult>& diffs) override {
        QMap<QString, QVector<FileDiffResult>> grouped;
        
        for (const FileDiffResult& diff : diffs) {
            QString type;
            
            switch (diff.change) {
                case FileDiffResult::Added:
                    type = "added";
                    break;
                case FileDiffResult::Modified:
                    type = "modified";
                    break;
                case FileDiffResult::Deleted:
                    type = "deleted";
                    break;
                case FileDiffResult::ModifiedContent:
                    type = "content-changed";
                    break;
                default:
                    type = "unchanged";
            }
            
            if (!grouped.contains(type)) {
                grouped[type] = QVector<FileDiffResult>();
            }
            
            grouped[type] << diff;
        }
        
        return grouped;
    }
    
    virtual QString getDiffSummary(const QVector<FileDiffResult>& diffs) const override {
        QMap<QString, int> counts;
        
        for (const FileDiffResult& diff : diffs) {
            QString type = changeToString(diff.change);
            counts[type] = counts.value(type, 0) + 1;
        }
        
        QString summary = tr("Summary: ") + QString::number(diffs.size()) + tr(" changes found\n");
        
        if (counts.contains("added")) {
            summary += tr("  Added: %1 files\n").arg(counts["added"]);
        }
        if (counts.contains("modified")) {
            summary += tr("  Modified: %1 files\n").arg(counts["modified"]);
        }
        if (counts.contains("deleted")) {
            summary += tr("  Deleted: %1 files\n").arg(counts["deleted"]);
        }
        
        return summary;
    }
    
private:
    FileDiffResult compareStructuredFiles(const QString& oldPath, const QString& newPath) {
        FileDiffResult result;
        result.filePath = newPath;
        
        QFile file1(oldPath);
        QFile file2(newPath);
        
        if (!file1.open(QIODevice::ReadOnly) || !file2.open(QIODevice::ReadOnly)) {
            result.change = FileDiffResult::Modified;
            return result;
        }
        
        QByteArray content1 = file1.readAll();
        QByteArray content2 = file2.readAll();
        
        // Try to parse as JSON
        bool parse1 = false, parse2 = false;
        QJsonObject obj1, obj2;
        
        QJsonParseError error1, error2;
        
        if (detectFileType(oldPath) == "json") {
            QJsonDocument doc1 = QJsonDocument::fromJson(content1, &error1);
            if (error1.error == QJsonParseError::NoError) {
                parse1 = true;
                obj1 = doc1.object();
            }
        }
        
        if (detectFileType(newPath) == "json") {
            QJsonDocument doc2 = QJsonDocument::fromJson(content2, &error2);
            if (error2.error == QJsonParseError::NoError) {
                parse2 = true;
                obj2 = doc2.object();
            }
        }
        
        if (!parse1 && !parse2) {
            // Default to content comparison
            return compareFileContent(newPath, content1, content2);
        }
        
        if (parse1 && !parse2) {
            result.change = FileDiffResult::Deleted;
            return result;
        }
        
        if (!parse1 && parse2) {
            result.change = FileDiffResult::Added;
            return result;
        }
        
        if (parse1 && parse2) {
            // Compare JSON objects
            if (obj1 != obj2) {
                result.change = FileDiffResult::Modified;
                
                // Find differences
                QSet<QString> allKeys;
                for (const QString& key : obj1.keys()) {
                    allKeys.insert(key);
                }
                for (const QString& key : obj2.keys()) {
                    allKeys.insert(key);
                }
                
                for (const QString& key : allKeys) {
                    if (!obj1.contains(key)) {
                        result.addedLines << "+ " + key + ": " + obj2[key].toString();
                    } else if (!obj2.contains(key)) {
                        result.removedLines << "- " + key + ": " + obj1[key].toString();
                    } else if (obj1[key] != obj2[key]) {
                        result.addedLines << "~ " + key + ": changed";
                        result.removedLines << "  " + key + " (old)";
                    }
                }
            }
        }
        
        return result;
    }
    
    FileDiffResult compareTextFiles(const QString& oldPath, const QString& newPath) {
        FileDiffResult result;
        result.filePath = newPath;
        
        QFile file1(oldPath);
        QFile file2(newPath);
        
        if (!file1.open(QIODevice::ReadOnly) || !file2.open(QIODevice::ReadOnly)) {
            result.change = FileDiffResult::Modified;
            return result;
        }
        
        QByteArray content1 = file1.readAll();
        QByteArray content2 = file2.readAll();
        
        return compareFileContent(newPath, content1, content2);
    }
    
    QString changeToString(FileDiffResult::ChangeType change) const {
        switch (change) {
            case FileDiffResult::None: return "none";
            case FileDiffResult::Added: return "added";
            case FileDiffResult::Modified: return "modified";
            case FileDiffResult::Deleted: return "deleted";
            case FileDiffResult::Renamed: return "renamed";
            case FileDiffResult::ModifiedContent: return "content-changed";
            default: return "unknown";
        }
    }
    
    FileDiffResult::ChangeType stringToChange(const QString& changeStr) {
        if (changeStr == "none") return FileDiffResult::None;
        if (changeStr == "added") return FileDiffResult::Added;
        if (changeStr == "modified") return FileDiffResult::Modified;
        if (changeStr == "deleted") return FileDiffResult::Deleted;
        if (changeStr == "renamed") return FileDiffResult::Renamed;
        if (changeStr == "content-changed") return FileDiffResult::ModifiedContent;
        return FileDiffResult::None;
    }
    
    QString formatContentVector(const QVector<QString>& lines) const {
        if (lines.isEmpty()) {
            return "";
        }
        
        QString result;
        for (const QString& line : lines) {
            result += line + "\n";
        }
        return result;
    }
    
    int m_maxFileSize;
    std::function<void(const QJsonObject&)> m_reportCallback;
    std::function<QVector<QString>(const QString&, const QString&)> m_textDiffCallback;
    std::function<bool(const QString&)> m_binaryFileCallback;
};

FileComparisonEngine* FileComparisonEngine::instance()
{
    if (!s_instance) s_instance = new DefaultFileComparisonEngine();
    return s_instance;
}

} // namespace ks
