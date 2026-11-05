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
    menu.onStartDrivingRequested = [&](GameSessionMode m) { picked = m; ++calls; };

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

    // Each row reports its own mode and closes the menu.
    menu.setSelectedIndex(kPractice);
    KS_CHECK(menu.handleKeyPress(0x0D));
    KS_CHECK(calls == 1);
    KS_CHECK(picked == GameSessionMode::Practice);
    KS_CHECK(!menu.isVisible());

    menu.setVisible(true); // state stays Singleplayer after a start
    menu.setSelectedIndex(kQuickRace);
    KS_CHECK(menu.handleKeyPress(0x0D));
    KS_CHECK(calls == 2);
    KS_CHECK(picked == GameSessionMode::Race);

    menu.setVisible(true);
    menu.setSelectedIndex(kTimeAttack);
    KS_CHECK(menu.handleKeyPress(0x0D));
    KS_CHECK(calls == 3);
    KS_CHECK(picked == GameSessionMode::TimeAttack);
    KS_CHECK(!menu.isVisible());

    // The five modes added after the original three report their own mode too.
    const std::pair<int, GameSessionMode> extra[] = {
        {kHotlap, GameSessionMode::Hotlap},
        {kWeekend, GameSessionMode::Weekend},
        {kChampionship, GameSessionMode::Championship},
        {kDrift, GameSessionMode::Drift},
        {kDragRace, GameSessionMode::DragRace},
    };
    int expected = 3;
    for (const auto& e : extra) {
        menu.setVisible(true);
        menu.setSelectedIndex(e.first);
        KS_CHECK(menu.handleKeyPress(0x0D));
        KS_CHECK(calls == ++expected);
        KS_CHECK(picked == e.second);
        KS_CHECK(!menu.isVisible());
    }

    return KS_TEST_RESULT("menu_session_gate_test");
}
