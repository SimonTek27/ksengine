#include "TrackBuilderTools.h"
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QVector3D>
#include <QSet>
#include "sdk/kseditor/engine/FileFormat/FBXParser.h"

// ============================================================================
// Project management
// ============================================================================

TrackBuilderTools::TrackProject TrackBuilderTools::createProject(const QString& path, const QString& name) {
    TrackProject project;
    project.name = name;
    project.path = path;
    project.trackName = name;
    project.created = QDateTime::currentDateTime();
    project.modified = project.created;

    // Create directory structure
    QDir().mkpath(path);
    QDir().mkpath(path + "/data");
    QDir().mkpath(path + "/map");
    QDir().mkpath(path + "/ui");
    QDir().mkpath(path + "/ai");
    QDir().mkpath(path + "/models");

    return project;
}

TrackBuilderTools::TrackProject TrackBuilderTools::loadProject(const QString& path) {
    TrackProject project;
    project.path = path;

    QString jsonPath = path + "/track_project.json";
    QFile file(jsonPath);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray data = file.readAll();
        file.close();

        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            project.name = obj["name"].toString();
            project.trackName = obj["trackName"].toString();
            project.author = obj["author"].toString();
            project.description = obj["description"].toString();
            project.length = obj["length"].toDouble();
            project.pitboxCount = obj["pitboxCount"].toInt();
            project.hasNightLighting = obj["hasNightLighting"].toBool();
            project.hasPitboxes = obj["hasPitboxes"].toBool();
        }
    }

    return project;
}

