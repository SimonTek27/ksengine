// Geotrack fase 1 — CLI.
//
//   ks_geotrack <centerline> <outDir> [opzioni]
//
// Esce 0 con una cartella pista completa, 1 su errore di generazione, 2 su uso
// sbagliato. Nessuna rete: la cache di vettoriale e tile la prepara
// fetch_cache.py.

#include "geotrack/TrackSceneCore.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

void usage() {
    std::fprintf(
        stderr,
        "usage: ks_geotrack <centerline> <outDir> [options]\n"
        "  --name N                 track name (default: centerline file stem)\n"
        "  --width M                track width in metres (default 12)\n"
        "  --pad M                  terrain square margin per side (default 250)\n"
        "  --step M                 station spacing (default 5)\n"
        "  --closed | --open        closed lap (default) or point-to-point\n"
        "  --pit from,to,offset,halfWidth[,ramp]\n"
        "                           pit lane window in lap metres\n"
        "  --ground-texture FILE    PNG covering the terrain square\n"
        "centerline: .csv (lat,lon), .geojson (LineString) or .gpx\n");
}

/// Primo numero della lista, avanzando oltre le virgole (strtod si ferma sul
/// separatore e non lo consuma).
double nextNumber(const std::string& list, std::size_t& cursor) {
    while (cursor < list.size() &&
           (list[cursor] == ',' || list[cursor] == ' ' || list[cursor] == ';'))
        ++cursor;
    if (cursor >= list.size()) return 0.0;
    const char* begin = list.c_str() + cursor;
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin) {
        ++cursor;
        return 0.0;
    }
    cursor += static_cast<std::size_t>(end - begin);
    return value;
}

} // namespace

int main(int argc, char** argv) {
    ks::geo::GeotrackConfig cfg;
    std::vector<std::string> positional;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto value = [&](const char* what) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "ks_geotrack: %s needs a value\n", what);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        } else if (arg == "--name") {
            cfg.trackName = value("--name");
        } else if (arg == "--width") {
            cfg.widthM = std::atof(value("--width").c_str());
        } else if (arg == "--pad") {
            cfg.padM = std::atof(value("--pad").c_str());
        } else if (arg == "--step") {
            cfg.stepM = std::atof(value("--step").c_str());
        } else if (arg == "--closed") {
            cfg.closed = true;
        } else if (arg == "--open") {
            cfg.closed = false;
        } else if (arg == "--ground-texture") {
            cfg.groundTexturePath = value("--ground-texture");
        } else if (arg == "--pit") {
            const std::string list = value("--pit");
            std::size_t cursor = 0;
            cfg.pit.fromS = nextNumber(list, cursor);
            cfg.pit.toS = nextNumber(list, cursor);
            cfg.pit.offset = nextNumber(list, cursor);
            cfg.pit.halfWidth = nextNumber(list, cursor);
            cfg.pit.rampMetres = nextNumber(list, cursor);
            if (cfg.pit.rampMetres <= 0.0) cfg.pit.rampMetres = 60.0;
            cfg.pit.enabled = true;
        } else if (arg.rfind("--", 0) == 0) {
            std::fprintf(stderr, "ks_geotrack: unknown option %s\n", arg.c_str());
            usage();
            return 2;
        } else {
            positional.push_back(arg);
        }
    }

    if (positional.size() < 2) {
        usage();
        return 2;
    }
    cfg.centerlinePath = positional[0];
    cfg.outDir = positional[1];

    ks::geo::GeotrackResult res;
    std::string error;
    if (!ks::geo::generateTrack(cfg, &res, &error)) {
        std::fprintf(stderr, "ks_geotrack: %s\n", error.c_str());
        return 1;
    }

    std::printf("length=%.1f m\n", res.lengthM);
    std::printf("side=%.1f m\n", res.sideM);
    std::printf("corners=%d\n", res.corners);
    std::printf("marshal posts=%d\n", res.marshalPosts);
    std::printf("lap points=%d\n", res.lapPoints);
    std::printf("grid slots=%d\n", res.gridSlots);
    std::printf("out=%s\n", cfg.outDir.c_str());
    return 0;
}
