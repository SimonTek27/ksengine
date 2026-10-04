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

// One-shot: chain RaceSession events onto ksnet host broadcasts without
// dropping handlers already installed in SimulationLoop::initialize.
{
    static bool s_ksnetSessionHooks = false;
    if (!s_ksnetSessionHooks) {
        s_ksnetSessionHooks = true;
#if HAS_KSNET
        {
            auto prev = m_raceSession.onCountdownTick;
            m_raceSession.onCountdownTick = [this, prev](int seconds) {
                if (prev) prev(seconds);
                if (m_network && m_network->isHosting())
                    m_network->broadcastRaceCountdown(seconds);
            };
        }
        {
            auto prev = m_raceSession.onCountdownFinished;
            m_raceSession.onCountdownFinished = [this, prev]() {
                if (prev) prev();
                if (m_network && m_network->isHosting())
                    m_network->broadcastRaceCountdown(0);
            };
        }
        {
            auto prev = m_raceSession.onFlagChanged;
            m_raceSession.onFlagChanged = [this, prev](RaceFlag f) {
                if (prev) prev(f);
                if (m_network && m_network->isHosting()) {
                    m_network->broadcastSessionState(
                        m_sessionType, m_sessionPhase,
                        m_currentLap, m_totalLaps, m_timeRemaining);
                }
            };
        }
        {
            auto prev = m_raceSession.onPenaltyIssued;
            m_raceSession.onPenaltyIssued = [this, prev](int carIndex, const std::string& typeName,
                                                         const std::string& reason) {
                if (prev) prev(carIndex, typeName, reason);
                if (!m_network || !m_network->isHosting()) return;
                uint8_t ptype = 2;
                if (typeName.find("Drive") != std::string::npos) ptype = 0;
                else if (typeName.find("Stop") != std::string::npos) ptype = 1;
                else if (typeName.find("Disqual") != std::string::npos) ptype = 3;
                float value = 0.f;
                const auto& pending = m_raceSession.pendingPenalties();
                if (!pending.empty()) value = pending.back().value;
                m_network->broadcastPenalty(static_cast<uint32_t>(carIndex), ptype, value, reason);
            };
        }
        {
            auto prev = m_raceSession.onSessionEnded;
            m_raceSession.onSessionEnded = [this, prev]() {
                if (prev) prev();
                if (m_network && m_network->isHosting()) {
                    m_network->broadcastSessionState(
                        m_sessionType, m_sessionPhase,
                        m_currentLap, m_totalLaps, 0.0);
                }
            };
        }
        {
            auto prevLap = m_raceSession.onLapCompleted;
            m_raceSession.onLapCompleted = [this, prevLap](int lap, float time, float best) {
                if (prevLap) prevLap(lap, time, best);
                if (m_network && m_network->isHosting()) {
                    const uint32_t carId = m_multiCar
                        ? static_cast<uint32_t>(m_multiCar->playerCarId() >= 0
                              ? m_multiCar->playerCarId() : 0)
                        : 0u;
                    m_network->broadcastLapTime(carId, lap, static_cast<double>(time),
                                                0.0, 0.0, 0.0, true);
                    (void)best;
                }
            };
        }
        {
            auto prev = m_features.onPenalty;
            m_features.onPenalty = [this, prev](int car, int kind, float value, const std::string& reason) {
                if (prev) prev(car, kind, value, reason);
                if (m_network && m_network->isHosting())
                    m_network->broadcastPenalty(static_cast<uint32_t>(car),
                                                static_cast<uint8_t>(kind & 0xFF),
                                                value, reason);
            };
        }
#endif
    }
}

#if HAS_KSNET
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
