// Phase 3.3 — the LAYOUT row on the SINGLE PLAYER page.
//
// The row is driven by two providers: the loop hands out the layouts of the
// loaded track as (id, name) pairs, and says which id is currently bound. The
// menu must cycle through those and hand the id back; a track with a single
// layout gets no row at all, because a picker with one choice is a dead end.

#include "GameMenuOverlay.h"
#include "KsTest.h"

#include <string>
#include <utility>
#include <vector>

namespace {

int findLayoutRow(const ks::sim::GameMenuOverlay& menu) {
    const auto& items = menu.items();
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].text.rfind("LAYOUT", 0) == 0) return static_cast<int>(i);
    }
    return -1;
}

} // namespace

int main() {
    ks::sim::GameMenuOverlay menu;
    menu.setVisible(true);
    menu.setCarName("Test Car");
    menu.setTrackName("Test Track");

    std::vector<std::pair<std::string, std::string>> choices{
        {"default", "Grand Prix"},
        {"national", "National"},
    };
    std::string active = "default";
    std::string picked;
    menu.layoutChoices = [&choices]() { return choices; };
    menu.activeLayoutId = [&active]() { return active; };
    // The menu reports the choice; the loop is what actually rebinds, so the
    // test stands in for it and flips the active id.
    menu.onLayoutChosen = [&](const std::string& id) {
        picked = id;
        active = id;
    };

    menu.openScreen(ks::sim::MenuState::Singleplayer);
    int row = findLayoutRow(menu);
    KS_CHECK(row >= 0);
    KS_CHECK(menu.items()[static_cast<size_t>(row)].text.find("Grand Prix") != std::string::npos);

    menu.setSelectedIndex(row);
    menu.activateSelection();
    KS_CHECK(picked == "national");

    // The label follows the active id, not the order the rows were built in.
    menu.openScreen(ks::sim::MenuState::Singleplayer);
    row = findLayoutRow(menu);
    KS_CHECK(row >= 0);
    KS_CHECK(menu.items()[static_cast<size_t>(row)].text.find("National") != std::string::npos);

    menu.setSelectedIndex(row);
    menu.activateSelection();
    KS_CHECK(picked == "default");

    // One layout: the row disappears instead of cycling onto itself.
    choices = {{"default", "Grand Prix"}};
    active = "default";
    menu.openScreen(ks::sim::MenuState::Singleplayer);
    KS_CHECK(findLayoutRow(menu) < 0);

    // No catalog at all (no track loaded yet): still no row.
    choices.clear();
    menu.openScreen(ks::sim::MenuState::Singleplayer);
    KS_CHECK(findLayoutRow(menu) < 0);

    return KS_TEST_RESULT("menu_layout_picker_test");
}
