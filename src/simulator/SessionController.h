#pragma once
/**
 * Session modes and start params for FeatureHub / menu.
 */
#include <string>
#include <cstdint>
#include <cctype>
#include <algorithm>
#include <map>
#include <vector>

#include "engine/Config/Json.h"

namespace ks {
namespace sim {

enum class GameSessionMode : uint8_t {
    Practice = 0,
    Qualifying,
    Race,
    TimeAttack,
    Hotlap,
    Replay,
    Weekend,
    Championship,
    Drift,
    DragRace
};

struct SessionStartParams {
    int totalLaps = 10;
    int qualifyMinutes = 15;
    int practiceMinutes = 30;
    int gridSize = 16;
    bool standingStart = true;
    bool trackLimits = true;
    // beginSession() reads these: a timed session runs on the clock instead of
    // laps, aiCars fills the grid behind the player, startInGarage spawns the
    // player in its box instead of on the grid.
    int sessionTimeSeconds = 0;
    int aiCars = 0;
    bool startInGarage = false;
};

inline const char* sessionModeName(GameSessionMode m) {
    switch (m) {
    case GameSessionMode::Practice: return "PRACTICE";
    case GameSessionMode::Qualifying: return "QUALIFYING";
    case GameSessionMode::Race: return "RACE";
    case GameSessionMode::TimeAttack: return "TIME_ATTACK";
    case GameSessionMode::Hotlap: return "HOTLAP";
    case GameSessionMode::Replay: return "REPLAY";
    case GameSessionMode::Weekend: return "WEEKEND";
    case GameSessionMode::Championship: return "CHAMPIONSHIP";
    case GameSessionMode::Drift: return "DRIFT";
    case GameSessionMode::DragRace: return "DRAG_RACE";
    }
    return "RACE";
}

/** Name -> mode without a fallback: false means the name matches no mode, so a
 *  config that misspells one can report it instead of silently racing. */
inline bool tryModeFromMenuEntry(std::string entry, GameSessionMode* out) {
    for (char& c : entry) c = (char)toupper((unsigned char)c);
    GameSessionMode mode;
    if (entry.find("QUAL") != std::string::npos) mode = GameSessionMode::Qualifying;
    else if (entry.find("PRACT") != std::string::npos) mode = GameSessionMode::Practice;
    else if (entry.find("TIME") != std::string::npos) mode = GameSessionMode::TimeAttack;
    else if (entry.find("HOT") != std::string::npos) mode = GameSessionMode::Hotlap;
    else if (entry.find("REPLAY") != std::string::npos) mode = GameSessionMode::Replay;
    else if (entry.find("WEEK") != std::string::npos) mode = GameSessionMode::Weekend;
    else if (entry.find("CHAMP") != std::string::npos) mode = GameSessionMode::Championship;
    else if (entry.find("DRIFT") != std::string::npos) mode = GameSessionMode::Drift;
    else if (entry.find("DRAG") != std::string::npos) mode = GameSessionMode::DragRace;
    else if (entry.find("RACE") != std::string::npos) mode = GameSessionMode::Race;
    else if (entry.find("QUICK") != std::string::npos) mode = GameSessionMode::Race;
    else return false;
    if (out) *out = mode;
    return true;
}

/** The menu/KS_SESSION form: an unmatched name races, which is what the
 *  environment has always done. The config loader uses the strict version. */
inline GameSessionMode modeFromMenuEntry(std::string entry) {
    GameSessionMode mode = GameSessionMode::Race;
    tryModeFromMenuEntry(entry, &mode);
    return mode;
}

/** One entry of singleplayer.defaults. Only the fields the file carried are
 *  applied (-1 = leave the built-in value), so an entry that sets the laps does
 *  not silently zero the grid and the clock of that mode. */
struct SessionDefaultsOverride {
    int totalLaps = -1;
    int aiCars = -1;
    int sessionTimeSeconds = -1;
    int startInGarage = -1; // 0/1, -1 = not in the file
    int standingStart = -1; // 0/1, -1 = not in the file
};

/** The table singleplayer.defaults fills at start-up. The menu and the loop
 *  both ask defaultsForMode(), so one table keeps them in step. */
inline std::map<GameSessionMode, SessionDefaultsOverride>& sessionDefaultsOverride() {
    static std::map<GameSessionMode, SessionDefaultsOverride> table;
    return table;
}

inline void clearSessionDefaultsOverride() { sessionDefaultsOverride().clear(); }

inline SessionStartParams defaultsForMode(GameSessionMode m) {
    SessionStartParams p;
    switch (m) {
    case GameSessionMode::Practice:
        p.totalLaps = 0; p.practiceMinutes = 30; break;
    case GameSessionMode::Qualifying:
        p.totalLaps = 0; p.qualifyMinutes = 15; break;
    case GameSessionMode::TimeAttack:
    case GameSessionMode::Hotlap:
        p.totalLaps = 0; break;
    case GameSessionMode::Race:
        p.totalLaps = 10; break;
    case GameSessionMode::Weekend:
        p.totalLaps = 12; p.qualifyMinutes = 15; p.practiceMinutes = 20; break;
    case GameSessionMode::Championship:
        p.totalLaps = 15; break;
    case GameSessionMode::Drift:
        p.totalLaps = 0; p.sessionTimeSeconds = 180; p.aiCars = 0; break;
    case GameSessionMode::DragRace:
        p.totalLaps = 1; p.aiCars = 1; p.standingStart = true; break;
    default: break;
    }
    // singleplayer.defaults wins over the table above, field by field.
    const auto& overrides = sessionDefaultsOverride();
    const auto it = overrides.find(m);
    if (it != overrides.end()) {
        const SessionDefaultsOverride& o = it->second;
        if (o.totalLaps >= 0) p.totalLaps = o.totalLaps;
        if (o.aiCars >= 0) p.aiCars = std::min(31, o.aiCars);
        if (o.sessionTimeSeconds >= 0) p.sessionTimeSeconds = o.sessionTimeSeconds;
        if (o.startInGarage >= 0) p.startInGarage = o.startInGarage != 0;
        if (o.standingStart >= 0) p.standingStart = o.standingStart != 0;
    }
    return p;
}

/** Reads singleplayer.defaults into the override table: an object keyed by mode
 *  name, each value carrying any of laps, aiCars, clockSeconds, startInGarage.
 *  Returns the names that match no mode, so a typo is reported instead of
 *  quietly turning the entry into a race. */
inline std::vector<std::string> applySessionDefaults(const ks::engine::json::Value& block) {
    std::vector<std::string> unknown;
    if (!block.isObject()) return unknown;
    const auto readFlag = [](const ks::engine::json::Value* v) -> int {
            if (!v) return -1;
            if (v->isBool()) return v->asBool() ? 1 : 0;
            if (v->isNumber()) return v->asNumber() != 0 ? 1 : 0;
            return -1;
    };
    for (const std::string& name : block.keys()) {
        GameSessionMode mode = GameSessionMode::Race;
        if (!tryModeFromMenuEntry(name, &mode)) {
            unknown.push_back(name);
            continue;
        }
        const ks::engine::json::Value* entry = block.find(name);
        if (!entry || !entry->isObject()) continue;
        // Merged into whatever an earlier file set, so the user file can
        // override one field without dropping the rest of the entry.
        SessionDefaultsOverride& o = sessionDefaultsOverride()[mode];
        if (const ks::engine::json::Value* v = entry->find("laps"); v && v->isNumber())
            o.totalLaps = std::max(0, static_cast<int>(v->asNumber()));
        if (const ks::engine::json::Value* v = entry->find("aiCars"); v && v->isNumber())
            o.aiCars = std::max(0, static_cast<int>(v->asNumber()));
        if (const ks::engine::json::Value* v = entry->find("clockSeconds"); v && v->isNumber())
            o.sessionTimeSeconds = std::max(0, static_cast<int>(v->asNumber()));
        const int garageFlag = readFlag(entry->find("startInGarage"));
        if (garageFlag >= 0) o.startInGarage = garageFlag;
        const int standingFlag = readFlag(entry->find("standingStart"));
        if (standingFlag >= 0) o.standingStart = standingFlag;
    }
    return unknown;
}

inline uint8_t toNetSessionType(GameSessionMode m) {
    return static_cast<uint8_t>(m);
}

} // namespace sim
} // namespace ks
