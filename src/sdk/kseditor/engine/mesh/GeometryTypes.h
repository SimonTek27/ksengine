#pragma once

#include <string>
#include <vector>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <QVector>
#include <QVector3D>

namespace ks::geometry {

struct GeoVertex {
    double x, y, z;

    GeoVertex() : x(0), y(0), z(0) {}
    GeoVertex(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}

    GeoVertex operator+(const GeoVertex& o) const { return {x+o.x, y+o.y, z+o.z}; }
    GeoVertex operator-(const GeoVertex& o) const { return {x-o.x, y-o.y, z-o.z}; }
    GeoVertex& operator+=(const GeoVertex& o) { x+=o.x; y+=o.y; z+=o.z; return *this; }
    double length() const { return std::sqrt(x*x + y*y + z*z); }
};

struct GeoFace {
    uint32_t v0, v1, v2;
    GeoFace() : v0(0), v1(0), v2(0) {}
    GeoFace(uint32_t a, uint32_t b, uint32_t c) : v0(a), v1(b), v2(c) {}
};

struct GeoMeshData {
    std::vector<GeoVertex> vertices;
    std::vector<GeoVertex> normals;
    std::vector<GeoFace> faces;

    bool isValid() const { return !vertices.empty() && !faces.empty(); }
    size_t getVertexCount() const { return vertices.size(); }
    size_t getFaceCount() const { return faces.size(); }
    void clear() { vertices.clear(); normals.clear(); faces.clear(); }
};

struct BoolOpResult {
    enum Status { Success = 0, InvalidInput = 1, EmptyResult = 2, ComputationFailed = 3, InvalidGeometry = 4 };
    Status status;
    GeoMeshData result;
    std::string errorMessage;
    double executionTimeMs;
    BoolOpResult() : status(Success), executionTimeMs(0.0) {}
    bool isSuccess() const { return status == Success && result.isValid(); }
};

struct UVCoord {
    float u, v;
    UVCoord() : u(0.0f), v(0.0f) {}
    UVCoord(float u_, float v_) : u(u_), v(v_) {}
};

struct UVIsland {
    uint32_t id;
    std::vector<uint32_t> faceIndices;
    float area;
    float stretchRatio;
    bool isOnBoundary;
    UVIsland() : id(0), area(0.0f), stretchRatio(1.0f), isOnBoundary(false) {}
};

struct NURBSCurve {
    QVector<QVector3D> controlPoints;
    QVector<double> knotVectorU;
    int degreeU;
    bool periodicU;

    NURBSCurve() : degreeU(3), periodicU(false) {}
};

struct NURBSSurface {
    QVector<QVector<QVector3D>> controlPoints; // 2D array of control points
    QVector<double> knotVectorU;
    QVector<double> knotVectorV;
    int degreeU;
    int degreeV;
    bool periodicU;
    bool periodicV;
    double uStart = 0.0;
    double uEnd = 1.0;
    double vStart = 0.0;
    double vEnd = 1.0;

    NURBSSurface() : degreeU(3), degreeV(3), periodicU(false), periodicV(false) {}
};

struct UnwrapResult {
    enum Status { Success = 0, InvalidMesh = 1, NoIslands = 2, PackingFailed = 3, UnwrapFailed = 4 };
    Status status;
    std::vector<UVCoord> uvCoordinates;
    std::vector<UVIsland> islands;
    float packingEfficiency;
    float maxStretchRatio;
    double executionTimeMs;
    std::string errorMessage;
    UnwrapResult() : status(Success), packingEfficiency(0.0f), maxStretchRatio(1.0f), executionTimeMs(0.0) {}
    bool isSuccess() const { return status == Success && !uvCoordinates.empty(); }
};

} // namespace ks::geometry
