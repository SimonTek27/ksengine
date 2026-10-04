#pragma once

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QFileInfo>
#include <QDir>
#include <vector>
#include <memory>

namespace ks {
namespace archive {

struct ArchiveEntry {
    QString name;
    qint64 size = 0;
    qint64 compressedSize = 0;
    bool isDir = false;
    QDateTime lastModified;
    QString crc32;
    QString hash;
};

struct ArchiveInfo {
    QString format;
    QStringList formats;
    qint64 totalSize = 0;
    qint64 compressedSize = 0;
    int fileCount = 0;
    int folderCount = 0;
    bool isEncrypted = false;
    bool isSolid = false;
};

class SevenZipLibrary {
public:
    static SevenZipLibrary* instance();
    
    ~SevenZipLibrary();
    
    // Extract archive to directory
    // Returns empty QJsonObject on success, error object on failure
    QJsonObject extract(const QString& archivePath, const QString& outputDir, const QString& password = QString());
    
    // Extract specific files from archive
    QJsonObject extractFiles(const QString& archivePath, const QStringList& filePaths, const QString& outputDir, const QString& password = QString());
    
    // Create/compress archive from files
    QJsonObject compress(const QStringList& files, const QString& outputArchive, const QString& format = "7z", int compressionLevel = 9);
    
    // List archive contents
    QJsonObject listContents(const QString& archivePath, const QString& password = QString());
    
    // Get archive information
    QJsonObject getArchiveInfo(const QString& archivePath);
    
    // Test archive integrity
    QJsonObject testArchive(const QString& archivePath, const QString& password = QString());
    
    // Check if archive format is supported
    static bool isFormatSupported(const QString& format);
    
    // Get supported formats
    static QStringList getSupportedFormats();
    
    // Get supported archive extensions
    static QStringList getSupportedExtensions();
    
private:
    SevenZipLibrary();
    static SevenZipLibrary* s_instance;
    
    // Internal implementation
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} } // namespace ks::archive