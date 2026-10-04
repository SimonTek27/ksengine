#include "BooleanOps.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QTextStream>

// Include CGAL headers conditionally
#if defined(HAS_CGAL) && HAS_CGAL
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Polygon_mesh_processing/corefinement.h>
#include <CGAL/Polygon_mesh_processing/repair.h>
#include <CGAL/Polygon_mesh_processing/orientation.h>
#include <CGAL/Polygon_mesh_processing/triangulate_faces.h>
#include <CGAL/boost/graph/helpers.h>
#include <CGAL/IO/STL.h>
#include <CGAL/IO/PLY.h>
#include <CGAL/IO/OBJ.h>

using Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;
using Point_3 = Kernel::Point_3;
using Triangle_3 = Kernel::Triangle_3;
using Triangle_mesh = CGAL::Surface_mesh<Point_3>;
using Face_index = Triangle_mesh::Face_index;

namespace PMP = CGAL::Polygon_mesh_processing;
#endif

namespace ks::geometry {

// ============================================================================
// Implementation
// ============================================================================

std::string BooleanOperations::validateMesh(const GeoMeshData& mesh) {
    if (mesh.vertices.empty()) {
        return "Mesh has no vertices";
    }

    if (mesh.faces.empty()) {
        return "Mesh has no faces";
    }

    return "";
}

// ============================================================================
// Boolean Operations Implementation
// ============================================================================

BoolOpResult BooleanOperations::performOperation(
    const GeoMeshData& meshA,
    const GeoMeshData& meshB,
    Operation op) {
    return performOperationImpl(meshA, meshB, op);
}

BoolOpResult BooleanOperations::performOperationImpl(
    const GeoMeshData& meshA,
    const GeoMeshData& meshB,
    Operation op) {

    BoolOpResult result;
    QElapsedTimer timer;
    timer.start();

#if defined(HAS_CGAL) && HAS_CGAL
    try {
        // Convert to CGAL format
        std::vector<Point_3> cgalVerticesA, cgalVerticesB;
        std::vector<std::vector<size_t>> cgalFacesA, cgalFacesB;

        // Convert meshA vertices and faces to CGAL
        for (const auto& vertex : meshA.vertices) {
            cgalVerticesA.push_back(Point_3(vertex.x, vertex.y, vertex.z));
        }

        for (const auto& face : meshA.faces) {
            cgalFacesA.push_back({face.v0, face.v1, face.v2});
        }

        // Convert meshB vertices and faces to CGAL
        for (const auto& vertex : meshB.vertices) {
            cgalVerticesB.push_back(Point_3(vertex.x, vertex.y, vertex.z));
        }

        for (const auto& face : meshB.faces) {
            cgalFacesB.push_back({face.v0, face.v1, face.v2});
        }

        // Create CGAL meshes
        Triangle_mesh cgalMeshA, cgalMeshB, cgalResult;

        // Build CGAL meshes robustly from polygon soups
        PMP::polygon_soup_to_polygon_mesh(cgalVerticesA, cgalFacesA, cgalMeshA);
        PMP::polygon_soup_to_polygon_mesh(cgalVerticesB, cgalFacesB, cgalMeshB);

        if (cgalMeshA.is_empty() || cgalMeshB.is_empty()) {
            result.status = BoolOpResult::InvalidInput;
            result.errorMessage = "Input mesh cannot be empty";
            result.executionTimeMs = timer.elapsed();
            return result;
        }

        // Ensure valid, closed, triangulated and consistently oriented inputs so
        // the corefinement does not hit degenerate/non-manifold cases.
        auto preprocess = [&](Triangle_mesh& m) {
            PMP::remove_degenerate_faces(m);
            PMP::triangulate_faces(m);
            PMP::remove_isolated_vertices(m);
            PMP::orient_to_bound_a_volume(m);
        };
        preprocess(cgalMeshA);
        preprocess(cgalMeshB);

        bool opSuccess = false;
        if (op == Operation::Union) {
            opSuccess = PMP::corefine_and_compute_union(cgalMeshA, cgalMeshB, cgalResult);
        } else if (op == Operation::Difference) {
            opSuccess = PMP::corefine_and_compute_difference(cgalMeshA, cgalMeshB, cgalResult);
        } else if (op == Operation::Intersection) {
            opSuccess = PMP::corefine_and_compute_intersection(cgalMeshA, cgalMeshB, cgalResult);
        } else if (op == Operation::SymmetricDiff) {
            Triangle_mesh diffAB, diffBA;
            bool ok1 = PMP::corefine_and_compute_difference(cgalMeshA, cgalMeshB, diffAB);
            bool ok2 = PMP::corefine_and_compute_difference(cgalMeshB, cgalMeshA, diffBA);
            if (ok1 && ok2) {
                opSuccess = PMP::corefine_and_compute_union(diffAB, diffBA, cgalResult);
            }
        }

        if (!opSuccess) {
            result.status = BoolOpResult::ComputationFailed;
            result.errorMessage = "CGAL boolean operation returned false";
            result.executionTimeMs = timer.elapsed();
            return result;
        }

        result.result.clear();

        PMP::triangulate_faces(cgalResult);

        for (auto vertex : cgalResult.vertices()) {
            const auto& p = cgalResult.point(vertex);
            GeoVertex v;
            v.x = CGAL::to_double(p.x());
            v.y = CGAL::to_double(p.y());
            v.z = CGAL::to_double(p.z());
            result.result.vertices.push_back(v);
        }

        for (auto face : cgalResult.faces()) {
            auto h = cgalResult.halfedge(face);
            if (h == Triangle_mesh::null_halfedge()) continue;
            std::vector<size_t> idxs;
            for (auto v : cgalResult.vertices_around_face(h))
                idxs.push_back(static_cast<size_t>(v));
            if (idxs.size() < 3) continue;
            GeoFace f;
            f.v0 = static_cast<uint32_t>(idxs[0]);
            f.v1 = static_cast<uint32_t>(idxs[1]);
            f.v2 = static_cast<uint32_t>(idxs[2]);
            result.result.faces.push_back(f);
        }

        recalculateNormals(result.result);

        result.status = BoolOpResult::Success;
        result.errorMessage = "";
        result.executionTimeMs = timer.elapsed();

    } catch (const std::exception& e) {
        result.status = BoolOpResult::ComputationFailed;
        result.errorMessage = std::string("CGAL exception: ") + e.what();
        result.executionTimeMs = timer.elapsed();

        qWarning() << "Boolean operation failed with exception:" << e.what();
    } catch (...) {
        result.status = BoolOpResult::ComputationFailed;
        result.errorMessage = "Unknown exception in boolean operation";
        result.executionTimeMs = timer.elapsed();

        qWarning() << "Boolean operation failed with unknown exception";
    }

#else
    // Fallback implementation without CGAL.
    struct Vec3 { float x = 0, y = 0, z = 0; };
    auto computeAABB = [](const GeoMeshData& mesh) -> std::pair<Vec3, Vec3> {
        if (mesh.vertices.empty()) return std::pair<Vec3, Vec3>();
        Vec3 minV{static_cast<float>(mesh.vertices[0].x), static_cast<float>(mesh.vertices[0].y), static_cast<float>(mesh.vertices[0].z)};
        Vec3 maxV = minV;
        for (const auto& v : mesh.vertices) {
            float vx = static_cast<float>(v.x); float vy = static_cast<float>(v.y); float vz = static_cast<float>(v.z);
            minV.x = (std::min)(minV.x, vx); minV.y = (std::min)(minV.y, vy); minV.z = (std::min)(minV.z, vz);
            maxV.x = (std::max)(maxV.x, vx); maxV.y = (std::max)(maxV.y, vy); maxV.z = (std::max)(maxV.z, vz);
        }
        return std::pair<Vec3, Vec3>(minV, maxV);
    };

    auto aabbsOverlap = [](const std::pair<Vec3, Vec3>& a, const std::pair<Vec3, Vec3>& b) -> bool {
        return (a.first.x <= b.second.x && a.second.x >= b.first.x &&
                a.first.y <= b.second.y && a.second.y >= b.first.y &&
                a.first.z <= b.second.z && a.second.z >= b.first.z);
    };

    auto aabbA = computeAABB(meshA);
    auto aabbB = computeAABB(meshB);
    bool disjoint = !aabbsOverlap(aabbA, aabbB);

    if (op == Operation::Union) {
        GeoMeshData merged;
        merged.vertices = meshA.vertices;
        merged.vertices.insert(merged.vertices.end(),
                                meshB.vertices.begin(),
                                meshB.vertices.end());

        merged.faces = meshA.faces;
        const uint32_t baseIdx = static_cast<uint32_t>(meshA.vertices.size());
        for (const auto& face : meshB.faces) {
            merged.faces.push_back({face.v0 + baseIdx,
                                    face.v1 + baseIdx,
                                    face.v2 + baseIdx});
        }

        merged.normals.resize(merged.vertices.size());
        recalculateNormals(merged);

        result.result = merged;
        result.status = BoolOpResult::Success;
        result.errorMessage = "";
        result.executionTimeMs = timer.elapsed();
    } else if (op == Operation::Difference) {
        if (disjoint) {
            result.result = meshA;
            result.status = BoolOpResult::Success;
            result.errorMessage = "";
        } else {
            result.status = BoolOpResult::ComputationFailed;
            result.errorMessage = "CGAL not available - Difference on overlapping meshes requires CGAL";
        }
        result.executionTimeMs = timer.elapsed();
    } else if (op == Operation::Intersection) {
        if (disjoint) {
            result.result = GeoMeshData();
            result.status = BoolOpResult::Success;
            result.errorMessage = "";
        } else {
            result.status = BoolOpResult::ComputationFailed;
            result.errorMessage = "CGAL not available - Intersection on overlapping meshes requires CGAL";
        }
        result.executionTimeMs = timer.elapsed();
    } else if (op == Operation::SymmetricDiff) {
        if (disjoint) {
            GeoMeshData merged;
            merged.vertices = meshA.vertices;
            merged.vertices.insert(merged.vertices.end(),
                                    meshB.vertices.begin(),
                                    meshB.vertices.end());
            merged.faces = meshA.faces;
            const uint32_t baseIdx = static_cast<uint32_t>(meshA.vertices.size());
            for (const auto& face : meshB.faces) {
                merged.faces.push_back({face.v0 + baseIdx,
                                        face.v1 + baseIdx,
                                        face.v2 + baseIdx});
            }
            merged.normals.resize(merged.vertices.size());
            recalculateNormals(merged);
            result.result = merged;
            result.status = BoolOpResult::Success;
            result.errorMessage = "";
        } else {
            result.status = BoolOpResult::ComputationFailed;
            result.errorMessage = "CGAL not available - SymmetricDiff on overlapping meshes requires CGAL";
        }
        result.executionTimeMs = timer.elapsed();
    }
#endif

    return result;
}

// ============================================================================
// Mesh Repair and Processing
// ============================================================================

GeoMeshData BooleanOperations::repairMesh(const GeoMeshData& mesh) {
    if (mesh.vertices.empty() || mesh.faces.empty()) {
        return mesh;
    }

#if defined(HAS_CGAL) && HAS_CGAL
    try {
        std::vector<Point_3> cgalVertices;
        std::vector<std::vector<size_t>> cgalFaces;

        for (const auto& vertex : mesh.vertices) {
            cgalVertices.push_back(Point_3(vertex.x, vertex.y, vertex.z));
        }

        for (const auto& face : mesh.faces) {
            cgalFaces.push_back({face.v0, face.v1, face.v2});
        }

        Triangle_mesh cgalMesh;
        // Drop degenerate faces (faces referencing the same vertex twice) so the
        // soup can be turned into a valid polygon mesh.
        std::vector<std::vector<size_t>> cleanFaces;
        for (const auto& f : cgalFaces) {
            bool degenerate = false;
            for (size_t i = 0; i < f.size() && !degenerate; ++i)
                for (size_t j = i + 1; j < f.size(); ++j)
                    if (f[i] == f[j]) { degenerate = true; break; }
            if (!degenerate) cleanFaces.push_back(f);
        }
        PMP::polygon_soup_to_polygon_mesh(cgalVertices, cleanFaces, cgalMesh);

        GeoMeshData repairedMesh;

        for (auto vertex : cgalMesh.vertices()) {
            const auto& p = cgalMesh.point(vertex);
            GeoVertex v;
            v.x = CGAL::to_double(p.x());
            v.y = CGAL::to_double(p.y());
            v.z = CGAL::to_double(p.z());
            repairedMesh.vertices.push_back(v);
        }

        for (auto face : cgalMesh.faces()) {
            auto h = cgalMesh.halfedge(face);
            if (h == Triangle_mesh::null_halfedge()) continue;
            std::vector<size_t> idxs;
            for (auto v : cgalMesh.vertices_around_face(h))
                idxs.push_back(static_cast<size_t>(v));
            if (idxs.size() < 3) continue;
            GeoFace f;
            f.v0 = static_cast<uint32_t>(idxs[0]);
            f.v1 = static_cast<uint32_t>(idxs[1]);
            f.v2 = static_cast<uint32_t>(idxs[2]);
            repairedMesh.faces.push_back(f);
        }

        qDebug() << "Repair result:" << repairedMesh.vertices.size() << "verts" << repairedMesh.faces.size() << "faces";
        return repairedMesh;

    } catch (...) {
        qWarning() << "Mesh repair failed";
        return mesh;
    }
#endif

    return mesh;
}

void BooleanOperations::recalculateNormals(GeoMeshData& mesh) {
    if (mesh.vertices.empty() || mesh.faces.empty()) {
        return;
    }

    mesh.normals.clear();
    mesh.normals.resize(mesh.vertices.size(), GeoVertex(0, 0, 0));

    for (const auto& face : mesh.faces) {
        if (face.v0 >= mesh.vertices.size() ||
            face.v1 >= mesh.vertices.size() ||
            face.v2 >= mesh.vertices.size())
            continue;

        const GeoVertex& v0 = mesh.vertices[face.v0];
        const GeoVertex& v1 = mesh.vertices[face.v1];
        const GeoVertex& v2 = mesh.vertices[face.v2];

        GeoVertex edge1(v1.x - v0.x, v1.y - v0.y, v1.z - v0.z);
        GeoVertex edge2(v2.x - v0.x, v2.y - v0.y, v2.z - v0.z);

        GeoVertex normal(
            edge1.y * edge2.z - edge1.z * edge2.y,
            edge1.z * edge2.x - edge1.x * edge2.z,
            edge1.x * edge2.y - edge1.y * edge2.x
        );

        double length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (length > 0) {
            normal.x /= length;
            normal.y /= length;
            normal.z /= length;
        }

        mesh.normals[face.v0] += normal;
        mesh.normals[face.v1] += normal;
        mesh.normals[face.v2] += normal;
    }

    for (auto& normal : mesh.normals) {
        double length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (length > 0) {
            normal.x /= length;
            normal.y /= length;
            normal.z /= length;
        }
    }
}

GeoMeshData BooleanOperations::computeNormals(const GeoMeshData& mesh) {
    GeoMeshData result = mesh;
    recalculateNormals(result);
    return result;
}

// ============================================================================
// Batch Operations
// ============================================================================

BoolOpResult BooleanOperations::performBatchOperations(
    const std::vector<GeoMeshData>& meshes,
    const std::vector<Operation>& operations) {

    if (meshes.size() < 2) {
        BoolOpResult result;
        result.status = BoolOpResult::InvalidInput;
        result.errorMessage = "Need at least 2 meshes for batch operations";
        return result;
    }

    if (operations.size() != meshes.size() - 1) {
        BoolOpResult result;
        result.status = BoolOpResult::InvalidInput;
        result.errorMessage = "Number of operations must be meshes.size() - 1";
        return result;
    }

    QElapsedTimer timer;
    timer.start();

    BoolOpResult result;
    GeoMeshData current = meshes[0];

    for (size_t i = 0; i < operations.size(); ++i) {
        auto opResult = performOperationImpl(current, meshes[i + 1], operations[i]);

        if (!opResult.isSuccess()) {
            result.status = BoolOpResult::ComputationFailed;
            result.errorMessage = opResult.errorMessage;
            result.executionTimeMs = timer.elapsed();
            return result;
        }

        current = opResult.result;
    }

    result.result = current;
    result.status = BoolOpResult::Success;
    result.errorMessage = "";
    result.executionTimeMs = timer.elapsed();

    return result;
}

const char* BooleanOperations::operationName(Operation op) {
    switch (op) {
        case Union: return "Union";
        case Difference: return "Difference";
        case Intersection: return "Intersection";
        case SymmetricDiff: return "Symmetric Diff";
    }
    return "Unknown";
}

bool BooleanOperations::canPerform() {
#if defined(HAS_CGAL) && HAS_CGAL
    return true;
#else
    return false;
#endif
}

GeoMeshData BooleanOperations::bevelOperation(const GeoMeshData& mesh, float bevelAmount, int bevelSegments) {
    GeoMeshData result = mesh;
    if (result.vertices.empty() || bevelAmount <= 0.0f) return result;
    
    // Simple bevel implementation: extrude and connect new edges
    // This is a placeholder - full implementation would use CGAL or custom mesh operations
    float segmentSize = bevelAmount / bevelSegments;
    
    // For now, return the original mesh - full CGAL-based bevel would require
    // CGAL integration which is conditionally compiled
    return result;
}

GeoMeshData BooleanOperations::bridgeOperation(const GeoMeshData& meshA, const GeoMeshData& meshB, float bridgeAmount, int bridgeSegments) {
    GeoMeshData result = meshA;
    if (meshA.vertices.empty() || meshB.vertices.empty() || bridgeAmount <= 0.0f) return result;
    
    // Simple bridge implementation: would connect two meshes with intermediate geometry
    // This is a placeholder - full implementation would use CGAL or custom mesh operations
    return result;
}

const char* BooleanOperations::bevelName(float amount, int segments) {
    return QString("Bevel %1 segments %2").arg(amount, 0, 'f', 2).arg(segments).toLocal8Bit().constData();
}

const char* BooleanOperations::bridgeName(float amount, int segments) {
    return QString("Bridge %1 segments %2").arg(amount, 0, 'f', 2).arg(segments).toLocal8Bit().constData();
}

} // namespace ks::geometry
