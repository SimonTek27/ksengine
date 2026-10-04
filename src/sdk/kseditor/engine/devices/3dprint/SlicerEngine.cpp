#include "SlicerEngine.h"
#include "3DPrintTypes.h"
#include "PrinterProfile.h"
#include "SupportGenerator.h"
#include "GeometryUtils.h"
#include <QThread>
#include <QFuture>
#include <QtConcurrent/QtConcurrent>
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <queue>
#include <limits>

namespace ks {
namespace printing {

using namespace geometry;

struct SlicerEngine::Impl {
    MeshTriangles m_triangles;
    BoundingBox m_bounds;
    std::vector<double> m_layerHeights;
    SliceSettings m_settings;
    const PrinterProfile* m_printer = nullptr;

    // Spatial index for fast triangle lookup
    struct SpatialIndex {
        double cellSize = 1.0;
        std::unordered_map<long long, std::vector<size_t>> cells; // hash -> triangle indices

        long long hashCell(int x, int y) const {
            return (static_cast<long long>(x) << 32) ^ static_cast<unsigned int>(y);
        }

        void build(const MeshTriangles& triangles, double cellSize_) {
            cellSize = cellSize_;
            cells.clear();
            for (size_t i = 0; i < triangles.size(); ++i) {
                const auto& tri = triangles[i];
                int minX = static_cast<int>(std::floor(std::min({tri.v[0].x, tri.v[1].x, tri.v[2].x}) / cellSize));
                int maxX = static_cast<int>(std::floor(std::max({tri.v[0].x, tri.v[1].x, tri.v[2].x}) / cellSize));
                int minY = static_cast<int>(std::floor(std::min({tri.v[0].y, tri.v[1].y, tri.v[2].y}) / cellSize));
                int maxY = static_cast<int>(std::floor(std::max({tri.v[0].y, tri.v[1].y, tri.v[2].y}) / cellSize));

                for (int cx = minX; cx <= maxX; ++cx) {
                    for (int cy = minY; cy <= maxY; ++cy) {
                        cells[hashCell(cx, cy)].push_back(i);
                    }
                }
            }
        }

        std::vector<size_t> query(double x, double y) const {
            int cx = static_cast<int>(std::floor(x / cellSize));
            int cy = static_cast<int>(std::floor(y / cellSize));
            auto it = cells.find(hashCell(cx, cy));
            return it != cells.end() ? it->second : std::vector<size_t>();
        }
    } m_spatialIndex;

    void buildSpatialIndex() {
        double cellSize = m_settings.infillLineDistance > 0 ? m_settings.infillLineDistance * 4 : 10.0;
        m_spatialIndex.build(m_triangles, cellSize);
    }

