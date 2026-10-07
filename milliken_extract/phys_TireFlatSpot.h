#pragma once
#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace ks::physics {

struct TireFlatSpotState {
    float severity = 0.0f;
    float angle = 0.0f;
    float vibration = 0.0f;
};

class TireFlatSpot {
public:
    TireFlatSpotState state;
    void update(bool locked, float speedMs, float loadN, float dt) {
        if (locked && speedMs > 10.0f) {
            float rate = (speedMs / 60.0f) * (loadN / 4000.0f);
            state.severity = std::clamp(state.severity + rate * dt * 0.25f, 0.0f, 1.0f);
        } else {
            state.severity = std::max(0.0f, state.severity - dt * 0.001f);
        }
        float wheelFreq = speedMs / (2.0f * 3.14159f * 0.33f);
        state.vibration = state.severity * std::clamp(wheelFreq / 20.0f, 0.0f, 1.0f);
    }
    float gripPenalty() const { return 1.0f - state.severity * 0.18f; }
    float vibrationForce(float speedMs) const {
        float wheelFreq = speedMs / (2.0f * 3.14159f * 0.33f);
        return state.severity * 400.0f * std::clamp(wheelFreq / 25.0f, 0.0f, 1.0f);
    }
    void reset() { state = {}; }
};

} // namespace ks::physics
