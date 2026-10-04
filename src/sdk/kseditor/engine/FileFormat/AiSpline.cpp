#include "AiSpline.h"
#include <QFile>
#include <QDataStream>
#include <QRegularExpression>

namespace ks {
namespace ai {

QVariantMap AiPoint::toVariant() const {
    QVariantMap map;
    map["x"] = position.x;
    map["y"] = position.y;
    map["z"] = position.z;
    map["curvature"] = curvature;
    map["speed"] = speed;
    map["lap"] = lap;
    map["distance"] = distance;
    return map;
}

AiPoint AiPoint::fromVariant(const QVariantMap& map) {
    AiPoint point;
    point.position.x = map.value("x", 0.0).toFloat();
    point.position.y = map.value("y", 0.0).toFloat();
    point.position.z = map.value("z", 0.0).toFloat();
    point.curvature = map.value("curvature", 0.0).toFloat();
    point.speed = map.value("speed", 0.0).toFloat();
    point.lap = map.value("lap", 0).toInt();
    point.distance = map.value("distance", 0.0).toFloat();
    return point;
}

QVariantMap AiPointExtra::toVariant() const {
    QVariantMap map;
    map["nx"] = normal.x;
    map["ny"] = normal.y;
    map["nz"] = normal.z;
    map["width"] = width;
    map["comfort"] = comfort;
    map["tag"] = tag;
    return map;
}

AiPointExtra AiPointExtra::fromVariant(const QVariantMap& map) {
    AiPointExtra extra;
    extra.normal.x = map.value("nx", 0.0).toFloat();
    extra.normal.y = map.value("ny", 1.0).toFloat();
    extra.normal.z = map.value("nz", 0.0).toFloat();
    extra.width = map.value("width", 8.0).toFloat();
    extra.comfort = map.value("comfort", 0.5).toFloat();
    extra.tag = map.value("tag", "").toString();
    return extra;
}

QVariantMap AiSpline::toVariant() const {
    QVariantMap map;
    map["name"] = name;
    map["totalDistance"] = totalDistance;
    map["laps"] = laps;
    
    QVariantList pointsList;
    for (const AiPoint& pt : points) {
        pointsList.append(pt.toVariant());
    }
    map["points"] = pointsList;
    
    QVariantList extrasList;
    for (const AiPointExtra& ex : extras) {
        extrasList.append(ex.toVariant());
    }
    map["extras"] = extrasList;
    
    return map;
}

AiSpline AiSpline::fromVariant(const QVariantMap& map) {
    AiSpline spline;
    spline.name = map.value("name", "").toString();
    spline.totalDistance = map.value("totalDistance", 0.0).toFloat();
    spline.laps = map.value("laps", 1).toInt();
    
    QVariantList pointsList = map.value("points").toList();
    for (const QVariant& v : pointsList) {
        spline.points.append(AiPoint::fromVariant(v.toMap()));
    }
    
    QVariantList extrasList = map.value("extras").toList();
    for (const QVariant& v : extrasList) {
        spline.extras.append(AiPointExtra::fromVariant(v.toMap()));
    }
    
    return spline;
}

QVariantMap AiSplineGrid::toVariant() const {
    QVariantMap map;
    map["trackName"] = trackName;
    
    QVariantList splinesList;
    for (const AiSpline& s : splines) {
        splinesList.append(s.toVariant());
    }
    map["splines"] = splinesList;
    
    return map;
}

AiSplineGrid AiSplineGrid::fromVariant(const QVariantMap& map) {
    AiSplineGrid grid;
    grid.trackName = map.value("trackName", "").toString();
    
    QVariantList splinesList = map.value("splines").toList();
    for (const QVariant& v : splinesList) {
        grid.splines.append(AiSpline::fromVariant(v.toMap()));
    }
    
    return grid;
}

AiSpline AiFileReader::readSpline(const QString& filename) {
    AiSplineGrid grid = readGrid(filename);
    if (grid.splines.isEmpty()) {
        return AiSpline();
    }
    return grid.splines.first();
}

AiSplineGrid AiFileReader::readGrid(const QString& filename) {
    AiSplineGrid grid;
    
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) {
        return grid;
    }
    
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    quint32 magic;
    stream >> magic;
    
