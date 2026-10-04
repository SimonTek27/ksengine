#include "ModPackager.h"
#include "Package.h"
#include "../sys/LogManager.h"
#include "sdk/kseditor/engine/FileFormat/INIParser.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>
#include <QDirIterator>
#include <QImage>
#include <QPainter>
#include <QtCore/private/qzipreader_p.h>
#include <QtCore/private/qzipwriter_p.h>
#include "Config/EditorConfig.h"

ModPackager::PackageInfo ModPackager::packageCar(const QString& carPath, const QString& outputPath, const PackageConfig& config)
{
    PackageInfo info;
    info.sourcePath = carPath;
    info.outputPath = outputPath;

    QStringList errors, warnings;
    if (!validateCarPackage(carPath, errors, warnings)) {
        info.errors = errors;
        info.warnings = warnings;
        info.isValid = false;
        return info;
    }

    info.warnings = warnings;
    info.name = config.name.isEmpty() ? getPackageName(carPath) : config.name;
    info.version = config.version.isEmpty() ? "1.0" : config.version;

    QString tempDir = QDir::tempPath() + "/kseditor_package_" + QUuid::createUuid().toString(QUuid::Id128);
    QDir().mkpath(tempDir);

    copyDirectoryRecursive(carPath, tempDir + "/" + info.name);

    if (config.createReadme) {
        generateReadme(config, tempDir + "/" + info.name + "/README.md");
    }

    if (config.includePreview && !config.previewPath.isEmpty()) {
        QFile::copy(config.previewPath, tempDir + "/" + info.name + "/preview.jpg");
    }

    QString zipPath = outputPath.isEmpty() ? getDefaultOutputPath(carPath) + "/" + info.name + ".zip" : outputPath;
    QDir().mkpath(QFileInfo(zipPath).absolutePath());

    if (createZip(tempDir, zipPath)) {
        info.outputPath = zipPath;
        info.totalSize = QFileInfo(zipPath).size();
        info.fileCount = getContentFiles(tempDir).size();
        info.isValid = true;
    } else {
        info.errors.append("Failed to create ZIP archive");
        info.isValid = false;
    }

    QDir(tempDir).removeRecursively();

    LOG_INFO("ModPackager", QString("Packaged car: %1 (%2 files, %3 bytes)")
        .arg(info.name).arg(info.fileCount).arg(info.totalSize));
    return info;
}

ModPackager::PackageInfo ModPackager::packageTrack(const QString& trackPath, const QString& outputPath, const PackageConfig& config)
{
    PackageInfo info;
    info.sourcePath = trackPath;
    info.outputPath = outputPath;

    QStringList errors, warnings;
    if (!validateTrackPackage(trackPath, errors, warnings)) {
        info.errors = errors;
        info.warnings = warnings;
        info.isValid = false;
        return info;
    }

    info.warnings = warnings;
    info.name = config.name.isEmpty() ? getPackageName(trackPath) : config.name;
    info.version = config.version.isEmpty() ? "1.0" : config.version;

    QString tempDir = QDir::tempPath() + "/kseditor_package_" + QUuid::createUuid().toString(QUuid::Id128);
    QDir().mkpath(tempDir);

    copyDirectoryRecursive(trackPath, tempDir + "/" + info.name);

    if (config.createReadme) {
        generateReadme(config, tempDir + "/" + info.name + "/README.md");
    }

    if (config.includePreview && !config.previewPath.isEmpty()) {
        QFile::copy(config.previewPath, tempDir + "/" + info.name + "/preview.jpg");
    }

    QString zipPath = outputPath.isEmpty() ? getDefaultOutputPath(trackPath) + "/" + info.name + ".zip" : outputPath;
    QDir().mkpath(QFileInfo(zipPath).absolutePath());

    if (createZip(tempDir, zipPath)) {
        info.outputPath = zipPath;
        info.totalSize = QFileInfo(zipPath).size();
        info.fileCount = getContentFiles(tempDir).size();
        info.isValid = true;
    } else {
        info.errors.append("Failed to create ZIP archive");
        info.isValid = false;
    }

    QDir(tempDir).removeRecursively();

    LOG_INFO("ModPackager", QString("Packaged track: %1 (%2 files, %3 bytes)")
        .arg(info.name).arg(info.fileCount).arg(info.totalSize));
    return info;
}

