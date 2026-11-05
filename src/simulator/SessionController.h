#pragma once
/**
 * Session modes and start params for FeatureHub / menu.
 */
#include <string>
#include <cstdint>
#include <cctype>

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
    return p;
}

inline uint8_t toNetSessionType(GameSessionMode m) {
    return static_cast<uint8_t>(m);
}

} // namespace sim
} // namespace ks
