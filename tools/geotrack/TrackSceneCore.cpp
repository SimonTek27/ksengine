// Geotrack fase 1 — orchestrazione e scrittura della cartella pista.
#include "geotrack/TrackSceneCore.h"

#include "engine/AI/AiFileReader.h"
#include "engine/AI/AiFileWriter.h"
#include "engine/FileFormat/Json.h"
#include "engine/terrain/TerrainMesh.h"
#include "geotrack/Centerline.h"
#include "geotrack/GeoProjection.h"
#include "geotrack/Materials.h"
#include "geotrack/PitLane.h"
#include "geotrack/Scenery.h"
#include "geotrack/Strips.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ks::geo {
namespace {

namespace fs = std::filesystem;
using ks::engine::terrain::TerrainMeshData;
using ks::engine::terrain::writeTerrainNMSH;

Json jsonInt(long long v) { return Json(v); }
Json jsonNum(double v) { return Json(v); }
Json jsonStr(const std::string& v) { return Json(v); }

std::string shortNameOf(const std::string& name) {
    std::string out;
    for (const char c : name)
        if (c != ' ' && c != '\t') out.push_back(c);
    return out;
}

bool writeLayer(const fs::path& bakedDir, const std::string& name, const TerrainMeshData& mesh,
                std::string* error) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        if (error) *error = "empty layer: " + name;
        return false;
    }
    const fs::path path = bakedDir / (name + ".nmsh");
    if (!writeTerrainNMSH(path.string(), mesh)) {
        if (error) *error = "cannot write " + path.string();
        return false;
    }
    return true;
}

/// Aggiunge `src` in coda a `dst` rimappando gli indici: due strisce (una per
/// lato) diventano un solo layer, quindi una sola riga di manifest.
void mergeInto(TerrainMeshData& dst, const TerrainMeshData& src) {
    const std::uint32_t base = static_cast<std::uint32_t>(dst.vertices.size());
    dst.vertices.insert(dst.vertices.end(), src.vertices.begin(), src.vertices.end());
    for (const std::uint32_t i : src.indices) dst.indices.push_back(base + i);
}

bool writeText(const fs::path& path, const std::string& text, std::string* error) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (error) *error = "cannot write " + path.string();
        return false;
    }
    out << text;
    if (!out.good()) {
        if (error) *error = "write failed: " + path.string();
        return false;
    }
    return true;
}

Json latLonJson(const LatLon& p) {
    Json j = Json::object();
    j.insert("lat", jsonNum(p.lat));
    j.insert("lon", jsonNum(p.lon));
    return j;
}

} // namespace