bool TrackBuilderTools::saveProject(const TrackProject& project) {
    QJsonObject obj;
    obj["name"] = project.name;
    obj["trackName"] = project.trackName;
    obj["author"] = project.author;
    obj["description"] = project.description;
    obj["length"] = project.length;
    obj["pitboxCount"] = project.pitboxCount;
    obj["hasNightLighting"] = project.hasNightLighting;
    obj["hasPitboxes"] = project.hasPitboxes;
    obj["created"] = project.created.toString(Qt::ISODate);
    obj["modified"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QFile file(project.path + "/track_project.json");
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QJsonDocument doc(obj);
    file.write(doc.toJson());
    file.close();

    return true;
}

bool TrackBuilderTools::validateProject(const QString& path, QString* error) {
    // Check required directories
    QStringList requiredDirs;
    requiredDirs << "data" << "map" << "ui";

    for (const QString& dir : requiredDirs) {
        if (!QDir(path + "/" + dir).exists()) {
            if (error) *error = "Missing required directory: " + dir;
            return false;
        }
    }

    // Check required files
    QStringList requiredFiles = getRequiredFiles();
    for (const QString& file : requiredFiles) {
        if (!QFile::exists(path + "/" + file)) {
            if (error) *error = "Missing required file: " + file;
            return false;
        }
    }

    return true;
}

// ============================================================================
// Track structure
// ============================================================================

bool TrackBuilderTools::initializeTrackStructure(const QString& path) {
    QDir().mkpath(path);
    QDir().mkpath(path + "/data");
    QDir().mkpath(path + "/map");
    QDir().mkpath(path + "/ui");
    QDir().mkpath(path + "/ai");
    QDir().mkpath(path + "/models");
    QDir().mkpath(path + "/models/environments");
    QDir().mkpath(path + "/models/grass");

    return createDefaultFiles(path);
}

bool TrackBuilderTools::createDefaultFiles(const QString& path) {
    // Create ui_track.json
    TrackProject project;
    project.name = QFileInfo(path).fileName();
    project.trackName = project.name;
    createUiTrackJson(project, path);

    // Create surfaces.ini
    createSurfacesIni(path);

    // Generate track map
    createMapPng(path);

    // Create models.ini
    createModelsIni(path);

    return true;
}

QStringList TrackBuilderTools::getRequiredFiles() {
    return QStringList() << "data/surfaces.ini" << "ui/ui_track.json" << "map.png";
}

QStringList TrackBuilderTools::getOptionalFiles() {
    return QStringList() << "data/ai_hints.ini" << "data/grass.ini" << "data/terrain.ini"
                         << "data/cameras.ini" << "models.ini" << "ui/ui_map.png";
}

// ============================================================================
// Mesh management
// ============================================================================

QVector<TrackBuilderTools::TrackMesh> TrackBuilderTools::scanMeshes(const QString& fbxPath) {
    QVector<TrackMesh> meshes;

    ks::FBXParser parser;
    if (!parser.loadFromFile(fbxPath.toStdString())) {
        return meshes;
    }

    for (const auto& fbxMesh : parser.scene().meshes) {
        TrackMesh tm;
        tm.name = QString::fromStdString(fbxMesh.name);
        tm.vertexCount = fbxMesh.vertices.size();
        tm.triangleCount = fbxMesh.indices.size() / 3;
        tm.materialName = QString::fromStdString(fbxMesh.materialName);
        meshes.append(tm);
    }

    return meshes;
}

bool TrackBuilderTools::validateMeshes(const QString& fbxPath, QString* error) {
    QFile file(fbxPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QString("Cannot open file: %1").arg(fbxPath);
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    if (data.size() < 27) {
        if (error) *error = "File too small to be a valid FBX";
        return false;
    }

    char magic[21];
    memcpy(magic, data.constData(), 20);
    magic[20] = '\0';
    if (strncmp(magic, "Kaydara FBX Binary  \x00", 20) != 0) {
        if (error) *error = "Not a valid FBX binary file";
        return false;
    }

    return true;
}

bool TrackBuilderTools::optimizeMesh(const QString& inputPath, const QString& outputPath) {
    QFileInfo info(inputPath);
    QString ext = info.suffix().toLower();

    if (ext == "obj") {
        QFile in(inputPath);
        if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

        QTextStream stream(&in);
        QStringList outLines;
        QSet<QString> seenVertices;
        int vertexCount = 0;
        int removedCount = 0;

        while (!stream.atEnd()) {
            QString line = stream.readLine().trimmed();
            if (line.startsWith("v ")) {
                // Deduplicate vertices
                if (!seenVertices.contains(line)) {
                    seenVertices.insert(line);
                    outLines.append(line);
                    vertexCount++;
                } else {
                    removedCount++;
                }
            } else if (line.startsWith("f ")) {
                // Remove degenerate faces (faces with < 3 indices)
                QStringList parts = line.mid(2).split(' ', Qt::SkipEmptyParts);
                if (parts.size() >= 3) {
                    outLines.append(line);
                } else {
                    removedCount++;
                }
            } else {
                outLines.append(line);
            }
        }
        in.close();

        qDebug() << "Mesh optimization: " << vertexCount << " vertices kept," << removedCount << " removed";

        QFile out(outputPath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
        QTextStream outStream(&out);
        for (const QString& l : outLines) {
            outStream << l << "\n";
        }
        return true;
    }

    // For other formats, just copy
    return QFile::copy(inputPath, outputPath);
}

// ============================================================================
// Start/pit positions
// ============================================================================

QVector<TrackBuilderTools::StartPosition> TrackBuilderTools::loadStartPositions(const QString& trackPath) {
    QVector<StartPosition> positions;

    QString jsonPath = trackPath + "/ui/ui_track.json";
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return positions;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        QJsonArray startArray = obj["start_position"].toArray();

        for (int i = 0; i < startArray.size(); ++i) {
            QJsonObject posObj = startArray[i].toObject();
            StartPosition pos;
            pos.carIndex = i;
            pos.position[0] = posObj["x"].toDouble();
            pos.position[1] = posObj["y"].toDouble();
            pos.position[2] = posObj["z"].toDouble();
            positions.append(pos);
        }
    }

    return positions;
}

bool TrackBuilderTools::saveStartPositions(const QVector<StartPosition>& positions, const QString& trackPath) {
    QString jsonPath = trackPath + "/ui/ui_track.json";
    QFile file(jsonPath);

    QJsonObject obj;
    QJsonArray startArray;

    for (const StartPosition& pos : positions) {
        QJsonObject posObj;
        posObj["x"] = pos.position[0];
        posObj["y"] = pos.position[1];
        posObj["z"] = pos.position[2];
        startArray.append(posObj);
    }

    obj["start_position"] = startArray;

    QJsonDocument doc(obj);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    file.write(doc.toJson());
    file.close();

    return true;
}

TrackBuilderTools::StartPosition TrackBuilderTools::createStartPosition(int carIndex, const float* position, const float* direction) {
    StartPosition pos;
    pos.carIndex = carIndex;
    for (int i = 0; i < 3; ++i) {
        pos.position[i] = position[i];
        pos.direction[i] = direction[i];
    }
    return pos;
}

QVector<TrackBuilderTools::PitPosition> TrackBuilderTools::loadPitPositions(const QString& trackPath) {
    QVector<PitPosition> positions;

    QString jsonPath = trackPath + "/ui/ui_track.json";
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return positions;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isObject()) {
        QJsonObject obj = doc.object();
        QJsonArray pitArray = obj["pitboxes"].toArray();

        for (int i = 0; i < pitArray.size(); ++i) {
            QJsonObject pitObj = pitArray[i].toObject();
            PitPosition pos;
            pos.index = i;
            pos.position[0] = pitObj["x"].toDouble();
            pos.position[1] = pitObj["y"].toDouble();
            pos.position[2] = pitObj["z"].toDouble();
            pos.direction[0] = pitObj["dx"].toDouble(0);
            pos.direction[1] = pitObj["dy"].toDouble(0);
            pos.direction[2] = pitObj["dz"].toDouble(1);
            positions.append(pos);
        }
    }

    return positions;
}

bool TrackBuilderTools::savePitPositions(const QVector<PitPosition>& positions, const QString& trackPath) {
    QString jsonPath = trackPath + "/ui/ui_track.json";
    QFile file(jsonPath);

    QJsonObject obj;
    QJsonArray pitArray;

    for (const PitPosition& pos : positions) {
        QJsonObject pitObj;
        pitObj["x"] = pos.position[0];
        pitObj["y"] = pos.position[1];
        pitObj["z"] = pos.position[2];
        pitObj["dx"] = pos.direction[0];
        pitObj["dy"] = pos.direction[1];
        pitObj["dz"] = pos.direction[2];
        pitArray.append(pitObj);
    }

    obj["pitboxes"] = pitArray;

    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    file.write(QJsonDocument(obj).toJson());
    file.close();
    return true;
}

TrackBuilderTools::PitPosition TrackBuilderTools::createPitPosition(int index, const float* position, const float* direction) {
    PitPosition pos;
    pos.index = index;
    for (int i = 0; i < 3; ++i) {
        pos.position[i] = position[i];
        pos.direction[i] = direction[i];
    }
    return pos;
}

// ============================================================================
// Camera management
// ============================================================================

QVector<TrackBuilderTools::CameraPosition> TrackBuilderTools::loadCameras(const QString& trackPath) {
    QVector<CameraPosition> cameras;

    QString iniPath = trackPath + "/data/cameras.ini";
    QFile file(iniPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return cameras;
    }

    QTextStream stream(&file);
    CameraPosition currentCamera;
    bool inCamera = false;

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();

        if (line.startsWith('[') && line.endsWith(']')) {
            if (inCamera) {
                cameras.append(currentCamera);
            }

            QString section = line.mid(1, line.length() - 2);
            inCamera = section.startsWith("CAMERA_");

            if (inCamera) {
                currentCamera = CameraPosition();
            }
        } else if (inCamera && line.contains('=')) {
            int eqPos = line.indexOf('=');
            QString key = line.left(eqPos).trimmed().toUpper();
            QString value = line.mid(eqPos + 1).trimmed();

            if (key == "POSITION") {
                QStringList pos = value.split(',');
                if (pos.size() >= 3) {
                    currentCamera.position[0] = pos[0].trimmed().toFloat();
                    currentCamera.position[1] = pos[1].trimmed().toFloat();
                    currentCamera.position[2] = pos[2].trimmed().toFloat();
                }
            } else if (key == "TARGET") {
                QStringList pos = value.split(',');
                if (pos.size() >= 3) {
                    currentCamera.target[0] = pos[0].trimmed().toFloat();
                    currentCamera.target[1] = pos[1].trimmed().toFloat();
                    currentCamera.target[2] = pos[2].trimmed().toFloat();
                }
            } else if (key == "FOV") {
                currentCamera.fov = value.toFloat();
            }
        }
    }

    if (inCamera) {
        cameras.append(currentCamera);
    }

    file.close();
    return cameras;
}

