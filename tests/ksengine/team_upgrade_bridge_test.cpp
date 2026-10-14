/**
 * Gate 2.13 — consolidate the team / upgrades / materials bridges with the
 * real parsers (docs/CHECKLIST.md 2.13, "casi extra" that were missing):
 *  - TeamInfoLoader::loadIni parses a team.ini roster ([Team] header,
 *    [Car_N] slots, quote stripping, GarageStart -> sequential garage
 *    indices);
 *  - buildFieldFromTeam() turns the roster into the session field: roster
 *    order kept, the Player=1 slot marked, bench AI trimmed from the back,
 *    synthetic "AI n" pads continuing the AI list, gridSlot sequential;
 *  - playerSlot() picks the Player=1 car (else the first active slot);
 *  - VehicleUpgradeSystem::loadFromIni parses an upgrade catalog (types,
 *    levels, id slugging, category inferred from the type name) and
 *    select()/appliedMods() aggregate PhysicsMods across types;
 *  - applyUpgradesToVehicleSimulator() applies the mods on top of a stored
 *    baseline: mass/power/drag follow the selected level and re-applying
 *    works off the baseline again (no stacking on modified state);
 *  - collectNodeOverrides()/selectedLivery() expose the instance/material
 *    swap keys the garage/materials path consumes.
 * Materials themselves are covered by material_cache_test +
 * content_library_test; this file closes the parser + bridge gap for the
 * team and upgrades sides.
 */
#include "KsTest.h"
#include "simulator/TeamInfo.h"
#include "simulator/TeamSessionBridge.h"
#include "engine/vehicle/VehicleUpgradeSystem.h"
#include "engine/vehicle/ApplyVehicleUpgrades.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

using ks::physics::VehicleSimulator;
using ks::sim::TeamCarSlot;
using ks::sim::TeamInfo;
using ks::sim::TeamInfoLoader;
using ks::vehicle::applyUpgradesToVehicleSimulator;
using ks::vehicle::BaselineVehicleParams;
using ks::vehicle::UpgradeCategory;
using ks::vehicle::VehicleUpgradeSystem;

namespace {
std::filesystem::path writeTemp(const char* name, const char* body) {
    const auto p = std::filesystem::temp_directory_path() / name;
    std::ofstream out(p, std::ios::trunc);
    out << body;
    return p;
}
} // namespace

