#pragma once

#include <QString>
#include <QVector>
#include <QMap>
#include <QVariant>
#include <QByteArray>
#include <QDateTime>

#include "../Math/MathCore.h"
#include "FBXParser.h"

/**
 * @brief CAD Data Type Definitions
 * 
 * Core data structures for representing CAD geometry,
 * curves, surfaces, solids, and assemblies.
 */

namespace CAD {

// ============================================================
// CAD Geometric Entity Types
// ============================================================

enum class CurveType {
    Line,
    Circle,
    Ellipse,
    Parabola,
    Hyperbola,
    BSpline,
    Bezier,
    OffsetCurve,
    TrimmedCurve,
    CompositeCurve
};

enum class SurfaceType {
    Plane,
    Cylinder,
    Cone,
    Sphere,
    Torus,
    BSplineSurface,
    BezierSurface,
    RuledSurface,
    OffsetSurface,
    TrimmedSurface,
    RevolutionSurface,
    ExtrusionSurface
};

enum class SolidType {
    Box,
    Sphere,
    Cylinder,
    Cone,
    Torus,
    Extrusion,
    Revolution,
    Loft,
    Sweep,
    BooleanUnion,
    BooleanIntersection,
    BooleanDifference,
    BrepSolid
};

// ============================================================
// Basic Geometric Structures
// ============================================================

/// 3D Point representation
struct Point3D {
    double x, y, z;
    
    Point3D() : x(0), y(0), z(0) {}
    Point3D(double _x, double _y, double _z) : x(_x), y(_y), z(_z) {}
    
    Vector3 toVector3(float scale = 1.0f) const {
        return Vector3(static_cast<float>(x * scale),
                       static_cast<float>(y * scale),
                       static_cast<float>(z * scale));
    }
};

/// 3D Vector representation
struct Vector3D {
    double x, y, z;
    
    Vector3D() : x(0), y(0), z(0) {}
    Vector3D(double _x, double _y, double _z) : x(_x), y(_y), z(_z) {}
    
    double length() const { return sqrt(x*x + y*y + z*z); }
    
    Vector3D normalized() const {
        double len = length();
        if (len < 1e-10) return Vector3D();
        return Vector3D(x/len, y/len, z/len);
    }
    
    Vector3 toVector3() const {
        return Vector3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
    }
};

/// 4x4 Transformation Matrix (row-major)
struct TransformMatrix {
    double m[4][4];
    
    TransformMatrix() {
        // Identity matrix
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                m[i][j] = (i == j) ? 1.0 : 0.0;
    }
    
    Point3D transform(const Point3D& p) const {
        return Point3D(
            m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3],
            m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3],
            m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3]
        );
    }
    
    Matrix4 toMatrix4() const {
        Matrix4 result;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                result.m[i][j] = static_cast<float>(m[i][j]);
        return result;
    }
};

// ============================================================
// CAD Curve Definition
// ============================================================

/// Parametric curve representation
struct Curve {
    CurveType type;
    QString name;
    
    QVector<double> parameters;      // Generic parameter storage
    QVector<Point3D> controlPoints;   // Control points (NURBS/Bezier)
    QVector<double> knots;            // Knot vector (NURBS)
    int degree;                        // Polynomial degree
    bool isPeriodic;                   // Periodic/closed curve
    
    QVector<double> weights;           // NURBS weights
    
    double startParam;                 // Domain start
    double endParam;                   // Domain end
};

// ============================================================
// CAD Surface Definition
// ============================================================

/// Parametric surface representation
struct Surface {
    SurfaceType type;
    QString name;
    
    QVector<double> parameters;
    QVector<Point3D> controlPoints;
    QVector<double> uKnots;
    QVector<double> vKnots;
    int uDegree;
    int vDegree;
    bool uPeriodic;
    bool vPeriodic;
    
    QVector<double> weights;
    
    double uStart, uEnd;
    double vStart, vEnd;
};

// ============================================================
// CAD Edge Definition
// ============================================================

/// Trimmed curve on surface edge
struct Edge {
    QString name;
    Curve curve;
    Surface* surface;           // Associated surface
    Point3D startPoint;
    Point3D endPoint;
    bool isReversed;
    double tolerance;
};

// ============================================================
// CAD Face Definition
// ============================================================

/// Bounded surface face
struct Face {
    QString name;
    Surface surface;
    QVector<QVector<Edge>> boundaries;  // Outer boundary + holes
    QVector<Point3D> triangulation;     // Pre-triangulated data (optional)
    QVector<int> triangles;
    bool isOriented;
};

// ============================================================
// CAD Solid Definition
// ============================================================

/// 3D Solid body
struct Solid {
    QString name;
    SolidType type;
    QVector<Face> faces;
    QVector<Edge> edges;
    QVector<Point3D> vertices;
    
    // For primitive solids
    Point3D center;
    Vector3D dimensions;        // width, height, depth
    double radius;
    
    // For Boolean operations
    Solid* leftOperand;
    Solid* rightOperand;
    
    // Tessellation data (for visualization)
    // Note: Using simple Vec3 arrays instead of FBX::Vertex/Polygon to avoid namespace issues
    QVector<Vector3> tessellatedVertices;
    QVector<QVector<int>> tessellatedPolygons;
    float tessellationQuality;  // 0.0 to 1.0
};

// ============================================================
// CAD Component & Assembly
// ============================================================

/// Component in an assembly hierarchy
struct Component {
    QString name;
    QString id;
    TransformMatrix transform;
    QVector<Solid> solids;
    QVector<Component> children;
    
    QMap<QString, QString> properties;
    QString material;
    QString color;
};

/// Assembly structure
struct Assembly {
    QString name;
    QString version;
    QString author;
    QDateTime creationDate;
    Component rootComponent;
    QMap<QString, QString> metadata;
};

// ============================================================
// Complete CAD File Representation
// ============================================================

/// Complete CAD file model
struct File {
    QString format;           // "STEP", "IGES", "STL", "OBJ", "DXF", "BREP"
    QString version;
    QString sourceFile;
    
    Assembly assembly;
    QByteArray rawData;       // Format-specific raw data
    
    // Statistics
    quint64 faceCount;
    quint64 edgeCount;
    quint64 vertexCount;
    quint64 solidCount;
    
    // Tolerance settings
    double linearTolerance;
    double angularTolerance;
};

} // namespace CAD