    if (magic == 0x41495000) { // "AIP\0"
        stream.skipRawData(4);
        
        QString trackName;
        stream >> trackName;
        grid.trackName = trackName;
        
        quint32 splineCount;
        stream >> splineCount;
        
        for (quint32 i = 0; i < splineCount; i++) {
            AiSpline spline;
            
            QString splineName;
            stream >> splineName;
            spline.name = splineName;
            
            quint32 pointCount;
            stream >> pointCount;
            
            for (quint32 j = 0; j < pointCount; j++) {
                AiPoint point;
                float x, y, z;
                stream >> x >> y >> z;
                point.position = Vector3(x, y, z);
                stream >> point.curvature >> point.speed >> point.lap;
                spline.points.append(point);
            }
            
            quint32 extraCount;
            stream >> extraCount;
            
            for (quint32 j = 0; j < extraCount; j++) {
                AiPointExtra extra;
                float nx, ny, nz;
                stream >> nx >> ny >> nz;
                extra.normal = Vector3(nx, ny, nz);
                stream >> extra.width >> extra.comfort;
                spline.extras.append(extra);
            }
            
            stream >> spline.totalDistance >> spline.laps;
            grid.splines.append(spline);
        }
    }
    
    return grid;
}

bool AiFileReader::read(const QString& filename, AiSplineGrid& outGrid) {
    outGrid = readGrid(filename);
    return outGrid.isValid();
}

bool AiFileWriter::writeSpline(const QString& filename, const AiSpline& spline) {
    AiSplineGrid grid;
    grid.splines.append(spline);
    return writeGrid(filename, grid);
}

bool AiFileWriter::writeGrid(const QString& filename, const AiSplineGrid& grid) {
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    stream << quint32(0x41495000); // "AIP\0"
    stream << quint32(0);
    
    stream << grid.trackName;
    stream << quint32(grid.splines.size());
    
    for (const AiSpline& spline : grid.splines) {
        stream << spline.name;
        stream << quint32(spline.points.size());
        
        for (const AiPoint& point : spline.points) {
            stream << point.position.x << point.position.y << point.position.z;
            stream << point.curvature << point.speed << point.lap;
        }
        
        stream << quint32(spline.extras.size());
        
        for (const AiPointExtra& extra : spline.extras) {
            stream << extra.normal.x << extra.normal.y << extra.normal.z;
            stream << extra.width << extra.comfort;
        }
        
        stream << spline.totalDistance << spline.laps;
    }
    
    file.close();
    return true;
}

bool AiFileWriter::write(const QString& filename, const AiSplineGrid& grid) {
    return writeGrid(filename, grid);
}

AiSplines::AiSplines(QObject* parent)
    : QObject(parent) {
}

AiSplines::~AiSplines() {
}

bool AiSplines::load(const QString& filename) {
    m_filename = filename;
    m_grid = AiFileReader::readGrid(filename);
    m_loaded = m_grid.isValid();
    
    if (m_loaded) {
        computeDistances();
        emit loaded();
    }
    
    return m_loaded;
}

bool AiSplines::save(const QString& filename) {
    QString targetFile = filename.isEmpty() ? m_filename : filename;
    bool success = AiFileWriter::writeGrid(targetFile, m_grid);
    
    if (success) {
        m_filename = targetFile;
        emit loaded();
    }
    
    return success;
}

void AiSplines::clear() {
    m_grid.splines.clear();
    m_loaded = false;
    m_filename.clear();
}

void AiSplines::setGrid(const AiSplineGrid& grid) {
    m_grid = grid;
    m_loaded = true;
    computeDistances();
    emit modified();
}

void AiSplines::computeDistances() {
    for (AiSpline& spline : m_grid.splines) {
        float distance = 0.0f;
        for (int i = 0; i < spline.points.size(); i++) {
            if (i > 0) {
                Vector3 diff = spline.points[i].position - spline.points[i-1].position;
                distance += length(diff);
            }
            spline.points[i].distance = distance;
        }
        spline.totalDistance = distance;
    }
}

float AiSplines::getTotalDistance(int splineIndex) const {
    if (splineIndex < 0 || splineIndex >= m_grid.splines.size()) {
        return 0.0f;
    }
    return m_grid.splines[splineIndex].totalDistance;
}

int AiSplines::getPointCount(int splineIndex) const {
    if (splineIndex < 0 || splineIndex >= m_grid.splines.size()) {
        return 0;
    }
    return m_grid.splines[splineIndex].points.size();
}

QVector<Vector3> AiSplines::getPath(int splineIndex) const {
    QVector<Vector3> path;
    
    if (splineIndex < 0 || splineIndex >= m_grid.splines.size()) {
        return path;
    }
    
    for (const AiPoint& point : m_grid.splines[splineIndex].points) {
        path.append(point.position);
    }
    
    return path;
}

QVector<float> AiSplines::getCurvatures(int splineIndex) const {
    QVector<float> curvatures;
    
    if (splineIndex < 0 || splineIndex >= m_grid.splines.size()) {
        return curvatures;
    }
    
    for (const AiPoint& point : m_grid.splines[splineIndex].points) {
        curvatures.append(point.curvature);
    }
    
    return curvatures;
}

AiSpline* AiSplines::getSpline(int index) {
    if (index < 0 || index >= m_grid.splines.size()) {
        return nullptr;
    }
    return &m_grid.splines[index];
}

}
}