int main() {
    // --- team.ini -> TeamInfo ------------------------------------------------
    const auto teamIni = writeTemp("ks_bridge_team.ini",
                                   "[Team]\n"
                                   "Id=team_alpha\n"
                                   "Name=\"Scuderia Alpha\"\n"
                                   "ShortName=ALP\n"
                                   "Country=IT\n"
                                   "CarCount=2\n"
                                   "GarageStart=3\n"
                                   "\n"
                                   "[Car_1]\n"
                                   "Number=7\n"
                                   "Driver=Bob\n"
                                   "Livery=sponsor_blue\n"
                                   "\n"
                                   "[Car_2]\n"
                                   "Number=8\n"
                                   "Driver=Alice\n"
                                   "Car=model_gt\n"
                                   "Livery=red\n"
                                   "Player=1\n");
    const TeamInfo team = TeamInfoLoader::loadIni(teamIni.string());
    KS_CHECK(team.id == "team_alpha");
    KS_CHECK(team.name == "Scuderia Alpha"); // quotes stripped
    KS_CHECK(team.shortName == "ALP");
    KS_CHECK(team.country == "IT");
    KS_CHECK(team.carCount == 2);
    KS_CHECK(team.slots.size() == 2);
    if (team.slots.size() == 2) {
        KS_CHECK(team.slots[0].raceNumber == 7);
        KS_CHECK(team.slots[0].driverName == "Bob");
        KS_CHECK(team.slots[0].liveryId == "sponsor_blue");
        KS_CHECK(!team.slots[0].isPlayer);
        KS_CHECK(team.slots[0].garageIndex == 3); // GarageStart + sequential
        KS_CHECK(team.slots[1].raceNumber == 8);
        KS_CHECK(team.slots[1].carModel == "model_gt");
        KS_CHECK(team.slots[1].isPlayer);
    }

    // --- roster -> session field --------------------------------------------
    const auto field = ks::sim::buildFieldFromTeam(team, 3);
    KS_CHECK(field.size() == 4); // Bob + Alice + 2 padded AI
    if (field.size() == 4) {
        KS_CHECK(field[0].driverName == "Bob");
        KS_CHECK(!field[0].isPlayer);
        KS_CHECK(field[1].driverName == "Alice");
        KS_CHECK(field[1].isPlayer);
        KS_CHECK(field[2].driverName == "AI 2"); // pad continues the AI list
        KS_CHECK(field[3].driverName == "AI 3");
        KS_CHECK(field[3].gridSlot == 3);
    }
    const auto bench = ks::sim::buildFieldFromTeam(team, 0);
    KS_CHECK(bench.size() == 1); // roster AI dropped from the back
    if (!bench.empty()) KS_CHECK(bench[0].isPlayer);
    const TeamCarSlot* ps = ks::sim::playerSlot(team);
    KS_CHECK(ps && ps->driverName == "Alice");

    // --- upgrade catalog -> PhysicsMods -------------------------------------
    const auto catIni = writeTemp("ks_bridge_upgrades.ini",
                                  "UpgradeType = Engine Stage 2\n"
                                  "{\n"
                                  "  UpgradeLevel = Stage 1\n"
                                  "  {\n"
                                  "    Description = street tune\n"
                                  "    Price = 1000\n"
                                  "    PowerKwAdd = 20\n"
                                  "    PowerMult = 1.05\n"
                                  "    MassAddKg = 5\n"
                                  "  }\n"
                                  "  UpgradeLevel = Stage 2\n"
                                  "  {\n"
                                  "    Price = 3000\n"
                                  "    PowerKwAdd = 45\n"
                                  "    CdMult = 0.95\n"
                                  "    EnableNode = splitter\n"
                                  "    Swap = body:kit_race\n"
                                  "  }\n"
                                  "}\n"
                                  "UpgradeType = Livery Sponsor\n"
                                  "{\n"
                                  "  UpgradeLevel = Sponsor Red\n"
                                  "  {\n"
                                  "    LiveryId = sponsor_red\n"
                                  "    RaceNumber = 7\n"
                                  "  }\n"
                                  "}\n");
    VehicleUpgradeSystem ups;
    KS_CHECK(ups.loadFromIni(catIni.string()));
    KS_CHECK(ups.types().size() == 2);
    if (ups.types().size() == 2) {
        KS_CHECK(ups.types()[0].category == UpgradeCategory::Engine); // from name
        KS_CHECK(ups.types()[0].levels.size() == 2);
        KS_CHECK(ups.types()[0].levels[0].id == "stage_1"); // slug: "Stage 1"
        KS_CHECK(ups.types()[0].levels[0].price == 1000);
        KS_CHECK(ups.types()[1].category == UpgradeCategory::Livery);
    }
    {
        const auto m = ups.appliedMods(); // default selection = level 0
        KS_CHECK_NEAR(m.powerKwAdd, 20.f, 1e-4);
        KS_CHECK_NEAR(m.powerMult, 1.05f, 1e-4);
        KS_CHECK_NEAR(m.massAddKg, 5.f, 1e-4);
    }
    KS_CHECK(!ups.selectById(UpgradeCategory::Aero, "race_wing")); // no aero type
    KS_CHECK(!ups.selectById(UpgradeCategory::Engine, "nope"));
    KS_CHECK(ups.selectById(UpgradeCategory::Engine, "stage_2"));
    {
        const auto m = ups.appliedMods();
        KS_CHECK_NEAR(m.powerKwAdd, 45.f, 1e-4);
        KS_CHECK_NEAR(m.cdMult, 0.95f, 1e-4);
        KS_CHECK_NEAR(m.massAddKg, 0.f, 1e-4); // per-level mods, not summed
    }
    {
        std::vector<std::string> en, dis;
        std::map<std::string, std::string> swaps;
        ups.collectNodeOverrides(en, dis, swaps);
        KS_CHECK(en.size() == 1 && en[0] == "splitter");
        KS_CHECK(dis.empty());
        KS_CHECK(swaps.count("body") == 1 && swaps["body"] == "kit_race");
    }
    {
        const auto lv = ups.selectedLivery();
        KS_CHECK(lv.id == "sponsor_red");
        KS_CHECK(lv.number == 7);
    }

    // --- PhysicsMods -> VehicleSimulator (the garage apply path) -------------
    {
        const BaselineVehicleParams base; // 1200 kg / 260 kW / 0.35 cd
        VehicleSimulator veh;             // stock defaults, no load needed
        const double liftBefore = veh.aero().getConfig().liftCoefficient;
        // mass() = base + ballast + fuel*0.74 (setMass semantics). Seed the
        // stock baseline exactly like the real flow ("call after
        // loadVehicleParams"), then verify the bridge as exact deltas.
        veh.setMass(base.massKg);
        const double massStock = veh.mass();

        ups.selectById(UpgradeCategory::Engine, "stage_1");
        applyUpgradesToVehicleSimulator(veh, base, ups);
        KS_CHECK_NEAR(veh.mass(), massStock + 5.0, 1e-6);  // +5 kg
        KS_CHECK_NEAR(veh.enginePowerKw(), 294.0, 1e-3);   // (260+20)*1.05
        KS_CHECK_NEAR(veh.dragCoeff(), 0.35, 1e-6);        // untouched
        KS_CHECK_NEAR(veh.aero().getConfig().liftCoefficient, liftBefore, 1e-6);

        // Re-applying another level works off the stored baseline again
        // (no stacking on the already-modified state).
        ups.selectById(UpgradeCategory::Engine, "stage_2");
        applyUpgradesToVehicleSimulator(veh, base, ups);
        KS_CHECK_NEAR(veh.mass(), massStock, 1e-6);        // baseline restored
        KS_CHECK_NEAR(veh.enginePowerKw(), 305.0, 1e-3);   // 260+45
        KS_CHECK_NEAR(veh.dragCoeff(), 0.35 * 0.95, 1e-6); // CdMult
    }

    std::remove(teamIni.string().c_str());
    std::remove(catIni.string().c_str());
    return KS_TEST_RESULT("team_upgrade_bridge_test");
}
