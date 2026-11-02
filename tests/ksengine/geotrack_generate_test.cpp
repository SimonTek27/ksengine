// Geotrack fase 1 — cartella pista scritta da una centerline (Task 4).
#include "KsTest.h"

#include "adapters/assetto_corsa/TrackDataJsonLoader.h"
#include "geotrack/Centerline.h"
#include "geotrack/GeoProjection.h"
#include "engine/AI/AiFileReader.h"
#include "geotrack/TrackSceneCore.h"
#include "simulator/TracksidePosts.h"

#include <cmath>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

static std::string slurp(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

int main() {
    const std::string dir = "geotrack_gen_tmp";
    fs::remove_all(dir);
    fs::create_directories(dir);
    {
        std::ofstream(dir + "/loop.csv")
            << "50.3355,6.9474\n50.3355,6.961470\n50.33999,6.961470\n50.33999,6.9474\n";
    }

    ks::geo::GeotrackConfig cfg;
    cfg.centerlinePath = dir + "/loop.csv";
    cfg.outDir = dir + "/My Track è";
    cfg.trackName = "My Track è";
    ks::geo::GeotrackResult res;
    std::string err;
    KS_CHECK(ks::geo::generateTrack(cfg, &res, &err));
    KS_CHECK(err.empty());

    KS_CHECK_NEAR(res.lengthM, 3000.0, 20.0);
    // Quadrato: estensione maggiore (~1000 m) + 2 * 250 m di margine.
    KS_CHECK_NEAR(res.sideM, 1500.0, 40.0);
    KS_CHECK(fs::is_regular_file(cfg.outDir + "/baked/manifest.txt"));
    KS_CHECK(fs::is_regular_file(cfg.outDir + "/baked/ground.nmsh"));
    KS_CHECK(fs::is_regular_file(cfg.outDir + "/baked/track.nmsh"));
    KS_CHECK(fs::is_regular_file(cfg.outDir + "/scene.json"));
    KS_CHECK(fs::is_regular_file(cfg.outDir + "/data/data.json"));

    // Il parser naive del simulatore deve leggere nome, lunghezza e larghezza.
    ks::ac::TrackDataJsonLoader loader;
    KS_CHECK(loader.loadForTrackDir(cfg.outDir));
    KS_CHECK(loader.meta().name == "My Track è");
    KS_CHECK_NEAR(loader.meta().lengthM, res.lengthM, 1.0);
    KS_CHECK_NEAR(loader.meta().widthM, 12.0, 1e-3);

    // Manifest: una riga per mesh, senza estensione, e ogni .nmsh esiste.
    // Scoped on purpose: a stream still open here makes the closing
    // fs::remove_all() fail on Windows (POSIX unlinks open files, Windows does
    // not), and the uncaught filesystem_error aborts the process instead of
    // reporting a test result.
    {
        std::ifstream man(cfg.outDir + "/baked/manifest.txt");
        std::string line;
        int lines = 0;
        while (std::getline(man, line)) {
            if (line.empty()) continue;
            ++lines;
            KS_CHECK(line.find('.') == std::string::npos);
            KS_CHECK(fs::is_regular_file(cfg.outDir + "/baked/" + line + ".nmsh"));
        }
        KS_CHECK(lines >= 2);
    }

    // scene.json dichiara bounds, lato del quadrato e i layer.
    const std::string scene = slurp(cfg.outDir + "/scene.json");
    KS_CHECK(scene.find("\"layers\"") != std::string::npos);
    KS_CHECK(scene.find("\"side_m\"") != std::string::npos);

    // --- corsia box e griglia --------------------------------------------
    ks::geo::GeotrackConfig pitCfg = cfg;
    pitCfg.outDir = dir + "/pit";
    pitCfg.trackName = "Pit Track";
    pitCfg.pit.enabled = true;
    pitCfg.pit.fromS = 200.0;
    pitCfg.pit.toS = 600.0;
    pitCfg.pit.offset = 10.0;
    pitCfg.pit.halfWidth = 4.0;
    pitCfg.pit.rampMetres = 60.0;
    ks::geo::GeotrackResult pitRes;
    KS_CHECK(ks::geo::generateTrack(pitCfg, &pitRes, &err));
    KS_CHECK(err.empty());
    KS_CHECK(fs::is_regular_file(pitCfg.outDir + "/baked/pitlane.nmsh"));
    KS_CHECK(fs::is_regular_file(pitCfg.outDir + "/baked/griddot.nmsh"));
    const std::string pitMan = slurp(pitCfg.outDir + "/baked/manifest.txt");
    KS_CHECK(pitMan.find("pitlane") != std::string::npos);
    KS_CHECK(pitMan.find("griddot") != std::string::npos);
    KS_CHECK(pitRes.gridSlots >= 10);
    KS_CHECK(slurp(pitCfg.outDir + "/data/data.json").find("\"pitboxes\"") != std::string::npos);

    // Corsia mal configurata: errore, non una pista rotta.
    ks::geo::GeotrackConfig badPit = pitCfg;
    badPit.outDir = dir + "/badpit";
    badPit.pit.toS = 100.0;
    KS_CHECK(!ks::geo::generateTrack(badPit, &pitRes, &err));

    // --- vie di fuga, kerb, commissari, safety car -----------------------
    const std::string baseManifest = slurp(cfg.outDir + "/baked/manifest.txt");
    KS_CHECK(baseManifest.find("runoff") != std::string::npos);
    KS_CHECK(baseManifest.find("kerbs") != std::string::npos);
    KS_CHECK(res.corners >= 3);
    KS_CHECK(res.marshalPosts >= 12);

    const std::string dj = slurp(cfg.outDir + "/data/data.json");
    KS_CHECK(dj.find("\"marshal_posts\"") != std::string::npos);
    KS_CHECK(dj.find("\"safety_car\"") != std::string::npos);
    KS_CHECK(dj.find("\"corner_count\"") != std::string::npos);
    KS_CHECK(dj.find("marshall_A_HI_010") != std::string::npos);

    // Il parser vero del simulatore deve ritrovare gli stessi posti.
    ks::sim::TracksidePosts posts;
    KS_CHECK(posts.loadFromFile(cfg.outDir + "/data/data.json"));
    KS_CHECK(static_cast<int>(posts.postCount()) == res.marshalPosts);
    KS_CHECK(posts.posts().front().mesh == std::string(ks::sim::kMarshalMeshName));
    KS_CHECK(posts.hasSafetyCarPad());

    // Nessun posto sull'asfalto: distanza dalla centerline > mezza larghezza.
    {
        std::string cerr;
        const auto lls = ks::geo::readCenterline(cfg.centerlinePath, &cerr);
        KS_CHECK(!lls.empty());
        ks::geo::LatLon c{0.0, 0.0};
        for (const auto& p : lls) {
            c.lat += p.lat;
            c.lon += p.lon;
        }
        c.lat /= static_cast<double>(lls.size());
        c.lon /= static_cast<double>(lls.size());
        const auto pr = ks::geo::Projection::fromOrigin(c);
        std::vector<ks::geo::Enu> pts;
        for (const auto& p : lls) pts.push_back(pr.toEnu(p));
        const auto stations = ks::geo::buildStations(pts, cfg.stepM, cfg.closed);
        double nearest = 1e9;
        for (const auto& post : posts.posts()) {
            for (const auto& s : stations)
                nearest = std::fmin(nearest, std::hypot(post.pose.x - s.p.x, post.pose.z - s.p.z));
        }
        // Fuori dall'asfalto. Nessun epsilon extra: all'angolo retto un posto
        // interno puo' finire a 8 m dalla gamba perpendicolare, e va bene.
        KS_CHECK(nearest > 0.5 * cfg.widthM);
    }

    // --- linea AI di bootstrap -------------------------------------------
    KS_CHECK(fs::is_regular_file(cfg.outDir + "/ai/fast_lane.ai"));
    const ks::ai::AiSpline ai = ks::ai::AiFileReader::readSpline(cfg.outDir + "/ai/fast_lane.ai");
    KS_CHECK(ai.isValid());
    KS_CHECK(static_cast<int>(ai.size()) == res.lapPoints);
    KS_CHECK(ai.points.front().speed >= 20.0f && ai.points.front().speed <= 90.0f);

    // --- materiali e fondo texturizzato ----------------------------------
    // Senza texture nessun sidecar: i layer restano ai default del renderer.
    KS_CHECK(!fs::is_regular_file(cfg.outDir + "/baked/materials.txt"));
    {
        std::ofstream(cfg.outDir + "/ground_src.png", std::ios::binary) << "\x89PNG\r\n";
    }
    ks::geo::GeotrackConfig texCfg = cfg;
    texCfg.outDir = dir + "/tex";
    texCfg.groundTexturePath = cfg.outDir + "/ground_src.png";
    KS_CHECK(ks::geo::generateTrack(texCfg, &pitRes, &err));
    KS_CHECK(fs::is_regular_file(texCfg.outDir + "/baked/textures/ground.png"));
    const std::string mats = slurp(texCfg.outDir + "/baked/materials.txt");
    KS_CHECK(mats.find("ground\tground.png") == 0);
    KS_CHECK(mats.find("track\t") != std::string::npos);
    KS_CHECK(slurp(texCfg.outDir + "/scene.json").find("\"uv_repeat_m\"") != std::string::npos);

    // Errori leggibili, nessuna cartella pista a meta'.
    ks::geo::GeotrackConfig bad = cfg;
    bad.outDir = dir + "/bad";
    bad.centerlinePath = dir + "/missing.csv";
    KS_CHECK(!ks::geo::generateTrack(bad, &res, &err));
    KS_CHECK(!err.empty());

    bad = cfg;
    bad.outDir = dir + "/bad";
    bad.widthM = 80.0;
    KS_CHECK(!ks::geo::generateTrack(bad, &res, &err));

    std::error_code cleanupEc;
    fs::remove_all(dir, cleanupEc);
    return KS_TEST_RESULT("geotrack_generate_test");
}
