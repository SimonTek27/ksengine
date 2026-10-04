#pragma once

#include "3DPrintTypes.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace ks {
namespace printing {
namespace geometry {

// ============================================================================
// Polygon Operations (Clipper-like but simplified)
// ============================================================================

// Simple polygon offset using miter join
inline Polygons2D offsetPolygons(const Polygons2D& input, double offset) {
    Polygons2D result;
    result.reserve(input.size());

    for (const auto& poly : input) {
        if (poly.vertices.size() < 3) continue;

        std::vector<Vector2> output;
        output.reserve(poly.vertices.size());

        for (size_t i = 0; i < poly.vertices.size(); ++i) {
            const Vector2& prev = poly.vertices[(i + poly.vertices.size() - 1) % poly.vertices.size()];
            const Vector2& curr = poly.vertices[i];
            const Vector2& next = poly.vertices[(i + 1) % poly.vertices.size()];

            Vector2 v1 = {curr.x - prev.x, curr.y - prev.y};
            Vector2 v2 = {next.x - curr.x, next.y - curr.y};

            double len1 = std::sqrt(v1.x*v1.x + v1.y*v1.y);
            double len2 = std::sqrt(v2.x*v2.x + v2.y*v2.y);

            if (len1 < 1e-10 || len2 < 1e-10) continue;

            // Normalize
            v1.x /= len1; v1.y /= len1;
            v2.x /= len2; v2.y /= len2;

            // Inward normals
            Vector2 n1 = {-v1.y, v1.x};
            Vector2 n2 = {-v2.y, v2.x};

            // Bisector
            Vector2 bisector = {n1.x + n2.x, n1.y + n2.y};
            double bisectorLen = std::sqrt(bisector.x*bisector.x + bisector.y*bisector.y);

            if (bisectorLen < 1e-10) {
                // Parallel edges
                output.push_back({curr.x + n1.x * offset, curr.y + n1.y * offset});
            } else {
                // Miter limit to avoid spikes
                double dot = n1.x*n2.x + n1.y*n2.y;
                double miterLimit = 2.0; // 1/sin(theta/2)
                double miterFactor = 1.0 / std::max(0.01, (1.0 + dot) * 0.5);
                miterFactor = std::min(miterFactor, miterLimit);

                bisector.x /= bisectorLen;
                bisector.y /= bisectorLen;

                output.push_back({curr.x + bisector.x * offset * miterFactor,
                                  curr.y + bisector.y * offset * miterFactor});
            }
        }

        if (output.size() >= 3) {
            Polygon2D p;
            p.vertices = std::move(output);
            result.push_back(std::move(p));
        }
    }

    return result;
}

// Polygon area (shoelace formula)
inline double polygonArea(const std::vector<Vector2>& verts) {
    double area = 0;
    for (size_t i = 0, j = verts.size() - 1; i < verts.size(); j = i++) {
        area += (verts[j].x + verts[i].x) * (verts[j].y - verts[i].y);
    }
    return area * 0.5;
}

// Point in polygon (ray casting)
inline bool pointInPolygon(const Vector2& pt, const std::vector<Vector2>& poly) {
    bool inside = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        if (((poly[i].y > pt.y) != (poly[j].y > pt.y)) &&
            (pt.x < (poly[j].x - poly[i].x) * (pt.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)) {
            inside = !inside;
        }
    }
    return inside;
}

// Line segment intersection
inline bool lineIntersection(const Vector2& a1, const Vector2& a2,
                             const Vector2& b1, const Vector2& b2,
                             Vector2* out = nullptr) {
    double d = (a1.x - a2.x) * (b1.y - b2.y) - (a1.y - a2.y) * (b1.x - b2.x);
    if (std::abs(d) < 1e-10) return false;

    double t = ((a1.x - b1.x) * (b1.y - b2.y) - (a1.y - b1.y) * (b1.x - b2.x)) / d;
    double u = -((a1.x - a2.x) * (a1.y - b1.y) - (a1.y - a2.y) * (a1.x - b1.x)) / d;

    if (t >= 0 && t <= 1 && u >= 0 && u <= 1) {
        if (out) {
            out->x = a1.x + t * (a2.x - a1.x);
            out->y = a1.y + t * (a2.y - a1.y);
        }
        return true;
    }
    return false;
}

// Grid-based infill generation
inline Polygons2D generateGridInfill(const Polygons2D& boundaries, double spacing, double angle, double offsetX, double offsetY) {
    Polygons2D infill;

    // Find bounding box of all boundaries
    BoundingBox bounds;
    bool hasBounds = false;
    for (const auto& poly : boundaries) {
        for (const auto& v : poly.vertices) {
            if (!hasBounds) {
                bounds.min = bounds.max = Vector3(v.x, v.y, 0);
                hasBounds = true;
            } else {
                bounds.expand(Vector3(v.x, v.y, 0));
            }
        }
    }
    if (!hasBounds) return infill;

    double cosA = std::cos(angle);
    double sinA = std::sin(angle);

    auto rotate = [&](double x, double y) -> Vector2 {
        double rx = x * cosA - y * sinA;
        double ry = x * sinA + y * cosA;
        return {rx, ry};
    };

    // Generate lines
    double startX = std::floor((bounds.min.x - offsetX) / spacing) * spacing + offsetX;
    double endX = std::ceil((bounds.max.x - offsetX) / spacing) * spacing + offsetX;

    std::vector<Vector2> linePoints;

    for (double x = startX; x <= endX; x += spacing) {
        linePoints.clear();

        // Vertical line in rotated space at x
        double minY = bounds.min.y - 100; // Extend beyond bounds
        double maxY = bounds.max.y + 100;

        // Find intersections with all polygons
        std::vector<double> intersections;

        for (const auto& poly : boundaries) {
            for (size_t i = 0, j = poly.vertices.size() - 1; i < poly.vertices.size(); j = i++) {
                const auto& p1 = poly.vertices[j];
                const auto& p2 = poly.vertices[i];

                // Rotate points
                Vector2 rp1 = rotate(p1.x, p1.y);
                Vector2 rp2 = rotate(p2.x, p2.y);

                if ((rp1.x <= x && rp2.x >= x) || (rp1.x >= x && rp2.x <= x)) {
                    if (std::abs(rp2.x - rp1.x) > 1e-10) {
                        double t = (x - rp1.x) / (rp2.x - rp1.x);
                        double y = rp1.y + t * (rp2.y - rp1.y);
                        intersections.push_back(y);
                    }
                }
            }
        }

        std::sort(intersections.begin(), intersections.end());

        // Create segments (pairs of intersections)
        for (size_t k = 0; k + 1 < intersections.size(); k += 2) {
            double y1 = intersections[k];
            double y2 = intersections[k + 1];

            if (y2 - y1 < spacing * 0.1) continue;

            // Create thin polygon for this support segment
            double w = spacing * 0.5;
            Vector2 p1 = {x * cosA + y1 * sinA, -x * sinA + y1 * cosA};
            Vector2 p2 = {x * cosA + y2 * sinA, -x * sinA + y2 * cosA};

            Vector2 dir = {p2.x - p1.x, p2.y - p1.y};
            double len = std::sqrt(dir.x*dir.x + dir.y*dir.y);
            if (len < 0.01) continue;
            dir.x /= len; dir.y /= len;
            Vector2 perp = {-dir.y * w, dir.x * w};

            Polygon2D segment;
            segment.vertices = {
                {p1.x + perp.x, p1.y + perp.y},
                {p2.x + perp.x, p2.y + perp.y},
                {p2.x - perp.x, p2.y - perp.y},
                {p1.x - perp.x, p1.y - perp.y}
            };
            infill.push_back(std::move(segment));
        }
    }

    return infill;
}

// Gyroid infill pattern
inline Polygons2D generateGyroidInfill(const Polygons2D& boundaries, double spacing, double thickness) {
    Polygons2D infill;
    if (boundaries.empty() || spacing <= 0) return infill;

    BoundingBox bounds;
    bool hasBounds = false;
    for (const auto& poly : boundaries) {
        for (const auto& v : poly.vertices) {
            if (!hasBounds) { bounds.min = bounds.max = Vector3(v.x, v.y, 0); hasBounds = true; }
            else { bounds.expand(Vector3(v.x, v.y, 0)); }
        }
    }
    if (!hasBounds) return infill;

    double cellSize = spacing;
    int stepsX = static_cast<int>((bounds.max.x - bounds.min.x) / cellSize) + 2;
    int stepsY = static_cast<int>((bounds.max.y - bounds.min.y) / cellSize) + 2;

    for (int ix = 0; ix < stepsX; ++ix) {
        for (int iy = 0; iy < stepsY; ++iy) {
            double cx = bounds.min.x + ix * cellSize + cellSize * 0.5;
            double cy = bounds.min.y + iy * cellSize + cellSize * 0.5;
            double cx2 = bounds.min.x + (ix + 0.5) * cellSize;
            double cy2 = bounds.min.y + (iy + 0.5) * cellSize;

            double phase1 = std::sin(cx * M_PI / spacing) + std::sin(cy * M_PI / spacing);
            double phase2 = std::cos(cx2 * M_PI / spacing) + std::cos(cy2 * M_PI / spacing);

            std::vector<Vector2> pts;
            const int segs = 6;
            for (int s = 0; s <= segs; ++s) {
                double t = static_cast<double>(s) / segs;
                double angle = t * M_PI * 2.0;
                double r = cellSize * 0.3 * (0.7 + 0.3 * std::sin(phase1 + phase2));
                double px = cx + r * std::cos(angle);
                double py = cy + r * std::sin(angle);
                pts.push_back({px, py});
            }

            bool inside = false;
            for (const auto& poly : boundaries) {
                if (pointInPolygon(Vector2(cx, cy), poly.vertices)) { inside = true; break; }
            }
            if (!inside) continue;

            if (pts.size() >= 3) {
                Polygon2D seg;
                seg.vertices = pts;
                infill.push_back(std::move(seg));
            }
        }
    }

    return infill;
}

} // namespace geometry
} // namespace printing
} // namespace ks