#pragma once
/**
 * Race strategy and pit stop planning — Qt-free.
 */
#include "PhysicsCoreTypes.h"
#include <vector>
#include <string>

namespace ks {
namespace physics {

struct StrategyConfig {
    int totalLaps = 50;
    float fuelPerLap = 1.5f;
    float tireDegredationRate = 0.02f;
    float pitStopTimeLoss = 22.0f;
    float trackPositionWeight = 0.7f;
};

struct StrategyState {
    int currentLap = 0;
    int position = 1;
    float currentFuel = 60.0f;
    float fuelPerLap = 1.5f;
    float tireWear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float lastLapTime = 0.0f;
    float bestLapTime = 1e9f;
    float gapAhead = 0.0f;
    float gapBehind = 0.0f;
};

struct PitStopRecommendation {
    bool shouldPit = false;
    int pitLap = -1;
    float estimatedTimeGain = 0.0f;
    float fuelToAdd = 0.0f;
    bool changeTires = true;
    std::string reason;
};

class StrategySimulator {
public:
    StrategySimulator() = default;
    ~StrategySimulator() = default;

    void setStrategyConfig(const StrategyConfig& config) { m_config = config; }
    StrategyState strategyState() const { return m_strategyState; }

    void update(double /*dt*/, const StrategyState& currentState) {
        m_strategyState = currentState;
    }

    PitStopRecommendation calculatePitStopRecommendation() const {
        PitStopRecommendation r;
        float avgWear = 0;
        for (int i = 0; i < 4; ++i) avgWear += m_strategyState.tireWear[i];
        avgWear *= 0.25f;
        float lapsLeft = static_cast<float>(m_config.totalLaps - m_strategyState.currentLap);
        float fuelLaps = m_strategyState.currentFuel /
            (m_strategyState.fuelPerLap > 1e-6f ? m_strategyState.fuelPerLap : 1.5f);

        if (fuelLaps < 3.0f || avgWear > 0.85f) {
            r.shouldPit = true;
            r.pitLap = m_strategyState.currentLap + 1;
            r.fuelToAdd = m_config.fuelPerLap * lapsLeft;
            r.changeTires = avgWear > 0.6f;
            r.reason = avgWear > 0.85f ? "Tire wear critical" : "Fuel low";
            r.estimatedTimeGain = -m_config.pitStopTimeLoss;
        }
        return r;
    }

    float calculateOptimalFuelLoad(int lapsRemaining) const {
        return m_config.fuelPerLap * static_cast<float>(lapsRemaining) * 1.05f;
    }

    float calculateTireLife() const {
        float maxW = 0;
        for (int i = 0; i < 4; ++i)
            if (m_strategyState.tireWear[i] > maxW) maxW = m_strategyState.tireWear[i];
        return 1.0f - maxW;
    }

    int currentLap() const { return m_strategyState.currentLap; }

private:
    StrategyConfig m_config;
    StrategyState m_strategyState;
};

} // namespace physics
} // namespace ks
