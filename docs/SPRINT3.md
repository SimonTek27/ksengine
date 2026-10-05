# Sprint 3 — P0.5 Replay play + P1.7 Session flow (2026-10-03)

## P0.5 Replay play

- `ReplayRecorder::seekTo` / `seekProgress` / `updatePlayback` / `isFinished`
- `SimulationLoop::loadReplayFile` → `setReplayMode(true)`
- `updateReplayPlayback` applies frame pose/inputs to vehicle (frozen)
- Physics integrate skipped in replay mode

## P1.7 Session flow

```
Idle → Qualifying → GridPublished → Countdown → Race → Finished
```

- `SessionFlow.h` — phase machine, qualy times, grid sort, results
- `beginSession(Qualifying)` starts timer + car list
- `beginSession(Race)` countdown + RaceSessionManager
- `buildGridFromQualifying` snaps cars to boxes by grid pos
- `finalizeRaceResults` sorts standings → Finished
- Countdown/Grid: vehicle frozen

## Control API

```
SESSION QUALIFYING
SESSION RACE 10
REPLAY path.ksrep
PLAY / STOP
```

## Next

Sprint 4: P1.3 track limits, P1.5 PB store, P1.2 auth TCP
