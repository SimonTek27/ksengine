# Parity status — 2026-10-04

## Restored on master

| Module | Status |
|--------|--------|
| FeatureHub + SessionController + ServerDiscovery + TrackLayout | OK |
| TrackLimitsMonitor / PersonalBestStore / ExternalControlApi AUTH | OK |
| WeatherControl / ApplySetup | OK |
| CarStateSync + CarStateSyncBridge + SimulationLoop_NetSync | OK |
| AIController.h/.cpp traffic overtake | OK |
| MenuFeatureBridge / PitStrategyBridge | OK |
| README architecture | OK |

## Local merge still needed

| Item | Note |
|------|------|
| SimulationLoop members | See `SimulationLoop_ParityHooks.h` |
| MultiCarManager::update | Use traffic body from artifacts / `_TrafficUpdate.inl` |
| GameMenuOverlay states | ServerBrowser / Weather / PitStrategy in artifacts |

## Docs

README.md, GAP_MATRIX, GITHUB_RESTORE_AUDIT, SPRINT notes where present.