ModPackager::PackageInfo ModPackager::packageContent(const QString& contentPath, const QString& outputPath, const PackageConfig& config)
{
    PackageInfo info;
    info.sourcePath = contentPath;
    info.outputPath = outputPath;

    info.name = config.name.isEmpty() ? getPackageName(contentPath) : config.name;
    info.version = config.version.isEmpty() ? "1.0" : config.version;

    QString tempDir = QDir::tempPath() + "/kseditor_package_" + QUuid::createUuid().toString(QUuid::Id128);
    QDir().mkpath(tempDir);

    copyDirectoryRecursive(contentPath, tempDir + "/" + info.name);

    if (config.createReadme) {
        generateReadme(config, tempDir + "/" + info.name + "/README.md");
    }

    if (config.includePreview && !config.previewPath.isEmpty()) {
        QFile::copy(config.previewPath, tempDir + "/" + info.name + "/preview.jpg");
    }

    QString zipPath = outputPath.isEmpty() ? getDefaultOutputPath(contentPath) + "/" + info.name + ".zip" : outputPath;
    QDir().mkpath(QFileInfo(zipPath).absolutePath());

    if (createZip(tempDir, zipPath)) {
        info.outputPath = zipPath;
        info.totalSize = QFileInfo(zipPath).size();
        info.fileCount = getContentFiles(tempDir).size();
        info.isValid = true;
    } else {
        info.errors.append("Failed to create ZIP archive");
        info.isValid = false;
    }

    QDir(tempDir).removeRecursively();

    LOG_INFO("ModPackager", QString("Packaged content: %1 (%2 files, %3 bytes)")
        .arg(info.name).arg(info.fileCount).arg(info.totalSize));
    return info;
}

bool ModPackager::createZip(const QString& sourceDir, const QString& zipPath)
{
    return createZipFromDirectory(sourceDir, zipPath);
}

bool ModPackager::extractZip(const QString& zipPath, const QString& outputDir)
{
    QZipReader reader(zipPath);
    if (!reader.exists()) {
        LOG_ERROR("ModPackager", QString("Failed to open ZIP: %1").arg(zipPath));
        return false;
    }

    QDir().mkpath(outputDir);
    reader.extractAll(outputDir);
    return true;
}

bool ModPackager::addToZip(const QString& zipPath, const QString& filePath, const QString& entryName)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR("ModPackager", QString("Cannot read file to add: %1").arg(filePath));
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QString entry = entryName.isEmpty() ? QFileInfo(filePath).fileName() : entryName;

    QMap<QString, QByteArray> existingEntries;
    if (QFile::exists(zipPath)) {
        QFile zipFile(zipPath);
        if (zipFile.open(QIODevice::ReadOnly)) {
            QZipReader reader(&zipFile);
            for (const QZipReader::FileInfo& info : reader.fileInfoList()) {
                if (!info.isDir) {
                    existingEntries[info.filePath] = reader.fileData(info.filePath);
                }
            }
            zipFile.close();
        }
    }

    existingEntries[entry] = data;

    QFile outFile(zipPath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        LOG_ERROR("ModPackager", QString("Cannot open ZIP for writing: %1").arg(zipPath));
        return false;
    }

    QZipWriter zip(&outFile);
    if (zip.status() != QZipWriter::NoError) {
        LOG_ERROR("ModPackager", QString("Cannot open ZIP for writing: %1").arg(zipPath));
        return false;
    }

    for (auto it = existingEntries.begin(); it != existingEntries.end(); ++it) {
        zip.addFile(it.key(), it.value());
    }
    zip.close();
    outFile.close();

    LOG_INFO("ModPackager", QString("Added to ZIP: %1 -> %2").arg(filePath, entry));
    return true;
}

