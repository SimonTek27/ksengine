#pragma once
/**
 * The startup screen: the row of tiles the game opens on.
 *
 * Layout and navigation only, no drawing. The hub paints from these rectangles
 * and the hit-test uses the same ones, so a click always lands on the tile the
 * player is looking at — and the geometry can be exercised without a window.
 *
 * The tiles are not a second menu: each slot points at a row of the Main
 * screen, which is where a section's label, description and icon live. Adding
 * a section adds a tile; nothing here needs to know what it does.
 */
#include "NativeUiTypes.h"

#include <algorithm>

namespace ks {
namespace sim {
namespace ui {

struct MainMenuLayout {
    /** Slots the strip can hold. A row with fewer rows than this gets wider
     *  tiles rather than empty space, so the sections the menu carries are on
     *  screen instead of waiting behind a scroll nothing announces. */
    static constexpr int kMaxSlots = 12;
    /** Header quick actions: sound, the VR screen, the controller, then the
     *  notification readout — the clock's time always reads just before it,
     *  so the bar still closes on a glyph. */
    static constexpr int kQuickCount = 4;
    static constexpr int kClockSlot = kQuickCount - 1;
    /** Navigation in the bar's left corner: home, back, forward, refresh. */
    static constexpr int kNavCount = 4;

    Rect tile[kMaxSlots]{};
    int item[kMaxSlots] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1}; // row per slot
    int count = 0;                             // slots actually used
    int perRow = 0;                            // tiles on the top row
    Rect quick[kQuickCount]{};                 // the header's quick actions
    Rect nav[kNavCount]{};                     // back, forward, refresh
    float margin = 0.f;                        // the bar's left inset
    int first = 0;                             // first row on screen
    Rect strip{};
    Rect topBar{};
    Rect bottomBar{};
    Rect title{};   // the selected tile's name, large
    Rect body{};    // its description
    Rect home{};    // "HOME" in the top bar
    Rect logo{};
    Rect avatar{};  // driver plate, top right
};

/** Geometry for `tileCount` rows with row `selected` current. The window of
 *  visible tiles follows the selection when it leaves the window, so the last
 *  tiles of a long row stay reachable without making the row smaller than a
 *  readable label. */
inline MainMenuLayout layoutMainMenu(int w, int h, int tileCount, int selected) {
    MainMenuLayout L;
    const float W = static_cast<float>(w);
    const float H = static_cast<float>(h);
    const float margin = W * 0.035f;
    const float gap = W * 0.008f;
    const float usable = W - margin * 2.f;
    const float top = H * 0.145f;
    const float rowGap = H * 0.018f;

    L.topBar = {0.f, 0.f, W, H * 0.078f};
    L.margin = margin;
    L.logo = {margin, H * 0.052f, H * 0.03f, H * 0.03f};
    const float mid = L.topBar.h * 0.5f;

    // Navigation sits in the bar's left corner with the section's name after
    // it: four flat buttons, the moves a window puts there, home first.
    {
        const float buttonSize = H * 0.030f;
        const float buttonGap = W * 0.007f;
        float x = margin;
        for (int i = 0; i < MainMenuLayout::kNavCount; ++i) {
            L.nav[i] = {x, mid - buttonSize * 0.5f, buttonSize, buttonSize};
            x += buttonSize + buttonGap;
        }
        L.home = {x + W * 0.010f, H * 0.024f, W * 0.24f, H * 0.034f};
    }

    // The clock closes the bar, so the row is laid out right to left from the
    // margin: the clock ends up last, the time reads in the gap reserved
    // before it, and the driver's plate sits to the left of the row.
    {
        const float iconSize = H * 0.036f;
        const float iconGap = W * 0.014f;
        const float clockGap = W * 0.075f; // room for "HH:MM" before the clock
        float right = W - margin;
        for (int i = MainMenuLayout::kQuickCount - 1; i >= 0; --i) {
            L.quick[i] = {right - iconSize, mid - iconSize * 0.5f, iconSize, iconSize};
            right -= iconSize + (i == MainMenuLayout::kClockSlot ? clockGap : iconGap);
        }
        const float plate = H * 0.052f;
        L.avatar = {right - plate, mid - plate * 0.5f, plate, plate};
    }
    const int rows = std::max(0, tileCount);
    L.count = std::min(MainMenuLayout::kMaxSlots, rows);
    // Two rows, the top one taking the first half: twelve sections do not fit
    // across a single strip at a readable label width, and whatever did not
    // fit was simply not on screen.
    L.perRow = L.count > 0 ? (L.count + 1) / 2 : 0;
    const int perRow = L.perRow > 0 ? L.perRow : 1;
    const int rowCount = L.count > 0 ? (L.count + perRow - 1) / perRow : 0;
    const float tileH = rowCount > 1 ? H * 0.185f : H * 0.230f;
    const float tileW = perRow > 0
        ? (usable - gap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow)
        : usable;
    const float rowsHeight = static_cast<float>(rowCount) * tileH +
        rowGap * static_cast<float>(rowCount > 1 ? rowCount - 1 : 0);
    L.strip = {margin, top, usable, rowsHeight};
    // Twelve rows, twelve slots: nothing scrolls, and a longer row than that
    // still keeps the selection inside the window.
    L.first = L.count > 0
        ? std::clamp(selected - (L.count - 1), 0, std::max(0, rows - L.count))
        : 0;
    for (int i = 0; i < L.count; ++i) {
        const int row = L.first + i;
        const int column = i % perRow;
        const int line = i / perRow;
        L.tile[i] = {margin + static_cast<float>(column) * (tileW + gap),
                     top + static_cast<float>(line) * (tileH + rowGap), tileW, tileH};
        L.item[i] = (row < rows) ? row : -1;
    }

    // The name and its text sit under both rows, over the scene: the tiles
    // leave that part of the screen empty.
    const float stripBottom = top + static_cast<float>(rowCount > 0 ? rowCount : 1) * tileH +
        rowGap * static_cast<float>(rowCount > 1 ? rowCount - 1 : 0);
    // The caption reads as a lower third under the tiles, left aligned with
    // them: on the right it landed on the scene and on the second row.
    L.title = {margin, stripBottom + H * 0.032f, usable * 0.52f, H * 0.085f};
    L.body = {margin, L.title.y + H * 0.072f, usable * 0.46f, H * 0.10f};
    L.bottomBar = {0.f, H * 0.955f, W, H * 0.045f};
    return L;
}

/** Row under the cursor, or -1 when the point is not on a tile. */
inline int mainMenuTileAt(const MainMenuLayout& layout, float x, float y) {
    for (int i = 0; i < layout.count; ++i) {
        if (layout.tile[i].contains(x, y)) return layout.item[i];
    }
    return -1;
}

/** Next row in `dir` (+1 / -1), wrapping at both ends. A row list is a ring:
 *  LEFT from the first tile lands on the last, which is where the arrow keys
 *  are expected to go on a horizontal strip. */
inline int mainMenuStep(int from, int dir, int count) {
    if (count <= 0) return 0;
    const int step = ((dir % count) + count) % count;
    const int start = std::clamp(from, 0, count - 1);
    return (start + step) % count;
}

} // namespace ui
} // namespace sim
} // namespace ks
