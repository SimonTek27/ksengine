// Parity tick body: FeatureHub + CarStateSync (include inside SimulationLoop::tick physics loop)
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
