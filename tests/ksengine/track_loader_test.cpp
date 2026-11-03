// Regression test for TrackLoader's KN5 header validation.
//
// The previous implementation expected a fabricated 13-u32 offset-table
// header with magic 0x346E6B73 ("skn4"), which rejected every real Assetto
// Corsa track file (real files start with the ASCII magic "sc6969"). These
// checks pin the real header layout: magic, version 4..6, the v6 extra
// int32, and a sane texture count — plus rejection of the old fake header.

#include "TrackLoader.h"

// TrackLoader.h only forward-declares AiSpline; dereferencing it needs the
// full definition.
#include "engine/AI/AiFileReader.h"

#include "KsTest.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void putI32(std::vector<char>& out, std::int32_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
}

std::string writeFile(const fs::path& dir, const std::string& name,
                      const std::vector<char>& bytes) {
    const fs::path path = dir / name;
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return path.string();
}

// Minimal real-format header: magic, version, (v6 extra), textureCount.
std::vector<char> kn5Header(int version, std::int32_t texture_count = 1) {
    std::vector<char> out;
    const char magic[] = "sc6969";
    out.insert(out.end(), magic, magic + 6);
    putI32(out, version);
    if (version > 5) putI32(out, 0);
    putI32(out, texture_count);
    return out;
}

bool validates(ks::sim::TrackLoader& loader, const std::string& path) {
    return loader.loadKn5File(path).kn5HeaderValidated;
}

} // namespace

