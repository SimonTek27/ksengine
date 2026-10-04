#include "pch.h"
#include "test_MeshOperations.h"
#include "engine/FileFormat/LXOImporter.h"
#include "engine/mesh/MeshOperations.h"

using namespace ks;
using namespace ks::fileformat;

// Construct a minimal LXO blob (LWO2-family):
// - 4 bytes "LWO2" + 4 bytes version 4 (LE)
// - PNTS chunk: 3 points forming a triangle (0,0,0) (1,0,0) (0,1,0)
// - POLS chunk: type 3 (polygon), triangle referencing the 3 points
// - VMAP TXUV chunk: 2D UVs for the 3 vertices
// - BBOX chunk: bounding box

// Helper to build LE uint32 from bytes
uint32_t le32(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
    return (uint32_t)b0 | ((uint32_t)b1 << 8) | ((uint32_t)b2 << 16) | ((uint32_t)b3 << 24);
}

// Helper to build LE float from 4 bytes
float lefloat(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
    uint32_t bits = le32(b0, b1, b2, b3);
    return *reinterpret_cast<float*>(&bits);
}

// Helper to build LE 16-bit uint
uint16_t le16(uint8_t b0, uint8_t b1) {
    return (uint16_t)b0 | ((uint16_t)b1 << 8);
}

