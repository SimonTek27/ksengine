/**
 * Roadmap 2.5 — pit strategy screen (Sprint 8 / P1.8):
 *  - GARAGE gains a PIT STRATEGY row that opens the screen; ESC returns to
 *    GARAGE;
 *  - the screen shows the seeded fuel target, edits it in 5 L steps, flips
 *    the repair toggles, rebuilds the value rows in place (cursor kept) and
 *    reports the edited tuple on CONFIRM PLAN without closing;
 *  - wirePitStrategyMenu() seeds the screen from loop-owned state, stores a
 *    confirmed tuple back into it and runs the applied hook (the stop
 *    request the SimulationLoop turns into a queued pit service);
 *  - applyStrategyToRepairInput() maps the strategy into PitRepairInput and
 *    never queues service by itself — requestService is a separate intent
 *    gated on box position in updatePitRepair().
 *
 * Compiles the GameMenuOverlay TU directly (menu_session_gate_test pattern).
 */
#include "KsTest.h"
#include "simulator/GameMenuOverlay.h"
#include "simulator/PitStrategyBridge.h"

#include <string>
#include <vector>

using ks::sim::applyStrategyToRepairInput;
using ks::sim::GameMenuOverlay;
using ks::sim::MenuItem;
using ks::sim::MenuState;
using ks::sim::PitRepairInput;
using ks::sim::PitStrategyState;
using ks::sim::wirePitStrategyMenu;

namespace {
// Row order built by buildPitStrategyMenu().
constexpr int kFuelDisplay = 0, kFuelMinus = 1, kFuelPlus = 2, kTyres = 4,
              kBody = 5, kSuspension = 6, kAero = 7, kEngine = 8,
              kConfirm = 10, kBack = 11;
} // namespace

