/**
 * Game menu (ksim) — navigation end-to-end: every screen reachable from MAIN
 * is entered, checked and left with ESC back to its parent, and the shared
 * invariants hold on all of them (no screen opens on a separator or a disabled
 * row, rows carry text, separators are never doubled, section titles exist,
 * DOWN/UP cycling only ever lands on enabled non-separator rows).
 *
 * What it adds over menu_session_gate_test / pit_strategy_menu_test: those
 * pin the SINGLE PLAYER gate and the pit strategy screen. This one walks the
 * whole graph — MULTI PLAYER, DRIVER, REPLAY, CONTENT, SETTINGS -> CONTROLS,
 * GARAGE -> PIT STRATEGY, the three select screens, RESULTS, EDITOR and QUIT —
 * so a screen that loses its rows, its BACK path or its callback wiring fails
 * here instead of only on screen.
 *
 * Compiles the GameMenuOverlay TU directly (menu_session_gate_test pattern).
 */
#include "simulator/ui/NativeUiHub.h"
#include "KsTest.h"
#include "simulator/GameMenuOverlay.h"

#include <string>
#include <vector>

using ks::sim::GameMenuOverlay;
using ks::sim::MenuItem;
using ks::sim::MenuState;
using ks::sim::ResultsRow;

namespace {

constexpr int KEY_UP = 0x26;
constexpr int KEY_DOWN = 0x28;
constexpr int KEY_ENTER = 0x0D;
constexpr int KEY_ESC = 0x1B;

int rowOf(const GameMenuOverlay& menu, const std::string& text) {
    const std::vector<MenuItem> items = menu.items();
    for (int i = 0; i < static_cast<int>(items.size()); ++i)
        if (items[i].text == text) return i;
    return -1;
}

/** Invariants every screen has to satisfy, whatever it contains. */
void checkScreen(const GameMenuOverlay& menu, const char* where) {
    const std::vector<MenuItem> items = menu.items();
    if (items.empty()) {
        KS_CHECK(false); // a screen with no rows is a dead end
        return;
    }
    // Never opens on a separator or a disabled row.
    const int sel = menu.selectedIndex();
    if (sel >= 0 && sel < static_cast<int>(items.size())) {
        KS_CHECK(!items[sel].isSeparator);
        KS_CHECK(items[sel].enabled);
    } else {
        KS_CHECK(false); // selection out of range
    }
    // Every row has text, separators never come first or double up.
    bool previousWasSeparator = true;
    for (const auto& item : items) {
        if (item.isSeparator) {
            KS_CHECK(!previousWasSeparator);
            previousWasSeparator = true;
        } else {
            KS_CHECK(!item.text.empty());
            previousWasSeparator = false;
        }
    }
    KS_CHECK(!menu.sectionTitle().empty());
    (void)where;
}

/** DOWN cycling from the current row must only ever stop on usable rows. */
void checkCycle(GameMenuOverlay& menu, int key, const char* where) {
    const int rows = static_cast<int>(menu.items().size());
    for (int i = 0; i < rows * 2 + 2; ++i) {
        KS_CHECK(menu.handleKeyPress(key));
        const auto items = menu.items();
        const int sel = menu.selectedIndex();
        KS_CHECK(sel >= 0 && sel < static_cast<int>(items.size()));
        if (sel < 0 || sel >= static_cast<int>(items.size())) return;
        KS_CHECK(!items[sel].isSeparator);
        KS_CHECK(items[sel].enabled);
    }
    (void)where;
}

/** MAIN row text -> the screen ENTER has to open. */
struct MainRow {
    const char* text;
    MenuState state;
};

} // namespace

