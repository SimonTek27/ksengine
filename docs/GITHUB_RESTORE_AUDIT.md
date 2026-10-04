# GitHub restore audit — 2026-10-04 (updated)

## Done on master

SessionController, TrackLayout, TrackLimitsMonitor, WeatherControl, ApplySetup,
PersonalBestStore, ExternalControlApi (AUTH), ServerDiscovery, FeatureHub,
MenuFeatureBridge, PitStrategyBridge, CarStateSyncBridge, SimulationLoop_NetSync.cpp,
AIController.h (traffic API), GAP_MATRIX.

## Still in artifacts only (copy/merge next)

- `CarStateSync.h` (~11 KB UDP protocol)
- `AIController.cpp` (evaluateTraffic overtake)
- Surgical SimulationLoop + GameMenuOverlay + MultiCarManager traffic feed

## Do not overwrite whole SimulationLoop.cpp from artifacts

User upload has stronger telemetry/render — merge hooks only.
