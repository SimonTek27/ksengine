#pragma once

#include <QVector3D>
#include <QVector2D>
#include <QVector>
#include <QMatrix4x4>
#include <QString>
#include <QVariantMap>
#include "sdk/kseditor/engine/mesh/MeshOperations.h"

namespace ks {

// Supported curve representations (CV = control point).
enum class CurveType {
    Polyline,   // straight segments between CVs (softimage "spline" polyline)
    Bezier,     // Bezier curve, degree = controlPoints.size() - 1
    Bspline,    // uniform B-spline, degree configurable, knots generated
    Arc,        // circular arc through 3 CVs
    Circle      // full circle: first CV is center, second CV sets radius/plane normal
};

inline QString curveTypeToString(CurveType t) {
    switch (t) {
    case CurveType::Polyline: return "Polyline";
    case CurveType::Bezier:    return "Bezier";
    case CurveType::Bspline:   return "BSpline";
    case CurveType::Arc:       return "Arc";
    case CurveType::Circle:    return "Circle";
    }
    return "Unknown";
}

inline CurveType curveTypeFromString(const QString& s) {
    if (s == "Polyline") return CurveType::Polyline;
    if (s == "Bezier")   return CurveType::Bezier;
    if (s == "BSpline")  return CurveType::Bspline;
    if (s == "Arc")      return CurveType::Arc;
    if (s == "Circle")   return CurveType::Circle;
    return CurveType::Polyline;
}

// A parametric curve made of control points. Supports evaluation and
// tessellation into a polyline, plus surface generation (loft/sweep/revolve/rail).
struct CurveData {
    CurveType type = CurveType::Polyline;
    QVector<QVector3D> controlPoints;
    int degree = 3;      // used by B-spline
    bool closed = false;

    // --- evaluation ---
    QVector3D evaluate(float t) const;
    // samples `segments` points along the curve (t in [0,1]).
    QVector<QVector3D> tessellate(int segments = 32) const;

    // --- helpers ---
    bool isValid() const { return controlPoints.size() >= (type == CurveType::Circle ? 2 : 2); }
    int cvCount() const { return controlPoints.size(); }
    void setControlPoints(const QVector<QVector3D>& pts) { controlPoints = pts; }

private:
    QVector3D evalPolyline(float t) const;
    QVector3D evalBezier(float t) const;   // De Casteljau
    QVector3D evalBspline(float t) const;  // Cox-de Boor (uniform knots)
    void evalArc(QVector3D center, float radius, QVector3D n0, QVector3D n1, float start, float sweep, float t, QVector3D& out) const;
    bool computeArc(QVector3D& center, float& radius, QVector3D& n0, QVector3D& n1, float& start, float& sweep) const;
};

// Factory helpers for curve primitives.
struct CurvePrimitives {
    static CurveData line(const QVector3D& a, const QVector3D& b);
    static CurveData polyline(const QVector<QVector3D>& pts);
    static CurveData bezier(const QVector<QVector3D>& cv);
    static CurveData bspline(const QVector<QVector3D>& cv, int degree = 3);
    static CurveData arc(const QVector3D& p0, const QVector3D& p1, const QVector3D& p2);
    static CurveData circle(const QVector3D& center, const QVector3D& normal, float radius);
};

// Surface generation from curves (returns polygon MeshData, matching
// MeshOperations::loft/sweep/spin).
struct CurveSurfaces {
    // Ruled surface between two curves with matching CV count.
    static MeshData loft(const CurveData& a, const CurveData& b, int segments = 32, bool close = false);
    // Multi-profile loft.
    static MeshData loftMulti(const QVector<CurveData>& profiles, int segments = 32, bool close = false);
    // Profile extruded along a path curve. `profile` should lie in the XY plane.
    static MeshData sweep(const CurveData& profile, const CurveData& path, int segments = 32);
    // Profile rotated around `axis` through `angle` degrees in `steps`.
    static MeshData revolve(const CurveData& profile, const QVector3D& axis, float angleDeg = 360.0f, int steps = 32);
    // Sweep with a varying scale (like a "rail" with two rails).
    static MeshData rail(const CurveData& rail1, const CurveData& rail2, const CurveData& profile, int segments = 32);
    // Converts a tessellated curve into a ribbon mesh (polyline with thickness)
    // so it can be displayed as a thin surface.
    static MeshData curveRibbon(const CurveData& curve, float width = 0.02f, int segments = 32);
};

// Serialization helper: curve <-> JSON-like map (for the QML bridge).
struct CurveIO {
    static QVariantMap toVariant(const CurveData& c);
    static CurveData fromVariant(const QVariantMap& m);
};

} // namespace ks
