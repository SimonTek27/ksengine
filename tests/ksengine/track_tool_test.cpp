// Parity 3.3 — track tooling: scaffold/validate roundtrip + AI spline
// writer compatibility with AiFileReader (and therefore the Qt editor's
// AiSplineEditor, which writes the same layout).
//
// Compiles TrackToolCore + TrackLoader directly (track_loader_test pattern).

#include "TrackToolCore.h"
#include "TrackLoader.h"
#include "engine/AI/AiFileReader.h"
#include "engine/AI/AiFileWriter.h"
#include "engine/FileFormat/Kn5Reader.h"
#include "KsTest.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string readAll(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
}

} // namespace

int main()
{
    const fs::path dir = fs::temp_directory_path() / "ks_qtfree_tracktool_test";
    std::error_code ec;
    fs::remove_all(dir, ec);

    // validate rejects a missing directory
    const ks::tracktool::TrackReport missing =
        ks::tracktool::validateTrack((dir / "nope").string());
    KS_CHECK(!missing.ok);
    KS_CHECK(!missing.errors.empty());

    // generated oval: geometry + constant curvature/speed
    const ks::ai::AiSpline oval = ks::tracktool::generateOvalSpline();
    KS_CHECK(oval.points.size() == 200);
    KS_CHECK_NEAR(oval.points[0].curvature, 1.0 / 500.0, 1e-6);
    for (const ks::ai::AiSplinePoint& point : oval.points) {
        const double radius = std::sqrt(
            double(point.position.x) * point.position.x +
            double(point.position.z) * point.position.z);
        KS_CHECK_NEAR(radius, 500.0, 1e-3);
        KS_CHECK_NEAR(point.speed, 55.0, 1e-6);
    }

    // AiFileWriter roundtrip through the engine reader
    fs::create_directories(dir);
    const fs::path aiFile = dir / "roundtrip.ai";
    KS_CHECK(ks::ai::AiFileWriter::writeSpline(aiFile.string(), oval));
    const std::string image = readAll(aiFile);
    KS_CHECK(image.size() == 12 + oval.points.size() * 20);
    // magic 0x00414900 stored little-endian: 00 49 41 00
    const auto* magic = reinterpret_cast<const unsigned char*>(image.data());
    KS_CHECK(magic[0] == 0x00 && magic[1] == 0x49 && magic[2] == 0x41 &&
             magic[3] == 0x00);

    const ks::ai::AiSpline readBack = ks::ai::AiFileReader::readSpline(aiFile.string());
    KS_CHECK(readBack.points.size() == oval.points.size());
    KS_CHECK(readBack.closed); // last→first gap is 1/N of the lap
    // finishSpline sums the 199 consecutive chords (the closing gap is the
    // "closed" test, not part of totalDistance): (N-1) * 2R*sin(pi/N)
    const double chord = 2.0 * 500.0 * std::sin(3.14159265358979323846 / 200.0);
    KS_CHECK_NEAR(readBack.totalDistance, 199.0 * chord, 0.1);
    if (readBack.points.size() == oval.points.size()) {
        for (std::size_t i = 0; i < oval.points.size(); ++i) {
            KS_CHECK(readBack.points[i].position.x == oval.points[i].position.x);
            KS_CHECK(readBack.points[i].position.y == oval.points[i].position.y);
            KS_CHECK(readBack.points[i].position.z == oval.points[i].position.z);
            KS_CHECK(readBack.points[i].curvature == oval.points[i].curvature);
            KS_CHECK(readBack.points[i].speed == oval.points[i].speed);
        }
    }

    // writer guards
    KS_CHECK(!ks::ai::AiFileWriter::writeSpline(aiFile.string(), ks::ai::AiSpline{}));
    KS_CHECK(!ks::ai::AiFileWriter::writeSpline(
        (dir / "no_such_dir" / "x.ai").string(), oval));

    // scaffold produces a folder TrackLoader + the full engine reader accept
    std::string error;
    KS_CHECK(ks::tracktool::scaffoldTrack(dir.string(), error));
    KS_CHECK(error.empty());
    KS_CHECK(fs::is_regular_file(dir / "model.kn5"));
    KS_CHECK(fs::is_regular_file(dir / "data" / "surfaces.ini"));
    KS_CHECK(fs::is_regular_file(dir / "ai" / "fast_lane.ai"));

    const ks::engine::fileformat::Kn5ParseResult kn5 =
        ks::engine::fileformat::parseKn5File((dir / "model.kn5").string());
    KS_CHECK(kn5.ok());
    KS_CHECK(kn5.file.version == 5);
    KS_CHECK(kn5.file.nodes.size() == 1);
    KS_CHECK(kn5.file.nodes[0].name == "root");

    ks::sim::TrackLoader loader;
    const ks::sim::TrackData track = loader.loadTrackFolder(dir.string());
    KS_CHECK(track.isComplete());
    KS_CHECK(track.kn5HeaderValidated);
    KS_CHECK(track.aiSplineLoaded);
    KS_CHECK(track.aiSplinePath == (dir / "ai" / "fast_lane.ai").string());
    KS_CHECK(track.surfacesLoaded);
    KS_CHECK(track.surfaceFriction.size() == 3);
    KS_CHECK_NEAR(track.baseGrip, 1.0, 1e-6);

    // full report on the scaffolded track: no errors, no warnings
    const ks::tracktool::TrackReport good =
        ks::tracktool::validateTrack(dir.string());
    KS_CHECK(good.ok);
    KS_CHECK(good.errors.empty());
    KS_CHECK(good.warnings.empty());
    KS_CHECK(good.splinePoints == 200);
    KS_CHECK(good.splineClosed);
    KS_CHECK(good.surfaceCount == 3);
    const std::string formatted = ks::tracktool::formatReport(good);
    KS_CHECK(formatted.find("result: OK") != std::string::npos);

    // scaffold twice in a row stays ok (idempotent)
    KS_CHECK(ks::tracktool::scaffoldTrack(dir.string(), error));

    // an empty directory fails validation
    const fs::path emptyDir = dir / "empty_track";
    fs::create_directories(emptyDir);
    const ks::tracktool::TrackReport empty =
        ks::tracktool::validateTrack(emptyDir.string());
    KS_CHECK(!empty.ok);
    KS_CHECK(!empty.errors.empty());

    fs::remove_all(dir, ec);
    return KS_TEST_RESULT("track_tool_test");
}
