/**
 * Fase 1 exit — "sessione practice da giocatore": the SINGLE PLAYER session
 * rows start a session only once a car AND a track are chosen, say what is
 * missing otherwise (and are skipped by keyboard navigation while gated),
 * and report the picked mode through onStartDrivingRequested.
 *
 * Compiles the GameMenuOverlay TU directly (precedent: track_loader_test).
 */
#include "KsTest.h"
#include "simulator/GameMenuOverlay.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using ks::sim::GameMenuOverlay;
using ks::sim::GameSessionMode;
using ks::sim::MenuItem;
using ks::sim::MenuState;

namespace {
// Row order built by buildSingleplayerMenu(): the eight singleplayer.modes
// entries of the config, in that order, then RESULTS.
constexpr int kCar = 0, kTrack = 1, kTeam = 2;
constexpr int kPractice = 4, kHotlap = 5, kTimeAttack = 6, kWeekend = 7,
              kChampionship = 8, kQuickRace = 9, kDrift = 10, kDragRace = 11,
              kResults = 12;
constexpr int kModes[] = {kPractice, kHotlap, kTimeAttack, kWeekend,
                          kChampionship, kQuickRace, kDrift, kDragRace};
}

int main() {
    GameMenuOverlay menu;
    menu.setVisible(true);
    KS_CHECK(menu.state() == MenuState::Main);

    // MAIN -> SINGLE PLAYER (first row, ENTER).
    KS_CHECK(menu.handleKeyPress(0x0D));
    KS_CHECK(menu.state() == MenuState::Singleplayer);

    GameSessionMode picked = GameSessionMode::Replay; // sentinel: untouched
    int calls = 0;
    ks::sim::SessionStartParams chosen;
    menu.onStartDrivingRequested = [&](GameSessionMode m, const ks::sim::SessionStartParams& p) {
        picked = m; ++calls; chosen = p;
    };

    // Nothing selected: the three session rows are disabled and the hint
    // names both missing pieces. The TEAM row never gates anything — no
    // team keeps the legacy identity (roadmap 2.8).
    {
        const std::vector<MenuItem> items = menu.items();
        KS_CHECK(items.size() > kResults);
        for (int row : kModes) KS_CHECK(!items[row].enabled);
        KS_CHECK(items[kResults].enabled); // results never gated
        KS_CHECK(items[kPractice].description == "Select a car and a track first");
        KS_CHECK(items[kTeam].enabled);
        KS_CHECK(items[kTeam].description == "Select a team (optional)");
    }

    // ENTER on a gated row must do nothing (no callback, menu stays open).
    menu.setSelectedIndex(kPractice);
    KS_CHECK(menu.handleKeyPress(0x0D));
    KS_CHECK(calls == 0);
    KS_CHECK(menu.isVisible());

    // Keyboard navigation skips gated rows: from TEAM, DOWN lands on
    // RESULTS, not PRACTICE.
    menu.setSelectedIndex(kTeam);
    KS_CHECK(menu.handleKeyPress(0x28)); // DOWN
    KS_CHECK(menu.selectedIndex() == kResults);

    // Track only -> still gated, hint names the car.
    menu.setTrackName("monza");
    {
        const std::vector<MenuItem> items = menu.items();
        KS_CHECK(!items[kPractice].enabled);
        KS_CHECK(items[kPractice].description == "Select a car first");
    }

    // Car only (track still set) -> ready: original descriptions back. The
    // team name surfaces on its row when one is loaded.
    menu.setCarName("gt3");
    menu.setTeamName("ksim_racing");
    {
        const std::vector<MenuItem> items = menu.items();
        for (int row : kModes) KS_CHECK(items[row].enabled);
        KS_CHECK(items[kPractice].description == "Open session on the selected circuit");
        KS_CHECK(items[kHotlap].description == "Flying laps on your own");
        KS_CHECK(items[kTimeAttack].description == "Clean laps against the clock");
        KS_CHECK(items[kWeekend].description == "Practice, qualifying and race");
        KS_CHECK(items[kChampionship].description == "A season across circuits");
        KS_CHECK(items[kQuickRace].description == "Grid start against AI");
        KS_CHECK(items[kDrift].description == "Score runs on the drift pad");
        KS_CHECK(items[kDragRace].description == "Standing start, quarter mile");
        KS_CHECK(items[kCar].description == "gt3"); // selection surfaced
        KS_CHECK(items[kTrack].description == "monza");
        KS_CHECK(items[kTeam].description == "ksim_racing");
    }

    // Each row opens MODE SETUP for its own mode instead of starting straight
    // away; START SESSION reports the mode together with the values shown.
    const GameSessionMode rowModes[] = {
        GameSessionMode::Practice, GameSessionMode::Hotlap, GameSessionMode::TimeAttack,
        GameSessionMode::Weekend, GameSessionMode::Championship, GameSessionMode::Race,
        GameSessionMode::Drift, GameSessionMode::DragRace,
    };
    int expected = 0;
    for (int i = 0; i < 8; ++i) {
        menu.setVisible(true);
        if (menu.state() != ks::sim::MenuState::Singleplayer) {
            KS_CHECK(menu.handleKeyPress(0x1B)); // BACK out of MODE SETUP
            KS_CHECK(menu.state() == ks::sim::MenuState::Singleplayer);
        }
        menu.setSelectedIndex(kModes[i]);
        KS_CHECK(menu.handleKeyPress(0x0D)); // opens MODE SETUP
        KS_CHECK(menu.state() == ks::sim::MenuState::ModeConfig);
        int startRow = -1;
        const std::vector<MenuItem> setup = menu.items();
        for (size_t r = 0; r < setup.size(); ++r)
            if (setup[r].text.rfind("START SESSION", 0) == 0) startRow = static_cast<int>(r);
        KS_CHECK(startRow >= 0);
        menu.setSelectedIndex(startRow);
        KS_CHECK(menu.handleKeyPress(0x0D));
        KS_CHECK(calls == ++expected);
        KS_CHECK(picked == rowModes[i]);
        KS_CHECK(!menu.isVisible());
        KS_CHECK(chosen.totalLaps == ks::sim::defaultsForMode(rowModes[i]).totalLaps);
        KS_CHECK(chosen.aiCars == ks::sim::defaultsForMode(rowModes[i]).aiCars);
    }

    // The rows edit the values the session starts with, and BACK from MODE
    // SETUP returns to the mode list without starting anything.
    {
        const int before = calls;
        menu.setVisible(true);
        KS_CHECK(menu.handleKeyPress(0x1B));
        KS_CHECK(menu.state() == ks::sim::MenuState::Singleplayer);
        menu.setSelectedIndex(kPractice);
        KS_CHECK(menu.handleKeyPress(0x0D));
        KS_CHECK(menu.state() == ks::sim::MenuState::ModeConfig);
        const int laps = ks::sim::defaultsForMode(GameSessionMode::Practice).totalLaps;
        const std::vector<MenuItem> setupRows = menu.items();
        int plus = -1;
        for (size_t r = 0; r < setupRows.size(); ++r)
            if (setupRows[r].text.rfind("LAPS  +1", 0) == 0) plus = static_cast<int>(r);
        KS_CHECK(plus >= 0);
        menu.setSelectedIndex(plus);
        KS_CHECK(menu.handleKeyPress(0x0D));
        KS_CHECK(menu.handleKeyPress(0x0D)); // two more laps
        const std::vector<MenuItem> edited = menu.items();
        KS_CHECK(edited[static_cast<size_t>(plus)].text == "LAPS  +1"); // cursor kept
        int startRow = -1;
        for (size_t r = 0; r < edited.size(); ++r)
            if (edited[r].text.rfind("START SESSION", 0) == 0) startRow = static_cast<int>(r);
        KS_CHECK(startRow >= 0);
        menu.setSelectedIndex(startRow);
        KS_CHECK(menu.handleKeyPress(0x0D));
        KS_CHECK(calls == before + 1);
        KS_CHECK(chosen.totalLaps == laps + 2);
        KS_CHECK(menu.state() == ks::sim::MenuState::ModeConfig);
        menu.setVisible(true);
        KS_CHECK(menu.handleKeyPress(0x1B));
        KS_CHECK(menu.state() == ks::sim::MenuState::Singleplayer);
    }

    // A config that lists its own modes decides the rows: two entries mean two
    // session rows, in that order, with RESULTS right after them.
    {
        GameMenuOverlay configured;
        configured.setSingleplayerModes({GameSessionMode::Drift, GameSessionMode::Practice});
        configured.setCarName("gt3");
        configured.setTrackName("monza");
        configured.setVisible(true);
        KS_CHECK(configured.handleKeyPress(0x0D)); // MAIN -> SINGLE PLAYER
        const std::vector<MenuItem> rows = configured.items();
        // car, track, team, separator, two modes, results, separator, back
        KS_CHECK(rows.size() == 9);
        KS_CHECK(rows[4].description == "Score runs on the drift pad");
        KS_CHECK(rows[5].description == "Open session on the selected circuit");
        KS_CHECK(rows[6].enabled); // RESULTS follows the two configured rows
    }

    // singleplayer.defaults overrides the built-in table field by field: the
    // laps land, the fields the entry omits keep their built-in value, another
    // mode is untouched, and an unknown name is reported instead of applied.
    {
        ks::sim::clearSessionDefaultsOverride();
        ks::engine::json::Value block;
        std::string error;
        KS_CHECK(ks::engine::json::parse(
            "{\"Practice\":{\"laps\":20,\"aiCars\":3},\"Endurance\":{\"laps\":5}}",
            block, &error));
        const std::vector<std::string> unknown = ks::sim::applySessionDefaults(block);
        KS_CHECK(unknown.size() == 1);
        KS_CHECK(unknown[0] == "Endurance");
        const ks::sim::SessionStartParams practice = ks::sim::defaultsForMode(GameSessionMode::Practice);
        KS_CHECK(practice.totalLaps == 20);
        KS_CHECK(practice.aiCars == 3);
        KS_CHECK(practice.sessionTimeSeconds == 0); // built-in, not zeroed by the entry
        KS_CHECK(!practice.startInGarage);
        KS_CHECK(ks::sim::defaultsForMode(GameSessionMode::Hotlap).totalLaps == 0);
        KS_CHECK(ks::sim::defaultsForMode(GameSessionMode::Race).totalLaps == 10);

        // MODE SETUP opens on the overridden values, and START hands them on.
        const int before = calls;
        menu.setVisible(true);
        if (menu.state() != ks::sim::MenuState::Singleplayer)
            KS_CHECK(menu.handleKeyPress(0x1B));
        menu.setSelectedIndex(kPractice);
        KS_CHECK(menu.handleKeyPress(0x0D));
        int startRow = -1;
        const std::vector<MenuItem> setup = menu.items();
        for (size_t r = 0; r < setup.size(); ++r)
            if (setup[r].text.rfind("START SESSION", 0) == 0) startRow = static_cast<int>(r);
        KS_CHECK(startRow >= 0);
        menu.setSelectedIndex(startRow);
        KS_CHECK(menu.handleKeyPress(0x0D));
        KS_CHECK(calls == before + 1);
        KS_CHECK(chosen.totalLaps == 20);
        KS_CHECK(chosen.aiCars == 3);


        // The two files in the order the app reads them: the later one
        // overrides only the fields it carries, the earlier values survive.
        ks::sim::clearSessionDefaultsOverride();
        ks::engine::json::Value systemBlock;
        KS_CHECK(ks::engine::json::parse(
            "{\"Race\":{\"laps\":8,\"aiCars\":2,\"clockSeconds\":600,\"startInGarage\":true}}",
            systemBlock, &error));
        KS_CHECK(ks::sim::applySessionDefaults(systemBlock).empty());
        ks::engine::json::Value userBlock;
        KS_CHECK(ks::engine::json::parse(
            "{\"Race\":{\"laps\":4},\"Practice\":{\"aiCars\":1}}", userBlock, &error));
        KS_CHECK(ks::sim::applySessionDefaults(userBlock).empty());
        const ks::sim::SessionStartParams merged = ks::sim::defaultsForMode(GameSessionMode::Race);
        KS_CHECK(merged.totalLaps == 4);          // from the user file
        KS_CHECK(merged.aiCars == 2);             // kept from the system file
        KS_CHECK(merged.sessionTimeSeconds == 600);
        KS_CHECK(merged.startInGarage);
        KS_CHECK(ks::sim::defaultsForMode(GameSessionMode::Practice).aiCars == 1);
        // A rolling race is a real choice: the flag reaches the loop, which
        // skips the countdown, and the screen has a row for it.
        ks::sim::clearSessionDefaultsOverride();
        ks::engine::json::Value rollBlock;
        KS_CHECK(ks::engine::json::parse("{\"Race\":{\"standingStart\":false}}",
                                        rollBlock, &error));
        KS_CHECK(ks::sim::applySessionDefaults(rollBlock).empty());
        KS_CHECK(!ks::sim::defaultsForMode(GameSessionMode::Race).standingStart);
        KS_CHECK(ks::sim::defaultsForMode(GameSessionMode::Hotlap).standingStart);

        const int beforeRoll = calls;
        menu.setVisible(true);
        if (menu.state() != ks::sim::MenuState::Singleplayer)
            KS_CHECK(menu.handleKeyPress(0x1B));
        menu.setSelectedIndex(kQuickRace);
        KS_CHECK(menu.handleKeyPress(0x0D));
        int standingRow = -1, startRowB = -1;
        const std::vector<MenuItem> rowsB = menu.items();
        for (size_t r = 0; r < rowsB.size(); ++r) {
            if (rowsB[r].text.rfind("STANDING START", 0) == 0) standingRow = static_cast<int>(r);
            if (rowsB[r].text.rfind("START SESSION", 0) == 0) startRowB = static_cast<int>(r);
        }
        KS_CHECK(standingRow >= 0 && startRowB >= 0);
        KS_CHECK(rowsB[static_cast<size_t>(standingRow)].text == "STANDING START  [OFF]");
        menu.setSelectedIndex(standingRow);
        KS_CHECK(menu.handleKeyPress(0x0D)); // OFF -> ON
        menu.setSelectedIndex(startRowB);
        KS_CHECK(menu.handleKeyPress(0x0D));
        KS_CHECK(calls == beforeRoll + 1);
        KS_CHECK(chosen.standingStart);
        ks::sim::clearSessionDefaultsOverride();

        // A mode the user file never mentions keeps the system entry whole.
        KS_CHECK(ks::sim::defaultsForMode(GameSessionMode::Hotlap).aiCars == 0);

        ks::sim::clearSessionDefaultsOverride();
        KS_CHECK(ks::sim::defaultsForMode(GameSessionMode::Practice).totalLaps == 0);
    }


    // CARS page filters come from the metadata the car folders really carry:
    // with ui_car.json the CLASS/YEAR rows appear and filter the list, with
    // none the page stays exactly as it was.
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path root = fs::temp_directory_path() / "ksim_cars_meta";
        const fs::path bare = fs::temp_directory_path() / "ksim_cars_bare";
        fs::remove_all(root, ec);
        fs::remove_all(bare, ec);
        fs::create_directories(root / "alpha", ec);
        fs::create_directories(root / "beta", ec);
        fs::create_directories(bare / "plain", ec);
        {
            std::ofstream meta(root / "alpha" / "ui_car.json");
            meta << "{ \"name\": \"Alpha GT\", \"brand\": \"Ks\", \"class\": \"GT3\", "
                    "\"year\": 2024 }";
        }

        GameMenuOverlay cars;
        cars.openCarSelect(root.string());
        std::vector<MenuItem> rows = cars.items();
        KS_CHECK(rows.size() > 3);
        KS_CHECK(rows[0].text == "CLASS  any");
        KS_CHECK(rows[1].text == "YEAR  any");
        KS_CHECK(rows[2].isSeparator);
        KS_CHECK(rows[3].text == "Alpha GT");
        KS_CHECK(rows[3].description == "Ks - GT3 - 2024");
        KS_CHECK(rows[4].text == "beta"); // no metadata: folder name, no detail

        // GT3 keeps alpha only: beta declares no class and is filtered out.
        cars.setSelectedIndex(0);
        KS_CHECK(cars.handleKeyPress(0x0D));
        rows = cars.items();
        KS_CHECK(rows[0].text == "CLASS  GT3");
        int listed = 0;
        for (const auto& r : rows)
            if (r.text == "Alpha GT" || r.text == "beta") ++listed;
        KS_CHECK(listed == 1);

        // Back to any brings both back.
        KS_CHECK(cars.handleKeyPress(0x0D)); // GT3 -> any (one class)
        rows = cars.items();
        KS_CHECK(rows[0].text == "CLASS  any");
        listed = 0;
        for (const auto& r : rows)
            if (r.text == "Alpha GT" || r.text == "beta") ++listed;
        KS_CHECK(listed == 2);

        // A library with no metadata never shows a filter it cannot fill.
        GameMenuOverlay plain;
        plain.openCarSelect(bare.string());
        const std::vector<MenuItem> plainRows = plain.items();
        KS_CHECK(plainRows[0].text == "plain");
        KS_CHECK(plainRows[1].isSeparator || plainRows[1].text == "BACK");

        fs::remove_all(root, ec);
        fs::remove_all(bare, ec);
    }

    // The strict parser a config goes through: known names map, an unknown one
    // is refused and leaves the caller's mode alone (KS_SESSION still races).
    {
        ks::sim::GameSessionMode mode = GameSessionMode::Replay;
        KS_CHECK(ks::sim::tryModeFromMenuEntry("HotLap", &mode) &&
                 mode == GameSessionMode::Hotlap);
        KS_CHECK(ks::sim::tryModeFromMenuEntry("DragRace", &mode) &&
                 mode == GameSessionMode::DragRace);
        KS_CHECK(ks::sim::tryModeFromMenuEntry("Weekend", &mode) &&
                 mode == GameSessionMode::Weekend);
        KS_CHECK(!ks::sim::tryModeFromMenuEntry("Endurance", &mode));
        KS_CHECK(mode == GameSessionMode::Weekend); // untouched on refusal
        KS_CHECK(ks::sim::modeFromMenuEntry("Endurance") == GameSessionMode::Race);
    }

    return KS_TEST_RESULT("menu_session_gate_test");
}
