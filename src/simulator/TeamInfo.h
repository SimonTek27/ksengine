#pragma once
/**
 * Team roster: car count, race numbers, garage positions.
 * Used for multi-car entries and pit/garage assignment (rF2-like).
 */
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cctype>

namespace ks {
namespace sim {

struct TeamCarSlot {
    int raceNumber = 0;          // door number
    int garageIndex = 0;         // 0-based garage in the pit row (shared by the team)
    int garageSlot = 0;          // 0 or 1: which spot of that garage (see PitGarage.h)
    std::string driverName;
    std::string carModel;        // optional override
    std::string liveryId;
    bool isPlayer = false;
    bool active = true;
};

struct TeamInfo {
    std::string id;              // "team_alpha"
    std::string name;            // display
    std::string shortName;
    std::string country;
    int carCount = 0;            // expected size of slots (may match slots.size())
    std::vector<TeamCarSlot> slots;
    int garageRow = 0;           // optional row on multi-row pits
    int garageStartIndex = 0;    // first box assigned to this team

    int assignedCarCount() const {
        int n = 0;
        for (const auto& s : slots)
            if (s.active) ++n;
        return n;
    }

    /** Race numbers in stable order. */
    std::vector<int> raceNumbers() const {
        std::vector<int> n;
        for (const auto& s : slots)
            if (s.active) n.push_back(s.raceNumber);
        return n;
    }

    const TeamCarSlot* slotByNumber(int raceNumber) const {
        for (const auto& s : slots)
            if (s.raceNumber == raceNumber) return &s;
        return nullptr;
    }

    const TeamCarSlot* slotByGarage(int garageIndex) const {
        for (const auto& s : slots)
            if (s.garageIndex == garageIndex) return &s;
        return nullptr;
    }
};

class TeamInfoLoader {
public:
    /**
 * team.ini example:
 *
 * [Team]
 * Id=ksim_racing
 * Name=ksim Racing
 * ShortName=KSM
 * CarCount=2
 * GarageRow=0
 * GarageStart=3
 *
 * [Car_0]
 * Number=7
 * Garage=3
 * Slot=0
 * Driver=Alice
 * Livery=sponsor_red
 * Player=1
 *
 * [Car_1]
 * Number=8
 * Garage=3
 * Slot=1
 * Driver=Bob
 * Livery=sponsor_blue
 *
 * Both cars of a team share the garage (and therefore the pit pad); Slot picks
 * which of the two spots inside it the car starts from.
 */
    static TeamInfo loadIni(const std::string& path) {
        TeamInfo t;
        std::ifstream in(path);
        if (!in) {
            std::fprintf(stderr, "TeamInfo: cannot open %s\n", path.c_str());
            return t;
        }

        auto trim = [](std::string s) {
            while (!s.empty() && (unsigned char)s.front() <= ' ') s.erase(s.begin());
            while (!s.empty() && (unsigned char)s.back() <= ' ') s.pop_back();
            return s;
        };
        auto unquote = [&](std::string s) {
            s = trim(s);
            if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
                return s.substr(1, s.size() - 2);
            return s;
        };

        std::string line, section;
        TeamCarSlot* cur = nullptr;

        while (std::getline(in, line)) {
            auto sc = line.find(';');
            if (sc != std::string::npos) line = line.substr(0, sc);
            line = trim(line);
            if (line.empty()) continue;

            if (line.front() == '[' && line.back() == ']') {
                section = line.substr(1, line.size() - 2);
                cur = nullptr;
                // [Car_N]
                if (section.size() > 4 && (section[0] == 'C' || section[0] == 'c') &&
                    section.find('_') != std::string::npos) {
                    t.slots.push_back({});
                    cur = &t.slots.back();
                }
                continue;
            }

            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = trim(line.substr(0, eq));
            std::string val = unquote(line.substr(eq + 1));

            std::string secLow = section;
            for (char& c : secLow) c = static_cast<char>(std::tolower((unsigned char)c));

            if (secLow == "team" || secLow == "squadra") {
                if (key == "Id") t.id = val;
                else if (key == "Name") t.name = val;
                else if (key == "ShortName") t.shortName = val;
                else if (key == "Country") t.country = val;
                else if (key == "CarCount") t.carCount = std::atoi(val.c_str());
                else if (key == "GarageRow") t.garageRow = std::atoi(val.c_str());
                else if (key == "GarageStart" || key == "GarageStartIndex")
                    t.garageStartIndex = std::atoi(val.c_str());
                continue;
            }

            if (cur) {
                if (key == "Number" || key == "RaceNumber") cur->raceNumber = std::atoi(val.c_str());
                else if (key == "Garage" || key == "GarageIndex" || key == "Box")
                    cur->garageIndex = std::atoi(val.c_str());
                else if (key == "Slot" || key == "GarageSlot")
                    cur->garageSlot = std::atoi(val.c_str());
                else if (key == "Driver") cur->driverName = val;
                else if (key == "Car" || key == "CarModel") cur->carModel = val;
                else if (key == "Livery" || key == "Skin") cur->liveryId = val;
                else if (key == "Player")
                    cur->isPlayer = (val == "1" || val == "true" || val == "yes");
                else if (key == "Active")
                    cur->active = !(val == "0" || val == "false" || val == "no");
            }
        }

        if (t.carCount <= 0)
            t.carCount = t.assignedCarCount();
        // Auto-assign garage indices if missing (sequential from GarageStart)
        int nextG = t.garageStartIndex;
        for (auto& s : t.slots) {
            if (s.garageIndex < 0) s.garageIndex = nextG;
            // if still 0 for all, still ok — fill sequential when all zero and count>1
            nextG = std::max(nextG, s.garageIndex + 1);
        }
        bool allZeroGarage = true;
        for (const auto& s : t.slots)
            if (s.garageIndex != 0) { allZeroGarage = false; break; }
        if (allZeroGarage && t.slots.size() > 1) {
            // Default roster: two cars per garage share the pad, so the garage
            // index advances every second car and Slot picks the spot inside
            // it (see PitGarage.h). A one-car team keeps garage 0 / slot 0.
            for (size_t i = 0; i < t.slots.size(); ++i) {
                t.slots[i].garageIndex = t.garageStartIndex + static_cast<int>(i) / 2;
                t.slots[i].garageSlot = static_cast<int>(i) % 2;
            }
        }

        std::fprintf(stderr, "TeamInfo: %s cars=%d numbers=", t.name.c_str(), t.carCount);
        for (int n : t.raceNumbers()) std::fprintf(stderr, "%d ", n);
        std::fprintf(stderr, "\n");
        return t;
    }

    /** Build a minimal 1-car team. */
    static TeamInfo single(const std::string& name, int raceNumber, int garageIndex,
                           const std::string& driver, bool player = true) {
        TeamInfo t;
        t.id = "solo";
        t.name = name;
        t.carCount = 1;
        t.garageStartIndex = garageIndex;
        TeamCarSlot s;
        s.raceNumber = raceNumber;
        s.garageIndex = garageIndex;
        s.driverName = driver;
        s.isPlayer = player;
        t.slots.push_back(s);
        return t;
    }
};

} // namespace sim
} // namespace ks