int main() {
    // --- MAIN -> GARAGE -> PIT STRATEGY ------------------------------------
    GameMenuOverlay menu;
    menu.setVisible(true);
    KS_CHECK(menu.state() == MenuState::Main);
    {
        const std::vector<MenuItem> main = menu.items();
        KS_CHECK(main.size() > 2);
        if (main.size() > 2) {
            KS_CHECK(main[0].text == "SINGLE PLAYER");
            KS_CHECK(main[2].text == "GARAGE");
        }
    }
    KS_CHECK(menu.handleKeyPress(0x28)); // DOWN
    KS_CHECK(menu.handleKeyPress(0x28)); // DOWN -> GARAGE
    KS_CHECK(menu.handleKeyPress(0x0D)); // ENTER
    KS_CHECK(menu.state() == MenuState::Garage);
    {
        const std::vector<MenuItem> garage = menu.items();
        KS_CHECK(garage.size() >= 3);
        if (garage.size() >= 3) {
            KS_CHECK(garage[0].text == "SETUP");
            KS_CHECK(garage[1].text == "PIT STRATEGY");
            KS_CHECK(garage[1].description == "Fuel, tyres and repair plan for next stop");
        }
    }

    int opened = 0;
    menu.onOpenPitStrategyRequested = [&]() { ++opened; };
    KS_CHECK(menu.handleKeyPress(0x28)); // DOWN -> PIT STRATEGY
    KS_CHECK(menu.handleKeyPress(0x0D)); // ENTER
    KS_CHECK(opened == 1);
    KS_CHECK(menu.state() == MenuState::PitStrategy);
    KS_CHECK(menu.sectionTitle() == "PIT STRATEGY");

    // --- default layout: display row (disabled), steps, toggles, confirm ----
    {
        const std::vector<MenuItem> pit = menu.items();
        KS_CHECK(pit.size() == kBack + 1);
        if (pit.size() == kBack + 1) {
            KS_CHECK(pit[kFuelDisplay].text == "FUEL TARGET  60 L");
            KS_CHECK(!pit[kFuelDisplay].enabled && !pit[kFuelDisplay].isSeparator);
            KS_CHECK(pit[kFuelMinus].text == "FUEL -5 L");
            KS_CHECK(pit[kFuelPlus].text == "FUEL +5 L");
            KS_CHECK(pit[kTyres].text == "TYRES  [ON]");
            KS_CHECK(pit[kBody].text == "BODY  [ON]");
            KS_CHECK(pit[kSuspension].text == "SUSPENSION  [ON]");
            KS_CHECK(pit[kAero].text == "AERO  [OFF]");
            KS_CHECK(pit[kEngine].text == "ENGINE  [OFF]");
            KS_CHECK(pit[kConfirm].text == "CONFIRM PLAN");
            KS_CHECK(pit[kBack].text == "BACK");
        }
    }

    // --- fuel steps edit the value row in place (cursor kept) ---------------
    // Selection starts on the disabled display row: DOWN lands on FUEL -5.
    KS_CHECK(menu.handleKeyPress(0x28));
    KS_CHECK(menu.handleKeyPress(0x0D)); // 60 -> 55
    KS_CHECK(menu.items()[kFuelDisplay].text == "FUEL TARGET  55 L");
    KS_CHECK(menu.handleKeyPress(0x28)); // DOWN -> FUEL +5
    KS_CHECK(menu.handleKeyPress(0x0D)); // 55 -> 60
    KS_CHECK(menu.items()[kFuelDisplay].text == "FUEL TARGET  60 L");
    // Floor: the selection stays on FUEL -5, never goes negative.
    KS_CHECK(menu.handleKeyPress(0x26)); // UP -> FUEL -5
    for (int i = 0; i < 12; ++i)
        menu.handleKeyPress(0x0D); // 60 -> 55 -> ... -> 0 (floored)
    KS_CHECK(menu.items()[kFuelDisplay].text == "FUEL TARGET  0 L");

    // --- toggles flip, CONFIRM PLAN reports the tuple ------------------------
    int confirms = 0;
    float fuel = -1.f;
    bool tyres = false, body = false, susp = false, aero = false, engine = false;
    menu.onPitStrategyConfirmRequested = [&](float f, bool t, bool b, bool s, bool a,
                                             bool e) {
        ++confirms;
        fuel = f; tyres = t; body = b; susp = s; aero = a; engine = e;
    };
    // Bring the fuel back up: selection is still FUEL -5, walk DOWN to
    // FUEL +5 and press ENTER 12 times (0 -> 60).
    KS_CHECK(menu.handleKeyPress(0x28));
    for (int i = 0; i < 12; ++i) {
        menu.handleKeyPress(0x0D);
    }
    KS_CHECK(menu.items()[kFuelDisplay].text == "FUEL TARGET  60 L");

    // From FUEL +5 walk to the toggles and flip TYRES / BODY / AERO / ENGINE.
    KS_CHECK(menu.handleKeyPress(0x28)); // TYRES
    KS_CHECK(menu.handleKeyPress(0x0D)); // ON -> OFF
    KS_CHECK(menu.items()[kTyres].text == "TYRES  [OFF]");
    KS_CHECK(menu.handleKeyPress(0x28)); // BODY
    KS_CHECK(menu.handleKeyPress(0x0D)); // ON -> OFF
    KS_CHECK(menu.items()[kBody].text == "BODY  [OFF]");
    KS_CHECK(menu.handleKeyPress(0x28)); // SUSPENSION (unchanged)
    KS_CHECK(menu.handleKeyPress(0x28)); // AERO
    KS_CHECK(menu.handleKeyPress(0x0D)); // OFF -> ON
    KS_CHECK(menu.items()[kAero].text == "AERO  [ON]");
    KS_CHECK(menu.handleKeyPress(0x28)); // ENGINE
    KS_CHECK(menu.handleKeyPress(0x0D)); // OFF -> ON
    KS_CHECK(menu.items()[kEngine].text == "ENGINE  [ON]");

    KS_CHECK(menu.handleKeyPress(0x28)); // DOWN -> CONFIRM PLAN (skips sep)
    KS_CHECK(menu.handleKeyPress(0x0D));
    KS_CHECK(confirms == 1);
    KS_CHECK_NEAR(fuel, 60.f, 1e-6);
    KS_CHECK(tyres == false);
    KS_CHECK(body == false);
    KS_CHECK(susp == true);
    KS_CHECK(aero == true);
    KS_CHECK(engine == true);
    KS_CHECK(menu.state() == MenuState::PitStrategy); // stays open

    // --- ESC returns to GARAGE ----------------------------------------------
    KS_CHECK(menu.handleKeyPress(0x1B));
    KS_CHECK(menu.state() == MenuState::Garage);

    // --- bridge: seed screen from loop state, confirm stores + applies -------
    GameMenuOverlay menu2;
    menu2.setVisible(true);
    PitStrategyState st;
    st.fuelTargetL = 85.f;
    st.tyres = false;
    st.body = true;
    st.suspension = false;
    st.aero = true;
    st.engine = false;
    int applied = 0;
    wirePitStrategyMenu(menu2, st, [&]() { ++applied; });

    KS_CHECK(menu2.handleKeyPress(0x28)); // MAIN: DOWN
    KS_CHECK(menu2.handleKeyPress(0x28)); // DOWN -> GARAGE
    KS_CHECK(menu2.handleKeyPress(0x0D)); // ENTER
    KS_CHECK(menu2.handleKeyPress(0x28)); // DOWN -> PIT STRATEGY
    KS_CHECK(menu2.handleKeyPress(0x0D)); // ENTER
    KS_CHECK(menu2.state() == MenuState::PitStrategy);
    {
        const std::vector<MenuItem> pit = menu2.items();
        KS_CHECK(pit.size() == kBack + 1);
        if (pit.size() == kBack + 1) {
            KS_CHECK(pit[kFuelDisplay].text == "FUEL TARGET  85 L");
            KS_CHECK(pit[kTyres].text == "TYRES  [OFF]");
            KS_CHECK(pit[kBody].text == "BODY  [ON]");
            KS_CHECK(pit[kSuspension].text == "SUSPENSION  [OFF]");
            KS_CHECK(pit[kAero].text == "AERO  [ON]");
            KS_CHECK(pit[kEngine].text == "ENGINE  [OFF]");
        }
    }

    // Edit: fuel -5 (85 -> 80), TYRES ON. Then confirm.
    KS_CHECK(menu2.handleKeyPress(0x28)); // FUEL -5
    KS_CHECK(menu2.handleKeyPress(0x0D));
    KS_CHECK(menu2.items()[kFuelDisplay].text == "FUEL TARGET  80 L");
    KS_CHECK(menu2.handleKeyPress(0x28)); // FUEL +5 (untouched)
    KS_CHECK(menu2.handleKeyPress(0x28)); // TYRES
    KS_CHECK(menu2.handleKeyPress(0x0D)); // OFF -> ON
    KS_CHECK(menu2.items()[kTyres].text == "TYRES  [ON]");
    // DOWN x6 -> CONFIRM PLAN (skips FUEL +5..ENGINE already passed? no:
    // from TYRES: BODY, SUSPENSION, AERO, ENGINE, sep, CONFIRM = 5 downs).
    for (int i = 0; i < 5; ++i) KS_CHECK(menu2.handleKeyPress(0x28));
    KS_CHECK(menu2.handleKeyPress(0x0D));
    KS_CHECK(applied == 1);
    KS_CHECK_NEAR(st.fuelTargetL, 80.f, 1e-6);
    KS_CHECK(st.tyres == true);
    KS_CHECK(st.body == true);      // untouched by the screen edits
    KS_CHECK(st.suspension == false);
    KS_CHECK(st.aero == true);
    KS_CHECK(st.engine == false);

    // --- strategy mapping: fields only, no service queued --------------------
    PitRepairInput in;
    applyStrategyToRepairInput(st, in);
    KS_CHECK_NEAR(in.targetFuelL, 80.f, 1e-6);
    KS_CHECK(in.wantTyres == true);
    KS_CHECK(in.wantBody == true);
    KS_CHECK(in.wantSuspension == false);
    KS_CHECK(in.wantAero == true);
    KS_CHECK(in.wantEngine == false);
    KS_CHECK(!in.requestService); // a plan alone queues nothing

    std::printf("pit_strategy: screen, fuel steps, toggles, confirm + bridge OK\n");
    return 0;
}