int main() {
    // ---------------------------------------------------------------------
    // MAIN: the full row set, separators included, opens visible on row 0.
    // ---------------------------------------------------------------------
    GameMenuOverlay fresh;
    KS_CHECK(fresh.isVisible());
    KS_CHECK(fresh.state() == MenuState::Main);
    KS_CHECK(fresh.sectionTitle() == "MAIN MENU");
    {
        const std::vector<MenuItem> items = fresh.items();
        int separators = 0;
        for (const auto& item : items) separators += item.isSeparator ? 1 : 0;
        KS_CHECK(separators == 2);
        KS_CHECK(rowOf(fresh, "SINGLE PLAYER") >= 0);
        KS_CHECK(rowOf(fresh, "MULTI PLAYER") >= 0);
        KS_CHECK(rowOf(fresh, "GARAGE") >= 0);
        KS_CHECK(rowOf(fresh, "REPLAY") >= 0);
        KS_CHECK(rowOf(fresh, "CONTENT") >= 0);
        KS_CHECK(rowOf(fresh, "DRIVER") >= 0);
        KS_CHECK(rowOf(fresh, "SETTINGS") >= 0);
        KS_CHECK(rowOf(fresh, "EDITOR") >= 0);
        KS_CHECK(rowOf(fresh, "QUIT") >= 0);
        // The first row is the default selection and it is usable.
        KS_CHECK(fresh.selectedIndex() == 0);
    }
    checkScreen(fresh, "main");
    checkCycle(fresh, KEY_DOWN, "main down");
    checkCycle(fresh, KEY_UP, "main up");

    // ---------------------------------------------------------------------
    // Every MAIN row opens its screen and ESC brings the menu back to MAIN.
    // ---------------------------------------------------------------------
    const MainRow graph[] = {
        {"SINGLE PLAYER", MenuState::Singleplayer},
        {"MULTI PLAYER", MenuState::Multiplayer},
        {"GARAGE", MenuState::Garage},
        {"REPLAY", MenuState::Replay},
        {"CONTENT", MenuState::ContentManager},
        {"DRIVER", MenuState::Profile},
        {"SETTINGS", MenuState::Settings},
        {"EDITOR", MenuState::DevModeConfirm},
        {"QUIT", MenuState::QuitConfirm},
    };
    for (const MainRow& row : graph) {
        GameMenuOverlay menu;
        menu.setVisible(true);
        const int index = rowOf(menu, row.text);
        KS_CHECK(index >= 0);
        if (index < 0) continue;
        menu.setSelectedIndex(index);
        KS_CHECK(menu.handleKeyPress(KEY_ENTER));
        KS_CHECK(menu.state() == row.state);
        KS_CHECK(menu.isVisible());
        checkScreen(menu, row.text);
        checkCycle(menu, KEY_DOWN, row.text);
        checkCycle(menu, KEY_UP, row.text);
        // ESC walks back to MAIN from every one of these screens.
        KS_CHECK(menu.handleKeyPress(KEY_ESC));
        KS_CHECK(menu.state() == MenuState::Main);
        KS_CHECK(menu.isVisible());
    }

    // ---------------------------------------------------------------------
    // Callsbacks the MAIN rows hand over to the app (nothing here touches
    // content, so it runs on any machine).
    // ---------------------------------------------------------------------
    {
        // MULTI PLAYER -> HOST SESSION.
        GameMenuOverlay menu;
        int hosted = 0;
        menu.onHostServerRequested = [&]() { ++hosted; };
        menu.setSelectedIndex(rowOf(menu, "MULTI PLAYER"));
        KS_CHECK(menu.handleKeyPress(KEY_ENTER));
        const int host = rowOf(menu, "HOST SESSION");
        KS_CHECK(host >= 0);
        if (host >= 0) {
            menu.setSelectedIndex(host);
            KS_CHECK(menu.handleKeyPress(KEY_ENTER));
            KS_CHECK(hosted == 1);
        }
    }
    {
        // DRIVER -> NAME opens the text input for that field.
        GameMenuOverlay menu;
        std::string field;
        int inputs = 0;
        menu.onTextInputRequested = [&](const std::string& f, const std::string&) {
            field = f;
            ++inputs;
        };
        menu.setSelectedIndex(rowOf(menu, "DRIVER"));
        KS_CHECK(menu.handleKeyPress(KEY_ENTER));
        const int name = rowOf(menu, "NAME");
        KS_CHECK(name >= 0);
        if (name >= 0) {
            menu.setSelectedIndex(name);
            KS_CHECK(menu.handleKeyPress(KEY_ENTER));
            KS_CHECK(inputs == 1);
            KS_CHECK(field == "NAME");
        }
    }
    {
        // SETTINGS -> CONTROLS -> ESC lands back on SETTINGS, not MAIN.
        GameMenuOverlay menu;
        std::string panel;
        int panels = 0;
        menu.onOpenSettingsPanelRequested = [&](const std::string& p) {
            panel = p;
            ++panels;
        };
        menu.setSelectedIndex(rowOf(menu, "SETTINGS"));
        KS_CHECK(menu.handleKeyPress(KEY_ENTER));
        KS_CHECK(menu.state() == MenuState::Settings);
        // SETTINGS -> CONTROLS is a screen switch (no callback of its own);
        // the CONTROLS screen is what reports a panel.
        const int controls = rowOf(menu, "CONTROLS");
        KS_CHECK(controls >= 0);
        if (controls >= 0) {
            menu.setSelectedIndex(controls);
            KS_CHECK(menu.handleKeyPress(KEY_ENTER));
            KS_CHECK(menu.state() == MenuState::Controls);
            KS_CHECK(panels == 0);
            checkScreen(menu, "controls");
            const int keyboard = rowOf(menu, "KEYBOARD");
            KS_CHECK(keyboard >= 0);
            if (keyboard >= 0) {
                menu.setSelectedIndex(keyboard);
                KS_CHECK(menu.handleKeyPress(KEY_ENTER));
                KS_CHECK(panels == 1);
                KS_CHECK(panel == "keyboard");
            }
            KS_CHECK(menu.handleKeyPress(KEY_ESC));
            KS_CHECK(menu.state() == MenuState::Settings);
            KS_CHECK(menu.handleKeyPress(KEY_ESC));
            KS_CHECK(menu.state() == MenuState::Main);
        }
    }
    {
        // GARAGE -> PIT STRATEGY -> ESC lands back on GARAGE.
        GameMenuOverlay menu;
        int opened = 0;
        menu.onOpenPitStrategyRequested = [&]() { ++opened; };
        menu.setSelectedIndex(rowOf(menu, "GARAGE"));
        KS_CHECK(menu.handleKeyPress(KEY_ENTER));
        const int pit = rowOf(menu, "PIT STRATEGY");
        KS_CHECK(pit >= 0);
        if (pit >= 0) {
            menu.setSelectedIndex(pit);
            KS_CHECK(menu.handleKeyPress(KEY_ENTER));
            KS_CHECK(opened == 1);
            KS_CHECK(menu.state() == MenuState::PitStrategy);
            KS_CHECK(menu.handleKeyPress(KEY_ESC));
            KS_CHECK(menu.state() == MenuState::Garage);
        }
    }
    {
        // QUIT: CONFIRM reports the exit, CANCEL just returns to MAIN.
        GameMenuOverlay menu;
        int exits = 0;
        menu.onExitRequested = [&]() { ++exits; };
        menu.setSelectedIndex(rowOf(menu, "QUIT"));
        KS_CHECK(menu.handleKeyPress(KEY_ENTER));
        KS_CHECK(menu.state() == MenuState::QuitConfirm);
        const int cancel = rowOf(menu, "CANCEL");
        KS_CHECK(cancel >= 0);
        if (cancel >= 0) {
            menu.setSelectedIndex(cancel);
            KS_CHECK(menu.handleKeyPress(KEY_ENTER));
            KS_CHECK(exits == 0);
            KS_CHECK(menu.state() == MenuState::Main);
        }
        menu.setSelectedIndex(rowOf(menu, "QUIT"));
        KS_CHECK(menu.handleKeyPress(KEY_ENTER));
        const int confirm = rowOf(menu, "CONFIRM");
        KS_CHECK(confirm >= 0);
        if (confirm >= 0) {
            menu.setSelectedIndex(confirm);
            KS_CHECK(menu.handleKeyPress(KEY_ENTER));
            KS_CHECK(exits == 1);
        }
    }
    {
        // EDITOR: same shape, through onDevModeRequested.
        GameMenuOverlay menu;
        int dev = 0;
        menu.onDevModeRequested = [&]() { ++dev; };
        menu.setSelectedIndex(rowOf(menu, "EDITOR"));
        KS_CHECK(menu.handleKeyPress(KEY_ENTER));
        KS_CHECK(menu.state() == MenuState::DevModeConfirm);
        const int confirm = rowOf(menu, "CONFIRM");
        KS_CHECK(confirm >= 0);
        if (confirm >= 0) {
            menu.setSelectedIndex(confirm);
            KS_CHECK(menu.handleKeyPress(KEY_ENTER));
            KS_CHECK(dev == 1);
        }
    }

    // ---------------------------------------------------------------------
    // Select screens: they open from SINGLE PLAYER and ESC returns there.
    // The lists come from content/ (possibly empty on this machine): either
    // way the screen has its rows, a usable selection and a BACK path.
    // ---------------------------------------------------------------------
    const char* selectRows[] = {"CAR", "TRACK"};
    const MenuState selectStates[] = {MenuState::CarSelect, MenuState::TrackSelect};
    for (int i = 0; i < 2; ++i) {
        GameMenuOverlay menu;
        menu.setSelectedIndex(rowOf(menu, "SINGLE PLAYER"));
        KS_CHECK(menu.handleKeyPress(KEY_ENTER));
        KS_CHECK(menu.state() == MenuState::Singleplayer);
        const int row = rowOf(menu, selectRows[i]);
        KS_CHECK(row >= 0);
        if (row < 0) continue;
        menu.setSelectedIndex(row);
        KS_CHECK(menu.handleKeyPress(KEY_ENTER));
        KS_CHECK(menu.state() == selectStates[i]);
        checkScreen(menu, selectRows[i]);
        checkCycle(menu, KEY_DOWN, selectRows[i]);
        KS_CHECK(menu.handleKeyPress(KEY_ESC));
        KS_CHECK(menu.state() == MenuState::Singleplayer);
    }

    // ---------------------------------------------------------------------
    // RESULTS: showResults() brings the menu back up on the results screen
    // and BACK returns to whoever opened it.
    // ---------------------------------------------------------------------
    {
        GameMenuOverlay menu;
        menu.setVisible(false);
        std::vector<ResultsRow> rows;
        rows.push_back({"1", "Player", "1:32.451"});
        rows.push_back({"2", "AI 1", "+0.8"});
        menu.showResults(std::move(rows));
        KS_CHECK(menu.isVisible());
        KS_CHECK(menu.state() == MenuState::Results);
        checkScreen(menu, "results");
        KS_CHECK(rowOf(menu, "1  Player") >= 0); // position + two spaces + driver
        KS_CHECK(rowOf(menu, "2  AI 1") >= 0);
        KS_CHECK(menu.handleKeyPress(KEY_ESC));
        KS_CHECK(menu.state() == MenuState::Main);
    }

    // ---------------------------------------------------------------------
    // ESC on MAIN closes the menu (it is the pause overlay once driving) and
    // a hidden menu swallows nothing.
    // ---------------------------------------------------------------------
    {
        GameMenuOverlay menu;
        KS_CHECK(menu.isVisible());
        KS_CHECK(menu.handleKeyPress(KEY_ESC));
        KS_CHECK(!menu.isVisible());
        KS_CHECK(!menu.isInputBlocked());
        KS_CHECK(!menu.handleKeyPress(KEY_DOWN));
        KS_CHECK(!menu.handleKeyPress(KEY_ENTER));
        KS_CHECK(!menu.handleKeyPress(KEY_ESC));
    }

    // ---------------------------------------------------------------------
    // Cinematic draw state: render() ramps the fade in while visible and out
    // once hidden — the hub only draws the list while alpha is above 0.01.
    // ---------------------------------------------------------------------
    {
        GameMenuOverlay menu;
        menu.setVisible(false);
        for (int i = 0; i < 30; ++i) menu.render(1920, 1080);
        KS_CHECK(menu.fadeAlpha() <= 0.01f);
        menu.setVisible(true);
        for (int i = 0; i < 30; ++i) menu.render(1920, 1080);
        KS_CHECK(menu.fadeAlpha() >= 0.99f);
        menu.setVisible(false);
        for (int i = 0; i < 30; ++i) menu.render(1920, 1080);
        KS_CHECK(menu.fadeAlpha() <= 0.01f);
    }

    // ---------------------------------------------------------------------
    // In-car overlay gate: the pause screen opens only when the car stands
    // still or the session is singleplayer.
    // ---------------------------------------------------------------------
    {
        KS_CHECK(!ks::sim::inCarMenuAllowed(true, 120.f));   // racing in multiplayer
        KS_CHECK(!ks::sim::inCarMenuAllowed(true, 12.f));
        KS_CHECK(ks::sim::inCarMenuAllowed(true, 0.f));      // parked on the grid
        KS_CHECK(ks::sim::inCarMenuAllowed(false, 240.f));   // singleplayer, flat out

        // The pre-session overlay is never gated, and a refused ESC leaves the
        // menu closed (the hub still consumes the key).
        GameMenuOverlay carMenu;
        carMenu.setVisible(false);
        KS_CHECK(!ks::sim::ui::NativeUiHub::openInCarMenu(carMenu, false));
        KS_CHECK(!carMenu.isVisible());
        KS_CHECK(ks::sim::ui::NativeUiHub::openInCarMenu(carMenu, true));
        KS_CHECK(carMenu.isVisible());
        KS_CHECK(carMenu.state() == MenuState::Pause); // pause screen, not MAIN
    }

    // ---------------------------------------------------------------------
    // In-car pause overlay: its own rows (resume / restart / garage /
    // settings / quit), ESC resumes the car, and screens it opens come back
    // to it instead of the pre-session MAIN menu.
    // ---------------------------------------------------------------------
    {
        GameMenuOverlay menu;
        menu.openPauseMenu();
        KS_CHECK(menu.isVisible());
        KS_CHECK(menu.state() == MenuState::Pause);
        KS_CHECK(rowOf(menu, "RESUME") >= 0);
        KS_CHECK(rowOf(menu, "RESTART SESSION") >= 0);
        KS_CHECK(rowOf(menu, "GARAGE") >= 0);
        KS_CHECK(rowOf(menu, "SETTINGS") >= 0);
        KS_CHECK(rowOf(menu, "QUIT") >= 0);
        // No "pick a car / track" rows: those belong to the pre-session menu.
        KS_CHECK(rowOf(menu, "SINGLE PLAYER") < 0);
        KS_CHECK(rowOf(menu, "MULTI PLAYER") < 0);

        // ESC on the pause screen resumes instead of swallowing the key.
        KS_CHECK(menu.handleKeyPress(KEY_ESC));
        KS_CHECK(!menu.isVisible());

        // GARAGE opened from the pause screen returns to it, not to MAIN.
        menu.openPauseMenu();
        menu.setSelectedIndex(rowOf(menu, "GARAGE"));
        KS_CHECK(menu.handleKeyPress(KEY_ENTER));
        KS_CHECK(menu.state() == MenuState::Garage);
        KS_CHECK(menu.handleKeyPress(KEY_ESC));
        KS_CHECK(menu.state() == MenuState::Pause);
        KS_CHECK(menu.handleKeyPress(KEY_ESC));
        KS_CHECK(!menu.isVisible());

        // RESUME is a plain close, and RESTART SESSION reports the intent.
        int restarts = 0;
        menu.onRestartSessionRequested = [&restarts]() { ++restarts; };
        menu.openPauseMenu();
        menu.setSelectedIndex(rowOf(menu, "RESTART SESSION"));
        KS_CHECK(menu.handleKeyPress(KEY_ENTER));
        KS_CHECK(restarts == 1);
        KS_CHECK(!menu.isVisible());

        menu.openPauseMenu();
        menu.setSelectedIndex(rowOf(menu, "RESUME"));
        KS_CHECK(menu.handleKeyPress(KEY_ENTER));
        KS_CHECK(!menu.isVisible());
        KS_CHECK(restarts == 1); // RESUME never restarts anything
    }

    std::printf("game_menu_navigation: main graph, sub-screens, callbacks, "
                "ESC paths and row invariants OK\n");
    return KS_TEST_RESULT("game_menu_navigation_test");
}
