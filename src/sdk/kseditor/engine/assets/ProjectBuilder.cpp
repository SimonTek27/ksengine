#include "ProjectBuilder.h"
#include "sys/LogManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDirIterator>
#include <QThread>

ProjectBuilder::ProjectBuilder(QObject* parent)
    : QObject(parent)
{}

ProjectBuilder::~ProjectBuilder()
{
    cancel();
    if (m_thread) {
        m_thread->wait(3000);
        delete m_thread;
    }
}

void ProjectBuilder::build(const QString& projectPath, const QString& simPath)
{
    if (m_running.load()) {
        LOG_WARNING("ProjectBuilder", "Build already in progress");
        return;
    }

    m_cancel.store(false);

    // Run the build on a worker thread so the UI stays responsive
    m_thread = QThread::create([this, projectPath, simPath]() {
        doBuild(projectPath, simPath);
    });
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_running.store(true);
    m_thread->start();
}

void ProjectBuilder::cancel()
{
    m_cancel.store(true);
}

// ---------------------------------------------------------------------------
// Private implementation
// ---------------------------------------------------------------------------

void ProjectBuilder::doBuild(const QString& projectPath, const QString& simPath)
{
    auto progress = [this](int p) { emit progressUpdated(p); };
    auto log      = [this](const QString& m) {
        LOG_INFO("ProjectBuilder", m);
        emit logMessage(m);
    };

    log("Build started: " + projectPath);

    // 1 – Validate
    progress(5);
    QString err;
    if (!validateProject(projectPath, err)) {
        m_running.store(false);
        emit buildComplete(false, "Validation failed: " + err);
        return;
    }
    if (m_cancel.load()) { m_running.store(false); emit buildComplete(false, "Cancelled"); return; }
    progress(20);

    // 2 – Copy assets to sim
    log("Copying assets to: " + simPath);
    if (!copyAssets(projectPath, simPath, 20, 90)) {
        m_running.store(false);
        emit buildComplete(false, "Failed to copy assets. Check log for details.");
        return;
    }
    if (m_cancel.load()) { m_running.store(false); emit buildComplete(false, "Cancelled"); return; }
    progress(95);

    // 3 – Done
    log("Build completed successfully.");
    progress(100);
    m_running.store(false);
    emit buildComplete(true, "Project built and deployed to: " + simPath);
}

bool ProjectBuilder::validateProject(const QString& projectPath, QString& error)
{
    QFile file(projectPath);
    if (!file.exists()) {
        error = "Project file not found: " + projectPath;
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        error = "Cannot open project file: " + projectPath;
        return false;
    }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (doc.isNull() || !doc.isObject()) {
        error = "Project file is not valid JSON";
        return false;
    }

    QJsonObject obj = doc.object();
    if (!obj.contains("name")) {
        error = "Project file missing 'name' field";
        return false;
    }

    return true;
}

bool ProjectBuilder::copyAssets(const QString& projectPath,
                                 const QString& simPath,
                                 int basePercent, int endPercent)
{
    QFileInfo info(projectPath);
    QString projectDir = info.absolutePath();

    // Determine target based on project type (car / track / etc.)
    // Convention: project dir contains a subfolder named "car", "track", etc.
    // We look for any of those and copy to the corresponding AC content dir.

    struct AssetMapping {
        QString srcFolder;
        QString destSubdir; // relative to simPath
    };

    QList<AssetMapping> mappings = {
        { "car",      "content/cars"   },
        { "track",    "content/tracks" },
        { "showroom", "content/showroom" },
        { "driver",   "content/driver3d" },
        { "fonts",    "content/fonts"  },
    };

    QList<AssetMapping> active;
    for (const AssetMapping& m : mappings) {
        QDir src(projectDir + "/" + m.srcFolder);
        if (src.exists()) active.append(m);
    }

    if (active.isEmpty()) {
        // No known asset folder — nothing to deploy. Not an error.
        emit logMessage("No deployable asset folders found; skipping asset copy.");
        return true;
    }

    int totalMappings = active.size();
    int done = 0;

    for (const AssetMapping& m : active) {
        if (m_cancel.load()) return false;

        QString srcPath  = projectDir + "/" + m.srcFolder;
        QString destPath = simPath + "/" + m.destSubdir;

        QDir destDir(destPath);
        if (!destDir.exists() && !destDir.mkpath(".")) {
            emit logMessage("Warning: cannot create destination dir: " + destPath);
            continue;
        }

        // Recursive copy
        QDirIterator it(srcPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (m_cancel.load()) return false;

            QString srcFile = it.next();
            QString rel     = QDir(srcPath).relativeFilePath(srcFile);
            QString dstFile = destPath + "/" + rel;

            QFileInfo fi(srcFile);
            if (fi.isDir()) {
                QDir().mkpath(dstFile);
            } else {
                QDir().mkpath(QFileInfo(dstFile).absolutePath());
                if (QFile::exists(dstFile)) QFile::remove(dstFile);
                if (!QFile::copy(srcFile, dstFile)) {
                    emit logMessage("Warning: could not copy: " + srcFile);
                }
            }
        }

        ++done;
        int p = basePercent + (endPercent - basePercent) * done / totalMappings;
        emit progressUpdated(p);
        emit logMessage("Deployed: " + m.srcFolder + " -> " + destPath);
    }

    return true;
}
