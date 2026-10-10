#pragma once
/**
 * Driver career stats, persisted to user/<player>/stats.json.
 *
 * The profile itself (name, nationality, number, bio, helmet) is menu state;
 * what outlives a run is the record: wins, poles, podiums, races started,
 * the personal-best lap time, and how many track/car PB records the
 * PersonalBestStore holds in user/pb. Those go to JSON so the per-player
 * folder is a complete, hand-readable account of the driver.
 *
 * Round trip (src/simulator/SimulatorApp.cpp):
 *   start-up  loadDriverStats()  -> applyDriverStats() seeds the profile
 *   on change / exit saveDriverStats(driverStatsFromProfile(profile, pbCount))
 *
 * Like everything under the per-player folder the file starts as "{}" — no
 * key is carried until there is a value for it.
 *
 * Header-only, uses only the engine's JSON + file helpers, so it is safe to
 * include from both the DLL and ksim.exe.
 */
#include "engine/Config/Json.h"
#include "engine/assets/UserData.h"

#include "GameMenuOverlay.h" // DriverProfile

#include <cstdio>
#include <string>

namespace ks {
namespace sim {

/** The persisted half of DriverProfile (stats only). */
struct DriverStats {
    int wins = 0;
    int poles = 0;
    int podiums = 0;
    int totalRaces = 0;
    /** Personal-best lap in seconds; 0 when the driver has not set one. */
    float bestLapTime = 0.0f;
    /** Records the PersonalBestStore holds (user/pb), the PB side of stats. */
    int pbRecords = 0;
};

/** Snapshot the profile (+ PB record count) for saving. */
inline DriverStats driverStatsFromProfile(const DriverProfile& p, int pbRecords) {
    DriverStats s;
    s.wins = p.wins;
    s.poles = p.poles;
    s.podiums = p.podiums;
    s.totalRaces = p.totalRaces;
    s.bestLapTime = p.bestLapTime;
    s.pbRecords = pbRecords;
    return s;
}

/** Seed the profile from a loaded snapshot (only the stats it owns). */
inline void applyDriverStats(const DriverStats& s, DriverProfile& p) {
    p.wins = s.wins;
    p.poles = s.poles;
    p.podiums = s.podiums;
    p.totalRaces = s.totalRaces;
    p.bestLapTime = s.bestLapTime;
}

/**
 * Read stats.json into `out` (values already in `out` survive missing keys,
 * so a "{}" or partially written file is a partial merge, not a wipe).
 * False when the file is missing or malformed — `out` is then untouched.
 */
inline bool loadDriverStats(const std::string& path, DriverStats& out) {
    namespace json = ks::engine::json;
    std::string text;
    if (!ks::engine::assets::UserData::readFile(path, text)) return false;
    json::Value root;
    std::string error;
    if (!json::parse(text, root, &error)) {
        std::fprintf(stderr, "stats.json: %s: %s\n", path.c_str(), error.c_str());
        return false;
    }
    if (!root.isObject()) return false;

    DriverStats s = out;
    const auto intAt = [&root](const char* key, int def) {
        const json::Value* v = root.find(key);
        return (v && v->isNumber() && v->asNumber() >= 0.0) ? v->asInt(def) : def;
    };
    s.wins = intAt("wins", s.wins);
    s.poles = intAt("poles", s.poles);
    s.podiums = intAt("podiums", s.podiums);
    s.totalRaces = intAt("totalRaces", s.totalRaces);
    s.pbRecords = intAt("pbRecords", s.pbRecords);
    if (const json::Value* v = root.find("bestLapTime");
        v && v->isNumber() && v->asNumber() >= 0.0 && v->asNumber() < 1.0e6) {
        s.bestLapTime = static_cast<float>(v->asNumber());
    }
    out = s;
    return true;
}

/** Write stats.json (creating the folder if needed). */
inline bool saveDriverStats(const std::string& path, const DriverStats& s) {
    namespace json = ks::engine::json;
    json::Value root = json::Value::object();
    root.set("wins", json::Value::number(s.wins));
    root.set("poles", json::Value::number(s.poles));
    root.set("podiums", json::Value::number(s.podiums));
    root.set("totalRaces", json::Value::number(s.totalRaces));
    root.set("bestLapTime", json::Value::number(static_cast<double>(s.bestLapTime)));
    root.set("pbRecords", json::Value::number(s.pbRecords));
    return ks::engine::assets::UserData::writeFile(path, json::dump(root));
}

} // namespace sim
} // namespace ks
