/**
 * The startup screen (ksim) — the row of tiles the game opens on.
 *
 * The rows are SINGLE PLAYER / MULTIPLAYER / DRIVER / CONTENT / SETTINGS, then
 * EDITOR / CREDITS / EXIT, so eight tiles: five fit across the window and the
 * strip scrolls for the rest.
 *
 * What this pins, and why it is not just arithmetic:
 *
 *  - geometry: the tiles sit between the title bar and the hint bar, they never
 *    overlap each other and none leaves the window. The hub paints from these
 *    rectangles and hit-tests the same ones, so a tile the player can see but
 *    not click is a real failure mode.
 *  - scrolling: a row longer than five tiles keeps the selection inside the
 *    visible window, and a short row never scrolls past its own end.
 *  - navigation: the arrows wrap at both ends, the way a horizontal strip is
 *    expected to.
 *  - content: every tile carries an icon the SVG set can actually draw and a
 *    description to show. A typo in an icon id would draw an empty plate
 *    instead of failing, so it is checked here.
 *  - the hub paths: arrows move the selection, ENTER opens the section, ESC
 *    offers to leave the game rather than hiding the menu the game opens on,
 *    and hovering a tile selects it.
 *
 * Compiles the GameMenuOverlay TU directly (the game_menu_navigation_test
 * pattern), so the tiles are checked against the real rows.
 */
#include "simulator/ui/MainMenu.h"
#include "simulator/ui/NativeUiHub.h"
#include "simulator/GameMenuOverlay.h"
#include "KsTest.h"

#include <cstdio>
#include <string>
#include <vector>

using ks::sim::GameMenuOverlay;
using ks::sim::MenuState;
using ks::sim::ui::MainMenuLayout;
using ks::sim::ui::layoutMainMenu;
using ks::sim::ui::mainMenuStep;
using ks::sim::ui::mainMenuTileAt;

namespace {

constexpr int KEY_LEFT = 0x25;
constexpr int KEY_UP = 0x26;
constexpr int KEY_RIGHT = 0x27;
constexpr int KEY_DOWN = 0x28;
constexpr int KEY_ENTER = 0x0D;
constexpr int KEY_ESC = 0x1B;

int rowOf(const GameMenuOverlay& menu, const std::string& text) {
    const std::vector<ks::sim::MenuItem> items = menu.items();
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        if (items[i].text == text) return i;
    }
    return -1;
}

} // namespace

