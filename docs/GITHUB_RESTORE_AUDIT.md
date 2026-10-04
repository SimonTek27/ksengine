# GitHub restore audit — 2026-10-04

## Context

Latest master commit: user **Upload changes** diverged from Sprint 1–8 incremental work.
Several parity modules are missing from the current tree.

## Missing (restoring)

- FeatureHub + SessionController, WeatherControl, ServerDiscovery, TrackLayout, ApplySetup
- TrackLimitsMonitor (P1.3), PersonalBestStore (P1.5), ExternalControlApi AUTH (P1.2)
- CarStateSync UDP (P1.1), MenuFeatureBridge, PitStrategyBridge
- AI overtake (Sprint 6)
- docs: GAP_MATRIX, SPRINT*, PARITY_STATUS

## Present but incomplete

- SimulationLoop: no FeatureHub/pit hooks/CarStateSync (user upload has stronger telemetry — merge surgically)
- AIController: line follow only, no traffic
- GameMenuOverlay: no PitStrategy/Weather/ServerBrowser states

## Still good on master

GarageExit, PitLaneQueue/Collision, RaceSessionManager, ReplayRecorder, NetworkManager, telemetry SM/UDP/TCP, MechanicalDamage.

## Fix order

1. Restore headers (this series of commits)
2. AI overtake merge
3. Surgical SimulationLoop + GameMenu wiring
4. CMake if needed
