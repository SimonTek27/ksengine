/**
 * Showroom content and turntable.
 *
 * A showroom is one flat folder: showroom.json plus the two optional sounds
 * found by base name. This pins the schema (defaults, the light vector, the
 * camera), the file resolution (which extension wins, which formats this build
 * can actually play), the folder rule (a model is a bare filename, never a
 * path out of the folder) and the camera maths the game runs at startup.
 */
#include "KsTest.h"
#include "simulator/SceneAssets.h"
#include "simulator/ShowroomConfig.h"
#include "simulator/ShowroomScene.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using ks::sim::CameraController;
using ks::sim::ShowroomAudioFormat;
using ks::sim::ShowroomConfig;
using ks::sim::ShowroomScene;

namespace {

namespace fs = std::filesystem;

fs::path makeRoom(const std::string& name, const std::string& json) {
    const fs::path root = fs::temp_directory_path() / ("ksim_showroom_" + name);
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    std::ofstream(root / "showroom.json") << json;
    return root;
}

void touch(const fs::path& path) {
    std::ofstream(path, std::ios::binary) << "x";
}

const char* kMinimal =
    "{\"name\":\"Studio\",\"description\":\"White seamless backdrop.\"}";

} // namespace

int main() {
    // --- defaults: a document with two strings still opens -----------------
    {
        const fs::path room = makeRoom("minimal", kMinimal);
        std::string error;
        const ShowroomConfig cfg = ks::sim::loadShowroom(room.string(), &error);
        KS_CHECK(cfg.valid);
        KS_CHECK(error.empty());
        KS_CHECK(cfg.id == room.filename().string());
        KS_CHECK(cfg.name == "Studio");
        KS_CHECK(cfg.description == "White seamless backdrop.");
        KS_CHECK(cfg.model.empty());
        KS_CHECK_NEAR(cfg.cameraDistance, 6.5f, 1e-4f);
        KS_CHECK_NEAR(cfg.cameraFov, 50.f, 1e-4f);
        KS_CHECK(cfg.cameraRotationDegPerSec > 0.f);
        // No sounds in the folder: both tracks absent, nothing to warn about.
        KS_CHECK(!cfg.hasAmbience());
        KS_CHECK(!cfg.hasMusic());
        KS_CHECK(cfg.warnings.empty());
        fs::remove_all(room);
    }

    // --- the whole document -------------------------------------------------
    {
        const fs::path room = makeRoom("full",
            "{\"name\":\"Hall\",\"description\":\"Cold light.\",\"model\":\"hall.kn5\","
            "\"camera\":{\"distance\":9,\"height\":2.4,\"angle\":120,\"fov\":38,"
            "\"rotation\":15,\"target_height\":0.9},"
            "\"light\":{\"sun_direction\":[0,1,0],\"sun_color\":[0.5,0.6,0.7],"
            "\"sun_intensity\":2.5,\"ambient_color\":[0.1,0.2,0.3],"
            "\"ambient_intensity\":0.25},"
            "\"audio\":{\"ambience_volume\":0.3,\"music_volume\":0.9}}");
        const ShowroomConfig cfg = ks::sim::loadShowroom(room.string());
        KS_CHECK(cfg.valid);
        KS_CHECK(cfg.model == "hall.kn5");
        KS_CHECK_NEAR(cfg.cameraDistance, 9.f, 1e-4f);
        KS_CHECK_NEAR(cfg.cameraHeight, 2.4f, 1e-4f);
        KS_CHECK_NEAR(cfg.cameraAngleDeg, 120.f, 1e-4f);
        KS_CHECK_NEAR(cfg.cameraFov, 38.f, 1e-4f);
        KS_CHECK_NEAR(cfg.cameraRotationDegPerSec, 15.f, 1e-4f);
        KS_CHECK_NEAR(cfg.lookAtHeight, 0.9f, 1e-4f);
        KS_CHECK_NEAR(cfg.sunDirection[1], 1.f, 1e-4f);
        KS_CHECK_NEAR(cfg.sunColor[2], 0.7f, 1e-4f);
        KS_CHECK_NEAR(cfg.sunIntensity, 2.5f, 1e-4f);
        KS_CHECK_NEAR(cfg.ambientColor[0], 0.1f, 1e-4f);
        KS_CHECK_NEAR(cfg.ambientIntensity, 0.25f, 1e-4f);
        fs::remove_all(room);
    }

    // --- the two sounds: found by base name, first known extension wins ----
    {
        const fs::path room = makeRoom("audio", kMinimal);
        touch(room / "ambience.wav");
        touch(room / "music_00.wav");
        touch(room / "unrelated.wav"); // never picked up
        const ShowroomConfig cfg = ks::sim::loadShowroom(room.string());
        KS_CHECK(cfg.valid);
        KS_CHECK(cfg.ambience.format == ShowroomAudioFormat::Wav);
        KS_CHECK(cfg.ambience.path.find("ambience.wav") != std::string::npos);
        KS_CHECK(cfg.ambience.playable());
        KS_CHECK(cfg.music.format == ShowroomAudioFormat::Wav);
        KS_CHECK(cfg.music.path.find("music_00.wav") != std::string::npos);
        KS_CHECK(cfg.music.playable());
        ; // volumes: defaults when the audio block is absent
        KS_CHECK_NEAR(cfg.ambience.volume, 0.6f, 1e-4f);
        KS_CHECK_NEAR(cfg.music.volume, 0.5f, 1e-4f);
        fs::remove_all(room);
    }
    {
        // A format the build cannot decode still loads, and says why: the
        // showroom opens silent instead of refusing to start.
        const fs::path room = makeRoom("mp3", kMinimal);
        touch(room / "music_00.mp3");
        const ShowroomConfig cfg = ks::sim::loadShowroom(room.string());
        KS_CHECK(cfg.valid);
        KS_CHECK(cfg.music.format == ShowroomAudioFormat::Mp3);
        KS_CHECK(!cfg.music.playable());
        KS_CHECK(cfg.music.silenceReason() != nullptr);
        KS_CHECK(cfg.warnings.size() == 1);
        fs::remove_all(room);
    }
    {
        // wav beats ogg beats mp3 beats bank when a folder offers several.
        const fs::path room = makeRoom("order", kMinimal);
        touch(room / "ambience.bank");
        touch(room / "ambience.mp3");
        touch(room / "ambience.wav");
        touch(room / "ambience.wav");
        const ShowroomConfig cfg = ks::sim::loadShowroom(room.string());
        KS_CHECK(cfg.ambience.format == ShowroomAudioFormat::Wav);
        fs::remove_all(room);
    }

    {
        // ogg is a supported showroom format, but this audio layer has no
        // decoder wired for it yet: the track is reported, not played.
        const fs::path room = makeRoom("ogg", kMinimal);
        touch(room / "ambience.ogg");
        const ShowroomConfig cfg = ks::sim::loadShowroom(room.string());
        KS_CHECK(cfg.valid);
        KS_CHECK(cfg.ambience.format == ShowroomAudioFormat::Ogg);
        KS_CHECK(!cfg.ambience.playable());
        KS_CHECK(cfg.warnings.size() == 1);
        fs::remove_all(room);
    }

    // --- the folder rule: a showroom never points outside itself -----------
    {
        const fs::path room = makeRoom("escape", R"({"model":"../other/hall.kn5"})");
        const ShowroomConfig cfg = ks::sim::loadShowroom(room.string());
        KS_CHECK(cfg.valid);
        KS_CHECK(cfg.model.empty());       // rejected
        KS_CHECK(cfg.warnings.size() == 1); // and explained
        fs::remove_all(room);
    }

    // --- a folder that is not a showroom ------------------------------------
    {
        const fs::path room = makeRoom("broken", "{ not json ");
        std::string error;
        const ShowroomConfig cfg = ks::sim::loadShowroom(room.string(), &error);
        KS_CHECK(!cfg.valid);
        KS_CHECK(!error.empty());
        fs::remove_all(room);
    }

    // --- scanning ----------------------------------------------------------
    {
        const fs::path root = fs::temp_directory_path() / "ksim_showroom_scan";
        std::error_code ec;
        fs::remove_all(root, ec);
        fs::create_directories(root / "zeta", ec);
        fs::create_directories(root / "alpha", ec);
        fs::create_directories(root / "not_a_showroom", ec);
        std::ofstream(root / "zeta" / "showroom.json") << kMinimal;
        std::ofstream(root / "alpha" / "showroom.json") << kMinimal;
        std::ofstream(root / "loose.json") << kMinimal; // a file, not a folder

        const std::vector<ks::sim::ShowroomEntry> rooms = ks::sim::scanShowrooms(root.string());
        KS_CHECK(rooms.size() == 2);
        if (rooms.size() == 2) {
            KS_CHECK(rooms[0].id == "alpha"); // sorted, so startup is stable
            KS_CHECK(rooms[1].id == "zeta");
            KS_CHECK(rooms[0].label == "alpha");
        }
        KS_CHECK(ks::sim::scanShowrooms((root / "missing").string()).empty());
        fs::remove_all(root);
    }

    // --- the turntable -----------------------------------------------------
    {
        const fs::path room = makeRoom("orbit",
            "{\"camera\":{\"distance\":10,\"height\":3,\"angle\":0,\"fov\":45,"
            "\"rotation\":90,\"target_height\":1}}");
        ShowroomScene scene;
        KS_CHECK(scene.open(room.string()));
        KS_CHECK(scene.active());

        CameraController camera;
        scene.apply(0.f, camera); // no time passed yet: the opening angle holds
        KS_CHECK_NEAR(scene.angleDeg(), 0.f, 1e-4f);
        // Angle 0 = in front of the car: straight down +Z, at eye height.
        KS_CHECK_NEAR(camera.position().x, 0.f, 1e-4f);
        KS_CHECK_NEAR(camera.position().y, 3.f, 1e-4f);
        KS_CHECK_NEAR(camera.position().z, 10.f, 1e-4f);
        // Looking at the car's middle (0, 1, 0), not at its roof or its feet.
        const auto forward = camera.forward();
        KS_CHECK_NEAR(forward.y, (1.f - 3.f) / std::sqrt(4.f + 100.f), 1e-4f);
        KS_CHECK(forward.z < 0.f);
        KS_CHECK_NEAR(camera.mode(), CameraController::Mode::Free, 0.f);
        KS_CHECK_NEAR(camera.aspectRatio(), 16.f / 9.f, 1e-4f); // untouched

        scene.apply(0.5f, camera); // 90 deg/s for half a second
        KS_CHECK_NEAR(scene.angleDeg(), 45.f, 1e-3f);
        KS_CHECK(camera.position().x > 0.f); // swung to the car's right
        scene.apply(4.f, camera);            // wraps instead of growing forever
        KS_CHECK(scene.angleDeg() >= 0.f && scene.angleDeg() < 360.f);

        scene.close();
        KS_CHECK(!scene.active());
        fs::remove_all(room);
    }
    {
        // rotation 0 keeps the camera still: a showroom can be a photograph.
        const fs::path room = makeRoom("still",
            "{\"camera\":{\"rotation\":0,\"angle\":15}}");
        ShowroomScene scene;
        KS_CHECK(scene.open(room.string()));
        CameraController camera;
        scene.apply(2.f, camera);
        KS_CHECK_NEAR(scene.angleDeg(), 15.f, 1e-4f);
        fs::remove_all(room);
    }

    // --- the shipped showroom parses ---------------------------------------
    {
        // __FILE__ keeps this independent of the working directory CTest picks:
        // the binary lives in the build tree, the content does not.
        const fs::path source = fs::path(__FILE__).parent_path().parent_path().parent_path();
        const ShowroomConfig cfg =
            ks::sim::loadShowroom((source / "content/showrooms/industrial").string());
        KS_CHECK(cfg.valid);
        KS_CHECK(cfg.name == "Industrial");
        KS_CHECK(cfg.cameraDistance > 0.f);
    }

    // ---------------------------------------------------------------------
    // The venue built in code: an unbaked showroom still has to show a room,
    // and every face has to look inward or the camera ends up inside a wall.
    // ---------------------------------------------------------------------
    {
        const ks::sim::ShowroomVenue venue = ks::sim::makeShowroomVenue(12.f, 7.f);
        KS_CHECK(venue.vertices.size() == 24); // six quads: floor, ceiling, four walls
        KS_CHECK(venue.indices.size() == 36);
        for (const auto& v : venue.vertices) {
            // Vector from the vertex back to the middle of the room.
            const float toCentreX = -v.px;
            const float toCentreY = 3.5f - v.py;
            const float toCentreZ = -v.pz;
            const float dot = v.nx * toCentreX + v.ny * toCentreY + v.nz * toCentreZ;
            KS_CHECK(dot > 0.f);
        }
    }

    // ---------------------------------------------------------------------
    // A showroom folder may name its file data.json: the name is our
    // invention, and a folder with the geometry but the other name used to
    // disappear from the scan entirely.
    // ---------------------------------------------------------------------
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path dir = fs::temp_directory_path(ec) / "ksim_showroom_alias";
        fs::remove_all(dir, ec);
        fs::create_directories(dir, ec);
        const std::string json = R"({"name": "Alias room", "camera": {"distance": 5.0}})";
        std::FILE* f = std::fopen((dir / "data.json").string().c_str(), "wb");
        KS_CHECK(f != nullptr);
        if (f != nullptr) {
            std::fwrite(json.data(), 1, json.size(), f);
            std::fclose(f);
        }
        std::string error;
        const ks::sim::ShowroomConfig cfg = ks::sim::loadShowroom(dir.string(), &error);
        KS_CHECK(cfg.valid);
        KS_CHECK(cfg.name == "Alias room");
        KS_CHECK(!ks::sim::showroomFile(dir.string()).empty());
        ks::sim::ShowroomConfig startup;
        fs::remove_all(dir, ec);
    }

    return KS_TEST_RESULT("showroom_test");
}
