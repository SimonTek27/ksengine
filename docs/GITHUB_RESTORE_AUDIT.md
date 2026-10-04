# GitHub restore audit — 2026-10-05

## Context

Earlier master uploads diverged from sprint work. All P0–P1 modules are now
on master and wired.

## Done on master

| Module | Role |
|--------|------|
| FeatureHub.h | discovery, control, limits, weather, PB, session |
| ExternalControlApi.h | TCP control + AUTH token |
| CarStateSync.h / Bridge | UDP state ≥20 Hz |
| SimulationLoop_NetSync.cpp | host/client sync tick |
| SimulationLoop_FeatureTick.inl | FeatureHub + NetSync inject |
| AIController (evaluateTraffic) | racing line + overtake |
| MultiCarManager + TrafficUpdate.inl | traffic snapshot |
| GarageExit / PitLaneQueue / PitLaneCollision | pit ops stack |
| PersonalBestStore / TrackLimitsMonitor | PB + penalties |
| ServerDiscovery / WeatherControl / SessionController | LAN + session |
| MenuFeatureBridge / PitStrategyBridge | UI bridges |
| cmake/simloop_z0–z4.b64 + restore_simloop.cmake | full SimulationLoop source |

## SimulationLoop.cpp

Intentionally a **stub** on the repo. Full 1203-line source is embedded in
`cmake/simloop_z*.b64` and expanded on cmake configure or via
`tools/restore_simloop.sh`. Includes FeatureTick inject after `m_multiCar->update`.

## Optional remaining

- Full remote car state path via ksnet (`HAS_KSNET`) — UDP CarStateSync already covers multiplayer state without it.
