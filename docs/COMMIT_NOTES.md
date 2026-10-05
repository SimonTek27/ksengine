# Commit 2026-10-03 — FeatureHub wiring

## Files

| Path | Change |
|------|--------|
| `GameMenuOverlay.cpp/.h` | Session modes via `onStartSessionRequested` |
| `FeatureHub.h` | Discovery, control API, limits, weather, PB, replay |
| `SimulationLoop.h/.cpp` | `beginSession`, `features()`, `loadReplayFile`, tick hub |
| `SimulatorApp.cpp` | Menu/session/replay/feature services |

## Env

- `KS_REPLAY_FILE` — replay path
- `KS_GOLDEN_CSV` — golden export
- `KS_AI_CARS` — AI grid size

## Control API (TCP 20780)

SESSION, FLAG, PENALTY, WEATHER, TIME, SETUP, REPLAY, CHAT, RESULT

## Discovery (UDP 20779)

LAN announce + query for F2 browser