ModPackager::PackageInfo ModPackager::validatePackage(const QString& contentPath)
{
    PackageInfo info;
    info.sourcePath = contentPath;
    info.name = getPackageName(contentPath);

    INIParser ini;
    QString carIni = contentPath + "/data/car.ini";
    QString trackIni = contentPath + "/data/track.ini";

    if (QFile::exists(carIni)) {
        QStringList errors, warnings;
        validateCarPackage(contentPath, errors, warnings);
        info.errors = errors;
        info.warnings = warnings;
    } else if (QFile::exists(trackIni)) {
        QStringList errors, warnings;
        validateTrackPackage(contentPath, errors, warnings);
        info.errors = errors;
        info.warnings = warnings;
    } else {
        info.warnings.append("Unknown content type (no car.ini or track.ini found)");
    }

    info.totalSize = calculateContentSize(contentPath);
    info.fileCount = getContentFiles(contentPath).size();
    info.isValid = info.errors.isEmpty();
    return info;
}

bool ModPackager::validateCarPackage(const QString& carPath, QStringList& errors, QStringList& warnings)
{
    errors.clear();
    warnings.clear();

    if (!QDir(carPath).exists()) {
        errors.append("Car directory does not exist");
        return false;
    }

    if (!QFile::exists(carPath + "/data/car.ini")) {
        errors.append("Missing data/car.ini");
    }
    if (!QFile::exists(carPath + "/data/tyres.ini")) {
        warnings.append("Missing data/tyres.ini");
    }
    if (!QFile::exists(carPath + "/data/engine.ini")) {
        warnings.append("Missing data/engine.ini");
    }

    bool hasKn5 = false;
    QDir rootDir(carPath);
    QStringList kn5Files = rootDir.entryList(QStringList() << "*.kn5", QDir::Files);
    if (!kn5Files.isEmpty()) {
        hasKn5 = true;
    }
    if (!hasKn5) {
        QStringList modelsKn5 = QDir(carPath + "/models").entryList(QStringList() << "*.kn5", QDir::Files);
        if (!modelsKn5.isEmpty()) hasKn5 = true;
    }
    if (!hasKn5) {
        errors.append("No .kn5 model files found");
    }

    bool hasTextures = QDir(carPath + "/textures").exists() &&
                       !QDir(carPath + "/textures").entryList(QDir::Files).isEmpty();
    bool hasSkins = QDir(carPath + "/skins").exists();
    if (!hasTextures && !hasSkins) {
        warnings.append("No textures or skins found");
    }

    return errors.isEmpty();
}

bool ModPackager::validateTrackPackage(const QString& trackPath, QStringList& errors, QStringList& warnings)
{
    errors.clear();
    warnings.clear();

    if (!QDir(trackPath).exists()) {
        errors.append("Track directory does not exist");
        return false;
    }

    if (!QFile::exists(trackPath + "/data/track.ini")) {
        errors.append("Missing data/track.ini");
    }
    if (!QFile::exists(trackPath + "/map.png")) {
        warnings.append("Missing map.png");
    }
    if (!QDir(trackPath + "/ai").exists()) {
        warnings.append("Missing /ai directory");
    }

    return errors.isEmpty();
}

bool ModPackager::generatePreview(const QString& contentPath, const QString& outputPath)
{
    QFileInfo fi(contentPath);
    QImage preview(512, 320, QImage::Format_RGB32);
    preview.fill(QColor("#1a1a2e"));

    QPainter painter(&preview);
    painter.setPen(QColor("#8888aa"));
    QFont font = painter.font();
    font.setPointSize(14);
    painter.setFont(font);

    QString name = fi.completeSuffix().isEmpty() ? fi.fileName() : fi.baseName();
    painter.drawText(preview.rect(), Qt::AlignCenter, name);

    font.setPointSize(9);
    painter.setFont(font);
    painter.setPen(QColor("#555577"));
    QRect textRect = preview.rect().adjusted(20, -40, -20, 40);
    painter.drawText(textRect, Qt::AlignBottom | Qt::AlignHCenter, fi.completeSuffix().toUpper());

    painter.end();
    return preview.save(outputPath);
}

bool ModPackager::hasPreview(const QString& contentPath)
{
    return QFile::exists(contentPath + "/preview.jpg") ||
           QFile::exists(contentPath + "/preview.png");
}

