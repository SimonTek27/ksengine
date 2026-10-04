#include "SupportGenerator.h"
#include "GeometryUtils.h"
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <queue>
#include <unordered_map>

namespace ks {
namespace printing {

using namespace geometry;
using TreeBranch = SupportGenerator::TreeBranch;

struct SupportGenerator::Impl {
    // Spatial grid for fast lookup
    struct Grid {
        double cellSize = 5.0;
        std::unordered_map<long long, std::vector<Vector2>> points; // For support tips

        long long hash(double x, double y) const {
            int gx = static_cast<int>(std::floor(x / cellSize));
            int gy = static_cast<int>(std::floor(y / cellSize));
            return (static_cast<long long>(gx) << 32) ^ static_cast<unsigned int>(gy);
        }

        void add(double x, double y) {
            points[hash(x, y)].push_back({x, y});
        }

        std::vector<Vector2> nearby(double x, double y, double radius) const {
            std::vector<Vector2> result;
            int cells = static_cast<int>(std::ceil(radius / cellSize));
            int gx = static_cast<int>(std::floor(x / cellSize));
            int gy = static_cast<int>(std::floor(y / cellSize));

            for (int dx = -cells; dx <= cells; ++dx) {
                for (int dy = -cells; dy <= cells; ++dy) {
                    auto it = points.find(hash(gx + dx, gy + dy));
                    if (it != points.end()) {
                        for (const auto& p : it->second) {
                            double dist2 = (p.x - x)*(p.x - x) + (p.y - y)*(p.y - y);
                            if (dist2 <= radius*radius) result.push_back(p);
                        }
                    }
                }
            }
            return result;
        }
    };

