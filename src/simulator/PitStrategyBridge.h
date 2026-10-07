#pragma once
/**
 * Map menu pit strategy → PitRepairInput (Sprint 8 / P1.8, roadmap 2.5).
 */
#include "GameMenuOverlay.h"
#include "PitLaneRepair.h"
#include <cstdio>

namespace ks {
namespace sim {

struct PitStrategyState {
    float fuelTargetL = 60.f;
    bool tyres = true;
    bool body = true;
    bool suspension = true;
    bool aero = false;
    bool engine = false;
};

// Maps WHAT the next box stop should do. requestService is deliberately not
// touched: it is a separate player intent that SimulationLoop gates on box
// position (updatePitRepair) — a strategy on its own queues nothing.
inline void applyStrategyToRepairInput(const PitStrategyState& s, PitRepairInput& in) {
    in.targetFuelL = s.fuelTargetL;
    in.wantTyres = s.tyres;
    in.wantBody = s.body;
    in.wantSuspension = s.suspension;
    in.wantAero = s.aero;
    in.wantEngine = s.engine;
}

inline void wirePitStrategyMenu(GameMenuOverlay& menu, PitStrategyState& state,
                                std::function<void()> onApplied = {}) {
    menu.setPitStrategy(state.fuelTargetL, state.tyres, state.body,
                        state.suspension, state.aero, state.engine);
    menu.onPitStrategyConfirmRequested =
        [&state, onApplied](float fuel, bool tyres, bool body, bool susp, bool aero,
                            bool engine) {
            state.fuelTargetL = fuel;
            state.tyres = tyres;
            state.body = body;
            state.suspension = susp;
            state.aero = aero;
            state.engine = engine;
            std::fprintf(stderr,
                "PitStrategy: fuel=%.0f tyres=%d body=%d susp=%d aero=%d eng=%d\n",
                fuel, tyres, body, susp, aero, engine);
            if (onApplied) onApplied();
        };
}

} // namespace sim
} // namespace ks
