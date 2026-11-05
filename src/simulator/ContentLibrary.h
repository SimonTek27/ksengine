#pragma once
/**
 * Qt-free content scanner (roadmap 1.3 / GAP P2.5 slice): lists the cars and
 * tracks ksim can load from its native content roots (content/cars and
 * content/tracks relative to the working directory). Any subdirectory is an
 * entry; a missing root yields an empty list, so a fresh checkout can still
 * open the menu and the select screens explain where content goes.
 *
 * Optional metadata files (first hit wins):
 *   <car>/ui_car.json  (or <car>/ui/ui_car.json, the layout AC ships)
 *                       { "name", "brand", "class", "year",
 *                         "colors": [ { "name", "rgb": [r,g,b] } ] }
 *  Colours are the car's own paint list: a channel above 1 is taken as a
 *  0..255 value and scaled down, so both list styles land in 0..1.
 *   <car>/data/car.json
 *   <track>/ui_track.json { "name", "country", "year" }
 */
#include "engine/Config/Json.h"
#include "engine/assets/UserData.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace ks::sim {

/** One paint from the car's own colour list. rgb is 0..1. */
struct CarPaint {
    std::string name;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

struct ContentEntry {
    std::string id;     // directory name, stable key
    std::string path;   // passed to SimulationLoop::loadCar / loadTrackFolder
    std::string label;  // display name
    std::string brand;  // optional filter key
    std::string carClass;
    int year = 0;
    // Paints the car folder declares. Empty = no colour choice to offer.
    std::vector<CarPaint> paints;
};

namespace content_detail {

inline void applyCarMeta(ContentEntry& entry) {
    namespace fs = std::filesystem;
    const char* candidates[] = {
        "ui_car.json",      // where the ksim content tools put it
        "ui/ui_car.json",   // where Assetto Corsa itself puts it
        "data/car.json",
        "car.json",
    };
    for (const char* rel : candidates) {
        const fs::path meta = fs::path(entry.path) / rel;
        std::string text;
        if (!ks::engine::assets::UserData::readFile(meta.string(), text))
            continue;
        ks::engine::json::Value root;
        std::string error;
        if (!ks::engine::json::parse(text, root, &error) || !root.isObject())
            continue;
        if (const auto* v = root.find("name"); v && v->isString() && !v->asString().empty())
            entry.label = v->asString();
        if (const auto* v = root.find("brand"); v && v->isString())
            entry.brand = v->asString();
        if (const auto* v = root.find("class"); v && v->isString())
            entry.carClass = v->asString();
        if (const auto* v = root.find("year"); v && v->isNumber())
            entry.year = static_cast<int>(v->asNumber());
        if (const auto* list = root.find("colors"); list && list->isArray()) {
            for (std::size_t i = 0; i < list->size(); ++i) {
                const auto* item = list->item(i);
                if (!item || !item->isObject()) continue;
                const auto* rgb = item->find("rgb");
                if (!rgb || !rgb->isArray() || rgb->size() < 3) continue;
                float channels[3] = {1.0f, 1.0f, 1.0f};
                bool usable = true;
                for (int k = 0; k < 3; ++k) {
                    const auto* c = rgb->item(k);
                    if (!c || !c->isNumber()) { usable = false; break; }
                    channels[k] = static_cast<float>(c->asNumber());
                }
                if (!usable) continue;
                // 0..255 lists are common in content tools: scale, do not clamp
                // everything to white.
                if (channels[0] > 1.0f || channels[1] > 1.0f || channels[2] > 1.0f)
                    for (float& c : channels) c /= 255.0f;
                for (float& c : channels)
                    c = c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
                CarPaint paint;
                paint.r = channels[0];
                paint.g = channels[1];
                paint.b = channels[2];
                if (const auto* n = item->find("name"); n && n->isString())
                    paint.name = n->asString();
                if (paint.name.empty()) paint.name = "colour " + std::to_string(i + 1);
                entry.paints.push_back(std::move(paint));
            }
        }
        // One line per car that declared metadata: the CARS page filters and
        // the paint picker both read exactly what this parsed.
        std::printf("[content] %s: \"%s\" class=%s year=%d colours=%d\n",
                    entry.id.c_str(), entry.label.c_str(), entry.carClass.c_str(),
                    entry.year, static_cast<int>(entry.paints.size()));
        break;
    }
}

inline std::vector<ContentEntry> scanRoot(const std::string& root, bool cars) {
    namespace fs = std::filesystem;
    std::vector<ContentEntry> out;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return out;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        if (!e.is_directory(ec)) continue;
        ContentEntry entry;
        entry.path = e.path().string();
        entry.id = e.path().filename().string();
        entry.label = entry.id;
        if (cars)
            applyCarMeta(entry);
        out.push_back(std::move(entry));
    }
    std::sort(out.begin(), out.end(), [](const ContentEntry& a, const ContentEntry& b) {
        return a.label < b.label;
    });
    return out;
}

} // namespace content_detail

