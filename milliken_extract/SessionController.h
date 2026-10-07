#pragma once
/**
 * Session mode selection — Practice / Qualifying / Race / TimeTrial / Hotlap.
 */
#include "RaceSessionManager.h"
#include "RaceSession.h"
#include <string>
#include <cstdint>
#include <cctype>

namespace ks {
namespace sim {

enum class GameSessionMode : uint8_t {
    Practice = 0,
    Qualifying,
    Race,
    TimeTrial,
    Hotlap,
    Autocross,
    Drift,
    Cruise
};

inline const char* sessionModeName(GameSessionMode m) {
    switch (m) {
    case GameSessionMode::Practice: return "Practice";
    case GameSessionMode::Qualifying: return "Qualifying";
    case GameSessionMode::Race: return "Race";
    case GameSessionMode::TimeTrial: return "Time Trial";
    case GameSessionMode::Hotlap: return "Hotlap";
    case GameSessionMode::Autocross: return "Autocross";
    case GameSessionMode::Drift: return "Drift";
    case GameSessionMode::Cruise: return "Cruise";
    default: return "Unknown";
    }
}

inline GameSessionMode sessionModeFromString(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    if (s.find("qual") != std::string::npos) return GameSessionMode::Qualifying;
    if (s.find("practice") != std::string::npos || s == "prac") return GameSessionMode::Practice;
    if (s.find("time") != std::string::npos || s == "tt") return GameSessionMode::TimeTrial;
    if (s.find("hot") != std::string::npos) return GameSessionMode::Hotlap;
    if (s.find("drift") != std::string::npos) return GameSessionMode::Drift;
    if (s.find("cruise") != std::string::npos || s.find("free") != std::string::npos)
        return GameSessionMode::Cruise;
    if (s.find("auto") != std::string::npos) return GameSessionMode::Autocross;
    return GameSessionMode::Race;
}

struct SessionStartParams {
    GameSessionMode mode = GameSessionMode::Race;
    int totalLaps = 5;
    int sessionTimeSeconds = 0;
    int qualifyingTimeSeconds = 600;
    bool useGrid = true;
    int aiCars = 0;
    bool startInGarage = false;
};

inline SessionStartParams defaultsForMode(GameSessionMode mode) {
    SessionStartParams p;
    p.mode = mode;
    switch (mode) {
    case GameSessionMode::Practice:
        p.totalLaps = 0; p.sessionTimeSeconds = 3600; p.useGrid = false; p.startInGarage = true;
        break;
    case GameSessionMode::Qualifying:
        p.totalLaps = 0; p.sessionTimeSeconds = 600; p.qualifyingTimeSeconds = 600;
        p.useGrid = false; p.startInGarage = true;
        break;
    case GameSessionMode::Race:
        p.totalLaps = 5; p.sessionTimeSeconds = 0; p.useGrid = true; p.startInGarage = false;
        break;
    case GameSessionMode::TimeTrial:
    case GameSessionMode::Hotlap:
        p.totalLaps = 0; p.sessionTimeSeconds = 0; p.useGrid = false; p.startInGarage = false; p.aiCars = 0;
        break;
    case GameSessionMode::Cruise:
        p.totalLaps = 0; p.sessionTimeSeconds = 0; p.useGrid = false; p.startInGarage = false;
        break;
    default: break;
    }
    return p;
}

inline RaceConfig toRaceConfig(const SessionStartParams& p, float trackLength = 1000.f) {
    RaceConfig rc;
    rc.trackLength = trackLength;
    rc.totalLaps = p.totalLaps;
    rc.sessionTimeSeconds = p.sessionTimeSeconds;
    rc.useGridPositions = p.useGrid;
    rc.qualifyingTimeSeconds = p.qualifyingTimeSeconds;
    switch (p.mode) {
    case GameSessionMode::Practice:
        rc.sessionType = RaceConfig::SessionType::Practice;
        break;
    case GameSessionMode::Qualifying:
        rc.sessionType = RaceConfig::SessionType::Qualifying;
        rc.qualifyingEnabled = true;
        break;
    default:
        rc.sessionType = RaceConfig::SessionType::Race;
        break;
    }
    return rc;
}

inline uint8_t toNetSessionType(GameSessionMode m) {
    switch (m) {
    case GameSessionMode::Practice: return 0;
    case GameSessionMode::Qualifying: return 1;
    case GameSessionMode::TimeTrial: return 3;
    case GameSessionMode::Hotlap: return 4;
    default: return 2;
    }
}

inline SessionType toLegacySessionType(GameSessionMode m) {
    switch (m) {
    case GameSessionMode::Practice: return SessionType::Practice;
    case GameSessionMode::Qualifying: return SessionType::Qualify;
    case GameSessionMode::Hotlap:
    case GameSessionMode::TimeTrial: return SessionType::Hotlap;
    default: return SessionType::Race;
    }
}

inline GameSessionMode modeFromMenuEntry(const std::string& entry) {
    return sessionModeFromString(entry);
}

} // namespace sim
} // namespace ks