    // Fast triangle-plane intersection
    bool intersectTrianglePlane(const Triangle3D& tri, double z, Vector2* out1, Vector2* out2) const {
        // Check if triangle crosses plane
        double z0 = tri.v[0].z - z;
        double z1 = tri.v[1].z - z;
        double z2 = tri.v[2].z - z;

        int pos = (z0 > 0) + (z1 > 0) + (z2 > 0);
        int neg = (z0 < 0) + (z1 < 0) + (z2 < 0);

        if (pos == 3 || neg == 3) return false; // All on one side

        // Count vertices on plane
        int onPlane = (std::abs(z0) < 1e-10) + (std::abs(z1) < 1e-10) + (std::abs(z2) < 1e-10);

        if (onPlane == 3) return false; // Degenerate - triangle in plane

        if (onPlane == 2) {
            // Edge on plane - return the edge
            Vector2 pts[2];
            int idx = 0;
            for (int i = 0; i < 3; ++i) {
                double zi = (i==0) ? z0 : (i==1) ? z1 : z2;
                if (std::abs(zi) < 1e-10) {
                    pts[idx++] = {tri.v[i].x, tri.v[i].y};
                }
            }
            *out1 = pts[0];
            *out2 = pts[1];
            return true;
        }

        if (onPlane == 1) {
            // One vertex on plane - return two segments
            Vector2 onPlanePt;
            Vector2 otherPts[2];
            int otherIdx = 0;
            for (int i = 0; i < 3; ++i) {
                double zi = (i==0) ? z0 : (i==1) ? z1 : z2;
                if (std::abs(zi) < 1e-10) {
                    onPlanePt = {tri.v[i].x, tri.v[i].y};
                } else {
                    otherPts[otherIdx++] = {tri.v[i].x, tri.v[i].y};
                }
            }
            *out1 = onPlanePt;
            *out2 = otherPts[0];
            return true;
        }

        // General case - triangle crosses plane
        Vector2 intersections[2];
        int intCount = 0;

        for (int i = 0, j = 2; i < 3; j = i++) {
            double zi = (i==0) ? z0 : (i==1) ? z1 : z2;
            double zj = (j==0) ? z0 : (j==1) ? z1 : z2;

            if ((zi > 0 && zj < 0) || (zi < 0 && zj > 0)) {
                double t = zi / (zi - zj);
                intersections[intCount++] = {
                    tri.v[i].x + t * (tri.v[j].x - tri.v[i].x),
                    tri.v[i].y + t * (tri.v[j].y - tri.v[i].y)
                };
            }
        }

        if (intCount == 2) {
            *out1 = intersections[0];
            *out2 = intersections[1];
            return true;
        }

        return false;
    }
};

SlicerEngine::SlicerEngine(QObject* parent) : QObject(parent), d(std::make_unique<Impl>()) {}
SlicerEngine::~SlicerEngine() = default;

// ============================================================================
// Main Slicing Entry Points
// ============================================================================

SliceInfo SlicerEngine::slice(const MeshTriangles& triangles, const SliceSettings& settings, const PrinterProfile& printer) {
    SliceInfo result;
    result.success = false;

    if (triangles.empty()) {
        result.errors.append("Empty mesh");
        return result;
    }

    d->m_triangles = triangles;
    d->m_settings = settings;
    d->m_printer = &printer;

    // Compute bounds
    d->m_bounds = {};
    bool first = true;
    for (const auto& tri : triangles) {
        for (int i = 0; i < 3; ++i) {
            if (first) {
                d->m_bounds.min = d->m_bounds.max = tri.v[i];
                first = false;
            } else {
                d->m_bounds.expand(tri.v[i]);
            }
        }
    }

    // Check build volume
    QString error;
    if (!checkBuildVolume(d->m_bounds, printer, &error)) {
        result.errors.append(error);
        return result;
    }

    // Build spatial index
    d->buildSpatialIndex();

    // Generate layer heights
    d->m_layerHeights = generateLayerHeights(d->m_bounds, settings);

    // Process layers
    LayerSlices slices = processLayers(triangles, settings, printer);

    // Generate supports if needed
    if (settings.generateSupport && !slices.empty()) {
        SupportGenerator supportGen;
        for (auto& layer : slices) {
            Polygons2D supports = supportGen.generateForLayer(layer, slices, settings, printer);
            layer.support = std::move(supports);
        }
    }

    // Estimate print time and filament
    result.slices = std::move(slices);
    result.layerHeights = d->m_layerHeights;
    result.boundingBox = d->m_bounds;
    result.settings = settings;

    // Calculate estimates
    double totalTime = 0;
    double totalFilament = 0;
    for (const auto& layer : result.slices) {
        totalTime += layer.printTimeEstimate;
        totalFilament += layer.filamentUsed;
    }
    result.printTime = totalTime;
    result.filamentUsed = totalFilament;
    result.totalLayers = static_cast<int>(result.slices.size());

    result.success = true;
    return result;
}

SliceInfo SlicerEngine::sliceScene(const std::vector<MeshTriangles>& objects, const SliceSettings& settings, const PrinterProfile& printer) {
    // Combine all objects into one triangle list
    MeshTriangles allTriangles;
    size_t totalTris = 0;
    for (const auto& obj : objects) totalTris += obj.size();
    allTriangles.reserve(totalTris);
    for (const auto& obj : objects) {
        allTriangles.insert(allTriangles.end(), obj.begin(), obj.end());
    }
    return slice(allTriangles, settings, printer);
}

void SlicerEngine::sliceAsync(const MeshTriangles& triangles, const SliceSettings& settings, const PrinterProfile& printer,
                              ProgressCallback progress, CompleteCallback complete) {
    m_cancelRequested = false;
    m_currentTriangles = triangles;
    m_currentSettings = settings;
    m_currentPrinter = &printer;
    m_progressCallback = std::move(progress);
    m_completeCallback = std::move(complete);

    // Run in thread pool
    QtConcurrent::run([this]() { processSlice(); });
}

void SlicerEngine::cancel() {
    m_cancelRequested = true;
}

// ============================================================================
// Layer Height Generation
// ============================================================================

std::vector<double> SlicerEngine::generateLayerHeights(const BoundingBox& bounds, const SliceSettings& settings) {
    std::vector<double> heights;

    double minZ = bounds.min.z;
    double maxZ = bounds.max.z;

    // First layer height
    double firstLayerHeight = settings.initialLayerHeight > 0 ? settings.initialLayerHeight : settings.layerHeight;
    double currentZ = minZ + firstLayerHeight * 0.5; // Center of first layer

    // Add first layer
    if (currentZ <= maxZ) {
        heights.push_back(currentZ);
    }

    // Subsequent layers
    currentZ = minZ + firstLayerHeight + settings.layerHeight * 0.5;
    int layerIndex = 1;

    while (currentZ <= maxZ + 1e-6) {
        heights.push_back(currentZ);
        currentZ += settings.layerHeight;
        layerIndex++;

        // Adaptive layer height (if enabled)
        if (settings.adaptiveLayerHeight && layerIndex > 10) {
            // Could implement curvature-based adaptation here
        }
    }

    return heights;
}

// ============================================================================
// Mesh-Plane Intersection
// ============================================================================

Polygons2D SlicerEngine::intersectMeshAtZ(const MeshTriangles& triangles, double z, double thickness) {
    Polygons2D result;
    std::vector<std::pair<Vector2, Vector2>> segments;
    segments.reserve(triangles.size());

    // Find intersecting triangles
    for (const auto& tri : triangles) {
        if (!tri.intersectsPlane(z)) continue;

        Vector2 p1, p2;
        if (d->intersectTrianglePlane(tri, z, &p1, &p2)) {
            segments.emplace_back(p1, p2);
        }
    }

    if (segments.empty()) return result;

    // Build polygons from segments (simplified - assumes clean manifold mesh)
    // In production, use a proper polygon reconstruction library like Clipper
    // For now, group segments into polygons
    std::vector<bool> used(segments.size(), false);

    for (size_t i = 0; i < segments.size(); ++i) {
        if (used[i]) continue;

        Polygon2D poly;
        poly.vertices.push_back(segments[i].first);
        poly.vertices.push_back(segments[i].second);
        used[i] = true;

        Vector2 current = segments[i].second;
        bool closed = false;

        // Walk forward
        for (int iter = 0; iter < static_cast<int>(segments.size()) * 2 && !closed; ++iter) {
            bool found = false;
            for (size_t j = 0; j < segments.size(); ++j) {
                if (used[j]) continue;

                double dx1 = segments[j].first.x - current.x;
                double dy1 = segments[j].first.y - current.y;
                double dx2 = segments[j].second.x - current.x;
                double dy2 = segments[j].second.y - current.y;

                if (dx1*dx1 + dy1*dy1 < 1e-10) {
                    // Continue from first point
                    if (std::abs(segments[j].second.x - segments[i].first.x) < 1e-6 &&
                        std::abs(segments[j].second.y - segments[i].first.y) < 1e-6) {
                        closed = true;
                    } else {
                        poly.vertices.push_back(segments[j].second);
                        current = segments[j].second;
                    }
                    used[j] = true;
                    found = true;
                    break;
                } else if (dx2*dx2 + dy2*dy2 < 1e-10) {
                    // Continue from second point
                    if (std::abs(segments[j].first.x - segments[i].first.x) < 1e-6 &&
                        std::abs(segments[j].first.y - segments[i].first.y) < 1e-6) {
                        closed = true;
                    } else {
                        poly.vertices.push_back(segments[j].first);
                        current = segments[j].first;
                    }
                    used[j] = true;
                    found = true;
                    break;
                }
            }
            if (!found) break;
        }

        if (poly.vertices.size() >= 3) {
            // Remove duplicate last point if closed
            if (closed && poly.vertices.size() > 1) {
                const auto& first = poly.vertices.front();
                const auto& last = poly.vertices.back();
                if (std::abs(first.x - last.x) < 1e-6 && std::abs(first.y - last.y) < 1e-6) {
                    poly.vertices.pop_back();
                }
            }
            result.push_back(std::move(poly));
        }
    }

    return result;
}

// ============================================================================
// Perimeter Generation
// ============================================================================

Polygons2D SlicerEngine::generatePerimeters(const Polygons2D& crossSection, const SliceSettings& settings, int layerIndex) {
    Polygons2D perimeters = crossSection;

    // Number of perimeters (walls)
    int wallCount = settings.wallCount;
    if (layerIndex == 0) wallCount = std::max(wallCount, settings.firstLayerWallCount);

    double lineWidth = settings.lineWidth > 0 ? settings.lineWidth : settings.nozzleDiameter;
    double offset = lineWidth * 0.5;

    for (int w = 1; w < wallCount; ++w) {
        Polygons2D inner = geometry::offsetPolygons(perimeters, -offset * w);
        if (inner.empty()) break;
        perimeters.insert(perimeters.end(), inner.begin(), inner.end());
    }

    // Ensure CCW winding for outer perimeters
    for (auto& poly : perimeters) {
        if (geometry::polygonArea(poly.vertices) < 0) {
            std::reverse(poly.vertices.begin(), poly.vertices.end());
        }
    }

    return perimeters;
}

// ============================================================================
// Infill Generation
// ============================================================================

Polygons2D SlicerEngine::generateInfill(const Polygons2D& perimeters, const SliceSettings& settings, int layerIndex) {
    if (settings.infillDensity <= 0) return {};

    // Calculate infill spacing based on density
    double lineWidth = settings.infillLineWidth > 0 ? settings.infillLineWidth : settings.lineWidth;
    double spacing = lineWidth * (100.0 / settings.infillDensity);

    // Pattern selection
    double angle = settings.infillPatternAngle;
    if (layerIndex % 2 == 1) angle += 90; // Alternate angles

    // Offset for pattern alignment
    double offsetX = 0, offsetY = 0;
    if (settings.infillOffsetPerLayer > 0) {
        offsetX = std::fmod(layerIndex * settings.infillOffsetPerLayer, spacing);
    }

    switch (settings.infillPattern) {
        case InfillPattern::Grid:
        case InfillPattern::Lines:
            return geometry::generateGridInfill(perimeters, spacing, angle * M_PI / 180.0, offsetX, offsetY);

        case InfillPattern::Triangles:
            // Triangle infill = 3 sets of lines at 60 degrees
            // Simplified: just use grid at 60 deg
            return geometry::generateGridInfill(perimeters, spacing, M_PI / 3, offsetX, offsetY);

        case InfillPattern::Cubic:
        case InfillPattern::Gyroid:
            return geometry::generateGyroidInfill(perimeters, spacing, lineWidth);

        case InfillPattern::Concentric:
            // Concentric infill - offset perimeters inward
            return geometry::offsetPolygons(perimeters, -spacing * 0.5);

        default:
            return geometry::generateGridInfill(perimeters, spacing, angle * M_PI / 180.0, offsetX, offsetY);
    }
}

// ============================================================================
// Bed Adhesion (Brim/Skirt/Raft)
// ============================================================================

Polygons2D SlicerEngine::generateBedAdhesion(const Polygons2D& firstLayerPerimeters, const SliceSettings& settings) {
    Polygons2D result;

    if (settings.brimWidth > 0) {
        double lineWidth = settings.lineWidth > 0 ? settings.lineWidth : settings.nozzleDiameter;
        int brimLines = static_cast<int>(settings.brimWidth / lineWidth);

        for (int i = 1; i <= brimLines; ++i) {
            Polygons2D brim = geometry::offsetPolygons(firstLayerPerimeters, lineWidth * i);
            result.insert(result.end(), brim.begin(), brim.end());
        }
    }

    if (settings.skirtLoops > 0) {
        double lineWidth = settings.lineWidth > 0 ? settings.lineWidth : settings.nozzleDiameter;
        double skirtDistance = settings.skirtDistance > 0 ? settings.skirtDistance : lineWidth * 2;

        Polygons2D skirt = geometry::offsetPolygons(firstLayerPerimeters, skirtDistance);
        for (int i = 1; i < settings.skirtLoops; ++i) {
            Polygons2D more = geometry::offsetPolygons(skirt, lineWidth * i);
            skirt.insert(skirt.end(), more.begin(), more.end());
        }
        result.insert(result.end(), skirt.begin(), skirt.end());
    }

    return result;
}

// ============================================================================
// Layer Processing
// ============================================================================

LayerSlices SlicerEngine::processLayers(const MeshTriangles& triangles, const SliceSettings& settings, const PrinterProfile& printer) {
    LayerSlices slices;
    slices.reserve(d->m_layerHeights.size());

    for (size_t i = 0; i < d->m_layerHeights.size(); ++i) {
        if (m_cancelRequested) break;

        double z = d->m_layerHeights[i];
        double thickness = (i == 0) ? settings.initialLayerHeight : settings.layerHeight;

        LayerSlice layer = processSingleLayer(triangles, z, thickness, settings, static_cast<int>(i), d->m_layerHeights.size());

        emit sliceProgress(static_cast<int>((i + 1) * 100 / d->m_layerHeights.size()),
                          QString("Slicing layer %1/%2").arg(i + 1).arg(d->m_layerHeights.size()));

        slices.push_back(std::move(layer));
    }

    return slices;
}

LayerSlice SlicerEngine::processSingleLayer(const MeshTriangles& triangles, double z, double thickness,
                                            const SliceSettings& settings, int layerIndex, int totalLayers) {
    LayerSlice layer;
    layer.z = z;
    layer.thickness = thickness;
    layer.layerIndex = layerIndex;
    layer.isFirstLayer = (layerIndex == 0);
    layer.isLastLayer = (layerIndex == totalLayers - 1);

    // 1. Cross-section
    Polygons2D crossSection = intersectMeshAtZ(triangles, z, thickness);
    if (crossSection.empty()) return layer;

    // 2. Perimeters
    layer.perimeters = generatePerimeters(crossSection, settings, layerIndex);

    // 3. Infill
    if (!layer.isFirstLayer && !layer.isLastLayer && settings.infillDensity > 0) {
        layer.infill = generateInfill(layer.perimeters, settings, layerIndex);
    } else if (layer.isFirstLayer || layer.isLastLayer) {
        // Solid top/bottom layers
        int solidLayers = layer.isFirstLayer ? settings.bottomSolidLayers : settings.topSolidLayers;
        if (solidLayers > 0) {
            // Fill completely (100% infill)
            SliceSettings solidSettings = settings;
            solidSettings.infillDensity = 100;
            solidSettings.infillPattern = InfillPattern::Lines;
            layer.infill = generateInfill(layer.perimeters, solidSettings, layerIndex);
        }
    }

    // 4. Bed adhesion (first layer only)
    if (layer.isFirstLayer) {
        layer.brim = generateBedAdhesion(layer.perimeters, settings);
    }

    // 5. Estimate print time and filament for this layer
    double perimeterLength = 0;
    for (const auto& poly : layer.perimeters) {
        for (size_t i = 0, j = poly.vertices.size() - 1; i < poly.vertices.size(); j = i++) {
            perimeterLength += std::sqrt(
                std::pow(poly.vertices[i].x - poly.vertices[j].x, 2) +
                std::pow(poly.vertices[i].y - poly.vertices[j].y, 2)
            );
        }
    }

    double infillLength = 0;
    for (const auto& poly : layer.infill) {
        for (size_t i = 0, j = poly.vertices.size() - 1; i < poly.vertices.size(); j = i++) {
            infillLength += std::sqrt(
                std::pow(poly.vertices[i].x - poly.vertices[j].x, 2) +
                std::pow(poly.vertices[i].y - poly.vertices[j].y, 2)
            );
        }
    }

    // Speed settings
    double printSpeed = layer.isFirstLayer ? settings.firstLayerSpeed : settings.printSpeed;
    double infillSpeed = settings.infillSpeed > 0 ? settings.infillSpeed : printSpeed;
    double travelSpeed = settings.travelSpeed > 0 ? settings.travelSpeed : printSpeed * 2;

    double lineWidth = settings.lineWidth > 0 ? settings.lineWidth : settings.nozzleDiameter;
    double filamentArea = lineWidth * thickness;
    double filamentDiameter = settings.filamentDiameter > 0 ? settings.filamentDiameter : 1.75;
    double filamentCrossSection = M_PI * filamentDiameter * filamentDiameter / 4.0;

    double printTime = perimeterLength / printSpeed + infillLength / infillSpeed;
    double travelEstimate = (perimeterLength + infillLength) * 0.1; // Rough estimate
    printTime += travelEstimate / travelSpeed;

    layer.printTimeEstimate = printTime;
    layer.filamentUsed = (perimeterLength + infillLength) * filamentArea / filamentCrossSection;

    return layer;
}

// ============================================================================
// Utility Functions
// ============================================================================

bool SlicerEngine::checkBuildVolume(const BoundingBox& bounds, const PrinterProfile& printer, QString* error) const {
    double maxX = printer.buildVolumeX;
    double maxY = printer.buildVolumeY;
    double maxZ = printer.buildVolumeZ;

    if (bounds.size().x > maxX) {
        if (error) *error = QString("Model width %.1fmm exceeds printer max %.1fmm").arg(bounds.size().x).arg(maxX);
        return false;
    }
    if (bounds.size().y > maxY) {
        if (error) *error = QString("Model depth %.1fmm exceeds printer max %.1fmm").arg(bounds.size().y).arg(maxY);
        return false;
    }
    if (bounds.size().z > maxZ) {
        if (error) *error = QString("Model height %.1fmm exceeds printer max %.1fmm").arg(bounds.size().z).arg(maxZ);
        return false;
    }
    return true;
}

QMatrix4x4 SlicerEngine::suggestOrientation(const MeshTriangles& triangles) {
    if (triangles.empty()) return QMatrix4x4();

    QVector3D centroid;
    QVector3D avgNormal;
    float totalArea = 0.0f;

    for (const auto& tri : triangles) {
        QVector3D v0(tri.v[0].x, tri.v[0].y, tri.v[0].z);
        QVector3D v1(tri.v[1].x, tri.v[1].y, tri.v[1].z);
        QVector3D v2(tri.v[2].x, tri.v[2].y, tri.v[2].z);

        QVector3D e1 = v1 - v0;
        QVector3D e2 = v2 - v0;
        QVector3D n = QVector3D::crossProduct(e1, e2);
        float area = n.length() * 0.5f;
        avgNormal += n.normalized() * area;
        totalArea += area;
        centroid += (v0 + v1 + v2) / 3.0f;
    }

    if (totalArea > 0.0f) {
        centroid /= static_cast<float>(triangles.size());
        avgNormal.normalize();
    }

    QVector3D up(0, 1, 0);
    QVector3D axis = QVector3D::crossProduct(avgNormal, up);
    float sinA = axis.length();
    float cosA = QVector3D::dotProduct(avgNormal, up);

    QMatrix4x4 rotation;
    if (sinA > 0.001f) {
        axis.normalize();
        float angle = std::atan2(sinA, cosA);
        rotation.rotate(static_cast<float>(qRadiansToDegrees(angle)), axis);
    }

    QMatrix4x4 result;
    result.translate(-centroid);
    result = rotation * result;
    result.translate(0, 0, 0);

    return result;
}

double SlicerEngine::estimatePrintTimeQuick(const MeshTriangles& triangles, const SliceSettings& settings, const PrinterProfile& printer) {
    // Quick volume-based estimate
    double volume = 0;
    for (const auto& tri : triangles) {
        Vector3 v0 = tri.v[0];
        Vector3 v1 = tri.v[1];
        Vector3 v2 = tri.v[2];
        // Tetrahedron volume with origin
        volume += std::abs(v0.x * (v1.y * v2.z - v1.z * v2.y) +
                          v0.y * (v1.z * v2.x - v1.x * v2.z) +
                          v0.z * (v1.x * v2.y - v1.y * v2.x)) / 6.0;
    }

    double layerHeight = settings.layerHeight;
    double lineWidth = settings.nozzleDiameter;
    double infillDensity = settings.infillDensity / 100.0;

    double filamentVolume = volume * infillDensity * 1.2; // Approximate
    double filamentDiameter = settings.filamentDiameter > 0 ? settings.filamentDiameter : 1.75;
    double filamentLength = filamentVolume / (M_PI * filamentDiameter * filamentDiameter / 4.0);

    double printSpeed = settings.printSpeed > 0 ? settings.printSpeed : 50.0;
    return filamentLength / printSpeed * 1.5; // Fudge factor
}

void SlicerEngine::processSlice() {
    SliceInfo result = slice(m_currentTriangles, m_currentSettings, *m_currentPrinter);

    if (m_completeCallback) {
        m_completeCallback(result);
    }
    emit sliceComplete(result);
}

} // namespace printing
} // namespace ks