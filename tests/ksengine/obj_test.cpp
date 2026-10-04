// OBJ/MTL importer test: synthetic coverage for the tricky parts of the
// format (n-gon fan triangulation, vertex welding, `v//vn` and negative
// indices, object/group split, malformed input) plus a byte-exact run against
// a real AC model that mixes triangles, quads and n-gons up to 19 corners —
// the corner count the Qt copy silently truncated.

#include "engine/FileFormat/CADOBJParser.h"
#include "KsTest.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

namespace ff = ks::engine::fileformat;

void checkClose(const char* what, double actual, double expected,
                double eps = 1e-5) {
    ks::test::report(std::fabs(actual - expected) <= eps, what, __FILE__, __LINE__);
}

bool finiteVec(const Vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

void testQuadsAndWelding() {
    const std::string obj =
        "# quad\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 1 1 0\n"
        "v 0 1 0\n"
        "f 1 2 3 4\n";

    ff::CADOBJParser parser;
    KS_CHECK(parser.loadFromString(obj));
    KS_CHECK(parser.error().empty());
    KS_CHECK(parser.scene().meshes.size() == 1);
    if (parser.scene().meshes.size() != 1) return;

    const ff::ObjMesh& mesh = parser.scene().meshes.front();
    KS_CHECK(mesh.name == "default");
    // One weld per unique corner: four corners, not eight.
    KS_CHECK(mesh.vertices.size() == 4);
    KS_CHECK(mesh.triangleCount() == 2);
    KS_CHECK(mesh.indices.size() == 6);
    KS_CHECK(parser.scene().faceCount == 1);
    KS_CHECK(parser.scene().triangleCount == 2);

    const std::uint32_t expected[6] = {0, 1, 2, 0, 2, 3};
    for (std::size_t i = 0; i < 6 && i < mesh.indices.size(); ++i) {
        KS_CHECK(mesh.indices[i] == expected[i]);
    }
    for (const Vec3& v : mesh.vertices) KS_CHECK(finiteVec(v));
}

void testNGon() {
    // Five corners -> three triangles. The Qt copy read exactly four tokens
    // and dropped the rest of the polygon.
    const std::string obj =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 2 1 0\n"
        "v 1 2 0\n"
        "v 0 2 0\n"
        "f 1 2 3 4 5\n";

    ff::CADOBJParser parser;
    KS_CHECK(parser.loadFromString(obj));
    KS_CHECK(parser.scene().triangleCount == 3);
    if (!parser.scene().meshes.empty()) {
        KS_CHECK(parser.scene().meshes.front().triangleCount() == 3);
        KS_CHECK(parser.scene().meshes.front().vertices.size() == 5);
    }
}

void testNormalsTexCoords() {
    const std::string obj =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "vt 0 0\n"
        "vt 1 0\n"
        "vt 0 1\n"
        "vn 0 0 1\n"
        "f 1//1 2//1 3//1\n"
        "f 1/1/1 2/2/1 3/3/1\n";

    ff::CADOBJParser parser;
    KS_CHECK(parser.loadFromString(obj));
    KS_CHECK(parser.scene().triangleCount == 2);
    if (parser.scene().meshes.empty()) return;

    const ff::ObjMesh& mesh = parser.scene().meshes.front();
    KS_CHECK(mesh.hasNormals);
    // The first face has no texcoords, the second does, so both flags end up
    // set and the parallel arrays still line up.
    KS_CHECK(mesh.hasTexCoords);
    KS_CHECK(mesh.vertices.size() == mesh.normals.size());
    KS_CHECK(mesh.vertices.size() == mesh.texCoords.size());

    // `v//1` and `v/1/1` are different weld keys, so the three positions show
    // up twice: once with no texcoord, once with one.
    KS_CHECK(mesh.vertices.size() == 6);
    for (std::uint32_t idx : mesh.indices) KS_CHECK(idx < mesh.vertices.size());

    if (mesh.normals.size() == 6) {
        for (std::size_t i = 0; i < 3; ++i) {
            checkClose("implicit normal x", mesh.normals[i].x, 0.0);
            checkClose("implicit normal y", mesh.normals[i].y, 0.0);
            checkClose("explicit normal z", mesh.normals[i + 3].z, 1.0);
            // Corners without a `vt` must still carry a zero texcoord.
            checkClose("implicit u", mesh.texCoords[i].x, 0.0);
        }
        checkClose("explicit u", mesh.texCoords[4].x, 1.0);
        checkClose("explicit v", mesh.texCoords[5].y, 1.0);
    }
}

void testNegativeIndices() {
    const std::string obj =
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "f -3 -2 -1\n";

    ff::CADOBJParser parser;
    KS_CHECK(parser.loadFromString(obj));
    KS_CHECK(parser.scene().triangleCount == 1);
    if (!parser.scene().meshes.empty() && parser.scene().meshes.front().vertices.size() == 3) {
        const Vec3& v0 = parser.scene().meshes.front().vertices[0];
        checkClose("negative index v0.x", v0.x, 0.0f);
        const Vec3& v1 = parser.scene().meshes.front().vertices[1];
        checkClose("negative index v1.x", v1.x, 1.0f);
    }
}

void testObjectsAndMaterials() {
    const std::string obj =
        "mtllib no_such_library.mtl\n"
        "o First\n"
        "v 0 0 0\n"
        "v 1 0 0\n"
        "v 0 1 0\n"
        "usemtl Alpha\n"
        "f 1 2 3\n"
        "g SecondGroup\n"
        "f 1 3 2\n"
        "usemtl Beta\n";

    ff::CADOBJParser parser;
    // A missing material library is not fatal: the geometry still parses.
    KS_CHECK(parser.loadFromString(obj));
    KS_CHECK(parser.scene().meshes.size() == 2);
    if (parser.scene().meshes.size() == 2) {
        KS_CHECK(parser.scene().meshes[0].name == "First");
        KS_CHECK(parser.scene().meshes[0].materialName == "Alpha");
        KS_CHECK(parser.scene().meshes[1].name == "SecondGroup");
        KS_CHECK(parser.scene().meshes[1].materialName == "Beta");
    }
}

void testFailures() {
    ff::CADOBJParser parser;

    KS_CHECK(!parser.loadFromString(""));
    KS_CHECK(!parser.error().empty());

    KS_CHECK(!parser.loadFromString("# only a comment\n"));
    KS_CHECK(parser.error() == "no faces");

    // Two corners is not a polygon.
    KS_CHECK(!parser.loadFromString("v 0 0 0\nv 1 0 0\nf 1 2\n"));
    KS_CHECK(!parser.error().empty());

    // Index past the end of the vertex list.
    KS_CHECK(!parser.loadFromString("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 99\n"));
    KS_CHECK(parser.error().find("out of range") != std::string::npos);

    // OBJ index 0 is illegal.
    KS_CHECK(!parser.loadFromString("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 0 2 3\n"));
    KS_CHECK(!parser.error().empty());

    // Truncated position line.
    KS_CHECK(!parser.loadFromString("v 0 0\nf 1 1 1\n"));
    KS_CHECK(!parser.error().empty());

    // Unreadable file.
    KS_CHECK(!parser.loadFromFile("no/such/directory/model.obj"));
    KS_CHECK(parser.error().find("cannot open") != std::string::npos);

    // Unreadable material library.
    KS_CHECK(!parser.loadMTL("no/such/directory/model.mtl"));
}

constexpr const char* kModelDir =
    "F:/SteamLibrary/steamapps/common/assettocorsa/content/cars/"
    "vsf1_ferrari_f300/template/";

// Blender export: 23279 vertices, 22606 texcoords, 21054 normals, 19608 faces
// (527 tris, 18839 quads, and 142 n-gons up to 19 corners) across 11 objects,
// sharing two materials from f300.mtl.
void testRealFile() {
    const std::string path = std::string(kModelDir) + "f300.obj";
    if (!fs::exists(path)) return;

    ff::CADOBJParser parser;
    const bool ok = parser.loadFromFile(path);
    KS_CHECK(ok);
    if (!ok) std::printf("  f300.obj: %s\n", parser.error().c_str());
    KS_CHECK(parser.error().empty());

    const ff::ObjScene& scene = parser.scene();
    KS_CHECK(scene.sourcePath == path);
    KS_CHECK(scene.positionCount == 23279);
    KS_CHECK(scene.texCoordCount == 22606);
    KS_CHECK(scene.normalCount == 21054);
    KS_CHECK(scene.faceCount == 19608);
    // Fan triangulation: sum of (corners - 2) over every face.
    KS_CHECK(scene.triangleCount == 39191);
    KS_CHECK(scene.meshes.size() == 11);

    // `mtllib f300.mtl` resolved next to the OBJ.
    KS_CHECK(scene.materials.size() == 2);
    const ff::ObjMaterial* paint = scene.findMaterial("Paint");
    KS_CHECK(paint != nullptr);
    if (paint != nullptr) {
        checkClose("Paint Kd r", paint->kd.x, 0.64f, 1e-5f);
        checkClose("Paint Kd g", paint->kd.y, 0.074426f, 1e-5f);
        checkClose("Paint Kd b", paint->kd.z, 0.09119f, 1e-5f);
        checkClose("Paint Ns", paint->ns, 96.078431f, 1e-3f);
        checkClose("Paint d", paint->d, 1.0f);
        KS_CHECK(paint->illum == 2);
        // map_Kd contains spaces — it must be read as the rest of the line.
        KS_CHECK(paint->mapKd.find("paint.dds") != std::string::npos);
        KS_CHECK(paint->mapKd.find("Program Files") != std::string::npos);
    }
    KS_CHECK(scene.findMaterial("wing") != nullptr);
    KS_CHECK(scene.findMaterial("missing") == nullptr);

    const ff::ObjMesh* first = scene.findMesh("Cube.049_Cube.040");
    KS_CHECK(first != nullptr);
    KS_CHECK(first != nullptr && first->materialName == "Paint");

    std::size_t triangles = 0;
    std::size_t normals = 0;
    std::size_t texcoords = 0;
    for (const ff::ObjMesh& mesh : scene.meshes) {
        triangles += mesh.triangleCount();
        if (mesh.hasNormals) ++normals;
        if (mesh.hasTexCoords) ++texcoords;
        KS_CHECK(!mesh.materialName.empty());
        KS_CHECK(mesh.vertices.size() == mesh.normals.size());
        KS_CHECK(mesh.vertices.size() == mesh.texCoords.size());
        KS_CHECK(mesh.indices.size() % 3 == 0);
        for (const Vec3& v : mesh.vertices) KS_CHECK(finiteVec(v));
        for (std::uint32_t idx : mesh.indices) KS_CHECK(idx < mesh.vertices.size());
    }
    KS_CHECK(triangles == 39191);
    KS_CHECK(normals == 11);
    KS_CHECK(texcoords == 11);
}

} // namespace

int main() {
    testQuadsAndWelding();
    testNGon();
    testNormalsTexCoords();
    testNegativeIndices();
    testObjectsAndMaterials();
    testFailures();
    testRealFile();
    return KS_TEST_RESULT("obj_test");
}
