#pragma once
/**
 * Map menu pit strategy → PitRepairInput (Sprint 8 / P1.8).
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

inline void applyStrategyToRepairInput(const PitStrategyState& s, PitRepairInput& in) {
    in.targetFuelL = s.fuelTargetL;
    in.wantTyres = s.tyres;
    in.wantBody = s.body;
    in.wantSuspension = s.suspension;
    in.wantAero = s.aero;
    in.wantEngine = s.engine;
    in.requestService = true;
}

inline void wirePitStrategyMenu(GameMenuOverlay& menu, PitStrategyState& state) {
    menu.setPitStrategy(state.fuelTargetL, state.tyres, state.body,
                        state.suspension, state.aero, state.engine);
    menu.onPitStrategyConfirmRequested =
        [&](float fuel, bool tyres, bool body, bool susp, bool aero, bool engine) {
            state.fuelTargetL = fuel;
            state.tyres = tyres;
            state.body = body;
            state.suspension = susp;
            state.aero = aero;
            state.engine = engine;
            std::fprintf(stderr,
                "PitStrategy: fuel=%.0f tyres=%d body=%d susp=%d aero=%d eng=%d\n",
                fuel, tyres, body, susp, aero, engine);
        };
}

} // namespace sim
} // namespace ks
