# Parity status — 2026-10-04

## Restored on master (this session)

| Module | Notes |
|--------|--------|
| TrackLimitsMonitor | P1.3 escalation |
| WeatherControl | presets + TOD |
| ApplySetup | SetupParams |
| SessionController | modes |
| TrackLayout | catalog |
| PersonalBestStore | P1.5 + leaderboard |
| ExternalControlApi | AUTH P1.2 |
| ServerDiscovery | LAN UDP |
| FeatureHub | bundle |
| MenuFeatureBridge | menu glue |
| PitStrategyBridge | P1.8 |
| CarStateSyncBridge | needs CarStateSync.h |
| SimulationLoop_NetSync.cpp | needs m_carSync in SimulationLoop |
| AIController.h | traffic API |

## Still open

| Item | Action |
|------|--------|
| `CarStateSync.h` | Copy from artifacts (UDP 20 Hz) |
| `AIController.cpp` | Overtake implementation (artifacts) |
| `SimulationLoop` merge | Add FeatureHub + pit + net sync surgically |
| `GameMenuOverlay` | ServerBrowser / Weather / PitStrategy states |
| `MultiCarManager` | Feed AiTrafficCar |

See `docs/GITHUB_RESTORE_AUDIT.md`.
