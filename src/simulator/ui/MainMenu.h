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
    /** Tiles that fit side by side; the strip scrolls for the rest. */
    static constexpr int kVisible = 5;

    Rect tile[kVisible]{};
    int item[kVisible] = {-1, -1, -1, -1, -1}; // row index behind each slot
    int count = 0;                             // slots actually used
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
 *  visible tiles follows the selection, so the last tiles of a long row stay
 *  reachable without making the row smaller than a readable label. */
inline MainMenuLayout layoutMainMenu(int w, int h, int tileCount, int selected) {
    MainMenuLayout L;
    const float W = static_cast<float>(w);
    const float H = static_cast<float>(h);
    const float margin = W * 0.035f;
    const float gap = W * 0.008f;
    const float tileW = W * 0.18f; // five of these plus the gaps fit inside W
    const float tileH = H * 0.246f;
    const float top = H * 0.173f;

    L.topBar = {0.f, 0.f, W, H * 0.078f};
    L.home = {margin, H * 0.024f, W * 0.24f, H * 0.034f};
    L.logo = {margin, H * 0.052f, H * 0.03f, H * 0.03f};
    L.avatar = {W - margin - H * 0.052f, H * 0.014f, H * 0.052f, H * 0.052f};
    L.strip = {margin, top,
               tileW * static_cast<float>(MainMenuLayout::kVisible) +
                   gap * static_cast<float>(MainMenuLayout::kVisible - 1),
               tileH};

    const int rows = std::max(0, tileCount);
    L.count = std::min(MainMenuLayout::kVisible, rows);
    L.first = std::clamp(selected - 2, 0, std::max(0, rows - MainMenuLayout::kVisible));
    for (int i = 0; i < L.count; ++i) {
        const int row = L.first + i;
        L.tile[i] = {margin + static_cast<float>(i) * (tileW + gap), top, tileW, tileH};
        L.item[i] = (row < rows) ? row : -1;
    }

    // The name and its text sit in the lower right, over the scene: that is
    // the part of the screen the tiles leave empty.
    L.title = {W * 0.55f, H * 0.455f, W * 0.42f, H * 0.10f};
    L.body = {W * 0.55f, H * 0.575f, W * 0.40f, H * 0.22f};
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
