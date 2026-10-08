#pragma once
/**
 * team.ini -> session field bridge (roadmap 2.8).
 *
 * Turns a loaded TeamInfo roster into the starting field a session uses:
 * grid order (roster order), driver names, race numbers, liveries and garage
 * boxes, plus the log table that makes the mapping inspectable. Pure
 * functions — no renderer, no physics, no UI — so tests can pin the mapping
 * (tests/ksengine/team_upgrade_test.cpp).
 *
 * The models (TeamInfo / TeamCarSlot) already existed; this is the wire-up
 * into SimulationLoop::beginRaceSession the roadmap note asks for.
 */
#include "TeamInfo.h"

#include <cstdio>
#include <string>
#include <vector>

namespace ks {
namespace sim {

/** One entry of the starting field, in grid order (gridSlot == index). */
struct FieldSlot {
    std::string driverName; // empty = keep the default identity ("Car N")
    std::string carModel;   // empty = the player's loaded car
    std::string liveryId;   // empty = base car textures
    int raceNumber = 0;
    int garageIndex = 0;
    int gridSlot = 0; // 0-based; == index inside the field vector
    bool isPlayer = false;
};

/**
 * Build the session field from a roster (roadmap 2.8).
 *
 * Rules (deterministic, tested):
 *  - only active slots participate; roster order == grid order;
 *  - the player is the slot flagged Player=1, else the first slot;
 *  - trailing roster AI beyond `aiCount` are dropped (bench drivers), the
 *    player is never dropped;
 *  - the field is padded with synthetic AI named "<prefix><n>" (numbered over
 *    the whole AI list) up to 1 player + aiCount entries;
 *  - with no roster loaded the field is a nameless player + aiCount synthetic
 *    AI — exactly today's default identity ("AI 1".."AI n"; standings fall
 *    back to "Car N" because driverName stays empty).
 */
inline std::vector<FieldSlot> buildFieldFromTeam(const TeamInfo& team, int aiCount,
                                                 const std::string& driverPrefix = "AI ")
{
    if (aiCount < 0) aiCount = 0;

    std::vector<FieldSlot> field;
    int playerIdx = -1;
    for (const auto& s : team.slots) {
        if (!s.active) continue;
        FieldSlot f;
        f.driverName = s.driverName;
        f.carModel = s.carModel;
        f.liveryId = s.liveryId;
        f.raceNumber = s.raceNumber;
        f.garageIndex = s.garageIndex;
        f.isPlayer = s.isPlayer;
        if (playerIdx < 0 && s.isPlayer) playerIdx = static_cast<int>(field.size());
        field.push_back(std::move(f));
    }

    if (field.empty()) {
        FieldSlot p;
        p.isPlayer = true;
        field.push_back(std::move(p));
        playerIdx = 0;
    } else if (playerIdx < 0) {
        playerIdx = 0;
        field[0].isPlayer = true;
    }

    auto aiIn = [&field]() {
        int n = 0;
        for (const auto& f : field)
            if (!f.isPlayer) ++n;
        return n;
    };

    // Bench: drop roster AI beyond aiCount from the back (never the player).
    while (aiIn() > aiCount) {
        for (int i = static_cast<int>(field.size()) - 1; i >= 0; --i) {
            if (!field[i].isPlayer) {
                field.erase(field.begin() + i);
                break;
            }
        }
    }
    // Pad with synthetic AI; the number is the position in the whole AI list,
    // so an all-synthetic field reads "AI 1".."AI n" exactly like before.
    int guard = 0;
    while (aiIn() < aiCount && ++guard < 10000) {
        FieldSlot s;
        s.driverName = driverPrefix + std::to_string(aiIn() + 1);
        field.push_back(std::move(s));
    }

    for (int i = 0; i < static_cast<int>(field.size()); ++i) field[i].gridSlot = i;
    return field;
}

/** The player's roster slot (Player=1, else the first active slot). */
inline const TeamCarSlot* playerSlot(const TeamInfo& team)
{
    for (const auto& s : team.slots)
        if (s.active && s.isPlayer) return &s;
    for (const auto& s : team.slots)
        if (s.active) return &s;
    return nullptr;
}

/** Session-start roster table for the log (one row per grid slot). */
inline std::string fieldTable(const std::vector<FieldSlot>& field, const std::string& teamName)
{
    std::string out = "[team] session field (";
    out += teamName.empty() ? std::string("no team") : teamName;
    out += "):\n";
    for (const auto& f : field) {
        char buf[192];
        std::snprintf(buf, sizeof(buf), "[team]   P%d #%-3d %-16s livery=%-14s garage=%d%s\n",
                      f.gridSlot + 1, f.raceNumber,
                      f.driverName.empty() ? "(unnamed)" : f.driverName.c_str(),
                      f.liveryId.empty() ? "-" : f.liveryId.c_str(), f.garageIndex,
                      f.isPlayer ? "  (player)" : "");
        out += buf;
    }
    return out;
}

} // namespace sim
} // namespace ks