bool TrackBuilderTools::saveCameras(const QVector<CameraPosition>& cameras, const QString& trackPath) {
    QString iniPath = trackPath + "/data/cameras.ini";
    QFile file(iniPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "; Camera Configuration\n";
    stream << "; Generated by ksEditor\n\n";

    for (int i = 0; i < cameras.size(); ++i) {
        const CameraPosition& cam = cameras[i];
        stream << "[CAMERA_" << i << "]\n";
        stream << "NAME=" << cam.name << "\n";
        stream << "POSITION=" << cam.position[0] << "," << cam.position[1] << "," << cam.position[2] << "\n";
        stream << "TARGET=" << cam.target[0] << "," << cam.target[1] << "," << cam.target[2] << "\n";
        stream << "FOV=" << cam.fov << "\n\n";
    }

    file.close();
    return true;
}

// ============================================================================
// FBX export
// ============================================================================

bool TrackBuilderTools::exportToFbx(const QString& inputPath, const QString& outputPath,
                                     const TrackProject& project) {
    // Based on nothke/blender_ac_exporter:
    // - Ensure all objects are unlinked
    // - Ensure all objects have materials
    // - Check vertex count limits (65k)
    // - Set correct FBX export settings

    // This would interface with a 3D application or FBX SDK
    return QFile::copy(inputPath, outputPath);
}

bool TrackBuilderTools::validateFbxExport(const QString& fbxPath, QString* error) {
    // Validate FBX file for AC compatibility
    QFileInfo info(fbxPath);
    if (!info.exists()) {
        if (error) *error = "FBX file not found";
        return false;
    }

    if (info.size() == 0) {
        if (error) *error = "FBX file is empty";
        return false;
    }

    return true;
}

// ============================================================================
// Track validation
// ============================================================================

bool TrackBuilderTools::validateTrackData(const QString& trackPath, QString* error) {
    // Validate track data files
    QStringList requiredFiles;
    requiredFiles << "data/surfaces.ini" << "ui/ui_track.json";

    for (const QString& file : requiredFiles) {
        if (!QFile::exists(trackPath + "/" + file)) {
            if (error) *error = "Missing required file: " + file;
            return false;
        }
    }

    return true;
}

bool TrackBuilderTools::validateSurfaces(const QString& trackPath, QString* error) {
    QString surfacesPath = trackPath + "/data/surfaces.ini";
    if (!QFile::exists(surfacesPath)) {
        if (error) *error = "Missing surfaces.ini";
        return false;
    }

    // Validate surface definitions
    QFile file(surfacesPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = "Cannot open surfaces.ini";
        return false;
    }

    QTextStream stream(&file);
    bool hasSurfaces = false;

    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.startsWith("[SURFACE_")) {
            hasSurfaces = true;
            break;
        }
    }

    file.close();

    if (!hasSurfaces) {
        if (error) *error = "No surfaces defined in surfaces.ini";
        return false;
    }

    return true;
}

