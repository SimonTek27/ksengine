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

#include <string>

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

    return ks::test::finish("setup_garage_aids_test");
}
