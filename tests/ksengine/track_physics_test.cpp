// TrackPhysics — porta una pista JSON attraverso loadTrack e verifica la
// mappatura dei campi, piu' i due casi di errore (JSON rotto, file assente).
//
// Serve a coprire il port Qt-free: il file compilava e linkava, ma nessun test
// lo eseguiva, quindi un errore nel parsing sarebbe passato inosservato.

#include "KsTest.h"

#include "engine/physics/TrackPhysics.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
namespace ph = ks::physics;

namespace {

fs::path writeSample(const char* name, const std::string& body) {
    const fs::path path = fs::temp_directory_path() / name;
    std::ofstream out(path, std::ios::binary);
    out << body;
    return path;
}

} // namespace

int main() {
    const std::string json = R"({
  "name": "Prova",
  "config": "GP",
  "length": 4321.5,
  "startFinish": { "x": 1.0, "y": 2.0, "z": 3.0, "dirX": 0.0, "dirY": 0.0, "dirZ": 1.0 },
  "sectors": [ { "name": "S1", "startDistance": 0.0, "endDistance": 1000.0 } ],
  "corners": [ { "number": 1, "name": "T1", "position": 120.0, "radius": 45.0,
                 "entrySpeed": 60.0, "apexSpeed": 50.0, "exitSpeed": 70.0,
                 "banking": 2.0, "type": 0 } ],
  "racingLine": [ { "x": 0.0, "y": 0.0, "z": 0.0, "distance": 0.0 },
                  { "x": 1.0, "y": 0.0, "z": 10.0, "distance": 10.0 } ],
  "elevation": [ { "distance": 0.0, "height": 12.5 } ],
  "surfaceSections": [ { "startDistance": 0.0, "endDistance": 500.0,
                         "gripLevel": 1.0, "bumpiness": 0.1, "surfaceType": 0 } ],
  "pitEntryDistance": 3000.0,
  "pitExitDistance": 3100.0,
  "pitLaneLength": 100.0,
  "pitSpeedLimit": 60.0,
  "drsZones": [ { "detectionPoint": 2000.0, "activationPoint": 2100.0, "endPoint": 2500.0 } ]
})";

    const fs::path path = writeSample("ks_track_physics_test.json", json);

    ph::TrackSimulator sim;
    KS_CHECK(sim.loadTrack(path.string()));

    const ph::TrackLayout layout = sim.currentTrack();
    KS_CHECK(layout.name == "Prova");
    KS_CHECK(layout.config == "GP");
    KS_CHECK_NEAR(layout.length, 4321.5, 1e-9);

    KS_CHECK(layout.sectors.size() == 1);
    KS_CHECK(layout.corners.size() == 1);
    KS_CHECK(layout.racingLine.size() == 2);
    KS_CHECK(layout.racingLineDistances.size() == 2);
    KS_CHECK(layout.elevationProfile.size() == 1);
    KS_CHECK(layout.surfaceSections.size() == 1);
    KS_CHECK(layout.drsZones.size() == 1);

    if (layout.corners.size() == 1) {
        KS_CHECK(layout.corners[0].name == "T1");
        KS_CHECK_NEAR(layout.corners[0].radius, 45.0, 1e-9);
        KS_CHECK_NEAR(layout.corners[0].banking, 2.0, 1e-9);
    }
    if (layout.elevationProfile.size() == 1) {
        KS_CHECK_NEAR(layout.elevationProfile[0].second, 12.5, 1e-9);
    }
    if (layout.drsZones.size() == 1) {
        // activationPoint finisce in startDistance, endPoint in endDistance.
        KS_CHECK_NEAR(layout.drsZones[0].startDistance, 2100.0, 1e-9);
        KS_CHECK_NEAR(layout.drsZones[0].endDistance, 2500.0, 1e-9);
        KS_CHECK_NEAR(layout.drsZones[0].detectionPoint, 2000.0, 1e-9);
    }
    KS_CHECK_NEAR(layout.startFinishPosition.x, 1.0, 1e-6);
    KS_CHECK_NEAR(layout.startFinishDirection.z, 1.0, 1e-6);
    KS_CHECK_NEAR(layout.pitSpeedLimit, 60.0, 1e-9);
    KS_CHECK_NEAR(layout.pitLaneLength, 100.0, 1e-9);

    // JSON invalido: loadTrack deve rifiutare, non lasciare un layout a meta'.
    const fs::path bad = writeSample("ks_track_physics_bad.json", "{ non json");
    ph::TrackSimulator badSim;
    KS_CHECK(!badSim.loadTrack(bad.string()));

    // File inesistente.
    KS_CHECK(!badSim.loadTrack((fs::temp_directory_path() / "ks_nessuna_pista.json").string()));

    fs::remove(path);
    fs::remove(bad);
    return KS_TEST_RESULT("track_physics_test");
}
