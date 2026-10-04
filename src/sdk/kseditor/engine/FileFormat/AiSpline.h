#pragma once

#include <QString>
#include <QVector>
#include <QMap>
#include <QVariant>
#include <QObject>
#include <QFile>
#include <QDataStream>

#include "engine/Math/MathCore.h"

namespace ks {
namespace ai {

struct AiPoint {
    Vector3 position;
    float curvature = 0.0f;
    float speed = 0.0f;
    int lap = 0;
    float distance = 0.0f;
    
    QVariantMap toVariant() const;
    static AiPoint fromVariant(const QVariantMap& map);
};

struct AiPointExtra {
    Vector3 normal;
    float width = 8.0f;
    float comfort = 0.5f;
    QString tag;
    
    QVariantMap toVariant() const;
    static AiPointExtra fromVariant(const QVariantMap& map);
};

struct AiSpline {
    QString name;
    QVector<AiPoint> points;
    QVector<AiPointExtra> extras;
    float totalDistance = 0.0f;
    int laps = 1;
    
    bool isValid() const { return !points.isEmpty(); }
    int pointCount() const { return points.size(); }
    
    QVariantMap toVariant() const;
    static AiSpline fromVariant(const QVariantMap& map);
};

struct AiSplineGrid {
    QString trackName;
    QVector<AiSpline> splines;
    
    bool isValid() const { return !splines.isEmpty(); }
    int splineCount() const { return splines.size(); }
    
    QVariantMap toVariant() const;
    static AiSplineGrid fromVariant(const QVariantMap& map);
};

class AiFileReader {
public:
    static AiSpline readSpline(const QString& filename);
    static AiSplineGrid readGrid(const QString& filename);
    static bool read(const QString& filename, AiSplineGrid& outGrid);
};

class AiFileWriter {
public:
    static bool writeSpline(const QString& filename, const AiSpline& spline);
    static bool writeGrid(const QString& filename, const AiSplineGrid& grid);
    static bool write(const QString& filename, const AiSplineGrid& grid);
};

class AiSplines : public QObject {
    Q_OBJECT

public:
    explicit AiSplines(QObject* parent = nullptr);
    ~AiSplines();

    bool load(const QString& filename);
    bool save(const QString& filename);
    
    void clear();
    bool isLoaded() const { return m_loaded; }
    QString filename() const { return m_filename; }
    
    const AiSplineGrid& grid() const { return m_grid; }
    void setGrid(const AiSplineGrid& grid);
    
    float getTotalDistance(int splineIndex = 0) const;
    int getPointCount(int splineIndex = 0) const;
    
    QVector<Vector3> getPath(int splineIndex = 0) const;
    QVector<float> getCurvatures(int splineIndex = 0) const;
    
    AiSpline* getSpline(int index);
    
signals:
    void loaded();
    void modified();
    void error(const QString& message);

private:
    void computeDistances();

    QString m_filename;
    bool m_loaded = false;
    AiSplineGrid m_grid;
};

}
}