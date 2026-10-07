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
#include <vector>

using ks::sim::GameMenuOverlay;
using ks::sim::GameSessionMode;
using ks::sim::MenuItem;
using ks::sim::MenuState;

namespace {
// Row order built by buildSingleplayerMenu().
constexpr int kCar = 0, kTrack = 1, kPractice = 3, kQuickRace = 4,
              kTimeAttack = 5, kResults = 6;
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
    // names both missing pieces.
    {
        const std::vector<MenuItem> items = menu.items();
        KS_CHECK(items.size() > kResults);
        KS_CHECK(!items[kPractice].enabled);
        KS_CHECK(!items[kQuickRace].enabled);
        KS_CHECK(!items[kTimeAttack].enabled);
        KS_CHECK(items[kResults].enabled); // results never gated
        KS_CHECK(items[kPractice].description == "Select a car and a track first");
    }

    // ENTER on a gated row must do nothing (no callback, menu stays open).
    menu.setSelectedIndex(kPractice);
    KS_CHECK(menu.handleKeyPress(0x0D));
    KS_CHECK(calls == 0);
    KS_CHECK(menu.isVisible());

    // Keyboard navigation skips gated rows: from TRACK, DOWN lands on
    // RESULTS, not PRACTICE.
    menu.setSelectedIndex(kTrack);
    KS_CHECK(menu.handleKeyPress(0x28)); // DOWN
    KS_CHECK(menu.selectedIndex() == kResults);

    // Track only -> still gated, hint names the car.
    menu.setTrackName("monza");
    {
        const std::vector<MenuItem> items = menu.items();
        KS_CHECK(!items[kPractice].enabled);
        KS_CHECK(items[kPractice].description == "Select a car first");
    }

    // Car only (track still set) -> ready: original descriptions back.
    menu.setCarName("gt3");
    {
        const std::vector<MenuItem> items = menu.items();
        KS_CHECK(items[kPractice].enabled);
        KS_CHECK(items[kQuickRace].enabled);
        KS_CHECK(items[kTimeAttack].enabled);
        KS_CHECK(items[kPractice].description == "Open session on the selected circuit");
        KS_CHECK(items[kQuickRace].description == "Grid start against AI");
        KS_CHECK(items[kTimeAttack].description == "Clean laps against the clock");
        KS_CHECK(items[kCar].description == "gt3"); // selection surfaced
        KS_CHECK(items[kTrack].description == "monza");
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

    return KS_TEST_RESULT("menu_session_gate_test");
}
