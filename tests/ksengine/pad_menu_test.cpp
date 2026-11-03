/**
 * Pad navigation (ksim) — the mapping from controller buttons to the virtual
 * keys the menu already understands.
 *
 * The property that matters is the edge: a held stick has to be one step, not
 * a burst, and a released button has to arm the next press. A pad that reports
 * keys every frame makes a menu that scrolls at frame rate, which is what this
 * file exists to prevent.
 */
#include "simulator/ui/MenuInput.h"
#include "KsTest.h"

#include <cstdio>

using ks::sim::ui::PadButtons;
using ks::sim::ui::padMenuKey;

int main() {
    // ---------------------------------------------------------------------
    // One press, one key.
    // ---------------------------------------------------------------------
    {
        PadButtons before;
        PadButtons now;
        now.right = true;
        KS_CHECK(padMenuKey(now, before) == ks::sim::ui::kKeyRight);
        KS_CHECK(padMenuKey(now, now) == 0); // still held: no second step
        PadButtons released = now;
        released.right = false;
        KS_CHECK(padMenuKey(released, now) == 0);
        now = released;
        now.right = true; // pressed again after release
        KS_CHECK(padMenuKey(now, released) == ks::sim::ui::kKeyRight);
    }

    // ---------------------------------------------------------------------
    // Every direction maps, and the two face buttons map to ENTER and ESC.
    // ---------------------------------------------------------------------
    {
        const PadButtons before;
        PadButtons up;
        up.up = true;
        PadButtons down;
        down.down = true;
        PadButtons left;
        left.left = true;
        PadButtons confirm;
        confirm.confirm = true;
        PadButtons back;
        back.back = true;
        KS_CHECK(padMenuKey(up, before) == ks::sim::ui::kKeyUp);
        KS_CHECK(padMenuKey(down, before) == ks::sim::ui::kKeyDown);
        KS_CHECK(padMenuKey(left, before) == ks::sim::ui::kKeyLeft);
        KS_CHECK(padMenuKey(confirm, before) == ks::sim::ui::kKeyEnter);
        KS_CHECK(padMenuKey(back, before) == ks::sim::ui::kKeyEscape);
    }

    // ---------------------------------------------------------------------
    // Priority: a direction beats a face button pressed in the same frame,
    // and confirm beats back — pressing A on a tile must not close the screen.
    // ---------------------------------------------------------------------
    {
        const PadButtons before;
        PadButtons both;
        both.right = true;
        both.confirm = true;
        KS_CHECK(padMenuKey(both, before) == ks::sim::ui::kKeyRight);

        PadButtons face;
        face.confirm = true;
        face.back = true;
        KS_CHECK(padMenuKey(face, before) == ks::sim::ui::kKeyEnter);
    }

    // ---------------------------------------------------------------------
    // Silence: nothing pressed, nothing reported.
    // ---------------------------------------------------------------------
    {
        const PadButtons before;
        const PadButtons now;
        KS_CHECK(padMenuKey(now, before) == 0);
    }

    std::printf("pad_menu: press edges, every direction, priority and silence OK\n");
    return KS_TEST_RESULT("pad_menu_test");
}
