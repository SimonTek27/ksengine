/**
 * The garage screen's driver-aid rows.
 *
 * A level for a system the car does not have is a dead control: the driver can
 * pick it, nothing happens. The screen therefore leaves the row out when the
 * car's own electronics say PRESENT = 0, which is what AC and Content Manager
 * do. This pins the row list itself — the one thing that a test can see without
 * drawing the screen — including that a car which says nothing keeps both rows,
 * and that the rows for fuel and ballast are never touched.
 */
#include "KsTest.h"

#include "SetupGarage.h"
#include "ui/NativeUiTypes.h"
#include "ui/TextRenderer.h"

#include <string>
#include <vector>

namespace {

bool hasRow(const ks::sim::SetupGarage& garage, const char* label) {
    for (const auto& row : garage.rows()) {
        if (row.label == label) return true;
    }
    return false;
}

int rowCount(const ks::sim::SetupGarage& garage) {
    return static_cast<int>(garage.rows().size());
}

/** Draw one settled frame into `dl`: the screen fades in over a few frames, so
 *  a single build() call would draw almost nothing. */
void buildFrame(ks::sim::SetupGarage& garage, ks::sim::ui::DrawList& dl,
                ks::sim::ui::TextRenderer& text, int w, int h) {
    garage.setVisible(true);
    for (int i = 0; i < 12; ++i) {
        dl.clear();
        garage.build(dl, text, w, h, nullptr);
    }
}

/** Whether the frame carries that string as drawn text. Text commands keep the
 *  string, so this asks what the screen puts on the glass, not what the row list
 *  holds internally. */
bool drawsText(const ks::sim::ui::DrawList& dl, const char* s) {
    for (const auto& cmd : dl.commands()) {
        if (cmd.text == s) return true;
    }
    return false;
}

/** Shapes that leave the viewport. Text is skipped: a text command records where
 *  the string starts, and the glyph box is the renderer's business. */
int commandsOutside(const ks::sim::ui::DrawList& dl, int w, int h) {
    int outside = 0;
    for (const auto& cmd : dl.commands()) {
        if (cmd.type == ks::sim::ui::DrawCmdType::Text) continue;
        if (cmd.x0 < -1.f || cmd.y0 < -1.f || cmd.x1 > float(w) + 1.f ||
            cmd.y1 > float(h) + 1.f)
            ++outside;
    }
    return outside;
}

} // namespace

int main() {
    using ks::sim::SetupGarage;

    // --- A car that says nothing keeps the screen it always had ------------
    {
        SetupGarage garage; // defaults: neither system is known to be absent
        KS_CHECK(garage.aidsAvailable(true));
        KS_CHECK(garage.aidsAvailable(false));
        KS_CHECK(hasRow(garage, "TC"));
        KS_CHECK(hasRow(garage, "ABS"));
        KS_CHECK(hasRow(garage, "FUEL"));
        KS_CHECK(hasRow(garage, "BALLAST"));
        KS_CHECK(rowCount(garage) == 4);
    }

    // --- A car with neither system offers neither level --------------------
    {
        SetupGarage garage;
        garage.setAidsAvailable(false, false);
        KS_CHECK(!hasRow(garage, "TC"));
        KS_CHECK(!hasRow(garage, "ABS"));
        // The rest of the tab is not about aids and stays where it was.
        KS_CHECK(hasRow(garage, "FUEL"));
        KS_CHECK(hasRow(garage, "BALLAST"));
        KS_CHECK(rowCount(garage) == 2);
    }

    // --- One of the two is enough to keep its own row ----------------------
    {
        SetupGarage garage;
        garage.setAidsAvailable(true, false); // traction control only
        KS_CHECK(hasRow(garage, "TC"));
        KS_CHECK(!hasRow(garage, "ABS"));
        KS_CHECK(rowCount(garage) == 3);

        garage.setAidsAvailable(false, true); // ABS only
        KS_CHECK(!hasRow(garage, "TC"));
        KS_CHECK(hasRow(garage, "ABS"));
        KS_CHECK(rowCount(garage) == 3);
    }

    // --- Loading another car brings the rows back -------------------------
    // The screen outlives the car: a second car that has the systems must not
    // inherit the first one's missing rows.
    {
        SetupGarage garage;
        garage.setAidsAvailable(false, false);
        KS_CHECK(rowCount(garage) == 2);
        garage.setAidsAvailable(true, true);
        KS_CHECK(rowCount(garage) == 4);
        KS_CHECK(hasRow(garage, "TC"));
        KS_CHECK(hasRow(garage, "ABS"));
    }

    // --- The selection cannot point past the shortened list ---------------
    {
        SetupGarage garage;
        garage.setSetup(garage.setup()); // rebuild with the full list
        // Walk to the last row, then take the aids away under the cursor.
        for (int i = 0; i < 8; ++i) garage.handleKeyPress(0x28); // DOWN
        garage.setAidsAvailable(false, false);
        KS_CHECK(garage.selectedRow() >= 0);
        KS_CHECK(garage.selectedRow() < rowCount(garage));
    }

    // --- What reaches the glass, not just the row list ---------------------
    // The row list and the frame are two different things: a row could be built
    // and never drawn, or drawn in the wrong place. This builds real frames.
    {
        ks::sim::ui::DrawList dl;
        ks::sim::ui::TextRenderer text;

        SetupGarage full;
        buildFrame(full, dl, text, 1280, 720);
        KS_CHECK(drawsText(dl, "TC"));
        KS_CHECK(drawsText(dl, "ABS"));
        KS_CHECK(drawsText(dl, "FUEL"));
        KS_CHECK(commandsOutside(dl, 1280, 720) == 0);
        const int fullCommands = static_cast<int>(dl.commands().size());
        KS_CHECK(fullCommands > 10);

        // A car with neither system: the labels are not on the screen either.
        SetupGarage bare;
        bare.setAidsAvailable(false, false);
        buildFrame(bare, dl, text, 1280, 720);
        KS_CHECK(!drawsText(dl, "TC"));
        KS_CHECK(!drawsText(dl, "ABS"));
        KS_CHECK(drawsText(dl, "FUEL"));
        KS_CHECK(drawsText(dl, "BALLAST"));
        KS_CHECK(commandsOutside(dl, 1280, 720) == 0);
        KS_CHECK(static_cast<int>(dl.commands().size()) < fullCommands);

        // The shortened tab still fits the smallest window the app runs in: the
        // two remaining rows sit above the footer, nothing runs off the bottom.
        SetupGarage narrow;
        narrow.setAidsAvailable(false, false);
        buildFrame(narrow, dl, text, 1280, 480);
        KS_CHECK(commandsOutside(dl, 1280, 480) == 0);
        KS_CHECK(!drawsText(dl, "TC"));
    }

    return ks::test::finish("setup_garage_aids_test");
}
