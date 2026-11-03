#pragma once
/**
 * Pad-to-menu mapping.
 *
 * The menu already speaks virtual keys — the arrow keys walk a row, ENTER
 * opens, ESC goes back — so a controller only has to report the same keys.
 * Keeping the translation here, as a pure function, means the pad can be
 * exercised without a pad: a held stick has to be one step, not a burst.
 *
 * Buttons are reported as "pressed now"; the caller owns the previous frame and
 * passes it back in, which is what turns a held button into a single press.
 */

namespace ks {
namespace sim {
namespace ui {

struct PadButtons {
    bool up = false;
    bool down = false;
    bool left = false;
    bool right = false;
    bool confirm = false; // A
    bool back = false;    // B / Start
};

constexpr int kKeyLeft = 0x25;
constexpr int kKeyUp = 0x26;
constexpr int kKeyRight = 0x27;
constexpr int kKeyDown = 0x28;
constexpr int kKeyEnter = 0x0D;
constexpr int kKeyEscape = 0x1B;

/** The key this frame's press asks for, or 0 when nothing was pressed since
 *  `before`. Direction wins over confirm, confirm wins over back: a player
 *  standing on a tile and pressing A does not want the screen to close. */
inline int padMenuKey(const PadButtons& now, const PadButtons& before) {
    const bool right = now.right && !before.right;
    const bool left = now.left && !before.left;
    const bool up = now.up && !before.up;
    const bool down = now.down && !before.down;
    if (right) return kKeyRight;
    if (left) return kKeyLeft;
    if (up) return kKeyUp;
    if (down) return kKeyDown;
    if (now.confirm && !before.confirm) return kKeyEnter;
    if (now.back && !before.back) return kKeyEscape;
    return 0;
}

} // namespace ui
} // namespace sim
} // namespace ks
