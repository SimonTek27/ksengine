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

// Rows on the MAIN screen: the two modes, then driver, content, settings and
// the exit group. The quick actions live in the header bar, not here.
constexpr int kRows = 8;

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
    // Geometry: the tiles of an eight-row screen, in order, between the two
    // bars, none overlapping and none clipped.
    // ---------------------------------------------------------------------
    {
        // Two rows of tiles: an eight-row screen shows all eight, so nothing
        // waits behind a scroll nothing announces.
        const MainMenuLayout L = layoutMainMenu(1600, 900, 8, 0);
        KS_CHECK(L.count == 8);
        KS_CHECK(L.perRow == 4); // 4 + 4
        KS_CHECK(L.first == 0);
        float lowest = 0.f;
        for (int i = 0; i < L.count; ++i) {
            KS_CHECK(L.item[i] == i); // the row behind each slot
            KS_CHECK(L.tile[i].w > 0.f && L.tile[i].h > 0.f);
            KS_CHECK(L.tile[i].x >= 0.f);
            KS_CHECK(L.tile[i].x + L.tile[i].w <= 1600.f);
            KS_CHECK(L.tile[i].y > L.topBar.h);                 // under the bar
            KS_CHECK(L.tile[i].y + L.tile[i].h < L.bottomBar.y); // over the hints
            // Left to right inside a row; the next row starts again at the margin.
            if (i > 0 && i % L.perRow != 0) KS_CHECK(L.tile[i].x > L.tile[i - 1].x);
            if (i >= L.perRow) KS_CHECK(L.tile[i].y > L.tile[i - L.perRow].y);
            lowest = std::max(lowest, L.tile[i].y + L.tile[i].h);
        }
        // No two tiles overlap, on either row.
        for (int i = 0; i < L.count; ++i) {
            for (int j = i + 1; j < L.count; ++j) {
                const auto& a = L.tile[i];
                const auto& b = L.tile[j];
                const bool apart = a.x + a.w <= b.x || b.x + b.w <= a.x ||
                                   a.y + a.h <= b.y || b.y + b.h <= a.y;
                KS_CHECK(apart);
            }
        }
        // The section's name and text sit under both rows, not on them.
        KS_CHECK(L.title.y >= lowest);
        KS_CHECK(L.body.y > L.title.y);
        KS_CHECK(L.body.y + L.body.h <= L.bottomBar.y + 1.f);
        KS_CHECK(L.avatar.x + L.avatar.w <= 1600.f);
        // Bar navigation: home, back, forward and refresh in the corner,
        // before the section's name.
        for (int i = 0; i < MainMenuLayout::kNavCount; ++i) {
            KS_CHECK(L.nav[i].w > 0.f && L.nav[i].h > 0.f);
            KS_CHECK(L.nav[i].y >= 0.f);
            KS_CHECK(L.nav[i].y + L.nav[i].h <= L.topBar.h);
            KS_CHECK(L.nav[i].x + L.nav[i].w <= L.home.x);
            if (i > 0) KS_CHECK(L.nav[i].x >= L.nav[i - 1].x + L.nav[i - 1].w);
        }
        // Quick actions: after the driver's plate, in a row of their own.
        for (int i = 0; i < MainMenuLayout::kQuickCount; ++i) {
            KS_CHECK(L.quick[i].w > 0.f && L.quick[i].h > 0.f);
            KS_CHECK(L.quick[i].y >= 0.f);
            KS_CHECK(L.quick[i].y + L.quick[i].h <= L.topBar.h);
            KS_CHECK(L.avatar.x + L.avatar.w <= L.quick[i].x);
            if (i > 0) KS_CHECK(L.quick[i].x >= L.quick[i - 1].x + L.quick[i - 1].w);
        }
        // The clock is the last element: nothing sits to the right of it.
        const auto& clockBox = L.quick[MainMenuLayout::kClockSlot];
        KS_CHECK(clockBox.x + clockBox.w >= 1600.f - L.margin - 1.f);
    }

    // ---------------------------------------------------------------------
    // Scrolling: the window follows the selection; short rows do not scroll.
    // ---------------------------------------------------------------------
    {
        // A row longer than the strip scrolls, and only then.
        const int many = MainMenuLayout::kMaxSlots + 4;
        const MainMenuLayout last = layoutMainMenu(1600, 900, many, many - 1);
        KS_CHECK(last.count == MainMenuLayout::kMaxSlots);
        KS_CHECK(last.first == 4); // rows - slots
        KS_CHECK(last.item[0] == 4);
        KS_CHECK(last.item[last.count - 1] == many - 1);
        KS_CHECK(layoutMainMenu(1600, 900, many, 3).first == 0);
        // An eight-row screen never scrolls, so a hovered tile cannot slide
        // away from under the cursor.
        for (int slot = 0; slot < 8; ++slot) {
            const MainMenuLayout hovered = layoutMainMenu(1600, 900, 8, slot);
            KS_CHECK(hovered.first == 0);
            KS_CHECK(hovered.item[slot] == slot);
        }

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
        KS_CHECK(tiles == kRows);
        KS_CHECK(separators == 1); // the gap before EDITOR
        for (int i = 0; i < kRows; ++i) KS_CHECK(rowOf(menu, kExpected[i]) >= 0);
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
        const MainMenuLayout L = layoutMainMenu(1600, 900, kRows, 0);
        hub.menu().setSelectedIndex(rowOf(hub.menu(), "SINGLE PLAYER"));
        hub.handleMouseMove(L.tile[1].x + 12.f, L.tile[1].y + 12.f);
        KS_CHECK(hub.menu().selectedIndex() == rowOf(hub.menu(), "MULTIPLAYER"));

        // A click activates the tile under the cursor (the mouse path), and
        // the section it opens is the one under the pointer.
        hub.menu().openScreen(MenuState::Main);
        const auto& contentTile = L.tile[3]; // 4 + 4: CONTENT closes the top row
        hub.handleMouseButton(ks::sim::ui::MouseButton::Left, true,
                              contentTile.x + 12.f, contentTile.y + 12.f);
        // The press alone must already have selected the tile under the
        // cursor: that is the hover path the click builds on.
        KS_CHECK(hub.menu().selectedIndex() == rowOf(hub.menu(), "CONTENT"));
        hub.handleMouseButton(ks::sim::ui::MouseButton::Left, false,
                              contentTile.x + 12.f, contentTile.y + 12.f);
        KS_CHECK(hub.menu().state() == MenuState::ContentManager);
        // The page lists the three libraries the loader scans.
        KS_CHECK(rowOf(hub.menu(), "CARS") >= 0);
        KS_CHECK(rowOf(hub.menu(), "TRACKS") >= 0);
        KS_CHECK(rowOf(hub.menu(), "TEAMS") >= 0);

        // The header's quick actions are hit-tested in the bar, not on a tile.
        hub.menu().openScreen(MenuState::Main);
        bool fullscreenAsked = false;
        hub.menu().onToggleFullscreenRequested = [&fullscreenAsked]() { fullscreenAsked = true; };
        hub.handleMouseButton(ks::sim::ui::MouseButton::Left, true,
                              L.quick[1].x + 2.f, L.quick[1].y + 2.f);
        hub.handleMouseButton(ks::sim::ui::MouseButton::Left, false,
                              L.quick[1].x + 2.f, L.quick[1].y + 2.f);
        KS_CHECK(fullscreenAsked);
        KS_CHECK(hub.menu().state() == MenuState::Main); // a switch, not a section

        // The bell is a readout too, but an openable one: a notice raises a
        // badge, the bell opens the list and clears it, and a press on the
        // panel closes the panel instead of the tile underneath.
        hub.menu().openScreen(MenuState::Main);
        const auto& bell = L.quick[MainMenuLayout::kClockSlot];
        hub.notifications().clear();
        hub.renderFrame(1600, 900);
        KS_CHECK(!hub.notifications().open());
        hub.notifications().push("Hosting a session on port 40000");
        KS_CHECK(hub.notifications().unread() == 1);

        hub.handleMouseMove(bell.x + 2.f, bell.y + 2.f);
        hub.handleMouseButton(ks::sim::ui::MouseButton::Left, false, bell.x + 2.f, bell.y + 2.f);
        KS_CHECK(hub.notifications().open());
        KS_CHECK(hub.notifications().unread() == 0); // opening is reading

        hub.renderFrame(1600, 900);
        bool noticeListed = false;
        for (const auto& cmd : hub.renderer().list().commands()) {
            if (cmd.type == ks::sim::ui::DrawCmdType::Text &&
                cmd.text == "Hosting a session on port 40000")
                noticeListed = true;
        }
        KS_CHECK(noticeListed);

        // A press inside the panel consumes the click and closes it.
        const float panelW = 380.f;
        const float panelX = bell.x + bell.w - panelW;
        const float panelY = L.topBar.h + 8.f;
        hub.handleMouseButton(ks::sim::ui::MouseButton::Left, false,
                              panelX + 20.f, panelY + 20.f);
        KS_CHECK(!hub.notifications().open());
        KS_CHECK(hub.menu().state() == MenuState::Main);

        // The bar's navigation works on the submenus too: back, forward and
        // refresh walk the trail the player walked.
        hub.menu().openScreen(MenuState::Main);
        hub.handleMouseMove(4.f, 4.f); // nothing in the bar under the cursor
        hub.menu().openScreen(MenuState::Settings);
        hub.handleMouseMove(L.nav[1].x + 2.f, L.nav[1].y + 2.f);
        hub.handleMouseButton(ks::sim::ui::MouseButton::Left, false,
                              L.nav[1].x + 2.f, L.nav[1].y + 2.f);
        KS_CHECK(hub.menu().state() == MenuState::Main);       // back
        hub.handleMouseMove(L.nav[2].x + 2.f, L.nav[2].y + 2.f);
        hub.handleMouseButton(ks::sim::ui::MouseButton::Left, false,
                              L.nav[2].x + 2.f, L.nav[2].y + 2.f);
        KS_CHECK(hub.menu().state() == MenuState::Settings);   // forward
        hub.handleMouseButton(ks::sim::ui::MouseButton::Left, false,
                              L.nav[3].x + 2.f, L.nav[3].y + 2.f);
        KS_CHECK(hub.menu().state() == MenuState::Settings);   // refresh stays put

        // The first button is HOME: from a submenu it opens the arrival
        // screen, and from there it has nowhere to go.
        hub.menu().openScreen(MenuState::ContentManager);
        hub.handleMouseMove(L.nav[0].x + 2.f, L.nav[0].y + 2.f);
        hub.handleMouseButton(ks::sim::ui::MouseButton::Left, false,
                              L.nav[0].x + 2.f, L.nav[0].y + 2.f);
        KS_CHECK(hub.menu().state() == MenuState::Main);

        // ...and the bar is drawn on a submenu, with that screen's name in it.
        hub.menu().openScreen(MenuState::Settings);
        hub.renderFrame(1600, 900);
        bool barPlate = false;
        bool nameInBar = false;
        for (const auto& cmd : hub.renderer().list().commands()) {
            if (cmd.type == ks::sim::ui::DrawCmdType::RectFilled && cmd.x0 == 0.f &&
                cmd.y0 == 0.f && cmd.x1 >= 1599.f &&
                cmd.y1 >= 900.f * 0.05f && cmd.y1 <= 900.f * 0.10f)
                barPlate = true;
            if (cmd.type == ks::sim::ui::DrawCmdType::Text && cmd.text == "SETTINGS" &&
                cmd.y0 < 900.f * 0.10f)
                nameInBar = true;
        }
        KS_CHECK(barPlate);
        KS_CHECK(nameInBar);

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