int main() {
    ks::sim::TrackLoader loader;
    const fs::path dir = fs::temp_directory_path() / "ks_qtfree_trackloader_test";
    fs::remove_all(dir);
    fs::create_directories(dir);

    // Every real version must be accepted (v6 is the majority of content).
    KS_CHECK(validates(loader, writeFile(dir, "v4.kn5", kn5Header(4))));
    KS_CHECK(validates(loader, writeFile(dir, "v5.kn5", kn5Header(5))));
    KS_CHECK(validates(loader, writeFile(dir, "v6.kn5", kn5Header(6))));

    // The old fabricated header (magic "skn4", 13-u32 table) must be
    // rejected — it never matched a real file.
    std::vector<char> old_fake(13 * 4, 0);
    old_fake[0] = 's';
    old_fake[1] = 'k';
    old_fake[2] = 'n';
    old_fake[3] = '4';
    KS_CHECK(!validates(loader, writeFile(dir, "old_fake.kn5", old_fake)));
    KS_CHECK(loader.lastError().find("magic") != std::string::npos);

    // Unsupported versions and implausible counts.
    KS_CHECK(!validates(loader, writeFile(dir, "v3.kn5", kn5Header(3))));
    KS_CHECK(!validates(loader, writeFile(dir, "v7.kn5", kn5Header(7))));
    KS_CHECK(!validates(loader,
                        writeFile(dir, "neg_textures.kn5",
                                  kn5Header(6, -1))));
    KS_CHECK(!validates(loader,
                        writeFile(dir, "huge_textures.kn5",
                                  kn5Header(6, 10000001))));

    // Truncated file (magic only, shorter than the minimum header).
    const std::vector<char> truncated{'s', 'c', '6', '9', '6', '9'};
    KS_CHECK(!validates(loader, writeFile(dir, "truncated.kn5", truncated)));

    // Missing file.
    KS_CHECK(!validates(loader, (dir / "does_not_exist.kn5").string()));

    // A real track file from the AC install must pass (skipped elsewhere).
    const fs::path tracks_root =
        "F:/SteamLibrary/steamapps/common/assettocorsa/content/tracks";
    if (fs::is_directory(tracks_root)) {
        std::error_code ec;
        bool found = false;
        for (fs::recursive_directory_iterator it(tracks_root, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            if (it->path().extension() != ".kn5") continue;
            KS_CHECK(validates(loader, it->path().string()));
            found = true;
            break;
        }
        KS_CHECK(found);
    }

    // AI spline fallback: a track folder that ships no .ai at all still gets a
    // racing line from data/lanes.csv (findAiSpline -> readLanesCsv).
    {
        const fs::path trackDir = dir / "track_with_lanes_csv";
        fs::create_directories(trackDir / "data");
        writeFile(trackDir, "t.kn5", kn5Header(5));
        std::ofstream lanes(trackDir / "data" / "lanes.csv");
        lanes << "source_file,row_index,x,y,z,dist_m,speed,radius_m,edge_left,edge_right\n"
                 "fast_lane.ai,0,0,0,0,0.000,30.0,1000.0,7.5,6.5\n"
                 "fast_lane.ai,1,10,0,0,10.000,40.0,50.0,7.5,6.5\n"
                 "fast_lane.ai,2,20,0,0,20.000,50.0,1000.0,7.5,6.5\n"
                 "pit_lane.ai,0,100,0,0,0.000,20.0,500.0,3.0,3.0\n";
        lanes.close();

        auto t = loader.loadTrackFolder(trackDir.string());
        KS_CHECK(t.kn5HeaderValidated);
        KS_CHECK(t.aiSplineLoaded);
        KS_CHECK(t.aiSplinePath == (trackDir / "data" / "lanes.csv").string());
        // Guarded so a regression fails instead of dereferencing null.
        KS_CHECK(t.aiSpline && t.aiSpline->size() == 3);
        if (t.aiSpline && t.aiSpline->size() == 3) {
            KS_CHECK(std::fabs(t.aiSpline->points[2].position.x - 20.0f) < 1e-6f);
            KS_CHECK(std::fabs(t.aiSpline->points[1].curvature - 0.02f) < 1e-6f);
            KS_CHECK(std::fabs(t.trackLength - 20.0f) < 1e-5f);
        }
    }

    // surfaces.ini parsing (roadmap 2.2): friction map + ROAD base grip.
    {
        const fs::path trackDir = dir / "track_with_surfaces";
        fs::create_directories(trackDir / "data");
        writeFile(trackDir, "s.kn5", kn5Header(5));
        std::ofstream surf(trackDir / "data" / "surfaces.ini");
        surf << "; comment line\n"
                "[SURFACE_0]\nKEY=ROAD\nFRICTION=1.05\n"
                "[SURFACE_1]\nKEY=GRASS\nFRICTION=0.55\n"
                "[SURFACE_2]\nKEY=KERB\nGRIP=0.8\n";
        surf.close();

        auto t = loader.loadTrackFolder(trackDir.string());
        KS_CHECK(t.kn5HeaderValidated);
        KS_CHECK(t.surfacesLoaded);
        KS_CHECK(t.surfaceFriction.size() == 3);
        KS_CHECK(t.surfaceFriction.count("ROAD") == 1);
        KS_CHECK(t.surfaceFriction.count("GRASS") == 1);
        KS_CHECK(std::fabs(t.surfaceFriction["GRASS"] - 0.55f) < 1e-5f);
        KS_CHECK(std::fabs(t.surfaceFriction["KERB"] - 0.8f) < 1e-5f);
        KS_CHECK(std::fabs(t.baseGrip - 1.05f) < 1e-5f);
    }

    // Optional on-disk check: the real Mugello folder, whose track dir ships
    // no .ai at all — only data/lanes.csv. Skipped without that install.
    const fs::path mugello =
        "E:/ksimulator/content/tracks/mugello";
    if (fs::is_directory(mugello) &&
        !fs::exists(mugello / "ai" / "fast_lane.ai")) {
        auto t = loader.loadTrackFolder(mugello.string());
        KS_CHECK(t.kn5HeaderValidated);
        KS_CHECK(t.aiSplineLoaded);
        KS_CHECK(t.aiSpline && t.aiSpline->size() == 3374);
        if (t.aiSpline && t.aiSpline->size() == 3374) {
            KS_CHECK(t.aiSpline->closed);
            KS_CHECK(std::fabs(t.aiSpline->totalDistance - 5195.87f) < 1.0f);
            KS_CHECK(std::fabs(t.trackLength - 5195.87f) < 1.0f);
        }
    }

    fs::remove_all(dir);
    return KS_TEST_RESULT("track_loader_test");
}