/** Paint list of one car folder, in the order the file declares it. Empty
 *  when the folder has no metadata or no colours, which is the case every car
 *  without a paint choice falls into. */
inline std::vector<CarPaint> carPaintsFor(const std::string& carDir) {
    ContentEntry entry;
    entry.path = carDir;
    entry.id = carDir;
    content_detail::applyCarMeta(entry);
    return entry.paints;
}

inline std::vector<ContentEntry> scanCarLibrary(const std::string& root = "content/cars") {
    return content_detail::scanRoot(root, true);
}

inline std::vector<ContentEntry> scanTrackLibrary(const std::string& root = "content/tracks") {
    return content_detail::scanRoot(root, false);
}

/** Teams (roadmap 2.8): a team folder is any subdirectory of the root that
 *  holds a team.ini. A missing root yields an empty list, so a fresh
 *  checkout just shows the empty select screen. */
inline std::vector<ContentEntry> scanTeamLibrary(const std::string& root = "content/teams") {
    namespace fs = std::filesystem;
    std::vector<ContentEntry> out;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return out;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        if (!e.is_directory(ec)) continue;
        if (!fs::is_regular_file(e.path() / "team.ini", ec)) continue;
        ContentEntry entry;
        entry.path = e.path().string();
        entry.id = e.path().filename().string();
        entry.label = entry.id;
        out.push_back(std::move(entry));
    }
    std::sort(out.begin(), out.end(), [](const ContentEntry& a, const ContentEntry& b) {
        return a.label < b.label;
    });
    return out;
}

/** Filter helpers for the CARS page (class / year / brand). Empty filter = keep all. */
inline std::vector<ContentEntry> filterCars(const std::vector<ContentEntry>& in,
                                            const std::string& brand,
                                            const std::string& carClass,
                                            int yearMin,
                                            int yearMax) {
    std::vector<ContentEntry> out;
    out.reserve(in.size());
    for (const auto& e : in) {
        if (!brand.empty() && e.brand != brand) continue;
        if (!carClass.empty() && e.carClass != carClass) continue;
        if (yearMin > 0 && e.year > 0 && e.year < yearMin) continue;
        if (yearMax > 0 && e.year > 0 && e.year > yearMax) continue;
        out.push_back(e);
    }
    return out;
}


/** Skins under <car>/skins/*, driver gear under content/drivers/{suits,helmets}. */
inline std::vector<ContentEntry> scanSkinLibrary(const std::string& carDir) {
    namespace fs = std::filesystem;
    return content_detail::scanRoot((fs::path(carDir) / "skins").string(), false);
}

inline std::vector<ContentEntry> scanDriverSuits(const std::string& root = "content/drivers/suits") {
    return content_detail::scanRoot(root, false);
}

inline std::vector<ContentEntry> scanDriverHelmets(const std::string& root = "content/drivers/helmets") {
    return content_detail::scanRoot(root, false);
}

/** Write a minimal content manifest JSON listing cars/tracks/teams found. */
inline bool writeContentManifest(const std::string& outPath,
                                 const std::string& carsRoot = "content/cars",
                                 const std::string& tracksRoot = "content/tracks",
                                 const std::string& teamsRoot = "content/teams") {
    auto cars = scanCarLibrary(carsRoot);
    auto tracks = scanTrackLibrary(tracksRoot);
    auto teams = scanTeamLibrary(teamsRoot);
    std::string json = "{\n  \"schema\": \"ksengine.content/1\",\n  \"cars\": [";
    for (size_t i = 0; i < cars.size(); ++i) {
        if (i) json += ", ";
        json += "\"" + cars[i].id + "\"";
    }
    json += "],\n  \"tracks\": [";
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (i) json += ", ";
        json += "\"" + tracks[i].id + "\"";
    }
    json += "],\n  \"teams\": [";
    for (size_t i = 0; i < teams.size(); ++i) {
        if (i) json += ", ";
        json += "\"" + teams[i].id + "\"";
    }
    json += "]\n}\n";
    return ks::engine::assets::UserData::writeFile(outPath, json);
}

} // namespace ks::sim
