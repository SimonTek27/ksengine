# Wiring (2026-10-03)

## SimulationLoop

1. `#include "FeatureHub.h"`
2. Public: `beginSession`, `features()`, `startFeatureServices`, `loadReplayFile`
3. Member: `FeatureHub m_features`
4. `#include "SimulationLoop_Features.inl"` at end of cpp (or paste methods)
5. In `tick` after lap update: `m_features.tick(dt, &m_raceSession, 0, pos, onTrack)`
6. After vehicle create: `startFeatureServices(false)`
7. `beginRaceSession`: `rc = toRaceConfig(m_features.sessionParams, ...)`

## Menu

- `onStartSessionRequested(label)` for PRACTICE / QUICK RACE / TIME ATTACK

## SimulatorApp

- Session callback → `beginSession(modeFromMenuEntry(label))`
- Replay → `loadReplayFile(KS_REPLAY_FILE or replays/last.ksreplay)`
- Host → `features().announceHost` + `startFeatureServices(true)`
- Browser → `discovery.queryLan()` + `setServers` from entries
