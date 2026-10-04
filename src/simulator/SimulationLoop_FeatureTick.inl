// Auto-included parity tick (FeatureHub + CarStateSync + ksnet host bridge)
m_simTimeSec += m_physicsDt;
updateNetworkSync(static_cast<float>(m_physicsDt));
{
    vec3 pos{};
    bool onTrack = (m_sessionPhase == PHASE_GREEN_FLAG);
#if HAS_VEHICLE_SIM
    if (m_vehicle) {
        const auto st = m_vehicle->getState();
        pos = { static_cast<float>(st.position.x),
                static_cast<float>(st.position.y),
                static_cast<float>(st.position.z) };
    }
#endif
    m_features.tick(static_cast<float>(m_physicsDt), &m_raceSession, 0, pos, onTrack);
}

// One-shot: wire RaceSession / FeatureHub events onto ksnet host broadcasts.
// (FeatureTick is included from SimulationLoop.cpp after multiCar->update.)
{
    static bool s_ksnetSessionHooks = false;
    if (!s_ksnetSessionHooks) {
        s_ksnetSessionHooks = true;
#if HAS_KSNET
        m_raceSession.onCountdownTick = [this](int seconds) {
            if (m_network && m_network->isHosting())
                m_network->broadcastRaceCountdown(seconds);
        };
        m_raceSession.onCountdownFinished = [this]() {
            if (m_network && m_network->isHosting())
                m_network->broadcastRaceCountdown(0);
        };
        m_raceSession.onFlagChanged = [this](RaceFlag f) {
            (void)f;
            if (m_network && m_network->isHosting()) {
                m_network->broadcastSessionState(
                    m_sessionType, m_sessionPhase,
                    m_currentLap, m_totalLaps, m_timeRemaining);
            }
        };
        m_raceSession.onPenaltyIssued = [this](int carIndex, const std::string& typeName,
                                               const std::string& reason) {
            if (!m_network || !m_network->isHosting()) return;
            uint8_t ptype = 2; // TimeAdded default
            if (typeName.find("Drive") != std::string::npos) ptype = 0;
            else if (typeName.find("Stop") != std::string::npos) ptype = 1;
            else if (typeName.find("Disqual") != std::string::npos) ptype = 3;
            float value = 0.f;
            const auto& pending = m_raceSession.pendingPenalties();
            if (!pending.empty()) value = pending.back().value;
            m_network->broadcastPenalty(static_cast<uint32_t>(carIndex), ptype, value, reason);
        };
        m_raceSession.onSessionEnded = [this]() {
            if (m_network && m_network->isHosting()) {
                m_network->broadcastSessionState(
                    m_sessionType, m_sessionPhase,
                    m_currentLap, m_totalLaps, 0.0);
            }
        };
        // FeatureHub PB/lap events → optional lap-time relay when hosting
        m_features.onPenalty = [this](int car, int kind, float value, const std::string& reason) {
            if (m_network && m_network->isHosting())
                m_network->broadcastPenalty(static_cast<uint32_t>(car),
                                            static_cast<uint8_t>(kind & 0xFF),
                                            value, reason);
        };
#endif
    }
}

#if HAS_KSNET
// Host: low-rate session snapshot so late joiners and clients stay in phase.
if (m_network && m_network->isHosting()) {
    static double s_sessionAccum = 0.0;
    s_sessionAccum += m_physicsDt;
    if (s_sessionAccum >= 0.5) {
        s_sessionAccum = 0.0;
        m_network->broadcastSessionState(
            m_sessionType, m_sessionPhase,
            m_currentLap, m_totalLaps, m_timeRemaining);
    }
}
#endif