bool TrackBuilderTools::validateAiLine(const QString& trackPath, QString* error) {
    QString aiPath = trackPath + "/ai/fast_lane.ai";
    if (!QFile::exists(aiPath)) {
        if (error) *error = "Missing AI line (fast_lane.ai)";
        return false;
    }

    return true;
}

// ============================================================================
// Utility
// ============================================================================

float TrackBuilderTools::calculateTrackLength(const QString& trackPath) {
    QString aiPath = trackPath + "/ai/fast_lane.ai";
    if (!QFile::exists(aiPath)) {
        return 0.0f;
    }

    QFile file(aiPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return 0.0f;

    float totalLength = 0.0f;
    QVector3D prevPoint;
    bool hasPrev = false;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;

        QStringList parts = line.split(',');
        if (parts.size() >= 3) {
            float x = parts[0].toFloat();
            float y = parts[1].toFloat();
            float z = parts[2].toFloat();
            QVector3D point(x, y, z);
            if (hasPrev) {
                totalLength += (point - prevPoint).length();
            }
            prevPoint = point;
            hasPrev = true;
        }
    }
    file.close();
    return totalLength;
}

QStringList TrackBuilderTools::getSurfaceTypes() {
    return QStringList() << "ROAD" << "GRASS" << "GRAVEL" << "SAND" << "KERB"
                         << "WALL" << "PIT" << "OUT" << "RUMBLE";
}