    std::vector<TreeBranch> generateTreeStructure(const LayerSlices& slices, const SliceSettings& settings);
};

SupportGenerator::SupportGenerator(QObject* parent) : QObject(parent), d(std::make_unique<Impl>()) {}
SupportGenerator::~SupportGenerator() = default;

// ============================================================================
// Main Entry Points
// ============================================================================

Polygons2D SupportGenerator::generateForLayer(const LayerSlice& layer, const LayerSlices& allLayers,
                                              const SliceSettings& settings, const PrinterProfile& printer) {
    if (settings.supportType == SupportType::None) return {};

    if (settings.supportType == SupportType::Tree) {
        return generateTreeSupports(layer, allLayers, settings, printer);
    } else {
        return generateGridSupports(layer, settings);
    }
}

void SupportGenerator::generateSupports(const LayerSlices& slices, const SliceSettings& settings, const PrinterProfile& printer,
                                        std::function<void(int layerIndex, const Polygons2D& support)> layerCallback) {
    if (settings.supportType == SupportType::Tree) {
        // Tree supports need full analysis first
        auto treeBranches = d->generateTreeStructure(slices, settings);

        // Convert tree to per-layer polygons
        for (size_t i = 0; i < slices.size(); ++i) {
            Polygons2D layerSupport;
            for (const auto& branch : treeBranches) {
                if (i >= branch.layerStart && i <= branch.layerEnd) {
                    // Calculate branch diameter at this layer
                    double t = (branch.layerEnd - branch.layerStart > 0)
                        ? double(i - branch.layerStart) / (branch.layerEnd - branch.layerStart)
                        : 0.0;
                    double diameter = branch.diameter * (1.0 - t * 0.5); // Taper toward tip

                    // Create circle polygon
                    Polygon2D circle;
                    int segments = std::max(6, static_cast<int>(diameter * M_PI / 0.5));
                    for (int s = 0; s < segments; ++s) {
                        double angle = 2 * M_PI * s / segments;
                        circle.vertices.push_back({branch.tip.x + diameter/2 * std::cos(angle),
                                                   branch.tip.y + diameter/2 * std::sin(angle)});
                    }
                    layerSupport.push_back(std::move(circle));
                }
            }
            if (layerCallback) layerCallback(static_cast<int>(i), layerSupport);
        }
    } else {
        // Grid supports - layer by layer
        for (size_t i = 0; i < slices.size(); ++i) {
            Polygons2D support = generateGridSupports(slices[i], settings);
            if (layerCallback) layerCallback(static_cast<int>(i), support);
            emit supportProgress(static_cast<int>((i + 1) * 100 / slices.size()),
                                QString("Generating supports %1/%2").arg(i + 1).arg(slices.size()));
        }
    }
    emit supportComplete(slices.size());
}

// ============================================================================
// Tree Supports
// ============================================================================

std::vector<SupportGenerator::TreeBranch> SupportGenerator::Impl::generateTreeStructure(const LayerSlices& slices, const SliceSettings& settings) {
    std::vector<SupportGenerator::TreeBranch> branches;
    Grid grid;
    grid.cellSize = settings.supportTreeBranchDiameter;

    // Process from top to bottom to find support tips
    for (int layerIdx = static_cast<int>(slices.size()) - 1; layerIdx >= 0; --layerIdx) {
        const auto& layer = slices[layerIdx];

        // Find overhang areas that need support
        for (const auto& poly : layer.perimeters) {
            for (size_t i = 0; i < poly.vertices.size(); ++i) {
                const auto& v = poly.vertices[i];
                const auto& prev = poly.vertices[(i + poly.vertices.size() - 1) % poly.vertices.size()];
                const auto& next = poly.vertices[(i + 1) % poly.vertices.size()];

                // Calculate edge normal
                Vector2 edge = {next.x - prev.x, next.y - prev.y};
                double len = std::sqrt(edge.x*edge.x + edge.y*edge.y);
                if (len < 0.01) continue;
                edge.x /= len; edge.y /= len;

                // Outward normal (CCW polygon)
                Vector2 normal = {-edge.y, edge.x};

                // Check overhang angle - simplified: if normal has significant Z component
                // In 2D slice, we approximate by checking if this is an overhang region
                // For tree supports, we place tips at overhang areas
                if (layerIdx > 0) {
                    // Check if this area is unsupported by layer below
                    bool supported = false;
                    const auto& belowLayer = slices[layerIdx - 1];
                    for (const auto& belowPoly : belowLayer.perimeters) {
                        // Point in polygon test
                        if (geometry::pointInPolygon(v, belowPoly.vertices)) {
                            supported = true;
                            break;
                        }
                    }

                    if (!supported) {
                        // This vertex needs support - create or extend branch
                        TreeBranch* existingBranch = nullptr;
                        double minDist = settings.supportTreeBranchDiameter * 2;

                        for (auto& branch : branches) {
                            double dist = std::sqrt((branch.tip.x - v.x)*(branch.tip.x - v.x) +
                                                   (branch.tip.y - v.y)*(branch.tip.y - v.y));
                            if (dist < minDist) {
                                minDist = dist;
                                existingBranch = &branch;
                            }
                        }

                        if (existingBranch) {
                            // Extend existing branch downward
                            existingBranch->layerStart = layerIdx;
                            existingBranch->tip = v;
                        } else {
                            // Create new branch
                            TreeBranch branch;
                            branch.tip = v;
                            branch.base = v; // Will be projected to bed
                            branch.diameter = settings.supportTreeBranchDiameter;
                            branch.layerStart = layerIdx;
                            branch.layerEnd = layerIdx;
                            branches.push_back(std::move(branch));
                        }
                    }
                }
            }
        }
    }

    // Project bases to build plate (Z=0)
    for (auto& branch : branches) {
        branch.base = branch.tip; // Simplified - in reality, project avoiding obstacles
        branch.layerEnd = 0; // Goes to bed
    }

    // Branch merging - combine nearby branches
    std::vector<TreeBranch> merged;
    for (const auto& branch : branches) {
        bool merged_ = false;
        for (auto& m : merged) {
            double dist = std::sqrt((m.base.x - branch.base.x)*(m.base.x - branch.base.x) +
                                   (m.base.y - branch.base.y)*(m.base.y - branch.base.y));
            if (dist < settings.supportTreeBranchDiameter * 1.5) {
                // Merge - average position
                m.base.x = (m.base.x + branch.base.x) * 0.5;
                m.base.y = (m.base.y + branch.base.y) * 0.5;
                m.diameter = std::min(m.diameter * 1.2, settings.supportTreeBranchDiameter * 2);
                merged_ = true;
                break;
            }
        }
        if (!merged_) merged.push_back(branch);
    }

    return merged;
}

Polygons2D SupportGenerator::generateTreeSupports(const LayerSlice& layer, const LayerSlices& allLayers,
                                                  const SliceSettings& settings, const PrinterProfile& printer) {
    Polygons2D supportArea = calculateSupportArea(layer, allLayers, settings, 0);
    if (supportArea.empty()) return Polygons2D();

    double branchWidth = settings.lineWidth * 2.0;
    Polygons2D support;

    for (const auto& poly : supportArea) {
        BoundingBox bounds;
        bool hasBounds = false;
        for (const auto& v : poly.vertices) {
            if (!hasBounds) { bounds.min = bounds.max = Vector3(v.x, v.y, 0); hasBounds = true; }
            else { bounds.expand(Vector3(v.x, v.y, 0)); }
        }
        if (!hasBounds) continue;

        double cx = (bounds.min.x + bounds.max.x) * 0.5;
        double cy = (bounds.min.y + bounds.max.y) * 0.5;

        Polygon2D branch;
        double angleStep = M_PI * 2.0 / 8;
        for (int i = 0; i < 8; ++i) {
            double a = i * angleStep;
            double r = branchWidth * 0.5 * (0.6 + 0.4 * std::cos(a * 3));
            branch.vertices.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
        }
        support.push_back(std::move(branch));

        if (bounds.max.x - bounds.min.x > branchWidth * 2) {
            Polygon2D conn;
            double y = cy;
            conn.vertices.push_back({bounds.min.x, y - branchWidth * 0.25});
            conn.vertices.push_back({bounds.max.x, y - branchWidth * 0.25});
            conn.vertices.push_back({bounds.max.x, y + branchWidth * 0.25});
            conn.vertices.push_back({bounds.min.x, y + branchWidth * 0.25});
            support.push_back(std::move(conn));
        }
    }

    return support;
}

// ============================================================================
// Grid Supports
// ============================================================================

Polygons2D SupportGenerator::generateGridSupports(const LayerSlice& layer, const SliceSettings& settings) {
    Polygons2D support;

    // Calculate support area (where overhangs exist)
    Polygons2D supportArea = calculateSupportArea(layer, LayerSlices{}, settings, 0);

    if (supportArea.empty()) return support;

    // Generate grid pattern within support area
    double lineWidth = settings.lineWidth;
    double spacing = lineWidth / (settings.supportDensity * 100.0);

    // Find bounds of support area
    BoundingBox bounds;
    bool hasBounds = false;
    for (const auto& poly : supportArea) {
        for (const auto& v : poly.vertices) {
            if (!hasBounds) {
                bounds.min = bounds.max = {v.x, v.y, 0};
                hasBounds = true;
            } else {
                bounds.expand({v.x, v.y, 0});
            }
        }
    }
    if (!hasBounds) return support;

    // Generate grid lines
    double angle = 0; // Could alternate per layer
    double cosA = std::cos(angle);
    double sinA = std::sin(angle);

    auto rotate = [&](double x, double y) -> Vector2 {
        return {x * cosA - y * sinA, x * sinA + y * cosA};
    };

    auto unrotate = [&](double x, double y) -> Vector2 {
        return {x * cosA + y * sinA, -x * sinA + y * cosA};
    };

    // Bounding box in rotated space
    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();

    for (const auto& poly : supportArea) {
        for (const auto& v : poly.vertices) {
            Vector2 rv = rotate(v.x, v.y);
            minX = std::min(minX, rv.x);
            maxX = std::max(maxX, rv.x);
            minY = std::min(minY, rv.y);
            maxY = std::max(maxY, rv.y);
        }
    }

    // Generate lines
    for (double x = minX; x <= maxX; x += spacing) {
        std::vector<double> intersections;

        // Find intersections with support area boundary
        for (const auto& poly : supportArea) {
            for (size_t i = 0, j = poly.vertices.size() - 1; i < poly.vertices.size(); j = i++) {
                Vector2 r1 = rotate(poly.vertices[j].x, poly.vertices[j].y);
                Vector2 r2 = rotate(poly.vertices[i].x, poly.vertices[i].y);

                if ((r1.x <= x && r2.x >= x) || (r1.x >= x && r2.x <= x)) {
                    if (std::abs(r2.x - r1.x) > 1e-10) {
                        double t = (x - r1.x) / (r2.x - r1.x);
                        double y = r1.y + t * (r2.y - r1.y);
                        intersections.push_back(y);
                    }
                }
            }
        }

        std::sort(intersections.begin(), intersections.end());

        // Create support lines (pairs of intersections)
        for (size_t k = 0; k + 1 < intersections.size(); k += 2) {
            double y1 = intersections[k];
            double y2 = intersections[k + 1];

            if (y2 - y1 < lineWidth) continue;

            // Create thin polygon for this support segment
            Vector2 p1 = unrotate(x, y1);
            Vector2 p2 = unrotate(x, y2);

            double w = lineWidth * 0.5;
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
            support.push_back(std::move(segment));
        }
    }

    // Add support interface (dense roof)
    if (settings.supportInterfaceLayers > 0) {
        Polygons2D roof = generateSupportInterface(support, settings);
        support.insert(support.end(), roof.begin(), roof.end());
    }

    return support;
}

// ============================================================================
// Support Interface (Dense Roof/Floor)
// ============================================================================

Polygons2D SupportGenerator::generateSupportInterface(const Polygons2D& support, const SliceSettings& settings) {
    Polygons2D interface;

    // Interface is 100% dense pattern on top of support
    // Use concentric or grid pattern at high density
    double lineWidth = settings.lineWidth;
    double spacing = lineWidth * 0.5; // Very dense

    for (const auto& poly : support) {
        if (poly.vertices.size() < 3) continue;

        // Create dense fill inside polygon
        // For simplicity, just offset inward multiple times
        Polygons2D current = {poly};
        for (int i = 0; i < 3; ++i) {
            Polygons2D inner = geometry::offsetPolygons(current, -lineWidth);
            if (inner.empty()) break;
            interface.insert(interface.end(), inner.begin(), inner.end());
            current = std::move(inner);
        }
    }

    return interface;
}

// ============================================================================
// Support Area Calculation
// ============================================================================

Polygons2D SupportGenerator::calculateSupportArea(const LayerSlice& layer, const LayerSlices& allLayers,
                                                  const SliceSettings& settings, int layerIndex) const {
    Polygons2D supportArea;

    // For each perimeter, find overhanging regions
    for (const auto& poly : layer.perimeters) {
        if (poly.vertices.size() < 3) continue;

        // Check each edge for overhang
        for (size_t i = 0; i < poly.vertices.size(); ++i) {
            const auto& v = poly.vertices[i];
            const auto& next = poly.vertices[(i + 1) % poly.vertices.size()];

            // Check if supported by layer below
            bool needsSupport = true;
            if (layerIndex > 0 && !allLayers.empty()) {
                const auto& belowLayer = allLayers[layerIndex - 1];
                for (const auto& belowPoly : belowLayer.perimeters) {
                    // Check if midpoint of edge is inside below polygon
                    Vector2 mid = {(v.x + next.x) * 0.5, (v.y + next.y) * 0.5};
                    if (geometry::pointInPolygon(mid, belowPoly.vertices)) {
                        needsSupport = false;
                        break;
                    }
                }
            }

            if (needsSupport) {
                // Add small polygon around this overhanging edge
                Vector2 edge = {next.x - v.x, next.y - v.y};
                double len = std::sqrt(edge.x*edge.x + edge.y*edge.y);
                if (len < 0.01) continue;
                edge.x /= len; edge.y /= len;
                Vector2 normal = {-edge.y, edge.x}; // Outward

                double supportWidth = settings.supportXYDistance * 2;
                Polygon2D patch;
                patch.vertices = {
                    {v.x, v.y},
                    {next.x, next.y},
                    {next.x + normal.x * supportWidth, next.y + normal.y * supportWidth},
                    {v.x + normal.x * supportWidth, v.y + normal.y * supportWidth}
                };
                supportArea.push_back(std::move(patch));
            }
        }
    }

    // Apply blockers
    for (auto it = supportArea.begin(); it != supportArea.end();) {
        bool blocked = false;
        for (const auto& v : it->vertices) {
            if (isInBlocker(v)) {
                blocked = true;
                break;
            }
        }
        if (blocked) {
            it = supportArea.erase(it);
        } else {
            ++it;
        }
    }

    // Apply enforcers (add support even if not needed)
    // This would add additional areas - simplified for now

    return supportArea;
}

// ============================================================================
// Blockers/Enforcers
// ============================================================================

void SupportGenerator::addBlocker(const SupportBlocker& blocker) {
    m_blockers.push_back(blocker);
}

void SupportGenerator::addEnforcer(const SupportEnforcer& enforcer) {
    m_enforcers.push_back(enforcer);
}

void SupportGenerator::addCustomSupport(const CustomSupportPoint& point) {
    m_customSupports.push_back(point);
}

bool SupportGenerator::isInBlocker(const Vector2& point) const {
    for (const auto& blocker : m_blockers) {
        if (blocker.enabled && blocker.region.contains({point.x, point.y, 0})) {
            return true;
        }
    }
    return false;
}

bool SupportGenerator::isInEnforcer(const Vector2& point) const {
    for (const auto& enforcer : m_enforcers) {
        if (enforcer.enabled && enforcer.region.contains({point.x, point.y, 0})) {
            return true;
        }
    }
    return false;
}

// ============================================================================
// Overhang Detection
// ============================================================================

bool SupportGenerator::needsSupport(const Vector2& point, const LayerSlice& layer, const LayerSlices& allLayers,
                                    const SliceSettings& settings, int layerIndex) const {
    // Check blockers first
    if (isInBlocker(point)) return false;
    if (isInEnforcer(point)) return true;

    // Check if point is in an overhang area
    if (layerIndex > 0 && !allLayers.empty()) {
        const auto& belowLayer = allLayers[layerIndex - 1];
        for (const auto& poly : belowLayer.perimeters) {
            if (geometry::pointInPolygon(point, poly.vertices)) {
                return false;
            }
        }

        if (layerIndex > 1) {
            const auto& twoBelow = allLayers[layerIndex - 2];
            for (const auto& poly : twoBelow.perimeters) {
                if (geometry::pointInPolygon(point, poly.vertices)) {
                    return true;
                }
            }
        }

        bool coveredByBelow = false;
        for (const auto& poly : belowLayer.perimeters) {
            if (geometry::pointInPolygon(point, poly.vertices)) {
                coveredByBelow = true;
                break;
            }
        }

        const auto& currentLayer = allLayers[layerIndex];
        for (const auto& poly : currentLayer.perimeters) {
            if (geometry::pointInPolygon(point, poly.vertices)) {
                if (!coveredByBelow) return true;
                break;
            }
        }
    }
}

double SupportGenerator::overhangAngle(const Vector3& normal) const {
    // Angle from vertical (0 = vertical, 90 = horizontal)
    return std::acos(std::abs(normal.z)) * 180.0 / M_PI;
}

} // namespace printing
} // namespace ks