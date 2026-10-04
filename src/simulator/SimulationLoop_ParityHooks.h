#pragma once
/**
 * Merge notes for SimulationLoop (do not replace entire SimulationLoop.cpp).
 *
 * Includes: FeatureHub.h, CarStateSync.h, CarStateSyncBridge.h
 * Members: FeatureHub m_features; netsync::CarStateSync m_carSync; double m_simTimeSec;
 * In physics tick after pit: m_simTimeSec = m_simTime; updateNetworkSync(dt); m_features.tick(...);
 * Bodies: SimulationLoop_NetSync.cpp
 */
