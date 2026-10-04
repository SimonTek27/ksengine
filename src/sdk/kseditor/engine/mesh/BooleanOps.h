#pragma once

#include "GeometryTypes.h"
#include <vector>
#include <string>

namespace ks::geometry {

class BooleanOperations {
public:
    enum Operation { Union, Difference, Intersection, SymmetricDiff };

    static BoolOpResult performOperation(const GeoMeshData& meshA, const GeoMeshData& meshB, Operation op);
    static BoolOpResult performBatchOperations(const std::vector<GeoMeshData>& meshes, const std::vector<Operation>& operations);
    static std::string validateMesh(const GeoMeshData& mesh);
    static GeoMeshData repairMesh(const GeoMeshData& mesh);
    static GeoMeshData computeNormals(const GeoMeshData& mesh);
    static GeoMeshData bevelOperation(const GeoMeshData& mesh, float bevelAmount, int bevelSegments);
    static GeoMeshData bridgeOperation(const GeoMeshData& meshA, const GeoMeshData& meshB, float bridgeAmount, int bridgeSegments);

    static bool canPerform();
    static const char* operationName(Operation op);
    static const char* bevelName(float amount, int segments);
    static const char* bridgeName(float amount, int segments);

private:
    static BoolOpResult performOperationImpl(const GeoMeshData& meshA, const GeoMeshData& meshB, Operation op);
    static void recalculateNormals(GeoMeshData& mesh);
};

} // namespace ks::geometry