int main() {
    // ---------------------------------------------------------------------
    // Geometry: five tiles, in order, between the two bars, none overlapping.
    // ---------------------------------------------------------------------
    {
        const MainMenuLayout L = layoutMainMenu(1600, 900, 8, 0);
        KS_CHECK(L.count == MainMenuLayout::kVisible);
        KS_CHECK(L.first == 0);
        for (int i = 0; i < L.count; ++i) {
            KS_CHECK(L.item[i] == i); // the row behind each slot
            KS_CHECK(L.tile[i].w > 0.f && L.tile[i].h > 0.f);
            KS_CHECK(L.tile[i].x >= 0.f);
            KS_CHECK(L.tile[i].x + L.tile[i].w <= 1600.f);
            KS_CHECK(L.tile[i].y > L.topBar.h);                 // under the bar
            KS_CHECK(L.tile[i].y + L.tile[i].h < L.bottomBar.y); // over the hints
            if (i > 0) {
                KS_CHECK(L.tile[i].x >= L.tile[i - 1].x + L.tile[i - 1].w);
            }
        }
        // The section's name and text sit under the row, not on it.
        KS_CHECK(L.title.y >= L.tile[0].y + L.tile[0].h);
        KS_CHECK(L.body.y > L.title.y);
        KS_CHECK(L.body.y + L.body.h <= L.bottomBar.y + 1.f);
        KS_CHECK(L.avatar.x + L.avatar.w <= 1600.f);
    }

    // ---------------------------------------------------------------------
    // Scrolling: the window follows the selection; short rows do not scroll.
    // ---------------------------------------------------------------------
    {
        const MainMenuLayout last = layoutMainMenu(1600, 900, 8, 7);
        KS_CHECK(last.first == 3); // 8 rows, 5 slots: last window starts at 3
        KS_CHECK(last.item[0] == 3);
        KS_CHECK(last.item[last.count - 1] == 7);
        KS_CHECK(layoutMainMenu(1600, 900, 8, 6).first == 3); // 6 - 2, clamped
        KS_CHECK(layoutMainMenu(1600, 900, 8, 0).first == 0);

        const MainMenuLayout shortRow = layoutMainMenu(1280, 720, 3, 2);
        KS_CHECK(shortRow.count == 3);
        KS_CHECK(shortRow.first == 0);
        KS_CHECK(shortRow.item[2] == 2);
        KS_CHECK(shortRow.item[3] == -1); // an unused slot draws nothing
    }

    // ---------------------------------------------------------------------
    // Hit-test: a tile's centre is its row, the gap and the bars are nothing.
    // ---------------------------------------------------------------------
    {
        const MainMenuLayout L = layoutMainMenu(1600, 900, 8, 0);
        for (int i = 0; i < L.count; ++i) {
            KS_CHECK(mainMenuTileAt(L, L.tile[i].x + L.tile[i].w * 0.5f,
                                    L.tile[i].y + L.tile[i].h * 0.5f) == i);
        }
        KS_CHECK(mainMenuTileAt(L, L.tile[0].x + L.tile[0].w + 1.f, L.tile[0].y + 4.f) == -1);
        KS_CHECK(mainMenuTileAt(L, 10.f, 10.f) == -1);
        KS_CHECK(mainMenuTileAt(L, 800.f, 895.f) == -1); // the hint bar
    }

    // ---------------------------------------------------------------------
    // Navigation wraps at both ends.
    // ---------------------------------------------------------------------
    {
        KS_CHECK(mainMenuStep(0, -1, 8) == 7);
        KS_CHECK(mainMenuStep(7, +1, 8) == 0);
        KS_CHECK(mainMenuStep(4, +1, 8) == 5);
        KS_CHECK(mainMenuStep(4, -1, 8) == 3);
        KS_CHECK(mainMenuStep(0, +1, 0) == 0); // empty row: no index to land on
        KS_CHECK(mainMenuStep(7, +1, 3) == 0); // 7 clamps to the last row, then wraps
    }

    // ---------------------------------------------------------------------
    // The rows are the ones the game opens on, in order, each with an icon
    // the SVG set can draw and something to say.
    // ---------------------------------------------------------------------
    {
        GameMenuOverlay menu;
        static const char* kExpected[] = {
            "SINGLE PLAYER", "MULTIPLAYER", "DRIVER", "CONTENT",
            "SETTINGS", "EDITOR", "CREDITS", "EXIT",
        };
        int tiles = 0;
        int separators = 0;
        for (const auto& item : menu.items()) {
            if (item.isSeparator) {
                ++separators;
                continue;
            }
            ++tiles;
            KS_CHECK(item.icon != nullptr);
            KS_CHECK(ks::sim::ui::icons::get(item.icon) != nullptr);
            KS_CHECK(!item.description.empty());
        }
        KS_CHECK(tiles == 8);
        KS_CHECK(separators == 1); // the gap before EDITOR
        for (int i = 0; i < 8; ++i) KS_CHECK(rowOf(menu, kExpected[i]) >= 0);
        KS_CHECK(rowOf(menu, "SINGLE PLAYER") == 0); // the game opens on it
        KS_CHECK(rowOf(menu, "EXIT") == static_cast<int>(menu.items().size()) - 1);
        // The garage and the replay screen are reached elsewhere now.
        KS_CHECK(rowOf(menu, "GARAGE") < 0);
        KS_CHECK(rowOf(menu, "REPLAY") < 0);
    }

    // ---------------------------------------------------------------------
    // CREDITS is a real screen: usable rows, a claimed icon each, ESC back.
    // ---------------------------------------------------------------------
    {
        GameMenuOverlay menu;
        menu.openScreen(MenuState::Credits);
        KS_CHECK(menu.state() == MenuState::Credits);
        KS_CHECK(!menu.sectionTitle().empty());
        KS_CHECK(rowOf(menu, "BACK") >= 0);
        for (const auto& item : menu.items()) {
            if (item.isSeparator) continue;
            KS_CHECK(item.icon != nullptr);
            KS_CHECK(ks::sim::ui::icons::get(item.icon) != nullptr);
        }
        KS_CHECK(menu.handleKeyPress(KEY_ESC));
        KS_CHECK(menu.state() == MenuState::Main);
    }

    // ---------------------------------------------------------------------
    // The hub: arrows walk the row, ENTER opens the section, ESC offers to
    // leave, hovering a tile selects it.
    // ---------------------------------------------------------------------
    {
        ks::sim::ui::NativeUiHub hub;
        hub.resize(1600, 900);
        KS_CHECK(hub.menu().isVisible());
        KS_CHECK(hub.menu().state() == MenuState::Main);
        KS_CHECK(hub.menu().selectedIndex() == rowOf(hub.menu(), "SINGLE PLAYER"));

        KS_CHECK(hub.handleKey(KEY_RIGHT));
        KS_CHECK(hub.menu().selectedIndex() == rowOf(hub.menu(), "MULTIPLAYER"));
        KS_CHECK(hub.handleKey(KEY_DOWN)); // DOWN walks the same row
        KS_CHECK(hub.menu().selectedIndex() == rowOf(hub.menu(), "DRIVER"));
        KS_CHECK(hub.handleKey(KEY_UP));
        KS_CHECK(hub.menu().selectedIndex() == rowOf(hub.menu(), "MULTIPLAYER"));

        KS_CHECK(hub.handleKey(KEY_ENTER));
        KS_CHECK(hub.menu().state() == MenuState::Multiplayer);
        KS_CHECK(hub.handleKey(KEY_ESC)); // back to the tiles
        KS_CHECK(hub.menu().state() == MenuState::Main);
        KS_CHECK(hub.menu().selectedIndex() == rowOf(hub.menu(), "SINGLE PLAYER"));

        // LEFT from the first tile wraps to the last one (EXIT).
        KS_CHECK(hub.handleKey(KEY_LEFT));
        KS_CHECK(hub.menu().selectedIndex() == rowOf(hub.menu(), "EXIT"));

        // Hovering selects the tile under the cursor.
        const MainMenuLayout L = layoutMainMenu(1600, 900, 8, 0);
        hub.menu().setSelectedIndex(rowOf(hub.menu(), "SINGLE PLAYER"));
        hub.handleMouseMove(L.tile[1].x + 12.f, L.tile[1].y + 12.f);
        KS_CHECK(hub.menu().selectedIndex() == rowOf(hub.menu(), "MULTIPLAYER"));

        // ESC on the arrival screen offers to leave instead of hiding it.
        hub.menu().openScreen(MenuState::Main);
        KS_CHECK(hub.handleKey(KEY_ESC));
        KS_CHECK(hub.menu().state() == MenuState::QuitConfirm);
        KS_CHECK(hub.menu().isVisible());
    }

    std::printf("main_menu: geometry, scroll window, hit-test, icons, rows, "
                "credits, key and mouse paths OK\n");
    return KS_TEST_RESULT("main_menu_test");
}
