#pragma once
/**
 * Optional parity hooks to merge into SimulationLoop (do not replace whole .cpp).
 *
 * 1. Includes:
 *    #include "FeatureHub.h"
 *    #include "CarStateSync.h"
 *    #include "CarStateSyncBridge.h"
 *
 * 2. Members:
 *    FeatureHub m_features;
 *    netsync::CarStateSync m_carSync;
 *    double m_simTimeSec = 0.0;
 *
 * 3. In physics step of tick(), after pit updates:
 *    m_simTimeSec = m_simTime;
 *    updateNetworkSync((float)m_physicsDt);
 *    m_features.tick((float)m_physicsDt, &m_raceSession, 0, pos, onTrack);
 *
 * 4. Method bodies: SimulationLoop_NetSync.cpp
 * 5. Public API:
 *    startCarStateHost / startCarStateClient / stopCarStateSync / updateNetworkSync
 *    features() / carStateSync()
 */