bool ModPackager::generateReadme(const PackageConfig& config, const QString& outputPath)
{
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "# " << config.name << "\n\n";
    stream << config.description << "\n\n";
    stream << "## Author\n" << config.author << "\n\n";

    if (!config.version.isEmpty()) {
        stream << "## Version\n" << config.version << "\n\n";
    }
    if (!config.category.isEmpty()) {
        stream << "## Category\n" << config.category << "\n\n";
    }
    if (!config.license.isEmpty()) {
        stream << "## License\n" << config.license << "\n\n";
    }
    if (!config.website.isEmpty()) {
        stream << "## Website\n" << config.website << "\n\n";
    }

    stream << "## Installation\n\n";
    if (config.category == "car") {
        stream << "1. Extract to your simulator content/cars directory\n";
        stream << "2. The car will appear in the car selection screen\n";
    } else if (config.category == "track") {
        stream << "1. Extract to your simulator content/tracks directory\n";
        stream << "2. The track will appear in the track selection screen\n";
    } else {
        stream << "1. Extract to the appropriate simulator content directory\n";
    }

    file.close();
    return true;
}

bool ModPackager::generateChangelog(const QString& version, const QString& changes, const QString& outputPath)
{
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "# Changelog\n\n";
    stream << "## " << version << "\n\n";
    stream << changes << "\n";

    file.close();
    return true;
}

QString ModPackager::getDefaultOutputPath(const QString& contentPath)
{
    if (!contentPath.isEmpty()) {
        return QFileInfo(contentPath).path() + "/export";
    }
    return QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
}

QString ModPackager::getPackageName(const QString& contentPath)
{
    return QFileInfo(contentPath).fileName();
}

qint64 ModPackager::calculateContentSize(const QString& contentPath)
{
    qint64 totalSize = 0;
    QStringList files = getContentFiles(contentPath);
    for (const QString& file : files) {
        totalSize += QFileInfo(file).size();
    }
    return totalSize;
}

QStringList ModPackager::getContentFiles(const QString& contentPath)
{
    QStringList files;
    QDir dir(contentPath);
    if (!dir.exists()) return files;

    QDirIterator it(contentPath, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files.append(it.next());
    }
    return files;
}

bool ModPackager::copyDirectoryRecursive(const QString& source, const QString& destination)
{
    QDir sourceDir(source);
    if (!sourceDir.exists()) return false;

    QDir().mkpath(destination);

    QStringList entries = sourceDir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        QString srcPath = source + "/" + entry;
        QString dstPath = destination + "/" + entry;

        if (QFileInfo(srcPath).isDir()) {
            if (!copyDirectoryRecursive(srcPath, dstPath)) return false;
        } else {
            if (!QFile::copy(srcPath, dstPath)) return false;
        }
    }
    return true;
}

bool ModPackager::createZipFromDirectory(const QString& sourceDir, const QString& zipPath)
{
    QZipWriter zip(zipPath);
    if (zip.status() != QZipWriter::NoError) {
        LOG_ERROR("ModPackager", QString("Failed to create ZIP: %1").arg(zipPath));
        return false;
    }

    QStringList files = getContentFiles(sourceDir);
    for (const QString& file : files) {
        QString relativePath = QDir(sourceDir).relativeFilePath(file);
        QFile f(file);
        if (f.open(QIODevice::ReadOnly)) {
            zip.addFile(relativePath, f.readAll());
            f.close();
        }
    }

    zip.close();
    return true;
}

// ── ModPackagerManager ──────────────────────────────────────────

ModPackagerManager::ModPackagerManager(const QString& acPath)
    : m_acPath(acPath)
{
}

bool ModPackagerManager::packageContent(const QString& contentPath, const QString& outputPath)
{
    m_lastInfo = ModPackager::packageContent(contentPath, outputPath, m_config);
    return m_lastInfo.isValid;
}

bool ModPackagerManager::validateContent(const QString& contentPath)
{
    m_lastInfo = ModPackager::validatePackage(contentPath);
    return m_lastInfo.isValid;
}