bool generateTrack(const GeotrackConfig& cfg, GeotrackResult* out, std::string* error) {
    if (error) error->clear();
    if (out) *out = GeotrackResult{};

    const auto fail = [error](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };

    if (cfg.outDir.empty()) {
        if (error) *error = "output directory is empty";
        return false;
    }
    if (cfg.widthM < 3.0 || cfg.widthM > 40.0) {
        if (error) *error = "track width out of range (3..40 m): " + cfg.trackName;
        return false;
    }
    if (cfg.stepM < 1.0) {
        if (error) *error = "station step must be >= 1 m";
        return false;
    }

    std::string readError;
    const std::vector<LatLon> lls = readCenterline(cfg.centerlinePath, &readError);
    if (lls.empty()) {
        if (error) *error = readError.empty() ? "empty centerline" : readError;
        return false;
    }

    LatLon centre{0.0, 0.0};
    for (const LatLon& p : lls) {
        centre.lat += p.lat;
        centre.lon += p.lon;
    }
    centre.lat /= static_cast<double>(lls.size());
    centre.lon /= static_cast<double>(lls.size());

    const Projection projection = Projection::fromOrigin(centre);
    std::vector<Enu> pts;
    pts.reserve(lls.size());
    for (const LatLon& p : lls) pts.push_back(projection.toEnu(p));

    const double lengthM = polylineLength(pts, cfg.closed);
    if (lengthM < 100.0) {
        if (error) *error = "track shorter than 100 m";
        return false;
    }
    const std::vector<Station> stations = buildStations(pts, cfg.stepM, cfg.closed);
    if (stations.size() < 4) {
        if (error) *error = "not enough stations for a track";
        return false;
    }

    const Square square = squareAround(pts, cfg.padM);
    const fs::path outDir(cfg.outDir);
    const fs::path bakedDir = outDir / "baked";
    const fs::path dataDir = outDir / "data";
    std::error_code ec;
    fs::create_directories(bakedDir, ec);
    fs::create_directories(dataDir, ec);
    if (!fs::is_directory(bakedDir, ec)) {
        if (error) *error = "cannot create " + bakedDir.string();
        return false;
    }

    // --- layer -----------------------------------------------------------
    std::vector<std::string> layers;
    if (!writeLayer(bakedDir, "ground", groundQuad(square, 0.0, static_cast<float>(square.side())),
                    error))
        return false;
    layers.push_back("ground");

    StripSpec trackSpec;
    trackSpec.offset = 0.0;
    trackSpec.halfWidth = 0.5 * cfg.widthM;
    trackSpec.y = 0.02;
    if (!writeLayer(bakedDir, "track", ribbon(stations, trackSpec), error)) return false;
    layers.push_back("track");

    // --- vie di fuga e kerb ----------------------------------------------
    const double halfWidth = 0.5 * cfg.widthM;
    StripSpec runoffSpec;
    runoffSpec.halfWidth = 12.0;
    runoffSpec.y = 0.01;
    runoffSpec.offset = halfWidth + runoffSpec.halfWidth;
    TerrainMeshData runoffMesh = ribbon(stations, runoffSpec);
    runoffSpec.offset = -(halfWidth + runoffSpec.halfWidth);
    mergeInto(runoffMesh, ribbon(stations, runoffSpec));
    if (!writeLayer(bakedDir, "runoff", runoffMesh, error)) return false;
    layers.push_back("runoff");

    StripSpec kerbSpec;
    kerbSpec.halfWidth = 0.5;
    kerbSpec.y = 0.03;
    kerbSpec.offset = halfWidth + kerbSpec.halfWidth;
    TerrainMeshData kerbMesh = ribbon(stations, kerbSpec);
    kerbSpec.offset = -(halfWidth + kerbSpec.halfWidth);
    mergeInto(kerbMesh, ribbon(stations, kerbSpec));
    if (!writeLayer(bakedDir, "kerbs", kerbMesh, error)) return false;
    layers.push_back("kerbs");

    // --- corsia box e griglia --------------------------------------------
    int gridCount = 0;
    if (cfg.pit.enabled) {
        if (cfg.pit.toS <= cfg.pit.fromS)
            return fail("pit lane: toS must be greater than fromS");
        if (cfg.pit.offset < 0.5 * cfg.widthM)
            return fail("pit lane: offset must clear the track half width");
        if (cfg.pit.halfWidth <= 1.0)
            return fail("pit lane: half width too small");

        const std::vector<Station> pitStations =
            rampStations(stations, cfg.pit.fromS, cfg.pit.toS, cfg.pit.offset, cfg.pit.rampMetres);
        if (pitStations.size() < 2) return fail("pit lane: window holds no station");
        StripSpec pitSpec;
        pitSpec.offset = 0.0;
        pitSpec.halfWidth = cfg.pit.halfWidth;
        pitSpec.y = 0.02;
        if (!writeLayer(bakedDir, "pitlane", ribbon(pitStations, pitSpec), error)) return false;
        layers.push_back("pitlane");

        gridCount = std::min(24, std::max(8, static_cast<int>((cfg.pit.fromS - 20.0) / 8.0)));
        if (!writeLayer(bakedDir, "griddot",
                        gridSlots(stations, 0.0, gridCount, 8.0, 0.0, 1.2, 4.0, 0.06), error))
            return false;
        layers.push_back("griddot");
    }

    std::string manifest;
    for (const std::string& name : layers) manifest += name + "\n";
    if (!writeText(bakedDir / "manifest.txt", manifest, error)) return false;

    // --- materiali dei layer e fondo --------------------------------------
    if (!cfg.groundTexturePath.empty()) {
        const fs::path texturesDir = bakedDir / "textures";
        fs::create_directories(texturesDir, ec);
        std::error_code copyError;
        fs::copy_file(cfg.groundTexturePath, texturesDir / "ground.png",
                      fs::copy_options::overwrite_existing, copyError);
        if (copyError) return fail("cannot copy ground texture: " + cfg.groundTexturePath);
        const std::vector<LayerMaterial> rows = {
            {"ground", "ground.png", "0.95", "0.0"},
            {"track", "-", "0.85", "0.0"},
            {"pitlane", "-", "0.85", "0.0"},
            {"runoff", "-", "0.95", "0.0"},
            {"kerbs", "-", "0.7", "0.0"},
            {"griddot", "-", "0.8", "0.0"},
        };
        if (!writeText(bakedDir / "materials.txt", materialsTxt(rows), error)) return false;
    }

    // --- linea AI di bootstrap --------------------------------------------
    {
        const fs::path aiDir = outDir / "ai";
        fs::create_directories(aiDir, ec);
        ks::ai::AiSpline spline;
        spline.closed = cfg.closed;
        spline.points.reserve(stations.size());
        for (const Station& s : stations) {
            ks::ai::AiSplinePoint point;
            point.position.x = static_cast<float>(s.p.x);
            point.position.y = 0.02f;
            point.position.z = static_cast<float>(s.p.z);
            point.curvature = static_cast<float>(s.curvature);
            // Velocita' di riferimento: v = sqrt(a_lat * R) con 60 m/s^2, fra
            // 25 e 80 m/s. Non e' una racing line, e' un tracciato guidabile.
            const double radius = 1.0 / std::fmax(std::fabs(s.curvature), 1e-4);
            point.speed = static_cast<float>(std::min(80.0, std::max(25.0, std::sqrt(60.0 * radius))));
            spline.points.push_back(point);
        }
        if (!ks::ai::AiFileWriter::writeSpline((aiDir / "fast_lane.ai").string(), spline))
            return fail("cannot write ai/fast_lane.ai");
    }

    // --- data/data.json ---------------------------------------------------
    const std::string trackName =
        cfg.trackName.empty() ? fs::path(cfg.centerlinePath).stem().string() : cfg.trackName;
    const int corners = countCorners(stations, 80.0, 50.0);

    Json data = Json::object();
    data.insert("name", jsonStr(trackName));
    data.insert("short_name", jsonStr(shortNameOf(trackName)));
    data.insert("length", jsonInt(std::llround(lengthM)));
    data.insert("length_m", jsonNum(lengthM));
    data.insert("width", jsonNum(cfg.widthM));
    data.insert("width_m", jsonNum(cfg.widthM));
    data.insert("run", jsonInt(1));
    data.insert("pitboxes", jsonInt(gridCount));
    data.insert("pit_pads", jsonInt(gridCount));
    data.insert("car_slots", jsonInt(gridCount));
    data.insert("garages", jsonInt(0));
    data.insert("corner_count", jsonInt(corners));

    // Commissari e piazzola safety car: schema di TracksidePosts (x/y/z piatti).
    const std::vector<MarshalPost> posts = marshalPosts(stations, halfWidth, 80.0, 50.0);
    Json postArray = Json::array();
    for (const MarshalPost& post : posts) {
        Json o = Json::object();
        o.insert("corner", jsonInt(post.corner));
        o.insert("side", jsonStr(post.side));
        o.insert("station", jsonNum(post.s));
        o.insert("mesh", jsonStr(post.mesh));
        o.insert("x", jsonNum(post.x));
        o.insert("y", jsonNum(0.0));
        o.insert("z", jsonNum(post.z));
        postArray.append(o);
    }
    data.insert("marshal_posts", postArray);

    const SafetyCarBay bay =
        safetyCarBay(stations, halfWidth, cfg.pit.enabled ? cfg.pit.toS + 60.0 : -1.0);
    Json bayJson = Json::object();
    bayJson.insert("x", jsonNum(bay.x));
    bayJson.insert("y", jsonNum(0.0));
    bayJson.insert("z", jsonNum(bay.z));
    bayJson.insert("radius_m", jsonNum(bay.radius));
    data.insert("safety_car", bayJson);
    if (!writeText(dataDir / "data.json", data.dump(true) + "\n", error)) return false;

    // --- scene.json -------------------------------------------------------
    Json scene = Json::object();
    Json bounds = Json::object();
    bounds.insert("sw", latLonJson(projection.toLatLon(Enu{square.minX, square.minZ})));
    bounds.insert("ne", latLonJson(projection.toLatLon(Enu{square.maxX, square.maxZ})));
    scene.insert("bounds", bounds);
    Json squareJson = Json::object();
    squareJson.insert("side_m", jsonNum(square.side()));
    squareJson.insert("min_x", jsonNum(square.minX));
    squareJson.insert("min_z", jsonNum(square.minZ));
    squareJson.insert("centre_x", jsonNum(square.centreX()));
    squareJson.insert("centre_z", jsonNum(square.centreZ()));
    squareJson.insert("pad_m", jsonNum(cfg.padM));
    // Con una texture del fondo l'immagine copre il quadrato una volta sola.
    squareJson.insert("uv_repeat_m", jsonNum(square.side()));
    scene.insert("square", squareJson);
    scene.insert("track", jsonStr(trackName));
    scene.insert("width_m", jsonNum(cfg.widthM));
    scene.insert("length_m", jsonNum(lengthM));
    Json layerJson = Json::array();
    for (const std::string& name : layers) layerJson.append(jsonStr(name));
    scene.insert("layers", layerJson);
    scene.insert("tile", Json::object());
    if (!writeText(outDir / "scene.json", scene.dump(true) + "\n", error)) return false;

    if (out) {
        out->lengthM = lengthM;
        out->sideM = square.side();
        out->corners = corners;
        out->marshalPosts = static_cast<int>(posts.size());
        out->lapPoints = static_cast<int>(stations.size());
        out->gridSlots = gridCount;
    }
    return true;
}

} // namespace ks::geo
