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
    Replay
};

struct SessionStartParams {
    int totalLaps = 10;
    int qualifyMinutes = 15;
    int practiceMinutes = 30;
    int gridSize = 16;
    bool standingStart = true;
    bool trackLimits = true;
};

inline const char* sessionModeName(GameSessionMode m) {
    switch (m) {
    case GameSessionMode::Practice: return "PRACTICE";
    case GameSessionMode::Qualifying: return "QUALIFYING";
    case GameSessionMode::Race: return "RACE";
    case GameSessionMode::TimeAttack: return "TIME_ATTACK";
    case GameSessionMode::Hotlap: return "HOTLAP";
    case GameSessionMode::Replay: return "REPLAY";
    }
    return "RACE";
}

inline GameSessionMode modeFromMenuEntry(std::string entry) {
    for (char& c : entry) c = (char)toupper((unsigned char)c);
    if (entry.find("QUAL") != std::string::npos) return GameSessionMode::Qualifying;
    if (entry.find("PRACT") != std::string::npos) return GameSessionMode::Practice;
    if (entry.find("TIME") != std::string::npos) return GameSessionMode::TimeAttack;
    if (entry.find("HOT") != std::string::npos) return GameSessionMode::Hotlap;
    if (entry.find("REPLAY") != std::string::npos) return GameSessionMode::Replay;
    return GameSessionMode::Race;
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
    default: break;
    }
    return p;
}

inline uint8_t toNetSessionType(GameSessionMode m) {
    return static_cast<uint8_t>(m);
}

} // namespace sim
} // namespace ks