QString TrackBuilderTools::getDefaultSurfaceForMesh(const QString& meshName) {
    QString upper = meshName.toUpper();

    if (upper.contains("ROAD") || upper.contains("TARMAC")) return "ROAD";
    if (upper.contains("GRASS")) return "GRASS";
    if (upper.contains("GRAVEL")) return "GRAVEL";
    if (upper.contains("SAND")) return "SAND";
    if (upper.contains("KERB") || upper.contains("CURB")) return "KERB";
    if (upper.contains("WALL") || upper.contains("BARRIER")) return "WALL";
    if (upper.contains("PIT")) return "PIT";

    return "ROAD"; // Default
}

// ============================================================================
// Private helpers
// ============================================================================

bool TrackBuilderTools::createUiTrackJson(const TrackProject& project, const QString& path) {
    QJsonObject obj;
    obj["name"] = project.trackName;
    obj["description"] = project.description;
    obj["author"] = project.author;
    obj["version"] = "1.0";
    obj["length"] = project.length;

    QJsonArray startArray;
    QJsonObject startObj;
    startObj["x"] = 0;
    startObj["y"] = 0;
    startObj["z"] = 0;
    startArray.append(startObj);
    obj["start_position"] = startArray;

    obj["pitboxes"] = project.pitboxCount;
    obj["map"] = "map.png";

    QFile file(path + "/ui/ui_track.json");
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QJsonDocument doc(obj);
    file.write(doc.toJson());
    file.close();

    return true;
}

bool TrackBuilderTools::createSurfacesIni(const QString& path) {
    QFile file(path + "/data/surfaces.ini");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "; Track Surface Definitions\n";
    stream << "; Generated by ksEditor\n\n";

    stream << "[SURFACE_0]\n";
    stream << "NAME=ROAD\n";
    stream << "GRIP_K=1.0\n";
    stream << "GRIP_M=1.0\n";
    stream << "ROLLING_RESISTANCE=0.015\n";
    stream << "VIBRATION_GAIN=1.0\n";
    stream << "PARTICLES_GAIN=0.3\n\n";

    stream << "[SURFACE_1]\n";
    stream << "NAME=GRASS\n";
    stream << "GRIP_K=0.5\n";
    stream << "GRIP_M=0.5\n";
    stream << "ROLLING_RESISTANCE=0.05\n";
    stream << "VIBRATION_GAIN=1.5\n";
    stream << "PARTICLES_GAIN=1.0\n\n";

    stream << "[SURFACE_2]\n";
    stream << "NAME=KERB\n";
    stream << "GRIP_K=0.9\n";
    stream << "GRIP_M=0.9\n";
    stream << "ROLLING_RESISTANCE=0.02\n";
    stream << "VIBRATION_GAIN=2.0\n";
    stream << "PARTICLES_GAIN=0.2\n\n";

    file.close();
    return true;
}