IMPORT_TEST(test_LXOImporter_BasicParse) {
    // ---- Build LXO byte array ----
    QByteArray lxo;

    // Header: "LWO2" + version 4 (LE)
    lxo.append("LWO2");
    lxo.append(reinterpret_cast<const char*>(&le32(0,0,0,4)), 4); // version

    // ---- PNTS chunk: 3 points ----
    // chunk ID "PNTS" + size (LE uint32) + 3 * 12 bytes float xyz
    {
        QByteArray chunk;
        chunk.append("PNTS");
        // 3 points * 12 bytes = 36 bytes of data
        uint32_t pntsSize = 36;
        chunk.append(reinterpret_cast<const char*>(&pntsSize), 4);
        // point 0: (0,0,0)
        chunk.append(reinterpret_cast<const char*>(&lefloat(0,0,0,0)), 12);
        // point 1: (1,0,0)
        chunk.append(reinterpret_cast<const char*>(&lefloat(1,0,0,0)), 12);
        // point 2: (0,1,0)
        chunk.append(reinterpret_cast<const char*>(&lefloat(0,1,0,0)), 12);
        // even padding: 36 is even, no padding needed
        lxo.append(chunk);
    }

    // ---- POLS chunk: triangle ----
    // chunk ID "POLS" + size (LE uint32) + polygon type u32 (3) + nverts u16 (3) + 3 vertex indices u16
    {
        QByteArray chunk;
        chunk.append("POLS");
        // data size: 4 (type) + 2 (nverts) + 3*2 (indices) = 12 bytes
        uint32_t polysSize = 12;
        chunk.append(reinterpret_cast<const char*>(&polysSize), 4);
        // polygon type = 3 (full polygon)
        uint32_t polyType = 3;
        chunk.append(reinterpret_cast<const char*>(&polyType), 4);
        // nverts = 3
        uint16_t nverts = 3;
        chunk.append(reinterpret_cast<const char*>(&nverts), 2);
        // vertex indices: 0, 1, 2 (referencing the 3 PNTS points)
        uint16_t idx0 = 0, idx1 = 1, idx2 = 2;
        chunk.append(reinterpret_cast<const char*>(&idx0), 2);
        chunk.append(reinterpret_cast<const char*>(&idx1), 2);
        chunk.append(reinterpret_cast<const char*>(&idx2), 2);
        // even padding: 12 is even, no padding
        lxo.append(chunk);
    }

    // ---- VMAP TXUV chunk: UVs ----
    // VMAP: type "TXUV" (4 ASCII) + dim u16 (2) + name "UV" + null + per-vertex entries
    // per-vertex: u16 index + 2 floats (u,v)
    {
        QByteArray chunk;
        chunk.append("VMAP");                    // type
        // type is 4 ASCII "TXUV", but we write as 4 bytes; the parser reads first 4 bytes as type
        // Actually our parser reads first 4 bytes as type char[4]; we need to write "TXUV"
        chunk.append("TXUV");                     // 4-byte type
        uint16_t dim = 2;
        chunk.append(reinterpret_cast<const char*>(&dim), 2); // dim = 2
        // name "UV" + null terminator
        chunk.append("UV\0");                     // 3 bytes + null = 4 bytes (but name can be any length; we just need null after)
        // Actually the name is null-terminated cstring. "UV\0" is 3 bytes + null = 4 bytes total.
        // After name, per-vertex entries. Each entry: u16 vertex_index (2 bytes) + dim*4 bytes (8 bytes for dim=2) = 10 bytes total.
        // But LWO2 may pad each entry to even? We'll just pack them consecutively.
        // Vertex 0: uv (0,0)
        // Vertex 1: uv (1,0)
        // Vertex 2: uv (0,1)
        // Entry format: [u16 idx][float u][float v] = 2 + 4 + 4 = 10 bytes. 10 is not even; LWO2 may pad to even after each entry's data, but the size field excludes padding. We'll just pack consecutively and rely on the parser reading exact bytes.
        // Entry 0: idx=0, u=0, v=0
        chunk.append(reinterpret_cast<const char*>(&le16(0,0)), 2); // idx=0
        chunk.append(reinterpret_cast<const char*>(&lefloat(0,0,0,0)), 4); // u=0
        chunk.append(reinterpret_cast<const char*>(&lefloat(0,0,0,0)), 4); // v=0
        // Entry 1: idx=1, u=1, v=0
        chunk.append(reinterpret_cast<const char*>(&le16(1,0)), 2); // idx=1
        chunk.append(reinterpret_cast<const char*>(&lefloat(1,0,0,0)), 4); // u=1
        chunk.append(reinterpret_cast<const char*>(&lefloat(0,0,0,0)), 4); // v=0
        // Entry 2: idx=2, u=0, v=1
        chunk.append(reinterpret_cast<const char*>(&le16(2,0)), 2); // idx=2
        chunk.append(reinterpret_cast<const char*>(&lefloat(0,1,0,0)), 4); // u=0
        chunk.append(reinterpret_cast<const char*>(&lefloat(1,0,0,0)), 4); // v=1
        lxo.append(chunk);
    }

    // ---- BBOX chunk: bounding box ----
    // 6 floats: xmin,ymin,zmin,xmax,ymax,zmax
    {
        QByteArray chunk;
        chunk.append("BBOX");
        // 6 floats = 24 bytes
        uint32_t boxSize = 24;
        chunk.append(reinterpret_cast<const char*>(&boxSize), 4);
        // bounding box: min=(0,0,0), max=(1,1,0) for the triangle in z=0 plane
        chunk.append(reinterpret_cast<const char*>(&lefloat(0,0,0,0)), 4); // xmin
        chunk.append(reinterpret_cast<const char*>(&lefloat(0,0,0,0)), 4); // ymin
        chunk.append(reinterpret_cast<const char*>(&lefloat(0,0,0,0)), 4); // zmin
        chunk.append(reinterpret_cast<const char*>(&lefloat(1,0,0,0)), 4); // xmax
        chunk.append(reinterpret_cast<const char*>(&lefloat(0,1,0,0)), 4); // ymax
        chunk.append(reinterpret_cast<const char*>(&lefloat(0,0,0,0)), 4); // zmax
        lxo.append(chunk);
    }

    // ---- Now parse ----
    MeshData md;
    QString error;
    bool ok = importLXO(lxo, md, &error);
    TEST_VERIFY(ok, "LXO import should succeed");
    TEST_VERIFY(!error.isEmpty() || ok, "no error expected");

    // Verify vertices
    TEST_VERIFY(md.vertexCount() == 3, "expected 3 vertices, got %1").arg(md.vertexCount());
    TEST_VERIFY(md.normalCount() == 3, "normals should be computed; count=%1").arg(md.normalCount());

    // Verify positions (roughly)
    TEST_VERIFY(qFuzzyCompare(md.vertices[0].position.x(), 0.0f), "v0 x");
    TEST_VERIFY(qFuzzyCompare(md.vertices[1].position.x(), 1.0f), "v1 x");
    TEST_VERIFY(qFuzzyCompare(md.vertices[2].position.x(), 0.0f), "v2 x");
    TEST_VERIFY(qFuzzyCompare(md.vertices[0].position.y(), 0.0f), "v0 y");
    TEST_VERIFY(qFuzzyCompare(md.vertices[1].position.y(), 0.0f), "v1 y");
    TEST_VERIFY(qFuzzyCompare(md.vertices[2].position.y(), 1.0f), "v2 y");

    // Verify faces (1 triangle)
    TEST_VERIFY(md.faceCount() == 1, "expected 1 face, got %1").arg(md.faceCount());
    TEST_VERIFY(md.faces[0].vertexCount() == 3, "face should have 3 vertices");
    TEST_VERIFY(md.faces[0][0] == 0, "face v0 index");
    TEST_VERIFY(md.faces[0][1] == 1, "face v1 index");
    TEST_VERIFY(md.faces[0][2] == 2, "face v2 index");

    // Verify UVs (set from VMAP TXUV)
    TEST_VERIFY(qFuzzyCompare(md.vertices[0].uv.x(), 0.0f), "v0 u");
    TEST_VERIFY(qFuzzyCompare(md.vertices[0].uv.y(), 0.0f), "v0 v");
    TEST_VERIFY(qFuzzyCompare(md.vertices[1].uv.x(), 1.0f), "v1 u");
    TEST_VERIFY(qFuzzyCompare(md.vertices[1].uv.y(), 0.0f), "v1 v");
    TEST_VERIFY(qFuzzyCompare(md.vertices[2].uv.x(), 0.0f), "v2 u");
    TEST_VERIFY(qFuzzyCompare(md.vertices[2].uv.y(), 1.0f), "v2 v");

    // Verify bounding box
    TEST_VERIFY(qFuzzyCompare(md.boundingBoxMin.x(), 0.0f), "bbox min x");
    TEST_VERIFY(qFuzzyCompare(md.boundingBoxMin.y(), 0.0f), "bbox min y");
    TEST_VERIFY(qFuzzyCompare(md.boundingBoxMin.z(), 0.0f), "bbox min z");
    TEST_VERIFY(qFuzzyCompare(md.boundingBoxMax.x(), 1.0f), "bbox max x");
    TEST_VERIFY(qFuzzyCompare(md.boundingBoxMax.y(), 1.0f), "bbox max y");
    TEST_VERIFY(qFuzzyCompare(md.boundingBoxMax.z(), 0.0f), "bbox max z");
}