bool TrackBuilderTools::createMapPng(const QString& path) {
    QImage mapImage(1024, 1024, QImage::Format_RGB32);
    mapImage.fill(QColor("#1a1a2e"));

    QPainter painter(&mapImage);
    painter.setPen(QPen(QColor("#3a3a5e"), 2));
    painter.setBrush(Qt::NoBrush);

    int gridSize = 32;
    for (int i = 0; i <= gridSize; ++i) {
        int x = i * 1024 / gridSize;
        painter.drawLine(x, 0, x, 1024);
        painter.drawLine(0, x, 1024, x);
    }

    painter.setPen(QPen(QColor("#4488cc"), 3));
    QPainterPath centerLine;
    centerLine.moveTo(512, 100);
    centerLine.cubicTo(200, 400, 800, 600, 512, 900);
    painter.drawPath(centerLine);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor("#225588"));
    painter.drawEllipse(QPoint(512, 100), 20, 20);
    painter.drawEllipse(QPoint(512, 900), 20, 20);

    painter.end();
    return mapImage.save(path);
}

bool TrackBuilderTools::createModelsIni(const QString& path) {
    QFile file(path + "/models.ini");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    stream << "; Track Models Configuration\n";
    stream << "; Generated by ksEditor\n\n";
    stream << "[MODEL_0]\n";
    stream << "FILE=track.kn5\n";
    stream << "POSITION=0,0,0\n";
    stream << "ROTATION=0,0,0\n";
    stream << "SCALE=1,1,1\n";

    file.close();
    return true;
}

// ============================================================================
// TrackBuilderManager implementation
// ============================================================================

TrackBuilderManager::TrackBuilderManager(const QString& projectPath)
    : m_projectPath(projectPath) {
}

bool TrackBuilderManager::createProject(const QString& name) {
    m_project = TrackBuilderTools::createProject(m_projectPath, name);
    return TrackBuilderTools::initializeTrackStructure(m_projectPath);
}

bool TrackBuilderManager::loadProject() {
    m_project = TrackBuilderTools::loadProject(m_projectPath);
    return !m_project.name.isEmpty();
}

bool TrackBuilderManager::saveProject() {
    return TrackBuilderTools::saveProject(m_project);
}

bool TrackBuilderManager::validate(QString* error) {
    return TrackBuilderTools::validateProject(m_projectPath, error);
}

bool TrackBuilderManager::scanMeshes() {
    // Scan for FBX files in the project
    QDir dir(m_projectPath);
    QStringList fbxFiles = dir.entryList(QStringList() << "*.fbx", QDir::Files);

    for (const QString& fbx : fbxFiles) {
        QVector<TrackBuilderTools::TrackMesh> meshes = TrackBuilderTools::scanMeshes(m_projectPath + "/" + fbx);
        m_meshes.append(meshes);
    }

    return true;
}

bool TrackBuilderManager::validateMeshes(QString* error) {
    for (const TrackBuilderTools::TrackMesh& mesh : m_meshes) {
        if (mesh.vertexCount > 65000) {
            if (error) *error = "Mesh '" + mesh.name + "' has too many vertices (max 65000)";
            return false;
        }
    }
    return true;
}

bool TrackBuilderManager::addStartPosition(const TrackBuilderTools::StartPosition& position) {
    m_startPositions.append(position);
    return TrackBuilderTools::saveStartPositions(m_startPositions, m_projectPath);
}

bool TrackBuilderManager::addPitPosition(const TrackBuilderTools::PitPosition& position) {
    m_pitPositions.append(position);
    return TrackBuilderTools::savePitPositions(m_pitPositions, m_projectPath);
}

bool TrackBuilderManager::removeStartPosition(int index) {
    if (index >= 0 && index < m_startPositions.size()) {
        m_startPositions.removeAt(index);
        return TrackBuilderTools::saveStartPositions(m_startPositions, m_projectPath);
    }
    return false;
}

bool TrackBuilderManager::removePitPosition(int index) {
    if (index >= 0 && index < m_pitPositions.size()) {
        m_pitPositions.removeAt(index);
        return TrackBuilderTools::savePitPositions(m_pitPositions, m_projectPath);
    }
    return false;
}

bool TrackBuilderManager::exportToFbx(const QString& outputPath) {
    return TrackBuilderTools::exportToFbx(m_projectPath + "/track.fbx", outputPath, m_project);
}
